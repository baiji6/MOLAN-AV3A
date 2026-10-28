/* ==========================================================================
 * MOLAN-AV3A — AVS3-P3 (Audio Vivid) audio decoder, public C API
 * --------------------------------------------------------------------------
 * A thin, dependency-free wrapper around the AVS3-P3 reference decoder
 * (AVS3A_RMv3.3.1, UWA). The neural-network post-processing model is
 * embedded in the library — no external data files are required.
 *
 * Upstream code: Copyright 2023 HUAWEI TECHNOLOGIES CO., LTD., licensed
 * under the UWA Code Sharing Policy (see LICENSE / NOTICE).
 * ==========================================================================
 */
#ifndef MOLAN_AV3A_H
#define MOLAN_AV3A_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOLAN_AV3A_VERSION "0.1.1"

/* ---- Return codes ------------------------------------------------------ */
#define MOLAN_AV3A_OK           0   /* success (frame decoded or more input needed) */
#define MOLAN_AV3A_ERR_PARAM  (-1)  /* invalid argument */
#define MOLAN_AV3A_ERR_NOMEM (-2)   /* allocation failure */
#define MOLAN_AV3A_ERR_SYNC  (-3)   /* no sync word found in the buffer */
#define MOLAN_AV3A_ERR_HEADER (-4)  /* frame header parse failed */
#define MOLAN_AV3A_ERR_DECODE (-5)  /* frame decode failed */
#define MOLAN_AV3A_ERR_OUTPUT (-6)  /* output buffer too small */

/* Maximum decoded bytes of one frame: 16 ch * 1024 samples * 2 bytes. */
#define MOLAN_AV3A_MAX_PCM_BYTES  (16 * 1024 * 2)

/* A single call needs at most this many ES bytes to contain one full frame
 * (header is 9 bytes; payload cap well above the largest observed frame). */
#define MOLAN_AV3A_MAX_ES_BYTES   4096

/* Opaque decoder handle. */
typedef struct molan_av3a molan_av3a;

/* Metadata of the most recently decoded frame. */
typedef struct molan_av3a_frame_info {
    int sample_rate;    /* output sample rate, Hz (e.g. 44100)          */
    int channels;       /* output channel count (e.g. 12 for 7.1.4)     */
    int frame_samples;  /* samples per channel per frame (typ. 1024)    */
    int bits_per_frame; /* bits announced by the frame header           */
    int bit_depth;      /* internal resolution (16/24); output is s16   */
} molan_av3a_frame_info;

/* ---- Lifecycle --------------------------------------------------------- */

/* Create a decoder instance. Returns NULL on allocation failure.
 * The embedded NN model is loaded lazily on the first decoded frame. */
molan_av3a *molan_av3a_new(void);

/* Destroy a decoder instance (NULL is allowed). */
void molan_av3a_free(molan_av3a *dec);

/* Mark internal state for resynchronization at the next frame.
 * The upstream decoder keeps some history arrays by design; recreate the
 * instance instead if complete isolation is required. */
void molan_av3a_flush(molan_av3a *dec);

/* Library version string ("0.1.1"). */
const char *molan_av3a_version(void);

/* ---- Metadata ---------------------------------------------------------- */

/* Fill *info with metadata of the last decoded frame.
 * Returns MOLAN_AV3A_ERR_PARAM before any frame has been decoded. */
int molan_av3a_get_frame_info(molan_av3a *dec, molan_av3a_frame_info *info);

/* ---- Decoding ---------------------------------------------------------- */

/*
 * Decode one AV3A elementary-stream frame.
 *
 *   in / in_size    raw ES bytes; arbitrary leading garbage is allowed,
 *                   a 0xFFF + codecId sync word scan is performed first.
 *   out_pcm         caller buffer, interleaved s16; at least
 *                   MOLAN_AV3A_MAX_PCM_BYTES bytes is recommended.
 *   out_cap_bytes   capacity of out_pcm in bytes.
 *   consumed        set to the number of input bytes used by this call
 *                   (skipped padding + header + payload actually consumed).
 *                   Set to 0 when more input is required.
 *   out_pcm_bytes   set to the number of PCM bytes produced (0 when no
 *                   frame was produced).
 *
 * Return value:
 *   MOLAN_AV3A_OK          a frame was decoded, or the buffer did not yet
 *                          contain a full frame (*consumed == 0).
 *   MOLAN_AV3A_ERR_SYNC    no sync word at all in the buffer; all input is
 *                          skipped (*consumed == in_size).
 *   MOLAN_AV3A_ERR_*       hard error; the stream should be re-synced by
 *                          scanning forward or the decoder recreated.
 */
int molan_av3a_decode(molan_av3a *dec,
                      const uint8_t *in, size_t in_size,
                      int16_t *out_pcm, size_t out_cap_bytes,
                      size_t *consumed, size_t *out_pcm_bytes);

#ifdef __cplusplus
}
#endif

#endif /* MOLAN_AV3A_H */
