/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Mct_avx512.h"

/* Table F.2 - Inverse reversible multiple component transformation (Cpih=1). Every pixel is
 * independent (no neighbour access, unlike the CFA/star-tetrix path), and comps[0..2] are flat
 * w*h buffers with no row padding, so this can vectorize as one linear pass instead of per-row. */
void inverse_rct_avx512(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h) {
    int32_t* c0 = comps[0];
    int32_t* c1 = comps[1];
    int32_t* c2 = comps[2];
    const int64_t total = (int64_t)w * h;
    int64_t i = 0;

    for (; i <= total - 16; i += 16) {
        __m512i i0 = _mm512_loadu_si512((__m512i const*)(c0 + i));
        __m512i i1 = _mm512_loadu_si512((__m512i const*)(c1 + i));
        __m512i i2 = _mm512_loadu_si512((__m512i const*)(c2 + i));

        __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        __m512i o0 = _mm512_add_epi32(o1, i2);
        __m512i o2 = _mm512_add_epi32(o1, i1);

        _mm512_storeu_si512((__m512i*)(c0 + i), o0);
        _mm512_storeu_si512((__m512i*)(c1 + i), o1);
        _mm512_storeu_si512((__m512i*)(c2 + i), o2);
    }
    for (; i < total; i++) {
        int32_t i0 = c0[i];
        int32_t i1 = c1[i];
        int32_t i2 = c2[i];

        int32_t o1 = i0 - ((i1 + i2) >> 2);
        int32_t o0 = o1 + i2;
        int32_t o2 = o1 + i1;

        c0[i] = o0;
        c1[i] = o1;
        c2[i] = o2;
    }
}

void inverse_rct_output_8bit_line_avx512(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                         uint8_t* out0, uint8_t* out1, uint8_t* out2, uint32_t width) {
    const int32_t dzeta = (int32_t)(bw - depth);
    const int32_t round = (int32_t)(((1u << bw) >> 1) + ((1u << dzeta) >> 1));
    const int32_t m = (1 << depth) - 1;
    const __m512i round_v = _mm512_set1_epi32(round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(m);
    uint32_t x = 0;

    for (; x + 16 <= width; x += 16) {
        const __m512i i0 = _mm512_loadu_si512((const void*)(in0 + x));
        const __m512i i1 = _mm512_loadu_si512((const void*)(in1 + x));
        const __m512i i2 = _mm512_loadu_si512((const void*)(in2 + x));

        const __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        const __m512i o0 = _mm512_add_epi32(o1, i2);
        const __m512i o2 = _mm512_add_epi32(o1, i1);

        const __m512i s0 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o0, round_v), dzeta), max_v),
                                            zero_v);
        const __m512i s1 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o1, round_v), dzeta), max_v),
                                            zero_v);
        const __m512i s2 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o2, round_v), dzeta), max_v),
                                            zero_v);

        _mm_storeu_si128((__m128i*)(out0 + x), _mm512_cvtepi32_epi8(s0));
        _mm_storeu_si128((__m128i*)(out1 + x), _mm512_cvtepi32_epi8(s1));
        _mm_storeu_si128((__m128i*)(out2 + x), _mm512_cvtepi32_epi8(s2));
    }
    if (x < width) {
        const __mmask16 km = (__mmask16)((1u << (width - x)) - 1);
        const __m512i i0 = _mm512_maskz_loadu_epi32(km, in0 + x);
        const __m512i i1 = _mm512_maskz_loadu_epi32(km, in1 + x);
        const __m512i i2 = _mm512_maskz_loadu_epi32(km, in2 + x);

        const __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        const __m512i o0 = _mm512_add_epi32(o1, i2);
        const __m512i o2 = _mm512_add_epi32(o1, i1);

        const __m512i s0 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o0, round_v), dzeta), max_v),
                                            zero_v);
        const __m512i s1 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o1, round_v), dzeta), max_v),
                                            zero_v);
        const __m512i s2 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o2, round_v), dzeta), max_v),
                                            zero_v);

        _mm512_mask_cvtepi32_storeu_epi8(out0 + x, km, s0);
        _mm512_mask_cvtepi32_storeu_epi8(out1 + x, km, s1);
        _mm512_mask_cvtepi32_storeu_epi8(out2 + x, km, s2);
    }
}

