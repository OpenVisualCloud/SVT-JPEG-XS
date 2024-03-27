/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dwt53Decoder_AVX2.h"
#include <immintrin.h>
#include <string.h>
#include "Definitions.h"
#include "Idwt.h"

void idwt_horizontal_line_lf32_hf16_avx2(const int32_t *lf_ptr, const int16_t *hf_ptr, int32_t *out_ptr, uint32_t len,
                                         uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    int32_t prev_even = lf_ptr[0] - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);

    const __m256i reg_permutevar_mask_move_right = _mm256_setr_epi32(0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
    const __m256i two = _mm256_set1_epi32(2);

    int32_t simd_batch = (len - 2) / 16;

    for (int32_t i = 0; i < simd_batch; i++) {
        const __m256i lf_avx2 = _mm256_loadu_si256((__m256i *)(lf_ptr + 1));
        const __m256i hf1_avx2 = _mm256_slli_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(hf_ptr))), shift);
        const __m256i hf2_avx2 = _mm256_slli_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(hf_ptr + 1))), shift);

        __m256i even = _mm256_add_epi32(hf1_avx2, hf2_avx2);
        even = _mm256_add_epi32(even, two);
        even = _mm256_srai_epi32(even, 2);
        even = _mm256_sub_epi32(lf_avx2, even);

        int32_t next_even = _mm256_extract_epi32(even, 7);
        __m256i even_m1 = _mm256_permutevar8x32_epi32(even, reg_permutevar_mask_move_right);
        even_m1 = _mm256_insert_epi32(even_m1, prev_even, 0);

        //out[0] + out[2]
        __m256i odd = _mm256_add_epi32(even_m1, even);
        odd = _mm256_srai_epi32(odd, 1);
        odd = _mm256_add_epi32(odd, hf1_avx2);

        const __m256i unpack1_avx2 = _mm256_unpacklo_epi32(even_m1, odd);
        const __m256i unpack2_avx2 = _mm256_unpackhi_epi32(even_m1, odd);

        _mm_storeu_si128((__m128i *)(out_ptr + 0), _mm256_castsi256_si128(unpack1_avx2));
        _mm_storeu_si128((__m128i *)(out_ptr + 4), _mm256_castsi256_si128(unpack2_avx2));
        _mm_storeu_si128((__m128i *)(out_ptr + 8), _mm256_extracti128_si256(unpack1_avx2, 0x1));
        _mm_storeu_si128((__m128i *)(out_ptr + 12), _mm256_extracti128_si256(unpack2_avx2, 0x1));

        prev_even = next_even;

        out_ptr += 16;
        lf_ptr += 8;
        hf_ptr += 8;
    }
    out_ptr[0] = prev_even;
    for (uint32_t i = simd_batch * 16 + 1; i < (len - 2); i += 2) {
        out_ptr[2] = lf_ptr[1] - ((((int32_t)hf_ptr[0] << shift) + ((int32_t)hf_ptr[1] << shift) + 2) >> 2);
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + ((out_ptr[0] + out_ptr[2]) >> 1);
        lf_ptr++;
        hf_ptr++;
        out_ptr += 2;
    }

    if (len & 1) {
        out_ptr[2] = lf_ptr[1] - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + ((out_ptr[0] + out_ptr[2]) >> 1);
    }
    else { //!(len & 1)
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + out_ptr[0];
    }
}

void idwt_horizontal_line_lf16_hf16_avx2(const int16_t *lf_ptr, const int16_t *hf_ptr, int32_t *out_ptr, uint32_t len,
                                         uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    int32_t prev_even = ((int32_t)lf_ptr[0] << shift) - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);

    const __m256i reg_permutevar_mask_move_right = _mm256_setr_epi32(0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
    const __m256i two = _mm256_set1_epi32(2);

    int32_t simd_batch = (len - 2) / 16;

    for (int32_t i = 0; i < simd_batch; i++) {
        const __m256i lf_avx2 = _mm256_slli_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(lf_ptr + 1))), shift);
        const __m256i hf1_avx2 = _mm256_slli_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(hf_ptr))), shift);
        const __m256i hf2_avx2 = _mm256_slli_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(hf_ptr + 1))), shift);

        __m256i even = _mm256_add_epi32(hf1_avx2, hf2_avx2);
        even = _mm256_add_epi32(even, two);
        even = _mm256_srai_epi32(even, 2);
        even = _mm256_sub_epi32(lf_avx2, even);

        int32_t next_even = _mm256_extract_epi32(even, 7);
        __m256i even_m1 = _mm256_permutevar8x32_epi32(even, reg_permutevar_mask_move_right);
        even_m1 = _mm256_insert_epi32(even_m1, prev_even, 0);

        //out[0] + out[2]
        __m256i odd = _mm256_add_epi32(even_m1, even);
        odd = _mm256_srai_epi32(odd, 1);
        odd = _mm256_add_epi32(odd, hf1_avx2);

        const __m256i unpack1_avx2 = _mm256_unpacklo_epi32(even_m1, odd);
        const __m256i unpack2_avx2 = _mm256_unpackhi_epi32(even_m1, odd);

        _mm_storeu_si128((__m128i *)(out_ptr + 0), _mm256_castsi256_si128(unpack1_avx2));
        _mm_storeu_si128((__m128i *)(out_ptr + 4), _mm256_castsi256_si128(unpack2_avx2));
        _mm_storeu_si128((__m128i *)(out_ptr + 8), _mm256_extracti128_si256(unpack1_avx2, 0x1));
        _mm_storeu_si128((__m128i *)(out_ptr + 12), _mm256_extracti128_si256(unpack2_avx2, 0x1));

        prev_even = next_even;

        out_ptr += 16;
        lf_ptr += 8;
        hf_ptr += 8;
    }
    out_ptr[0] = prev_even;
    for (uint32_t i = simd_batch * 16 + 1; i < (len - 2); i += 2) {
        out_ptr[2] = ((int32_t)lf_ptr[1] << shift) - ((((int32_t)hf_ptr[0] << shift) + ((int32_t)hf_ptr[1] << shift) + 2) >> 2);
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + ((out_ptr[0] + out_ptr[2]) >> 1);
        lf_ptr++;
        hf_ptr++;
        out_ptr += 2;
    }

    if (len & 1) {
        out_ptr[2] = ((int32_t)lf_ptr[1] << shift) - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + ((out_ptr[0] + out_ptr[2]) >> 1);
    }
    else { //!(len & 1)
        out_ptr[1] = ((int32_t)hf_ptr[0] << shift) + out_ptr[0];
    }
}

