/* ==========================================================================
 * MOLAN-AV3A CLI — decode / info / bench
 *
 *   molan-av3a decode <input> [output.wav]
 *   molan-av3a info   <input>
 *   molan-av3a bench  <input> [rounds]
 *
 * Input: MP4/M4A with an AV3A audio track, or a raw AV3A elementary stream.
 * Output: 16-bit PCM WAV (WAVE_FORMAT_EXTENSIBLE for >2 channels).
 * ==========================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "molan_av3a.h"
#include "mp4_extract.h"
#include "wav_writer.h"

#ifdef _WIN32
#include <windows.h>
static double now_sec(void)
{
    static LARGE_INTEGER freq = { {0, 0} };
    LARGE_INTEGER c;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

static uint8_t *load_input(const char *path, size_t *size, char fourcc[5],
                           const char **err)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *data;
    long len;

    *size = 0;
    if (!fp) {
        if (err) *err = "cannot open input file";
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0) {
        fclose(fp);
        if (err) *err = "empty input file";
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)len);
    if (!data) {
        fclose(fp);
        if (err) *err = "out of memory";
        return NULL;
    }
    if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
        fclose(fp);
        free(data);
        if (err) *err = "read failed";
        return NULL;
    }
    fclose(fp);

    if (mp4_probe(data, (size_t)len)) {
        uint8_t *es = NULL;
        size_t es_size = 0;
        if (mp4_extract_audio(data, (size_t)len, &es, &es_size, fourcc,
                              err) != 0) {
            free(data);
            return NULL;
        }
        printf("input: MP4/M4A container, sample entry '%s', "
               "ES %.2f MiB\n", fourcc, (double)es_size / 1048576.0);
        free(data);
        *size = es_size;
        return es;
    }

    printf("input: raw AV3A elementary stream, %.2f MiB\n",
           (double)len / 1048576.0);
    if (fourcc)
        strcpy(fourcc, "(raw)");
    *size = (size_t)len;
    return data;
}

static int run_decode(const char *in_path, const char *out_path,
                      int quiet_pcm_write)
{
    uint8_t *es = NULL;
    size_t es_size = 0;
    const char *err = NULL;
    char fourcc[5] = {0};
    molan_av3a *dec = NULL;
    FILE *wav = NULL;
    int16_t *pcm = NULL;
    size_t pos = 0;
    unsigned long frames = 0;
    molan_av3a_frame_info info;
    int have_info = 0;
    int ret = 1;

    es = load_input(in_path, &es_size, fourcc, &err);
    if (!es) {
        fprintf(stderr, "error: %s\n", err ? err : "load failed");
        return 1;
    }

    dec = molan_av3a_new();
    pcm = (int16_t *)malloc(MOLAN_AV3A_MAX_PCM_BYTES);
    if (!dec || !pcm) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    while (pos < es_size) {
        size_t consumed = 0, pcm_bytes = 0;
        int rc = molan_av3a_decode(dec, es + pos, es_size - pos,
                                   pcm, MOLAN_AV3A_MAX_PCM_BYTES,
                                   &consumed, &pcm_bytes);
        if (rc == MOLAN_AV3A_OK && consumed == 0 && pcm_bytes == 0) {
            fprintf(stderr, "error: need more data at offset %zu "
                    "(truncated frame?)\n", pos);
            goto done;
        }
        if (rc != MOLAN_AV3A_OK) {
            fprintf(stderr, "error: decode failed at offset %zu (rc=%d: %s)\n",
                    pos, rc,
                    rc == MOLAN_AV3A_ERR_SYNC ? "sync lost" :
                    rc == MOLAN_AV3A_ERR_HEADER ? "header" :
                    rc == MOLAN_AV3A_ERR_DECODE ? "decode" :
                    rc == MOLAN_AV3A_ERR_OUTPUT ? "output too small" :
                    "unknown");
            goto done;
        }

        if (pcm_bytes > 0) {
            if (frames == 0) {
                molan_av3a_get_frame_info(dec, &info);
                have_info = 1;
                printf("stream: %d Hz, %d channels, %d samples/frame, "
                       "%d bits/frame\n",
                       info.sample_rate, info.channels, info.frame_samples,
                       info.bits_per_frame);
                if (!quiet_pcm_write && out_path) {
                    wav = wav_open(out_path, info.channels, info.sample_rate);
                    if (!wav) {
                        fprintf(stderr, "error: cannot open output '%s'\n",
                                out_path);
                        goto done;
                    }
                }
            }
            if (wav && wav_write(wav, pcm, pcm_bytes) != 0) {
                fprintf(stderr, "error: wav write failed\n");
                goto done;
            }
            frames++;
        }
        pos += consumed;

        if (!quiet_pcm_write && frames && frames % 2000 == 0)
            printf("  decoded %lu frames (%.1f%%)\r",
                   frames, 100.0 * (double)pos / (double)es_size);
    }

    {
        double dur = 0;
        int out_ch = have_info ? info.channels : 2;
        if (have_info && frames)
            dur = (double)frames * info.frame_samples /
                  (double)info.sample_rate;
        printf("\ndone: %lu frames decoded, %.2f s audio\n", frames, dur);
        if (wav) {
            printf("output: %s\n", out_path);
            wav_close(wav, out_ch);
            wav = NULL;
        }
    }
    ret = 0;

done:
    if (wav)
        wav_close(wav, 2);
    free(pcm);
    molan_av3a_free(dec);
    free(es);
    return ret;
}

static int run_info(const char *in_path)
{
    uint8_t *es = NULL;
    size_t es_size = 0;
    const char *err = NULL;
    char fourcc[5] = {0};
    molan_av3a *dec = NULL;
    int16_t *pcm = NULL;
    size_t pos = 0;
    unsigned long frames = 0;
    molan_av3a_frame_info info;
    int have_info = 0;
    int ret = 1;

    es = load_input(in_path, &es_size, fourcc, &err);
    if (!es) {
        fprintf(stderr, "error: %s\n", err ? err : "load failed");
        return 1;
    }
    dec = molan_av3a_new();
    pcm = (int16_t *)malloc(MOLAN_AV3A_MAX_PCM_BYTES);
    if (!dec || !pcm) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    while (pos < es_size) {
        size_t consumed = 0, pcm_bytes = 0;
        int rc = molan_av3a_decode(dec, es + pos, es_size - pos,
                                   pcm, MOLAN_AV3A_MAX_PCM_BYTES,
                                   &consumed, &pcm_bytes);
        if (rc != MOLAN_AV3A_OK) {
            fprintf(stderr, "error: decode failed at offset %zu (rc=%d)\n",
                    pos, rc);
            goto done;
        }
        if (rc == MOLAN_AV3A_OK && consumed == 0 && pcm_bytes == 0) {
            fprintf(stderr, "error: truncated frame at offset %zu\n", pos);
            goto done;
        }
        if (pcm_bytes > 0 && !have_info) {
            molan_av3a_get_frame_info(dec, &info);
            have_info = 1;
        }
        frames += pcm_bytes ? 1u : 0u;
        pos += consumed;
    }

    if (!have_info) {
        fprintf(stderr, "error: no frame decoded\n");
        goto done;
    }
    printf("codec      : AVS3-P3 (Audio Vivid)\n");
    printf("sample rate: %d Hz\n", info.sample_rate);
    printf("channels   : %d\n", info.channels);
    printf("frame      : %d samples, %d bits/frame\n",
           info.frame_samples, info.bits_per_frame);
    printf("bit depth  : %d (internal), output interleaved s16\n",
           info.bit_depth);
    printf("frames     : %lu (%.2f s)\n", frames,
           (double)frames * info.frame_samples / (double)info.sample_rate);
    ret = 0;

done:
    free(pcm);
    molan_av3a_free(dec);
    free(es);
    return ret;
}

static int run_bench(const char *in_path, int rounds)
{
    uint8_t *es = NULL;
    size_t es_size = 0;
    const char *err = NULL;
    char fourcc[5] = {0};
    int16_t *pcm = NULL;
    double best = 0, sum = 0;
    unsigned long frames_last = 0;
    int ret = 1;

    es = load_input(in_path, &es_size, fourcc, &err);
    if (!es) {
        fprintf(stderr, "error: %s\n", err ? err : "load failed");
        return 1;
    }
    pcm = (int16_t *)malloc(MOLAN_AV3A_MAX_PCM_BYTES);
    if (!pcm) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    for (int r = 0; r < rounds; r++) {
        molan_av3a *dec = molan_av3a_new();
        size_t pos = 0;
        unsigned long frames = 0;
        double t0, t1;

        if (!dec) {
            fprintf(stderr, "error: out of memory\n");
            goto done;
        }
        t0 = now_sec();
        while (pos < es_size) {
            size_t consumed = 0, pcm_bytes = 0;
            int rc = molan_av3a_decode(dec, es + pos, es_size - pos,
                                       pcm, MOLAN_AV3A_MAX_PCM_BYTES,
                                       &consumed, &pcm_bytes);
            if (rc != MOLAN_AV3A_OK ||
                (rc == MOLAN_AV3A_OK && consumed == 0 && pcm_bytes == 0)) {
                fprintf(stderr, "error: decode failed at offset %zu\n", pos);
                molan_av3a_free(dec);
                goto done;
            }
            frames += pcm_bytes ? 1u : 0u;
            pos += consumed;
        }
        t1 = now_sec();
        molan_av3a_free(dec);
        frames_last = frames;
        sum += t1 - t0;
        if (r == 0 || t1 - t0 < best)
            best = t1 - t0;
        printf("round %d/%d: %.3f s\n", r + 1, rounds, t1 - t0);
    }

    {
        molan_av3a *dec = molan_av3a_new();
        molan_av3a_frame_info info;
        double dur = 0;
        if (dec) {
            /* one frame to read metadata for speedup factor */
            size_t consumed = 0, pcm_bytes = 0;
            if (molan_av3a_decode(dec, es, es_size, pcm,
                                  MOLAN_AV3A_MAX_PCM_BYTES, &consumed,
                                  &pcm_bytes) == MOLAN_AV3A_OK &&
                pcm_bytes &&
                molan_av3a_get_frame_info(dec, &info) == MOLAN_AV3A_OK)
                dur = (double)frames_last * info.frame_samples /
                      (double)info.sample_rate;
            molan_av3a_free(dec);
        }
        printf("\nbest %.3f s, avg %.3f s over %d rounds, %lu frames",
               best, sum / rounds, rounds, frames_last);
        if (dur > 0)
            printf(" (%.1fx realtime)", dur / best);
        printf("\n");
    }
    ret = 0;

done:
    free(pcm);
    free(es);
    return ret;
}

static void usage(const char *argv0)
{
    printf("MOLAN-AV3A %s — AVS3-P3 (Audio Vivid) decoder\n\n",
           molan_av3a_version());
    printf("usage:\n");
    printf("  %s decode <input.mp4|.es|.av3a> [output.wav]\n", argv0);
    printf("  %s info   <input>\n", argv0);
    printf("  %s bench  <input> [rounds=3]\n", argv0);
    printf("\ninput: MP4/M4A with an AV3A track, or raw AV3A ES\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage(argv[0]);
        return argc < 2 ? 0 : 1;
    }

    if (strcmp(argv[1], "decode") == 0) {
        const char *out = argc >= 4 ? argv[3] : NULL;
        return run_decode(argv[2], out, out == NULL);
    }
    if (strcmp(argv[1], "info") == 0)
        return run_info(argv[2]);
    if (strcmp(argv[1], "bench") == 0)
        return run_bench(argv[2], argc >= 4 ? atoi(argv[3]) : 3);

    usage(argv[0]);
    return 1;
}