void inverse_rct_output_16bit_line_avx512(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                          uint16_t* out0, uint16_t* out1, uint16_t* out2, uint32_t width, uint8_t msb_aligned) {
    const int32_t dzeta = (int32_t)(bw - depth);
    const int32_t round = (int32_t)(((1u << bw) >> 1) + ((1u << dzeta) >> 1));
    const int32_t m = (1 << depth) - 1;
    const int32_t msb_shift = msb_aligned ? (int32_t)(16 - depth) : 0;
    const __m512i round_v = _mm512_set1_epi32(round);
    const __m512i zero_v = _mm512_setzero_si512();
    const __m512i max_v = _mm512_set1_epi32(m);
    uint32_t x = 0;

    for (; x + 16 <= width; x += 16) {
        const __m512i i0 = _mm512_loadu_si512((const void*)(in0 + x));
        const __m512i i1 = _mm512_loadu_si512((const void*)(in1 + x));
        const __m512i i2 = _mm512_loadu_si512((const void*)(in2 + x));

        const __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        const __m512i o0 = _mm512_add_epi32(o1, i2);
        const __m512i o2 = _mm512_add_epi32(o1, i1);

        __m512i s0 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o0, round_v), dzeta), max_v), zero_v);
        __m512i s1 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o1, round_v), dzeta), max_v), zero_v);
        __m512i s2 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o2, round_v), dzeta), max_v), zero_v);

        __m256i p0 = _mm512_cvtepi32_epi16(s0);
        __m256i p1 = _mm512_cvtepi32_epi16(s1);
        __m256i p2 = _mm512_cvtepi32_epi16(s2);
        if (msb_shift) {
            p0 = _mm256_slli_epi16(p0, msb_shift);
            p1 = _mm256_slli_epi16(p1, msb_shift);
            p2 = _mm256_slli_epi16(p2, msb_shift);
        }
        _mm256_storeu_si256((__m256i*)(out0 + x), p0);
        _mm256_storeu_si256((__m256i*)(out1 + x), p1);
        _mm256_storeu_si256((__m256i*)(out2 + x), p2);
    }
    if (x < width) {
        const __mmask16 km = (__mmask16)((1u << (width - x)) - 1);
        const __m512i i0 = _mm512_maskz_loadu_epi32(km, in0 + x);
        const __m512i i1 = _mm512_maskz_loadu_epi32(km, in1 + x);
        const __m512i i2 = _mm512_maskz_loadu_epi32(km, in2 + x);

        const __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        const __m512i o0 = _mm512_add_epi32(o1, i2);
        const __m512i o2 = _mm512_add_epi32(o1, i1);

        __m512i s0 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o0, round_v), dzeta), max_v), zero_v);
        __m512i s1 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o1, round_v), dzeta), max_v), zero_v);
        __m512i s2 = _mm512_max_epi32(_mm512_min_epi32(_mm512_srai_epi32(_mm512_add_epi32(o2, round_v), dzeta), max_v), zero_v);

        __m256i p0 = _mm512_cvtepi32_epi16(s0);
        __m256i p1 = _mm512_cvtepi32_epi16(s1);
        __m256i p2 = _mm512_cvtepi32_epi16(s2);
        if (msb_shift) {
            p0 = _mm256_slli_epi16(p0, msb_shift);
            p1 = _mm256_slli_epi16(p1, msb_shift);
            p2 = _mm256_slli_epi16(p2, msb_shift);
        }
        _mm256_mask_storeu_epi16(out0 + x, km, p0);
        _mm256_mask_storeu_epi16(out1 + x, km, p1);
        _mm256_mask_storeu_epi16(out2 + x, km, p2);
    }
}
