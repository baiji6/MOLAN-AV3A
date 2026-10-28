# ==========================================================================
# MOLAN-AV3A — AVS3-P3 (Audio Vivid) decoder library and CLI
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# Targets:
#   molan_av3a   static library (public header: include/molan_av3a.h)
#   molan-av3a   command-line decoder (decode / info / bench)
# ==========================================================================
cmake_minimum_required(VERSION 3.10)
project(MOLAN-AV3A C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- library -------------------------------------------------------------

set(MOLAN_AV3A_LIB_SOURCES
    # public API wrapper
    src/api/molan_av3a.c
    # AVS3-P3 reference decoder (AVS3A_RMv3.3.1)
    src/avs3Decoder/src/avs3_bitstream_dec.c
    src/avs3Decoder/src/avs3_dec.c
    src/avs3Decoder/src/avs3_dec_lib.c
    src/avs3Decoder/src/avs3_hoa_dec.c
    src/avs3Decoder/src/avs3_init_dec.c
    src/avs3Decoder/src/avs3_io_dec.c
    src/avs3Decoder/src/avs3_mc_dec.c
    src/avs3Decoder/src/avs3_metadata_dec.c
    src/avs3Decoder/src/avs3_mix_dec.c
    src/avs3Decoder/src/avs3_mono_dec.c
    src/avs3Decoder/src/avs3_stereo_dec.c
    src/avs3Decoder/src/avs3_write_wav.c
    src/avs3Decoder/src/decoder.c
    src/libavs3_common/activation_func.c
    src/libavs3_common/avs3_com.c
    src/libavs3_common/avs3_hoa_com.c
    src/libavs3_common/avs3_mc_com.c
    src/libavs3_common/avs3_rom_com.c
    src/libavs3_common/avs3_stereo_com.c
    src/libavs3_common/bwe_com.c
    src/libavs3_common/bwe_dec.c
    src/libavs3_common/cnn_layer.c
    src/libavs3_common/fd_spectrum_shaping_com.c
    src/libavs3_common/fd_spectrum_shaping_dec.c
    src/libavs3_common/fft.c
    src/libavs3_common/latent_quant.c
    src/libavs3_common/load_free_model.c
    src/libavs3_common/lsf_dec.c
    src/libavs3_common/mcr_com.c
    src/libavs3_common/mdct.c
    src/libavs3_common/mdct_dequant_decode.c
    src/libavs3_common/noise_filling.c
    src/libavs3_common/range_coder.c
    src/libavs3_common/tns.c
    src/libavs3_common/tools.c
    src/libavs3_debug/avs3_debug.c
)

add_library(molan_av3a STATIC ${MOLAN_AV3A_LIB_SOURCES})

target_include_directories(molan_av3a
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/avs3Decoder/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/libavs3_common
            ${CMAKE_CURRENT_SOURCE_DIR}/src/libavs3_debug
)

target_compile_definitions(molan_av3a PRIVATE
    # silence the reference decoder's stdio logging (printf -> no-op)
    printf=molan_av3a_quiet_printf
    # decoder.c doubles as the upstream CLI; exclude its main() from the lib
    AVS3_NO_MAIN=1
)
# Ubuntu enables _FORTIFY_SOURCE at -O2+, where bits/stdio2.h re-defines
# printf(...) -> __printf_chk(...) and would override the silencing macro
# above. Disable fortify for this target only.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(molan_av3a PRIVATE -U_FORTIFY_SOURCE)
endif()
if(MSVC)
    target_compile_definitions(molan_av3a PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(molan_av3a PRIVATE -fsigned-char)
    target_link_libraries(molan_av3a PUBLIC m)
endif()

# ---- CLI -----------------------------------------------------------------

add_executable(molan-av3a
    cli/main.c
    cli/mp4_extract.c
    cli/wav_writer.c
)

target_link_libraries(molan-av3a PRIVATE molan_av3a)

if(MSVC)
    target_compile_definitions(molan-av3a PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
