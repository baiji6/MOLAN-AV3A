# MOLAN-AV3A

**AVS3-P3 (Audio Vivid) audio decoding in pure C.**

English · [简体中文](README.zh-CN.md)

[Quick start](#quick-start) · [C API](#c-api) · [CLI](#cli) · [Support](#support) · [Integration](#integration) · [Building](#building)

`MOLAN-AV3A` provides a standalone AVS3-P3 (Audio Vivid / AVS3 Part 3, "臻音全景声") audio decoder and command-line tools for MP4, M4A and raw AV3A elementary streams. The decoder is built on the AVS3-P3 reference software (AVS3A_RMv3.3.1) published by the UHD World Association (UWA), packaged behind a small, dependency-free C API with the neural-network post-processing model **embedded in the library** — no external data files are required.

- **Reusable decoder** — AV3A ES frames in, interleaved `s16` PCM out.
- **MP4/M4A aware CLI** — extracts the AV3A track itself, no ffmpeg needed.
- **Multi-channel output** — validated bit-exact against the reference decoder on a real 12-channel (7.1.4) NetEase "臻音全景声" file.

## Quick start

```bash
git clone https://github.com/baiji6/MOLAN-AV3A.git
cd MOLAN-AV3A
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

```bash
# Decode (MP4 or raw ES) to 16-bit PCM WAV
build/molan-av3a decode input.mp4 output.wav

# Inspect stream metadata
build/molan-av3a info input.mp4

# Benchmark decoding
build/molan-av3a bench input.mp4 3
```

## C API

Add `include/molan_av3a.h` and link the static library:

```c
#include "molan_av3a.h"

molan_av3a *dec = molan_av3a_new();
int16_t pcm[MOLAN_AV3A_MAX_PCM_BYTES / 2];

const uint8_t *p = es; size_t left = es_size;
while (left) {
    size_t consumed = 0, pcm_bytes = 0;
    int rc = molan_av3a_decode(dec, p, left, pcm, sizeof(pcm),
                               &consumed, &pcm_bytes);
    if (rc != MOLAN_AV3A_OK) { /* resync or re-create */ break; }
    if (pcm_bytes == 0 && consumed == 0) { /* need more input */ break; }

    if (pcm_bytes) {
        molan_av3a_frame_info info;
        molan_av3a_get_frame_info(dec, &info);
        /* info.sample_rate / info.channels / info.frame_samples */
        /* consume pcm here, before decoding the next frame */
    }
    p += consumed; left -= consumed;
}
molan_av3a_free(dec);
```

The buffer may contain arbitrary leading garbage — a `0xFFF` + codecId sync scan runs per call. `MOLAN_AV3A_MAX_PCM_BYTES` (32 KiB) always holds one decoded frame.

## CLI

```
molan-av3a decode <input.mp4|.es|.av3a> [output.wav]
molan-av3a info   <input>
molan-av3a bench  <input> [rounds=3]
```

- `decode` writes a classic 44-byte WAV header for ≤2 channels and `WAVE_FORMAT_EXTENSIBLE` for multi-channel output.
- MP4/M4A inputs are demuxed internally (stsd/stsz/stsc/stco/co64); only the first `soun` track is used, and its sample entry fourcc is printed (typically `av3a`).

## Support

| Area | Status |
| --- | --- |
| AVS3-P3 baseline / low-bitrate / lossless profiles | Supported (upstream reference decoder, RMv3.3.1) |
| Mono / stereo / multi-channel bed / HOA | Supported, validated on 12-ch (7.1.4) real streams |
| Neural post-processing (NNQC model) | Supported — model is embedded, lazy-loaded on first frame |
| Metadata | Parsed by the decoder; not yet exposed via the C API |
| Output | interleaved s16 PCM (internal resolution 16/24 bit) |

The decoder inherits the upstream reference software's profile coverage. Not every profile/channel layout has been validated with real streams; the validated path is NetEase "臻音全景声" (44.1 kHz, 12 ch).

## Integration

Use `libmolan_av3a` as the codec layer and keep source access, demuxing, timestamps and audio output in your player. In the Baiji Music Android build the same code is compiled into libmpv/ffmpeg as the `av3a` decoder for NetEase vivid streams.

- AV3A decoding is **stateful**. Seek from a suitable frame boundary; recreate the decoder for complete history isolation.
- `molan_av3a_flush()` recreates the upstream instance (the reference decoder has no partial reset).
- The library never writes to stdout/stderr — upstream logging is compiled out (`printf` is redirected to a no-op; `_FORTIFY_SOURCE` is disabled for the lib target on GCC/Clang because glibc's fortified `stdio2.h` would otherwise re-wrap `printf`).

## Building

Requirements: CMake ≥ 3.10 and any C99 compiler (GCC / Clang / MSVC).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Verified on x86-64 Linux (GCC). CI builds Linux / macOS / Windows matrices.

## Validation

Bit-exactness was verified end-to-end against the unmodified upstream reference CLI on a real 285-second, 12-channel NetEase Audio Vivid file: the PCM produced by `molan-av3a decode` (from both MP4 and raw-ES inputs) is byte-for-byte identical to the reference decoder output. Benchmarked at ~14x realtime on a desktop x86-64 core.

## License

Upstream code: `Copyright 2023 HUAWEI TECHNOLOGIES CO., LTD.`, licensed under the [UWA Code Sharing Policy](http://www.theuwa.com/UWA_Code_Sharing_Policy.pdf). Original headers are retained in all source files; see [NOTICE](NOTICE). The wrapper, CLI and build system in this repository are provided under the same policy for interoperability.
