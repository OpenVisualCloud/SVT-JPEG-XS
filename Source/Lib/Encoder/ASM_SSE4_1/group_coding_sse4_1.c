/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "group_coding_sse4_1.h"
#include "Definitions.h"
#include <immintrin.h>

void gc_precinct_sigflags_max_sse4_1(uint8_t *significance_data_max_ptr, uint8_t *gcli_data_ptr, uint32_t group_sign_size,
                                     uint32_t gcli_width) {
    UNUSED(group_sign_size);
    assert(group_sign_size == SIGNIFICANCE_GROUP_SIZE);
    const uint32_t group_number = gcli_width / SIGNIFICANCE_GROUP_SIZE;
    const uint32_t size_leftover = gcli_width % SIGNIFICANCE_GROUP_SIZE;
    const uint32_t simd_group_number = group_number / 8;
    uint8_t *in = gcli_data_ptr;

    for (uint32_t group = 0; group < simd_group_number; group++) {
        //transpose 8x8
        const __m128i a0 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)(in + 8 * 0)), _mm_loadl_epi64((__m128i *)(in + 8 * 1)));
        const __m128i a1 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)(in + 8 * 2)), _mm_loadl_epi64((__m128i *)(in + 8 * 3)));
        const __m128i a2 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)(in + 8 * 4)), _mm_loadl_epi64((__m128i *)(in + 8 * 5)));
        const __m128i a3 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)(in + 8 * 6)), _mm_loadl_epi64((__m128i *)(in + 8 * 7)));
        const __m128i b0 = _mm_unpacklo_epi16(a0, a1);
        const __m128i b1 = _mm_unpackhi_epi16(a0, a1);
        const __m128i b2 = _mm_unpacklo_epi16(a2, a3);
        const __m128i b3 = _mm_unpackhi_epi16(a2, a3);
        const __m128i out0 = _mm_unpacklo_epi32(b0, b2);
        const __m128i out1 = _mm_unpackhi_epi32(b0, b2);
        const __m128i out2 = _mm_unpacklo_epi32(b1, b3);
        const __m128i out3 = _mm_unpackhi_epi32(b1, b3);

        __m128i simd_max = _mm_max_epi8(_mm_max_epi8(out0, out1), _mm_max_epi8(out2, out3));
        simd_max = _mm_max_epi8(simd_max, _mm_srli_si128(simd_max, 8));

        _mm_storel_epi64((__m128i *)significance_data_max_ptr, simd_max);
        in += 8 * SIGNIFICANCE_GROUP_SIZE;
        significance_data_max_ptr += 8;
    }

    const int16_t max_val_u8 = 255;
    const __m128i max_val = _mm_set1_epi16(max_val_u8);
    for (uint32_t group = 0; group < group_number % 8; group++) {
        __m128i vals = _mm_cvtepi8_epi16(_mm_loadl_epi64((__m128i *)in));
        __m128i neg_vals = _mm_sub_epi16(max_val, vals);
        __m128i min_pos = _mm_minpos_epu16(neg_vals);
        int16_t neg_val = _mm_extract_epi16(min_pos, 0);
        significance_data_max_ptr[0] = (uint8_t)(max_val_u8 - neg_val);
        significance_data_max_ptr++;
        in += SIGNIFICANCE_GROUP_SIZE;
    }

    //Leftover last column
    if (size_leftover) {
        uint8_t max = 0;
        for (uint32_t j = 0; j < size_leftover; j++) {
            if (in[j] > max) {
                max = in[j];
            }
        }
        significance_data_max_ptr[0] = max;
    }
}
