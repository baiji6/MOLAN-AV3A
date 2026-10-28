# MOLAN-AV3A

**纯 C 的 AVS3-P3（Audio Vivid / 臻音全景声）音频解码器。**

[English](README.md) · 简体中文

[快速开始](#快速开始) · [C API](#c-api) · [命令行](#命令行) · [支持范围](#支持范围) · [集成](#集成) · [构建](#构建)

`MOLAN-AV3A` 提供独立的 AVS3-P3（AVS3 第三部分沉浸式音频，即网易云「臻音全景声」Audio Vivid）音频解码器与命令行工具，支持 MP4、M4A 与裸 AV3A 码流。解码核心基于世界超高清视频产业联盟（UWA）发布的 AVS3-P3 参考软件（AVS3A_RMv3.3.1），封装为一套无依赖、无外部数据文件的简洁 C API——**神经网络后处理模型内嵌于库内**，首次解码时懒加载。

- **可复用解码器** — 输入 AV3A ES 帧，输出交错 `s16` PCM。
- **CLI 原生理解 MP4/M4A** — 自带轨道提取，无需 ffmpeg。
- **多声道输出** — 在真实网易云 12 声道（7.1.4）臻音全景声文件上与参考解码器逐字节一致。

## 快速开始

```bash
git clone https://github.com/baiji6/MOLAN-AV3A.git
cd MOLAN-AV3A
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

```bash
# 解码（MP4 或裸 ES）为 16-bit PCM WAV
build/molan-av3a decode input.mp4 output.wav

# 查看流信息
build/molan-av3a info input.mp4

# 解码性能基准
build/molan-av3a bench input.mp4 3
```

## C API

包含 `include/molan_av3a.h` 并链接静态库即可：

```c
#include "molan_av3a.h"

molan_av3a *dec = molan_av3a_new();
int16_t pcm[MOLAN_AV3A_MAX_PCM_BYTES / 2];

const uint8_t *p = es; size_t left = es_size;
while (left) {
    size_t consumed = 0, pcm_bytes = 0;
    int rc = molan_av3a_decode(dec, p, left, pcm, sizeof(pcm),
                               &consumed, &pcm_bytes);
    if (rc != MOLAN_AV3A_OK) { /* 重新同步或重建解码器 */ break; }
    if (pcm_bytes == 0 && consumed == 0) { /* 数据不足一帧 */ break; }

    if (pcm_bytes) {
        molan_av3a_frame_info info;
        molan_av3a_get_frame_info(dec, &info);
        /* info.sample_rate / info.channels / info.frame_samples */
        /* 在解下一帧之前消费 PCM */
    }
    p += consumed; left -= consumed;
}
molan_av3a_free(dec);
```

输入缓冲允许带任意前导填充——每次调用内部先做 `0xFFF` + codecId 同步字扫描。`MOLAN_AV3A_MAX_PCM_BYTES`（32 KiB）恒能容纳一帧解码输出。

## 命令行

```
molan-av3a decode <input.mp4|.es|.av3a> [output.wav]
molan-av3a info   <input>
molan-av3a bench  <input> [rounds=3]
```

- `decode`：≤2 声道写经典 44 字节 WAV 头；多声道写 `WAVE_FORMAT_EXTENSIBLE`。
- MP4/M4A 输入在 CLI 内部解封装（stsd/stsz/stsc/stco/co64）；只取第一条 `soun` 轨并打印其 sample entry fourcc（通常为 `av3a`）。

## 支持范围

| 能力 | 状态 |
| --- | --- |
| AVS3-P3 baseline / 低码率 / 无损档 | 支持（上游参考解码器 RMv3.3.1） |
| 单声道 / 立体声 / 多声道 bed / HOA | 支持，已在 12 声道（7.1.4）真实流上验证 |
| 神经网络后处理（NNQC 模型） | 支持——模型内嵌，首帧懒加载 |
| 元数据 | 解码器可解析；C API 暂未暴露 |
| 输出 | 交错 s16 PCM（内部精度 16/24 bit） |

解码覆盖范围继承上游参考软件。并非所有 profile/声道布局都经真实流验证；已验证路径为网易云「臻音全景声」（44.1 kHz，12 声道）。

## 集成

用 `libmolan_av3a` 作解码层，取流、解封装、时间戳与音频输出留在播放器内。在白姬音乐的 Android 构建中，同一套代码被编译进 libmpv/ffmpeg，作为网易云 vivid 流的 `av3a` 解码器。

- AV3A 解码是**有状态**的。请从合适的帧边界起播；需要完整历史隔离时重建解码器实例。
- `molan_av3a_flush()` 通过重建上游实例实现（参考解码器无部分重置入口）。
- 本库绝不向 stdout/stderr 输出——上游日志在编译期被移除（`printf` 重定向为空函数；GCC/Clang 下对该库目标禁用 `_FORTIFY_SOURCE`，否则 glibc 的 fortified `stdio2.h` 会重新包装 `printf`）。

## 构建

依赖：CMake ≥ 3.10 与任意 C99 编译器（GCC / Clang / MSVC）。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

已在 x86-64 Linux（GCC）验证。CI 覆盖 Linux / macOS / Windows 构建矩阵。

## 验证

在真实网易云 Audio Vivid 文件（285 秒、12 声道）上与**未修改的上游参考 CLI** 做了端到端逐字节比对：`molan-av3a decode` 产出的 PCM（MP4 与裸 ES 两条路径）与参考解码器输出完全一致。桌面 x86-64 单核基准约 14 倍实时。

## 许可

上游代码：`Copyright 2023 HUAWEI TECHNOLOGIES CO., LTD.`，遵循 [UWA 代码共享政策](http://www.theuwa.com/UWA_Code_Sharing_Policy.pdf)。全部源文件保留原始版权头；详见 [NOTICE](NOTICE)。本仓库的封装层、CLI 与构建体系为互操作目的按同一政策提供。
