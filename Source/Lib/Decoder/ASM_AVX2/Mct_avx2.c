/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Mct_avx2.h"
#include "Definitions.h"

/* Table F.2 - Inverse reversible multiple component transformation (Cpih=1). Every pixel is
 * independent (no neighbour access, unlike the CFA/star-tetrix path), and comps[0..2] are flat
 * w*h buffers with no row padding, so this can vectorize as one linear pass instead of per-row. */
void inverse_rct_avx2(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h) {
    int32_t* c0 = comps[0];
    int32_t* c1 = comps[1];
    int32_t* c2 = comps[2];
    const int64_t total = (int64_t)w * h;
    int64_t i = 0;

    for (; i <= total - 8; i += 8) {
        __m256i i0 = _mm256_loadu_si256((__m256i*)(c0 + i));
        __m256i i1 = _mm256_loadu_si256((__m256i*)(c1 + i));
        __m256i i2 = _mm256_loadu_si256((__m256i*)(c2 + i));

        __m256i o1 = _mm256_sub_epi32(i0, _mm256_srai_epi32(_mm256_add_epi32(i1, i2), 2));
        __m256i o0 = _mm256_add_epi32(o1, i2);
        __m256i o2 = _mm256_add_epi32(o1, i1);

        _mm256_storeu_si256((__m256i*)(c0 + i), o0);
        _mm256_storeu_si256((__m256i*)(c1 + i), o1);
        _mm256_storeu_si256((__m256i*)(c2 + i), o2);
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

static INLINE __m128i pack_epi32x2_to_epu8_avx2(__m256i lo, __m256i hi) {
    const __m256i p16 = _mm256_permute4x64_epi64(_mm256_packus_epi32(lo, hi), 0xD8);
    return _mm_packus_epi16(_mm256_castsi256_si128(p16), _mm256_extracti128_si256(p16, 1));
}

void inverse_rct_output_8bit_line_avx2(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                       uint8_t* out0, uint8_t* out1, uint8_t* out2, uint32_t width) {
    const int32_t dzeta = (int32_t)(bw - depth);
    const int32_t round = (int32_t)(((1u << bw) >> 1) + ((1u << dzeta) >> 1));
    const int32_t m = (1 << depth) - 1;
    const __m256i round_v = _mm256_set1_epi32(round);
    const __m256i zero_v = _mm256_setzero_si256();
    const __m256i max_v = _mm256_set1_epi32(m);
    uint32_t x = 0;

    for (; x + 16 <= width; x += 16) {
        const __m256i i0_a = _mm256_loadu_si256((const __m256i*)(in0 + x));
        const __m256i i1_a = _mm256_loadu_si256((const __m256i*)(in1 + x));
        const __m256i i2_a = _mm256_loadu_si256((const __m256i*)(in2 + x));
        const __m256i o1_a = _mm256_sub_epi32(i0_a, _mm256_srai_epi32(_mm256_add_epi32(i1_a, i2_a), 2));
        const __m256i o0_a = _mm256_add_epi32(o1_a, i2_a);
        const __m256i o2_a = _mm256_add_epi32(o1_a, i1_a);
        const __m256i s0_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o0_a, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s1_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o1_a, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s2_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o2_a, round_v), dzeta), max_v),
                                              zero_v);

        const __m256i i0_b = _mm256_loadu_si256((const __m256i*)(in0 + x + 8));
        const __m256i i1_b = _mm256_loadu_si256((const __m256i*)(in1 + x + 8));
        const __m256i i2_b = _mm256_loadu_si256((const __m256i*)(in2 + x + 8));
        const __m256i o1_b = _mm256_sub_epi32(i0_b, _mm256_srai_epi32(_mm256_add_epi32(i1_b, i2_b), 2));
        const __m256i o0_b = _mm256_add_epi32(o1_b, i2_b);
        const __m256i o2_b = _mm256_add_epi32(o1_b, i1_b);
        const __m256i s0_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o0_b, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s1_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o1_b, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s2_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o2_b, round_v), dzeta), max_v),
                                              zero_v);

        _mm_storeu_si128((__m128i*)(out0 + x), pack_epi32x2_to_epu8_avx2(s0_a, s0_b));
        _mm_storeu_si128((__m128i*)(out1 + x), pack_epi32x2_to_epu8_avx2(s1_a, s1_b));
        _mm_storeu_si128((__m128i*)(out2 + x), pack_epi32x2_to_epu8_avx2(s2_a, s2_b));
    }
    for (; x < width; x++) {
        const int32_t i0 = in0[x];
        const int32_t i1 = in1[x];
        const int32_t i2 = in2[x];
        const int32_t o1 = i0 - ((i1 + i2) >> 2);
        const int32_t o0 = o1 + i2;
        const int32_t o2 = o1 + i1;
        const int32_t v0 = (o0 + round) >> dzeta;
        const int32_t v1 = (o1 + round) >> dzeta;
        const int32_t v2 = (o2 + round) >> dzeta;
        out0[x] = (uint8_t)(v0 > m ? m : v0 < 0 ? 0 : v0);
        out1[x] = (uint8_t)(v1 > m ? m : v1 < 0 ? 0 : v1);
        out2[x] = (uint8_t)(v2 > m ? m : v2 < 0 ? 0 : v2);
    }
}

