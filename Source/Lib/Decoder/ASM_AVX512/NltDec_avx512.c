/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltDec_avx512.h"

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
