/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltDec_avx512.h"
#include "NltDec.h"
#include "V210.h"

void linear_output_scaling_8bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w) {
    uint32_t x = 0;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m512i round_v = _mm512_set1_epi32(round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(m);

    for (; x + 32 <= w; x += 32) {
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x)), round_v), dzeta);
        __m512i v1 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x + 16)), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        v1 = _mm512_max_epi32(_mm512_min_epi32(v1, max_v), zero_v);
        _mm_storeu_si128((__m128i*)(out + x), _mm512_cvtepi32_epi8(v0));
        _mm_storeu_si128((__m128i*)(out + x + 16), _mm512_cvtepi32_epi8(v1));
    }
    for (; x < w; x += 16) {
        const uint32_t rem = w - x;
        const __mmask16 km = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32(km, in + x), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        _mm512_mask_cvtepi32_storeu_epi8(out + x, km, v0);
    }
}

void linear_output_scaling_16bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w) {
    uint32_t x = 0;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m512i round_v = _mm512_set1_epi32(round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(m);

    for (; x + 32 <= w; x += 32) {
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x)), round_v), dzeta);
        __m512i v1 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x + 16)), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        v1 = _mm512_max_epi32(_mm512_min_epi32(v1, max_v), zero_v);
        _mm256_storeu_si256((__m256i*)(out + x), _mm512_cvtepi32_epi16(v0));
        _mm256_storeu_si256((__m256i*)(out + x + 16), _mm512_cvtepi32_epi16(v1));
    }
    for (; x < w; x += 16) {
        const uint32_t rem = w - x;
        const __mmask16 km = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32(km, in + x), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        _mm512_mask_cvtepi32_storeu_epi16(out + x, km, v0);
    }
}

void linear_output_scaling_16bit_line_msb_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w) {
    uint32_t x = 0;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const int32_t msb_shift = 16 - depth;
    const __m512i round_v = _mm512_set1_epi32(round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(m);

    for (; x + 32 <= w; x += 32) {
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x)), round_v), dzeta);
        __m512i v1 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in + x + 16)), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        v1 = _mm512_max_epi32(_mm512_min_epi32(v1, max_v), zero_v);
        const __m512i packed = _mm512_inserti64x4(
            _mm512_castsi256_si512(_mm512_cvtepi32_epi16(v0)), _mm512_cvtepi32_epi16(v1), 1);
        _mm512_storeu_si512((void*)(out + x), _mm512_slli_epi16(packed, msb_shift));
    }
    for (; x < w; x += 16) {
        const uint32_t rem = w - x;
        const __mmask16 km = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i v0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32(km, in + x), round_v), dzeta);
        v0 = _mm512_max_epi32(_mm512_min_epi32(v0, max_v), zero_v);
        const __m256i packed = _mm256_slli_epi16(_mm512_cvtepi32_epi16(v0), msb_shift);
        _mm256_mask_storeu_epi16(out + x, km, packed);
    }
}

void inv_sign_avx512(uint16_t* in_out, uint32_t width) {
    const __m512i coeff_mask = _mm512_set1_epi16((short)(~BITSTREAM_MASK_SIGN));
    const __m512i zero = _mm512_setzero_si512();
    uint32_t i = 0;

    for (; i + 32 <= width; i += 32) {
        const __m512i data = _mm512_loadu_si512((const void*)(in_out + i));
        const __mmask32 neg = _mm512_movepi16_mask(data);
        const __m512i mag = _mm512_and_si512(data, coeff_mask);
        const __m512i out = _mm512_mask_sub_epi16(mag, neg, zero, mag);
        _mm512_storeu_si512((void*)(in_out + i), out);
    }
    if (i < width) {
        const __mmask32 m = ((__mmask32)1 << (width - i)) - 1;
        const __m512i data = _mm512_maskz_loadu_epi16(m, in_out + i);
        const __mmask32 neg = _mm512_movepi16_mask(data);
        const __m512i mag = _mm512_and_si512(data, coeff_mask);
        const __m512i out = _mm512_mask_sub_epi16(mag, neg, zero, mag);
        _mm512_mask_storeu_epi16(in_out + i, m, out);
    }
}

/* Fused 10-bit linear output scaling + V210 packing (AVX-512BW).
 * Main loop processes 8 groups (48 pixels, 128 bytes) per step with zero redundant int32_t scaling
 * (3 exact ZMMs for 48 Y, 3 exact ZMMs for 24 Cb + 24 Cr), packing via 3x VPERMI2W + VPTERNLOGD. */
