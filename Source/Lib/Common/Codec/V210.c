/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <string.h>
#include "V210.h"

#define V210_SAMPLE_MASK 0x3FF

/* The words are little-endian whatever the host byte order. */
static INLINE uint32_t v210_read_word(const uint8_t* in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static INLINE void v210_write_word(uint8_t* out, uint32_t word) {
    out[0] = (uint8_t)word;
    out[1] = (uint8_t)(word >> 8);
    out[2] = (uint8_t)(word >> 16);
    out[3] = (uint8_t)(word >> 24);
}

static INLINE void v210_unpack_group(const uint8_t* in, uint16_t* y, uint16_t* cb, uint16_t* cr) {
    const uint32_t w0 = v210_read_word(in);
    const uint32_t w1 = v210_read_word(in + 4);
    const uint32_t w2 = v210_read_word(in + 8);
    const uint32_t w3 = v210_read_word(in + 12);
    cb[0] = (uint16_t)(w0 & V210_SAMPLE_MASK);
    y[0] = (uint16_t)((w0 >> 10) & V210_SAMPLE_MASK);
    cr[0] = (uint16_t)((w0 >> 20) & V210_SAMPLE_MASK);
    y[1] = (uint16_t)(w1 & V210_SAMPLE_MASK);
    cb[1] = (uint16_t)((w1 >> 10) & V210_SAMPLE_MASK);
    y[2] = (uint16_t)((w1 >> 20) & V210_SAMPLE_MASK);
    cr[1] = (uint16_t)(w2 & V210_SAMPLE_MASK);
    y[3] = (uint16_t)((w2 >> 10) & V210_SAMPLE_MASK);
    cb[2] = (uint16_t)((w2 >> 20) & V210_SAMPLE_MASK);
    y[4] = (uint16_t)(w3 & V210_SAMPLE_MASK);
    cr[2] = (uint16_t)((w3 >> 10) & V210_SAMPLE_MASK);
    y[5] = (uint16_t)((w3 >> 20) & V210_SAMPLE_MASK);
}

static INLINE uint32_t v210_word(uint16_t a, uint16_t b, uint16_t c) {
    return ((uint32_t)a & V210_SAMPLE_MASK) | (((uint32_t)b & V210_SAMPLE_MASK) << 10) | (((uint32_t)c & V210_SAMPLE_MASK) << 20);
}

static INLINE void v210_pack_group(const uint16_t* y, const uint16_t* cb, const uint16_t* cr, uint8_t* out) {
    v210_write_word(out, v210_word(cb[0], y[0], cr[0]));
    v210_write_word(out + 4, v210_word(y[1], cb[1], y[2]));
    v210_write_word(out + 8, v210_word(cr[1], y[3], cb[2]));
    v210_write_word(out + 12, v210_word(y[4], cr[2], y[5]));
}

void convert_packed_to_planar_v210_c(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width) {
    const uint8_t* in = (const uint8_t*)in_v210;
    uint16_t* y = (uint16_t*)out_y;
    uint16_t* cb = (uint16_t*)out_cb;
    uint16_t* cr = (uint16_t*)out_cr;
    uint32_t x = 0;
    for (; x + V210_PIXELS_PER_GROUP <= width; x += V210_PIXELS_PER_GROUP) {
        v210_unpack_group(in, y, cb, cr);
        in += V210_BYTES_PER_GROUP;
        y += V210_PIXELS_PER_GROUP;
        cb += V210_PIXELS_PER_GROUP / 2;
        cr += V210_PIXELS_PER_GROUP / 2;
    }
    if (x < width) {
        /* Last group with 2 or 4 pixels of the line. */
        uint16_t group_y[V210_PIXELS_PER_GROUP];
        uint16_t group_cb[V210_PIXELS_PER_GROUP / 2];
        uint16_t group_cr[V210_PIXELS_PER_GROUP / 2];
        const uint32_t pixels = width - x;
        v210_unpack_group(in, group_y, group_cb, group_cr);
        memcpy(y, group_y, pixels * sizeof(uint16_t));
        memcpy(cb, group_cb, pixels / 2 * sizeof(uint16_t));
        memcpy(cr, group_cr, pixels / 2 * sizeof(uint16_t));
    }
}

void convert_planar_to_packed_v210_c(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210,
                                     uint32_t width) {
    uint8_t* out = (uint8_t*)out_v210;
    uint32_t x = 0;
    for (; x + V210_PIXELS_PER_GROUP <= width; x += V210_PIXELS_PER_GROUP) {
        v210_pack_group(in_y, in_cb, in_cr, out);
        out += V210_BYTES_PER_GROUP;
        in_y += V210_PIXELS_PER_GROUP;
        in_cb += V210_PIXELS_PER_GROUP / 2;
        in_cr += V210_PIXELS_PER_GROUP / 2;
    }
    if (x < width) {
        /* Last group with 2 or 4 pixels of the line, the rest of the group is written as 0. */
        uint16_t group_y[V210_PIXELS_PER_GROUP] = {0};
        uint16_t group_cb[V210_PIXELS_PER_GROUP / 2] = {0};
        uint16_t group_cr[V210_PIXELS_PER_GROUP / 2] = {0};
        const uint32_t pixels = width - x;
        memcpy(group_y, in_y, pixels * sizeof(uint16_t));
        memcpy(group_cb, in_cb, pixels / 2 * sizeof(uint16_t));
        memcpy(group_cr, in_cr, pixels / 2 * sizeof(uint16_t));
        v210_pack_group(group_y, group_cb, group_cr, out);
    }
}