void idwt_vertical_line_avx2(const int32_t *in_lf, const int32_t *in_hf0, const int32_t *in_hf1, int32_t *out[4], uint32_t len,
                             int32_t first_precinct, int32_t last_precinct, int32_t height) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    uint32_t i = 0;
    uint32_t simd_batch = len / 8;
    uint32_t simd_leftover = len % 8;
    const __m256i one = _mm256_set1_epi32(1);
    const __m256i two = _mm256_set1_epi32(2);

    //Corner case: height is equal to 2
    if (height == 2) {
        int32_t *out_2 = out[2];
        int32_t *out_3 = out[3];
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m256i out2 = _mm256_add_epi32(hf1, one);
            out2 = _mm256_srai_epi32(out2, 1);
            out2 = _mm256_sub_epi32(lf, out2);

            //out_3[0] = in_hf1[0] + (out_2[0]);
            __m256i out3 = _mm256_add_epi32(hf1, out2);

            _mm256_storeu_si256((__m256i *)out_2, out2);
            _mm256_storeu_si256((__m256i *)out_3, out3);
            out_2 += 8;
            out_3 += 8;
            in_lf += 8;
            in_hf1 += 8;
        }
        for (i = 0; i < simd_leftover; i++) {
            out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_3[0] = in_hf1[0] + (out_2[0]);
            out_2++;
            out_3++;
            in_lf++;
            in_hf1++;
        }
        return;
    }
    //Corner case: first precinct in component
    if (first_precinct) {
        int32_t *out_2 = out[2];
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m256i out2 = _mm256_add_epi32(hf1, one);
            out2 = _mm256_srai_epi32(out2, 1);
            out2 = _mm256_sub_epi32(lf, out2);

            _mm256_storeu_si256((__m256i *)out_2, out2);
            out_2 += 8;
            in_lf += 8;
            in_hf1 += 8;
        }
        for (i = 0; i < simd_leftover; i++) {
            out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_2++;
            in_lf++;
            in_hf1++;
        }
        return;
    }
    //Corner case: last precinct in component, height odd
    if (last_precinct && (height & 1)) {
        int32_t *out_0 = out[0];
        int32_t *out_1 = out[1];
        int32_t *out_2 = out[2];
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf0 = _mm256_loadu_si256((__m256i *)in_hf0);

            //out_2[0] = in_lf[0] - ((in_hf0[0] + 1) >> 1);
            __m256i out2 = _mm256_add_epi32(hf0, one);
            out2 = _mm256_srai_epi32(out2, 1);
            out2 = _mm256_sub_epi32(lf, out2);

            //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            __m256i out0 = _mm256_loadu_si256((__m256i *)out_0);
            __m256i out1 = _mm256_add_epi32(out0, out2);
            out1 = _mm256_srai_epi32(out1, 1);
            out1 = _mm256_add_epi32(out1, hf0);

            _mm256_storeu_si256((__m256i *)out_2, out2);
            _mm256_storeu_si256((__m256i *)out_1, out1);
            out_0 += 8;
            out_1 += 8;
            out_2 += 8;
            in_lf += 8;
            in_hf0 += 8;
        }

        for (i = 0; i < simd_leftover; i++) {
            out_2[0] = in_lf[0] - ((in_hf0[0] + 1) >> 1);
            out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            out_0++;
            out_1++;
            out_2++;
            in_lf++;
            in_hf0++;
        }
        return;
    }
    //Corner case: last precinct in component, height even
    if (last_precinct && (!(height & 1))) {
        int32_t *out_0 = out[0];
        int32_t *out_1 = out[1];
        int32_t *out_2 = out[2];
        int32_t *out_3 = out[3];
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf0 = _mm256_loadu_si256((__m256i *)in_hf0);
            __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            __m256i out2 = _mm256_add_epi32(hf0, hf1);
            out2 = _mm256_add_epi32(out2, two);
            out2 = _mm256_srai_epi32(out2, 2);
            out2 = _mm256_sub_epi32(lf, out2);

            //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            __m256i out0 = _mm256_loadu_si256((__m256i *)out_0);
            __m256i out1 = _mm256_add_epi32(out0, out2);
            out1 = _mm256_srai_epi32(out1, 1);
            out1 = _mm256_add_epi32(out1, hf0);

            //out_3[0] = in_hf1[0] + (out_2[0]);
            __m256i out3 = _mm256_add_epi32(hf1, out2);

            _mm256_storeu_si256((__m256i *)out_2, out2);
            _mm256_storeu_si256((__m256i *)out_1, out1);
            _mm256_storeu_si256((__m256i *)out_3, out3);

            out_0 += 8;
            out_1 += 8;
            out_2 += 8;
            out_3 += 8;
            in_lf += 8;
            in_hf0 += 8;
            in_hf1 += 8;
        }

        for (i = 0; i < simd_leftover; i++) {
            out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            out_3[0] = in_hf1[0] + (out_2[0]);

            out_0++;
            out_1++;
            out_2++;
            out_3++;
            in_lf++;
            in_hf0++;
            in_hf1++;
        }
        return;
    }
    int32_t *out_0 = out[0];
    int32_t *out_1 = out[1];
    int32_t *out_2 = out[2];
    for (; i < simd_batch; i++) {
        __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
        __m256i hf0 = _mm256_loadu_si256((__m256i *)in_hf0);
        __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

        //out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
        __m256i out2 = _mm256_add_epi32(hf0, hf1);
        out2 = _mm256_add_epi32(out2, two);
        out2 = _mm256_srai_epi32(out2, 2);
        out2 = _mm256_sub_epi32(lf, out2);

        //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
        __m256i out0 = _mm256_loadu_si256((__m256i *)out_0);
        __m256i out1 = _mm256_add_epi32(out0, out2);
        out1 = _mm256_srai_epi32(out1, 1);
        out1 = _mm256_add_epi32(out1, hf0);

        _mm256_storeu_si256((__m256i *)out_2, out2);
        _mm256_storeu_si256((__m256i *)out_1, out1);

        out_0 += 8;
        out_1 += 8;
        out_2 += 8;
        in_lf += 8;
        in_hf0 += 8;
        in_hf1 += 8;
    }

    for (i = 0; i < simd_leftover; i++) {
        out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
        out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
        out_0++;
        out_1++;
        out_2++;
        in_lf++;
        in_hf0++;
        in_hf1++;
    }
}

