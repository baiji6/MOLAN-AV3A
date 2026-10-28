/* MOLAN-AV3A CLI — minimal MP4/M4A demuxer for a single AV3A audio track. */
#include "mp4_extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little helpers (MP4 is big-endian) -------------------------------- */

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t rd64(const uint8_t *p)
{
    return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

static int tag_is(const uint8_t *p, const char *tag)
{
    return memcmp(p, tag, 4) == 0;
}

/* ---- box cursor --------------------------------------------------------- */

typedef struct {
    const uint8_t *base;
    size_t size;
    size_t pos;    /* absolute offset of the next box header */
} box_cursor;

/* Parse one box header at the cursor; returns:
 *   1  ok, box described
 *   0  end of container
 *  -1  malformed */
static int box_next(box_cursor *c, uint8_t tag[4], const uint8_t **body,
                    size_t *body_size, size_t *header_size)
{
    size_t left, hdr;
    uint64_t box_size;

    if (c->pos >= c->size)
        return 0;
    left = c->size - c->pos;
    if (left < 8)
        return -1;

    box_size = rd32(c->base + c->pos);
    hdr = 8;
    if (box_size == 1) {
        if (left < 16)
            return -1;
        box_size = rd64(c->base + c->pos + 8);
        hdr = 16;
    } else if (box_size == 0) {
        box_size = left;    /* box extends to end of container */
    }
    if (box_size < hdr || box_size > left)
        return -1;

    memcpy(tag, c->base + c->pos + 4, 4);
    *body = c->base + c->pos + hdr;
    *body_size = (size_t)(box_size - hdr);
    *header_size = hdr;
    c->pos += (size_t)box_size;
    return 1;
}

/* Iterate boxes inside [start, start+span), invoking cb(tag, body, size, ud).
 * cb returns 1 to stop. Returns 1 if stopped, 0 if exhausted, -1 on error. */
static int boxes_foreach(const uint8_t *start, size_t span, int deep,
                         int (*cb)(const uint8_t tag[4], const uint8_t *body,
                                   size_t size, void *ud, int depth),
                         void *ud, int depth)
{
    box_cursor c = { start, span, 0 };
    for (;;) {
        uint8_t tag[4];
        const uint8_t *body;
        size_t body_size, header_size;
        int r = box_next(&c, tag, &body, &body_size, &header_size);
        if (r == 0)
            return 0;
        if (r < 0)
            return -1;
        if (cb(tag, body, body_size, ud, depth) == 1)
            return 1;
        if (deep) {
            /* container boxes worth descending into */
            static const char *cont[] = { "moov", "trak", "mdia", "minf",
                                          "stbl", "edts", NULL };
            int descend = 0;
            for (int i = 0; cont[i]; i++)
                if (memcmp(tag, cont[i], 4) == 0) { descend = 1; break; }
            if (descend) {
                int r2 = boxes_foreach(body, body_size, deep, cb, ud,
                                       depth + 1);
                if (r2 == 1)
                    return 1;
                if (r2 < 0)
                    return -1;
            }
        }
    }
}

/* ---- track parser state -------------------------------------------------- */

typedef struct {
    /* stsd */
    uint8_t fourcc[4];
    int have_stsd;
    /* stsz */
    uint32_t sample_size;      /* 0 => per-sample table */
    uint32_t sample_count;
    const uint8_t *sizes;      /* points into the file image */
    int have_stsz;
    /* stsc */
    const uint8_t *stsc_entries;
    uint32_t stsc_count;
    int have_stsc;
    /* stco / co64 */
    const uint8_t *chunk_offsets;
    uint32_t chunk_count;
    int is_co64;
    int have_stco;
    /* hdlr */
    int is_audio;
    int found;
} trak_state;

static int trak_cb(const uint8_t tag[4], const uint8_t *body, size_t size,
                   void *ud, int depth)
{
    trak_state *st = (trak_state *)ud;

    if (depth == 0 && memcmp(tag, "trak", 4) == 0) {
        /* reset per-track state */
        memset(st, 0, sizeof(*st));
        st->found = 0;
        /* recurse into this trak only */
        return boxes_foreach(body, size, 1, trak_cb, st, 1) == 1 ? 1 : 0;
    }

    if (memcmp(tag, "hdlr", 4) == 0 && size >= 12 + 4) {
        /* body: version/flags(4) pre_defined(4) handler_type(4) ... */
        st->is_audio = tag_is(body + 8, "soun");
    } else if (memcmp(tag, "stsd", 4) == 0 && size >= 8) {
        /* body: version/flags(4) entry_count(4) entries... */
        uint32_t entries = rd32(body + 4);
        if (entries >= 1 && size >= 8 + 8) {
            memcpy(st->fourcc, body + 8 + 4, 4);
            st->have_stsd = 1;
        }
    } else if (memcmp(tag, "stsz", 4) == 0 && size >= 12) {
        st->sample_size = rd32(body + 4);
        st->sample_count = rd32(body + 8);
        st->sizes = body + 12;
        if (st->sample_size == 0 && st->sample_count &&
            size < 12 + (size_t)st->sample_count * 4)
            return 0;                   /* malformed; ignore this trak */
        st->have_stsz = 1;
    } else if (memcmp(tag, "stsc", 4) == 0 && size >= 8) {
        st->stsc_count = rd32(body + 4);
        st->stsc_entries = body + 8;
        if (st->stsc_count && size < 8 + (size_t)st->stsc_count * 12)
            return 0;
        st->have_stsc = 1;
    } else if ((memcmp(tag, "stco", 4) == 0 || memcmp(tag, "co64", 4) == 0)
               && size >= 8) {
        st->is_co64 = (tag[3] == '6');
        st->chunk_count = rd32(body + 4);
        st->chunk_offsets = body + 8;
        if (st->chunk_count &&
            size < 8 + (size_t)st->chunk_count * (st->is_co64 ? 8 : 4))
            return 0;
        st->have_stco = 1;
    }

    if (st->is_audio && st->have_stsd && st->have_stsz && st->have_stsc &&
        st->have_stco) {
        st->found = 1;
        return 1;                       /* complete audio track found */
    }
    return 0;
}

static uint64_t chunk_offset(const trak_state *st, uint32_t i)
{
    if (st->is_co64)
        return rd64(st->chunk_offsets + (size_t)i * 8);
    return rd32(st->chunk_offsets + (size_t)i * 4);
}

static uint32_t sample_size_at(const trak_state *st, uint32_t i)
{
    return st->sample_size ? st->sample_size : rd32(st->sizes + (size_t)i * 4);
}

int mp4_probe(const uint8_t *data, size_t size)
{
    return size >= 12 && rd32(data) >= 8 && tag_is(data + 4, "ftyp");
}

int mp4_extract_audio(const uint8_t *data, size_t size,
                      uint8_t **out_data, size_t *out_size,
                      char fourcc[5], const char **err)
{
    box_cursor top = { data, size, 0 };
    trak_state st;
    uint8_t tag[4];
    const uint8_t *body;
    size_t body_size, header_size;
    int have_moov = 0, r;

    *out_data = NULL;
    *out_size = 0;
    if (err)
        *err = NULL;

    while ((r = box_next(&top, tag, &body, &body_size, &header_size)) == 1) {
        if (memcmp(tag, "moov", 4) == 0) {
            have_moov = 1;
            memset(&st, 0, sizeof(st));
            if (boxes_foreach(body, body_size, 1, trak_cb, &st, 0) == 1 &&
                st.found)
                break;
        }
    }
    if (r < 0 && !st.found) {
        if (err) *err = "malformed MP4 box structure";
        return -1;
    }
    if (!have_moov) {
        if (err) *err = "no moov box (fragmented MP4 not supported)";
        return -1;
    }
    if (!st.found) {
        if (err) *err = "no complete audio track found";
        return -1;
    }
    if (fourcc) {
        memcpy(fourcc, st.fourcc, 4);
        fourcc[4] = 0;
    }

    /* Walk stsc -> chunk offsets -> sample offsets, copying samples out. */
    {
        uint32_t total = st.sample_count;
        uint8_t *es;
        uint64_t total_bytes = 0;
        uint32_t sidx = 0;
        uint32_t sc_i = 0;

        /* pass 1: total bytes */
        {
            uint32_t first[2], spc;
            while (sidx < total && sc_i < st.stsc_count) {
                const uint8_t *e = st.stsc_entries + (size_t)sc_i * 12;
                first[0] = rd32(e);
                spc = rd32(e + 4);
                first[1] = (sc_i + 1 < st.stsc_count)
                    ? rd32(e + 12) : st.chunk_count + 1;
                if (first[1] <= first[0]) {
                    if (err) *err = "malformed stsc table";
                    return -1;
                }
                for (uint32_t ck = first[0]; ck < first[1] && sidx < total;
                     ck++) {
                    if (ck > st.chunk_count) {
                        if (err) *err = "stsc exceeds chunk table";
                        return -1;
                    }
                    for (uint32_t k = 0; k < spc && sidx < total; k++, sidx++)
                        total_bytes += sample_size_at(&st, sidx);
                }
                sc_i++;
            }
        }
        if (sidx != total) {
            if (err) *err = "stsc/stsz sample count mismatch";
            return -1;
        }

        es = (uint8_t *)malloc((size_t)total_bytes);
        if (!es) {
            if (err) *err = "out of memory";
            return -1;
        }

        /* pass 2: copy */
        {
            size_t off = 0;
            uint32_t first[2], spc;
            sidx = 0;
            sc_i = 0;
            while (sidx < total && sc_i < st.stsc_count) {
                const uint8_t *e = st.stsc_entries + (size_t)sc_i * 12;
                first[0] = rd32(e);
                spc = rd32(e + 4);
                first[1] = (sc_i + 1 < st.stsc_count)
                    ? rd32(e + 12) : st.chunk_count + 1;
                for (uint32_t ck = first[0]; ck < first[1] && sidx < total;
                     ck++) {
                    uint64_t fpos = chunk_offset(&st, ck - 1);
                    for (uint32_t k = 0; k < spc && sidx < total; k++, sidx++) {
                        uint32_t sz = sample_size_at(&st, sidx);
                        if ((uint64_t)fpos + sz > size) {
                            free(es);
                            if (err) *err = "sample range out of file bounds";
                            return -1;
                        }
                        memcpy(es + off, data + fpos, sz);
                        off += sz;
                        fpos += sz;
                    }
                }
                sc_i++;
            }
        }

        *out_data = es;
        *out_size = (size_t)total_bytes;
    }
    return 0;
}