void inverse_rct_output_16bit_line_avx2(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                        uint16_t* out0, uint16_t* out1, uint16_t* out2, uint32_t width, uint8_t msb_aligned) {
    const int32_t dzeta = (int32_t)(bw - depth);
    const int32_t round = (int32_t)(((1u << bw) >> 1) + ((1u << dzeta) >> 1));
    const int32_t m = (1 << depth) - 1;
    const int32_t msb_shift = msb_aligned ? (int32_t)(16 - depth) : 0;
    const __m256i round_v = _mm256_set1_epi32(round);
    const __m256i zero_v = _mm256_setzero_si256();
    const __m256i max_v = _mm256_set1_epi32(m);
    uint32_t x = 0;

    for (; x + 16 <= width; x += 16) {
        const __m256i i0_a = _mm256_loadu_si256((const __m256i*)(in0 + x));
        const __m256i i1_a = _mm256_loadu_si256((const __m256i*)(in1 + x));
        const __m256i i2_a = _mm256_loadu_si256((const __m256i*)(in2 + x));
        const __m256i o1_a = _mm256_sub_epi32(i0_a, _mm256_srai_epi32(_mm256_add_epi32(i1_a, i2_a), 2));
        const __m256i o0_a = _mm256_add_epi32(o1_a, i2_a);
        const __m256i o2_a = _mm256_add_epi32(o1_a, i1_a);
        const __m256i s0_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o0_a, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s1_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o1_a, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s2_a = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o2_a, round_v), dzeta), max_v),
                                              zero_v);

        const __m256i i0_b = _mm256_loadu_si256((const __m256i*)(in0 + x + 8));
        const __m256i i1_b = _mm256_loadu_si256((const __m256i*)(in1 + x + 8));
        const __m256i i2_b = _mm256_loadu_si256((const __m256i*)(in2 + x + 8));
        const __m256i o1_b = _mm256_sub_epi32(i0_b, _mm256_srai_epi32(_mm256_add_epi32(i1_b, i2_b), 2));
        const __m256i o0_b = _mm256_add_epi32(o1_b, i2_b);
        const __m256i o2_b = _mm256_add_epi32(o1_b, i1_b);
        const __m256i s0_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o0_b, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s1_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o1_b, round_v), dzeta), max_v),
                                              zero_v);
        const __m256i s2_b = _mm256_max_epi32(_mm256_min_epi32(_mm256_srai_epi32(_mm256_add_epi32(o2_b, round_v), dzeta), max_v),
                                              zero_v);

        __m256i p0 = _mm256_permute4x64_epi64(_mm256_packus_epi32(s0_a, s0_b), 0xD8);
        __m256i p1 = _mm256_permute4x64_epi64(_mm256_packus_epi32(s1_a, s1_b), 0xD8);
        __m256i p2 = _mm256_permute4x64_epi64(_mm256_packus_epi32(s2_a, s2_b), 0xD8);
        if (msb_shift) {
            p0 = _mm256_slli_epi16(p0, msb_shift);
            p1 = _mm256_slli_epi16(p1, msb_shift);
            p2 = _mm256_slli_epi16(p2, msb_shift);
        }
        _mm256_storeu_si256((__m256i*)(out0 + x), p0);
        _mm256_storeu_si256((__m256i*)(out1 + x), p1);
        _mm256_storeu_si256((__m256i*)(out2 + x), p2);
    }
    for (; x < width; x++) {
        const int32_t i0 = in0[x];
        const int32_t i1 = in1[x];
        const int32_t i2 = in2[x];
        const int32_t o1 = i0 - ((i1 + i2) >> 2);
        const int32_t o0 = o1 + i2;
        const int32_t o2 = o1 + i1;
        const int32_t v0 = (o0 + round) >> dzeta;
        const int32_t v1 = (o1 + round) >> dzeta;
        const int32_t v2 = (o2 + round) >> dzeta;
        out0[x] = (uint16_t)((v0 > m ? m : v0 < 0 ? 0 : v0) << msb_shift);
        out1[x] = (uint16_t)((v1 > m ? m : v1 < 0 ? 0 : v1) << msb_shift);
        out2[x] = (uint16_t)((v2 > m ? m : v2 < 0 ? 0 : v2) << msb_shift);
    }
}