void idwt_vertical_line_recalc_avx2(const int32_t *in_lf, const int32_t *in_hf0, const int32_t *in_hf1, int32_t *out[4],
                                    uint32_t len, uint32_t precinct_line_idx) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    uint32_t i = 0;
    uint32_t simd_batch = len / 8;
    uint32_t simd_leftover = len % 8;
    const __m256i one = _mm256_set1_epi32(1);
    const __m256i two = _mm256_set1_epi32(2);

    int32_t *out_0 = out[0];
    if (precinct_line_idx > 1) {
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf0 = _mm256_loadu_si256((__m256i *)in_hf0);
            __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

            //out_0[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            __m256i out0 = _mm256_add_epi32(hf0, hf1);
            out0 = _mm256_add_epi32(out0, two);
            out0 = _mm256_srai_epi32(out0, 2);
            out0 = _mm256_sub_epi32(lf, out0);
            _mm256_storeu_si256((__m256i *)out_0, out0);
            out_0 += 8;
            in_lf += 8;
            in_hf0 += 8;
            in_hf1 += 8;
        }

        for (i = 0; i < simd_leftover; i++) {
            out_0[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            out_0++;
            in_lf++;
            in_hf0++;
            in_hf1++;
        }
    }
    else {
        for (; i < simd_batch; i++) {
            __m256i lf = _mm256_loadu_si256((__m256i *)in_lf);
            __m256i hf1 = _mm256_loadu_si256((__m256i *)in_hf1);

            //out_0[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m256i out0 = _mm256_add_epi32(hf1, one);
            out0 = _mm256_srai_epi32(out0, 1);
            out0 = _mm256_sub_epi32(lf, out0);
            _mm256_storeu_si256((__m256i *)out_0, out0);
            out_0 += 8;
            in_lf += 8;
            in_hf1 += 8;
        }

        for (i = 0; i < simd_leftover; i++) {
            out_0[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_0++;
            in_lf++;
            in_hf1++;
        }
    }
}
