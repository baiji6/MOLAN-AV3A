/* MOLAN-AV3A CLI — minimal multi-channel WAV writer. */
#include "wav_writer.h"

#include <string.h>

static void put_u32(FILE *fp, uint32_t v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
        (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, fp);
}

static void put_u16(FILE *fp, uint16_t v)
{
    unsigned char b[2] = {
        (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF)
    };
    fwrite(b, 1, 2, fp);
}

/* dwChannelMask for standard speaker layouts (simple full-low-bit mask is
 * widely accepted; AVS3 channel semantics are carried by the stream itself). */
static uint32_t channel_mask(int channels)
{
    if (channels <= 0 || channels >= 18)
        return 0;
    return (channels >= 32) ? 0xFFFFFFFFu : ((1u << channels) - 1u);
}

static const unsigned char GUID_PCM[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

FILE *wav_open(const char *path, int channels, int sample_rate)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return NULL;

    const int extensible = channels > 2;
    const uint32_t block_align = (uint32_t)channels * 2u;
    const uint32_t byte_rate = block_align * (uint32_t)sample_rate;
    const uint32_t fmt_size = extensible ? 40u : 16u;

    fwrite("RIFF", 1, 4, fp);
    put_u32(fp, 0);            /* backfilled in wav_close() */
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    put_u32(fp, fmt_size);
    put_u16(fp, extensible ? 0xFFFEu : 1u);
    put_u16(fp, (uint16_t)channels);
    put_u32(fp, (uint32_t)sample_rate);
    put_u32(fp, byte_rate);
    put_u16(fp, (uint16_t)block_align);
    put_u16(fp, 16u);          /* container bits */
    if (extensible) {
        put_u16(fp, 22u);                          /* cbSize      */
        put_u16(fp, 16u);                          /* valid bits  */
        put_u32(fp, channel_mask(channels));
        fwrite(GUID_PCM, 1, 16, fp);
    }

    fwrite("data", 1, 4, fp);
    put_u32(fp, 0);            /* backfilled in wav_close() */
    return fp;
}

int wav_write(FILE *fp, const void *pcm, size_t bytes)
{
    return fwrite(pcm, 1, bytes, fp) == bytes ? 0 : -1;
}

int wav_close(FILE *fp, int channels)
{
    long end = 0, data_pos = 0;
    uint32_t riff_size = 0, data_size = 0;

    if (!fp)
        return -1;

    end = ftell(fp);
    data_pos = 12 + 8 + (channels > 2 ? 40 : 16) + 8;

    if (end <= data_pos) {
        fclose(fp);
        return -1;
    }
    data_size = (uint32_t)(end - data_pos);
    riff_size = data_size + (uint32_t)data_pos - 8u;

    fseek(fp, 4, SEEK_SET);
    put_u32(fp, riff_size);
    fseek(fp, data_pos - 4, SEEK_SET);
    put_u32(fp, data_size);

    fclose(fp);
    return 0;
}
