/* MOLAN-AV3A CLI — minimal multi-channel WAV writer
 * (WAVE_FORMAT_EXTENSIBLE for >2 channels, classic PCM header otherwise). */
#ifndef MOLAN_AV3A_WAV_WRITER_H
#define MOLAN_AV3A_WAV_WRITER_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

/* Open output WAV and write the header placeholder.
 * Returns NULL on failure; *out_fp receives the stream. */
FILE *wav_open(const char *path, int channels, int sample_rate);

/* Write interleaved s16 PCM bytes. Returns 0 on success. */
int wav_write(FILE *fp, const void *pcm, size_t bytes);

/* Backfill sizes and close. `channels` must match wav_open().
 * Returns 0 on success. */
int wav_close(FILE *fp, int channels);

#endif
