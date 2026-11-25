/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_V210_H_
#define _SVT_JPEG_XS_V210_H_

#include <stdint.h>
#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* V210 (COLOUR_FORMAT_PACKED_V210): 10-bit 4:2:2, a group of 6 pixels in 4 little-endian 32-bit words with 3
 * samples each, in bits 0-9, 10-19 and 20-29:
 *   word 0: Cb0 Y0 Cr0, word 1: Y1 Cb1 Y2, word 2: Cr1 Y3 Cb2, word 3: Y4 Cr2 Y5 */
#define V210_PIXELS_PER_GROUP 6
#define V210_BYTES_PER_GROUP  16

/* 32-bit words of a V210 line of width pixels without padding: whole groups. */
static INLINE uint32_t v210_line_words(uint32_t width) {
    return (width + V210_PIXELS_PER_GROUP - 1) / V210_PIXELS_PER_GROUP * (V210_BYTES_PER_GROUP / 4);
}

/* 32-bit words of a V210 line of width pixels padded to a multiple of 128 bytes (48 pixels). */
static INLINE uint32_t v210_aligned_line_words(uint32_t width) {
    return (width + 47) / 48 * (128 / 4);
}

/* One V210 line to planar 16-bit lines: width Y samples, width / 2 Cb and width / 2 Cr samples. width is even.
 * Reads (width + 5) / 6 whole groups. Bits 30-31 of each word are ignored. */
void convert_packed_to_planar_v210_c(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width);

/* Planar 16-bit lines (width Y samples, width / 2 Cb and width / 2 Cr samples, the low 10 bits of each are used)
 * to one V210 line of (width + 5) / 6 whole groups. width is even. In the last group, the samples after the end
 * of the line are written as 0. Bits 30-31 of each word are written as 0. */
void convert_planar_to_packed_v210_c(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210,
                                     uint32_t width);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /*_SVT_JPEG_XS_V210_H_*/
