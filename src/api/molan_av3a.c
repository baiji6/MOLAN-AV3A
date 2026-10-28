/* ==========================================================================
 * MOLAN-AV3A — public API implementation
 * --------------------------------------------------------------------------
 * Wraps the upstream AVS3-P3 reference decoder (AVS3A_RMv3.3.1) behind a
 * small, dependency-free C API:
 *
 *   - sync-word scan + frame-header parse + payload decode per call
 *   - interleaved s16 output, metadata exposed via molan_av3a_frame_info()
 *   - the NN post-processing model is embedded upstream (model.h -> g_model),
 *     loaded lazily on the first frame; no external data files needed.
 *
 * Upstream code: Copyright 2023 HUAWEI TECHNOLOGIES CO., LTD., licensed
 * under the UWA Code Sharing Policy (see LICENSE / NOTICE).
 * ==========================================================================
 */
#include "molan_av3a.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#include "avs3_stat_dec.h"

/* Upstream entry points (avs3Decoder/src/decoder.c).
 * Declared with void* to keep the public API free of upstream types. */
extern void *avs3_create_decoder(void);
extern void  avs3_destroy_decoder(void *h);
extern int   parse_header(void *h, unsigned char *pData, int nLenIn,
                          int isInitFrame, int *pnLenConsumed,
                          unsigned short *crc);
extern int   avs3_decode(void *h, unsigned char *pDataIN, int nLenIn,
                         unsigned char *pDataOut, int *pnLenOut,
                         int *pnLenConsumed);

#define AVS3_TRUE 1

/* Upstream logs through printf()/LOGD(); every library TU force-includes
 * src/api/molan_av3a_quiet.h, which shadows printf with this no-op so
 * nothing leaks to the host process (see that header for why the shadow
 * is applied via force-include rather than a -D rename). */
int molan_av3a_quiet_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

struct molan_av3a {
    void                *h;        /* upstream decoder instance            */
    int16_t             *scratch;  /* interleaved s16 frame staging buffer */
    molan_av3a_frame_info info;
    int                  has_info;
};

molan_av3a *molan_av3a_new(void)
{
    molan_av3a *d = (molan_av3a *)calloc(1, sizeof(*d));
    if (!d)
        return NULL;

    d->scratch = (int16_t *)malloc(MOLAN_AV3A_MAX_PCM_BYTES);
    if (!d->scratch) {
        free(d);
        return NULL;
    }

    d->h = avs3_create_decoder();
    if (!d->h) {
        free(d->scratch);
        free(d);
        return NULL;
    }
    return d;
}

void molan_av3a_free(molan_av3a *dec)
{
    if (!dec)
        return;
    if (dec->h) {
        avs3_destroy_decoder(dec->h);
        dec->h = NULL;
    }
    free(dec->scratch);
    free(dec);
}

void molan_av3a_flush(molan_av3a *dec)
{
    if (!dec)
        return;

    /* The upstream decoder has no explicit reset entry point; recreate the
     * instance, which is the semantically cleanest resync. */
    if (dec->h)
        avs3_destroy_decoder(dec->h);
    dec->h = avs3_create_decoder();
    dec->has_info = 0;
}

const char *molan_av3a_version(void)
{
    return MOLAN_AV3A_VERSION;
}

int molan_av3a_get_frame_info(molan_av3a *dec, molan_av3a_frame_info *info)
{
    if (!dec || !dec->has_info || !info)
        return MOLAN_AV3A_ERR_PARAM;

    *info = dec->info;
    return MOLAN_AV3A_OK;
}

int molan_av3a_decode(molan_av3a *dec,
                      const uint8_t *in, size_t in_size,
                      int16_t *out_pcm, size_t out_cap_bytes,
                      size_t *consumed, size_t *out_pcm_bytes)
{
    int header_len = 0;
    int payload_out = 0;
    int payload_used = 0;
    int ret;

    if (consumed)
        *consumed = 0;
    if (out_pcm_bytes)
        *out_pcm_bytes = 0;

    if (!dec || !dec->h || (!in && in_size) || !out_pcm || !out_cap_bytes ||
        !consumed || !out_pcm_bytes)
        return MOLAN_AV3A_ERR_PARAM;
    if (in_size > (size_t)INT_MAX)
        return MOLAN_AV3A_ERR_PARAM;

    /* ---- 1) frame header ----------------------------------------------
     * parse_header() scans for the 0xFFF + codecId sync word itself and
     * sets *pnLenConsumed to (skipped bytes + header length), i.e. the
     * offset of the frame payload. It returns AVS3_TRUE when a header was
     * parsed (stream metadata is updated in-place). */
    ret = parse_header(dec->h, (unsigned char *)in, (int)in_size, 0,
                       &header_len, NULL);
    if (ret != AVS3_TRUE)
        return MOLAN_AV3A_ERR_SYNC;
    if (header_len <= 0 || (size_t)header_len >= in_size) {
        /* Sync found but the frame is not fully buffered yet; ask the
         * caller to accumulate more data. Re-scanning next call is cheap. */
        return MOLAN_AV3A_OK;
    }

    /* ---- 2) payload decode ---------------------------------------------
     * Produces interleaved s16 PCM; the embedded NN model is loaded
     * transparently inside the upstream decoder on the first call. */
    ret = avs3_decode(dec->h, (unsigned char *)in + header_len,
                      (int)in_size - header_len,
                      (unsigned char *)dec->scratch, &payload_out,
                      &payload_used);
    if (ret != AVS3_TRUE)
        return MOLAN_AV3A_ERR_DECODE;
    if (payload_out <= 0 || payload_used < 0)
        return MOLAN_AV3A_ERR_DECODE;

    /* ---- 3) validate and publish metadata ------------------------------ */
    {
        AVS3DecoderHandle m = (AVS3DecoderHandle)dec->h;
        int chans = (int)m->numChansOutput;
        int samples = (int)m->frameLength;
        size_t expect;

        if (chans <= 0 || chans > 16 || samples <= 0)
            return MOLAN_AV3A_ERR_DECODE;

        expect = (size_t)chans * (size_t)samples * 2u;
        if ((size_t)payload_out != expect)
            return MOLAN_AV3A_ERR_DECODE;
        if (expect > out_cap_bytes)
            return MOLAN_AV3A_ERR_OUTPUT;

        dec->info.sample_rate    = (int)m->outputFs;
        dec->info.channels       = chans;
        dec->info.frame_samples  = samples;
        dec->info.bits_per_frame = (int)m->bitsPerFrame;
        dec->info.bit_depth      = (int)m->bitDepth;
        dec->has_info = 1;

        memcpy(out_pcm, dec->scratch, expect);
        *out_pcm_bytes = expect;
    }

    *consumed = (size_t)header_len + (size_t)payload_used;
    return MOLAN_AV3A_OK;
}
