/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltDec_avx512.h"

void linear_output_scaling_8bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m512i round_avx2 = _mm512_set1_epi32(round);
    const __m512i zero_avx2 = _mm512_setzero_si512();
    const __m512i max_avx2 = _mm512_set1_epi32(m);

    for (x = 0; x <= (int32_t)w - 16; x += 16) {
        __m512i v1_avx2 = _mm512_loadu_si512((__m512i*)(in + x));
        v1_avx2 = _mm512_add_epi32(v1_avx2, round_avx2);
        v1_avx2 = _mm512_srai_epi32(v1_avx2, dzeta);
        v1_avx2 = _mm512_max_epi32(_mm512_min_epi32(v1_avx2, max_avx2), zero_avx2);
        _mm_storeu_si128((__m128i*)(out + x), _mm512_cvtepi32_epi8(v1_avx2));
    }
    for (; x < (int32_t)w; x++) {
        int32_t v = in[x];
        v += round;
        v >>= dzeta;
        out[x] = (uint8_t)(v > m ? m : v < 0 ? 0 : v);
    }
}

void linear_output_scaling_16bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m512i round_avx2 = _mm512_set1_epi32(round);
    const __m512i zero_avx2 = _mm512_setzero_si512();
    const __m512i max_avx2 = _mm512_set1_epi32(m);

    for (x = 0; x <= (int32_t)w - 16; x += 16) {
        __m512i v1_avx2 = _mm512_loadu_si512((__m512i*)(in + x));
        v1_avx2 = _mm512_add_epi32(v1_avx2, round_avx2);
        v1_avx2 = _mm512_srai_epi32(v1_avx2, dzeta);
        v1_avx2 = _mm512_max_epi32(_mm512_min_epi32(v1_avx2, max_avx2), zero_avx2);

        _mm256_storeu_si256((__m256i*)(out + x), _mm512_cvtepi32_epi16(v1_avx2));
    }
    for (; x < (int32_t)w; x++) {
        int32_t v = in[x];
        v += round;
        v >>= dzeta;
        out[x] = (uint16_t)(v > m ? m : v < 0 ? 0 : v);
    }
}