void linear_output_scaling_v210_line_avx512(const int32_t* in_y, const int32_t* in_cb, const int32_t* in_cr, uint32_t bw,
                                            void* out_v210, uint32_t w) {
    const int32_t dzeta = (int32_t)bw - 10;
    const uint32_t round = ((1u << bw) >> 1) + ((1u << dzeta) >> 1);
    const __m512i round_v = _mm512_set1_epi32((int32_t)round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(0x3FF);
    const __mmask32 even_lanes = (__mmask32)0x55555555u;

    // clang-format off
    /* First 24 pixels: A (0..23) = Y[0..23]; B (32..43) = Cb[0..11], (48..59) = Cr[0..11]. */
    const __m512i idx_f0_a = _mm512_set_epi16(
        0, 22, 0, 58, 0, 19, 0, 41,
        0, 16, 0, 55, 0, 13, 0, 38,
        0, 10, 0, 52, 0,  7, 0, 35,
        0,  4, 0, 49, 0,  1, 0, 32);
    const __m512i idx_f1_a = _mm512_set_epi16(
        0, 59, 0, 21, 0, 42, 0, 18,
        0, 56, 0, 15, 0, 39, 0, 12,
        0, 53, 0,  9, 0, 36, 0,  6,
        0, 50, 0,  3, 0, 33, 0,  0);
    const __m512i idx_f2_a = _mm512_set_epi16(
        0, 23, 0, 43, 0, 20, 0, 57,
        0, 17, 0, 40, 0, 14, 0, 54,
        0, 11, 0, 37, 0,  8, 0, 51,
        0,  5, 0, 34, 0,  2, 0, 48);

    /* Second 24 pixels: A (8..31) = Y[24..47];
     * B (40..43) = Cb[12..15], (48..55) = Cb[16..23], (44..47) = Cr[12..15], (56..63) = Cr[16..23]. */
    const __m512i idx_f0_b = _mm512_set_epi16(
        0, 30, 0, 62, 0, 27, 0, 53,
        0, 24, 0, 59, 0, 21, 0, 50,
        0, 18, 0, 56, 0, 15, 0, 43,
        0, 12, 0, 45, 0,  9, 0, 40);
    const __m512i idx_f1_b = _mm512_set_epi16(
        0, 63, 0, 29, 0, 54, 0, 26,
        0, 60, 0, 23, 0, 51, 0, 20,
        0, 57, 0, 17, 0, 48, 0, 14,
        0, 46, 0, 11, 0, 41, 0,  8);
    const __m512i idx_f2_b = _mm512_set_epi16(
        0, 31, 0, 55, 0, 28, 0, 61,
        0, 25, 0, 52, 0, 22, 0, 58,
        0, 19, 0, 49, 0, 16, 0, 47,
        0, 13, 0, 42, 0, 10, 0, 44);
    // clang-format on

    uint8_t* out = (uint8_t*)out_v210;
    uint32_t x = 0;
    for (; x + 48 <= w; x += 48) {
        __m512i y0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_y + x)), round_v), dzeta);
        __m512i y1 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_y + x + 16)), round_v), dzeta);
        __m512i y2 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_y + x + 32)), round_v), dzeta);
        y0 = _mm512_max_epi32(_mm512_min_epi32(y0, max_v), zero_v);
        y1 = _mm512_max_epi32(_mm512_min_epi32(y1, max_v), zero_v);
        y2 = _mm512_max_epi32(_mm512_min_epi32(y2, max_v), zero_v);
        const __m256i y0_16 = _mm512_cvtepi32_epi16(y0);
        const __m256i y1_16 = _mm512_cvtepi32_epi16(y1);
        const __m256i y2_16 = _mm512_cvtepi32_epi16(y2);
        const __m512i luma_a = _mm512_inserti64x4(_mm512_castsi256_si512(y0_16), y1_16, 1);
        const __m512i luma_b = _mm512_inserti64x4(_mm512_castsi256_si512(y1_16), y2_16, 1);

        __m512i cb0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_cb + x / 2)), round_v), dzeta);
        __m512i cr0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_cr + x / 2)), round_v), dzeta);
        __m512i cbcr_hi = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*)(in_cb + x / 2 + 16))),
                                             _mm256_loadu_si256((const __m256i*)(in_cr + x / 2 + 16)),
                                             1);
        cbcr_hi = _mm512_srai_epi32(_mm512_add_epi32(cbcr_hi, round_v), dzeta);
        cb0 = _mm512_max_epi32(_mm512_min_epi32(cb0, max_v), zero_v);
        cr0 = _mm512_max_epi32(_mm512_min_epi32(cr0, max_v), zero_v);
        cbcr_hi = _mm512_max_epi32(_mm512_min_epi32(cbcr_hi, max_v), zero_v);

        const __m256i cb0_16 = _mm512_cvtepi32_epi16(cb0);
        const __m256i cr0_16 = _mm512_cvtepi32_epi16(cr0);
        const __m256i cbcr_hi_16 = _mm512_cvtepi32_epi16(cbcr_hi);
        const __m512i chroma_a = _mm512_inserti64x4(_mm512_castsi256_si512(cb0_16), cr0_16, 1);
        const __m256i c_12_15 = _mm256_unpackhi_epi64(cb0_16, cr0_16);
        const __m512i chroma_b = _mm512_inserti64x4(_mm512_castsi256_si512(c_12_15), cbcr_hi_16, 1);

        const __m512i f0_a = _mm512_maskz_permutex2var_epi16(even_lanes, luma_a, idx_f0_a, chroma_a);
        const __m512i f1_a = _mm512_maskz_permutex2var_epi16(even_lanes, luma_a, idx_f1_a, chroma_a);
        const __m512i f2_a = _mm512_maskz_permutex2var_epi16(even_lanes, luma_a, idx_f2_a, chroma_a);
        const __m512i words_a = _mm512_ternarylogic_epi32(f0_a, _mm512_slli_epi32(f1_a, 10), _mm512_slli_epi32(f2_a, 20), 0xFE);
        _mm512_storeu_si512((void*)out, words_a);

        const __m512i f0_b = _mm512_maskz_permutex2var_epi16(even_lanes, luma_b, idx_f0_b, chroma_b);
        const __m512i f1_b = _mm512_maskz_permutex2var_epi16(even_lanes, luma_b, idx_f1_b, chroma_b);
        const __m512i f2_b = _mm512_maskz_permutex2var_epi16(even_lanes, luma_b, idx_f2_b, chroma_b);
        const __m512i words_b = _mm512_ternarylogic_epi32(f0_b, _mm512_slli_epi32(f1_b, 10), _mm512_slli_epi32(f2_b, 20), 0xFE);
        _mm512_storeu_si512((void*)(out + 64), words_b);
        out += 128;
    }
    if (x + 24 <= w) {
        __m512i y0 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_loadu_si512((const void*)(in_y + x)), round_v), dzeta);
        __m512i y1 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32((__mmask16)0x00FFu, in_y + x + 16), round_v),
                                       dzeta);
        y0 = _mm512_max_epi32(_mm512_min_epi32(y0, max_v), zero_v);
        y1 = _mm512_max_epi32(_mm512_min_epi32(y1, max_v), zero_v);
        const __m512i luma = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi32_epi16(y0)), _mm512_cvtepi32_epi16(y1), 1);

        __m512i cb32 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32((__mmask16)0x0FFFu, in_cb + x / 2), round_v),
                                         dzeta);
        __m512i cr32 = _mm512_srai_epi32(_mm512_add_epi32(_mm512_maskz_loadu_epi32((__mmask16)0x0FFFu, in_cr + x / 2), round_v),
                                         dzeta);
        cb32 = _mm512_max_epi32(_mm512_min_epi32(cb32, max_v), zero_v);
        cr32 = _mm512_max_epi32(_mm512_min_epi32(cr32, max_v), zero_v);
        const __m512i chroma = _mm512_inserti64x4(
            _mm512_castsi256_si512(_mm512_cvtepi32_epi16(cb32)), _mm512_cvtepi32_epi16(cr32), 1);

        const __m512i f0 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f0_a, chroma);
        const __m512i f1 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f1_a, chroma);
        const __m512i f2 = _mm512_maskz_permutex2var_epi16(even_lanes, luma, idx_f2_a, chroma);
        const __m512i words = _mm512_ternarylogic_epi32(f0, _mm512_slli_epi32(f1, 10), _mm512_slli_epi32(f2, 20), 0xFE);
        _mm512_storeu_si512((void*)out, words);
        out += 64;
        x += 24;
    }
    if (x < w) {
        linear_output_scaling_v210_line_c(in_y + x, in_cb + x / 2, in_cr + x / 2, bw, out, w - x);
    }
}
