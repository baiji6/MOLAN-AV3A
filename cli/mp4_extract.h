/* MOLAN-AV3A CLI — minimal MP4/M4A demuxer for a single AV3A audio track.
 *
 * Supports the common "progressive download" layout produced by encoders and
 * streaming services (moov at either end, stco/stco64, stsz with per-sample
 * sizes). Only the first 'soun' track is extracted; its first sample entry
 * fourcc is reported (typically 'av3a'). */
#ifndef MOLAN_AV3A_MP4_EXTRACT_H
#define MOLAN_AV3A_MP4_EXTRACT_H

#include <stddef.h>
#include <stdint.h>

/* Detect an MP4/M4A container (leading 'ftyp' box). */
int mp4_probe(const uint8_t *data, size_t size);

/* Extract the elementary stream of the first audio track.
 * Returns 0 on success and fills *out_data/*out_size (caller frees).
 * On failure returns -1 and writes a reason into *err (if given). */
int mp4_extract_audio(const uint8_t *data, size_t size,
                      uint8_t **out_data, size_t *out_size,
                      char fourcc[5], const char **err);

#endif
