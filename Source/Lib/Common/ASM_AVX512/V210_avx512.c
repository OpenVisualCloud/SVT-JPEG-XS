/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "V210_avx512.h"
#include "V210.h"

/* 512-bit AVX-512BW V210 unpack: 4 groups (24 pixels, 64 bytes) per step.
 * Each 128-bit lane decodes one 6-pixel group using the same shuffle+mul+shift as the AVX2 kernel,
 * then one VPERMD packs all 24 Y samples and one VPERMW splits the 12 Cb and 12 Cr samples. */
void convert_packed_to_planar_v210_avx512(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width) {
    const uint8_t* in = (const uint8_t*)in_v210;
    uint16_t* y = (uint16_t*)out_y;
    uint16_t* cb = (uint16_t*)out_cb;
    uint16_t* cr = (uint16_t*)out_cr;

    // clang-format off
    const __m512i shuffle_y = _mm512_broadcast_i32x4(_mm_setr_epi8(
        1, 2, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, -1, -1, -1, -1));
    const __m512i multiply_y = _mm512_broadcast_i32x4(_mm_setr_epi16(
        4, 16, 1, 4, 16, 1, 0, 0));
    const __m512i shuffle_c = _mm512_broadcast_i32x4(_mm_setr_epi8(
        0, 1, 5, 6, 10, 11, -1, -1, 2, 3, 8, 9, 13, 14, -1, -1));
    const __m512i multiply_c = _mm512_broadcast_i32x4(_mm_setr_epi16(
        16, 4, 1, 0, 1, 16, 4, 0));
    const __m512i compact_y = _mm512_setr_epi32(
        0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 3, 7, 11, 15);
    const __m512i compact_c = _mm512_set_epi16(
        3, 3, 3, 3, 30, 29, 28, 22, 21, 20, 14, 13, 12, 6, 5, 4,
        3, 3, 3, 3, 26, 25, 24, 18, 17, 16, 10, 9, 8, 2, 1, 0);
    // clang-format on
    const __m512i mask = _mm512_set1_epi16(0x3FF);

    uint32_t x = 0;
    /* 4 groups (24 pixels) per step; unmasked 32-Y / 16-Cb / 16-Cr stores avoid masked-store penalties. */
    for (; x + 32 <= width; x += 4 * V210_PIXELS_PER_GROUP) {
        const __m512i groups = _mm512_loadu_si512((const void*)in);

        __m512i luma = _mm512_shuffle_epi8(groups, shuffle_y);
        luma = _mm512_and_si512(_mm512_srli_epi16(_mm512_mullo_epi16(luma, multiply_y), 4), mask);
        luma = _mm512_permutexvar_epi32(compact_y, luma);
        _mm512_storeu_si512((void*)y, luma);

        __m512i chroma = _mm512_shuffle_epi8(groups, shuffle_c);
        chroma = _mm512_and_si512(_mm512_srli_epi16(_mm512_mullo_epi16(chroma, multiply_c), 4), mask);
        chroma = _mm512_permutexvar_epi16(compact_c, chroma);
        _mm256_storeu_si256((__m256i*)cb, _mm512_castsi512_si256(chroma));
        _mm256_storeu_si256((__m256i*)cr, _mm512_extracti64x4_epi64(chroma, 1));

        in += 4 * V210_BYTES_PER_GROUP;
        y += 4 * V210_PIXELS_PER_GROUP;
        cb += 2 * V210_PIXELS_PER_GROUP;
        cr += 2 * V210_PIXELS_PER_GROUP;
    }
    if (x + 4 * V210_PIXELS_PER_GROUP <= width) {
        const __m512i groups = _mm512_loadu_si512((const void*)in);

        __m512i luma = _mm512_shuffle_epi8(groups, shuffle_y);
        luma = _mm512_and_si512(_mm512_srli_epi16(_mm512_mullo_epi16(luma, multiply_y), 4), mask);
        luma = _mm512_permutexvar_epi32(compact_y, luma);
        _mm512_mask_storeu_epi16(y, (__mmask32)0x00FFFFFFu, luma);

        __m512i chroma = _mm512_shuffle_epi8(groups, shuffle_c);
        chroma = _mm512_and_si512(_mm512_srli_epi16(_mm512_mullo_epi16(chroma, multiply_c), 4), mask);
        chroma = _mm512_permutexvar_epi16(compact_c, chroma);
        _mm256_mask_storeu_epi16(cb, (__mmask16)0x0FFFu, _mm512_castsi512_si256(chroma));
        _mm256_mask_storeu_epi16(cr, (__mmask16)0x0FFFu, _mm512_extracti64x4_epi64(chroma, 1));

        in += 4 * V210_BYTES_PER_GROUP;
        y += 4 * V210_PIXELS_PER_GROUP;
        cb += 2 * V210_PIXELS_PER_GROUP;
        cr += 2 * V210_PIXELS_PER_GROUP;
        x += 4 * V210_PIXELS_PER_GROUP;
    }
    if (x < width) {
        convert_packed_to_planar_v210_c(in, y, cb, cr, width - x);
    }
}

