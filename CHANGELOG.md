# Changelog

All notable changes to this project are documented in this file.

## 0.1.0 — 2026-10-29

Initial release.

- AVS3-P3 (Audio Vivid) reference decoder AVS3A_RMv3.3.1 packaged as the
  static library `libmolan_av3a` behind a dependency-free C API
  (`include/molan_av3a.h`):
  - `molan_av3a_new/decode/get_frame_info/flush/free/version`
  - sync-word scan per call, arbitrary leading garbage tolerated
  - interleaved s16 PCM output, 16-ch/1024-sample frame capacity constant
  - NN post-processing model embedded, no external data files
  - library is completely silent (upstream logging compiled out)
- CLI `molan-av3a` with `decode` / `info` / `bench`:
  - MP4/M4A input demuxing (stsd/stsz/stsc/stco/co64), first `soun` track
  - raw AV3A elementary stream input
  - `WAVE_FORMAT_EXTENSIBLE` WAV output for >2 channels
- Validated bit-exact against the unmodified upstream reference CLI on a
  real 285 s / 12 ch (7.1.4) NetEase "臻音全景声" file (MP4 and raw-ES
  paths); ~14x realtime on a desktop x86-64 core.
- CI: Linux / macOS / Windows build matrix.