/* 512-bit AVX-512BW V210 pack: 4 groups (24 pixels, 64 bytes) per step.
 * Three masked VPERMI2W instructions gather bits 0-9, 10-19 and 20-29 of all 16 output words
 * directly from luma (lanes 0..23) and chroma (Cb in 0..11, Cr in 16..27), then VPTERNLOGD
 * combines all three fields in one instruction. */
void convert_planar_to_packed_v210_avx512(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210,
                                          uint32_t width) {
    uint8_t* out = (uint8_t*)out_v210;

    // clang-format off
    /* Even 16-bit lanes hold the 16 word fields; odd lanes are zeroed by mask 0x55555555u.
     * Source A (0..23) = Y[0..23]; Source B (32..43) = Cb[0..11], (48..59) = Cr[0..11]. */
    const __m512i idx_f0 = _mm512_set_epi16(
        0, 22, 0, 58, 0, 19, 0, 41,
        0, 16, 0, 55, 0, 13, 0, 38,
        0, 10, 0, 52, 0,  7, 0, 35,
        0,  4, 0, 49, 0,  1, 0, 32);
    const __m512i idx_f1 = _mm512_set_epi16(
        0, 59, 0, 21, 0, 42, 0, 18,
        0, 56, 0, 15, 0, 39, 0, 12,
        0, 53, 0,  9, 0, 36, 0,  6,
        0, 50, 0,  3, 0, 33, 0,  0);
    const __m512i idx_f2 = _mm512_set_epi16(
        0, 23, 0, 43, 0, 20, 0, 57,
        0, 17, 0, 40, 0, 14, 0, 54,
        0, 11, 0, 37, 0,  8, 0, 51,
        0,  5, 0, 34, 0,  2, 0, 48);
    // clang-format on
    const __m512i mask = _mm512_set1_epi16(0x3FF);
    const __mmask32 even_lanes = (__mmask32)0x55555555u;

    uint32_t x = 0;
    for (; x + 32 <= width; x += 4 * V210_PIXELS_PER_GROUP) {
        const __m512i luma = _mm512_and_si512(_mm512_loadu_si512((const void*)in_y), mask);
        const __m512i chroma = _mm512_and_si512(
            _mm512_inserti64x4(
                _mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*)in_cb)), _mm256_loadu_si256((const __m256i*)in_cr), 1),
            mask);

        const __m512i f0 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f0, chroma);
        const __m512i f1 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f1, chroma);
        const __m512i f2 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f2, chroma);
        const __m512i words = _mm512_ternarylogic_epi32(f0, _mm512_slli_epi32(f1, 10), _mm512_slli_epi32(f2, 20), 0xFE);
        _mm512_storeu_si512((void*)out, words);

        out += 4 * V210_BYTES_PER_GROUP;
        in_y += 4 * V210_PIXELS_PER_GROUP;
        in_cb += 2 * V210_PIXELS_PER_GROUP;
        in_cr += 2 * V210_PIXELS_PER_GROUP;
    }
    if (x + 4 * V210_PIXELS_PER_GROUP <= width) {
        const __m512i luma = _mm512_and_si512(_mm512_maskz_loadu_epi16((__mmask32)0x00FFFFFFu, in_y), mask);
        const __m512i chroma = _mm512_and_si512(
            _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_maskz_loadu_epi16((__mmask16)0x0FFFu, in_cb)),
                               _mm256_maskz_loadu_epi16((__mmask16)0x0FFFu, in_cr),
                               1),
            mask);
        const __m512i f0 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f0, chroma);
        const __m512i f1 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f1, chroma);
        const __m512i f2 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f2, chroma);
        const __m512i words = _mm512_ternarylogic_epi32(f0, _mm512_slli_epi32(f1, 10), _mm512_slli_epi32(f2, 20), 0xFE);
        _mm512_storeu_si512((void*)out, words);

        out += 4 * V210_BYTES_PER_GROUP;
        in_y += 4 * V210_PIXELS_PER_GROUP;
        in_cb += 2 * V210_PIXELS_PER_GROUP;
        in_cr += 2 * V210_PIXELS_PER_GROUP;
        x += 4 * V210_PIXELS_PER_GROUP;
    }
    if (x < width) {
        convert_planar_to_packed_v210_c(in_y, in_cb, in_cr, out, width - x);
    }
}
