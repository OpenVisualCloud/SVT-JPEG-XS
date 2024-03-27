/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "idwt-avx512.h"
#include <immintrin.h>
#include "Definitions.h"

uint32_t loop_short_lf32_hf16(uint32_t len, const int32_t** lf_ptr, const int16_t** hf_ptr, int32_t** out_ptr, int32_t* prev_even,
                              uint8_t shift) {
    const uint32_t batch = (len - 2) / 8;
    const __m128i two = _mm_set1_epi32(2);

    for (uint32_t i = 0; i < batch; i++) {
        const __m128i lf_ = _mm_loadu_si128((__m128i*)(*lf_ptr + 1));
        const __m128i hf1_ = _mm_slli_epi32(_mm_cvtepi16_epi32(_mm_loadl_epi64((__m128i*)(*hf_ptr))), shift);
        const __m128i hf2_ = _mm_slli_epi32(_mm_cvtepi16_epi32(_mm_loadl_epi64((__m128i*)(*hf_ptr + 1))), shift);

        __m128i even = _mm_add_epi32(hf1_, hf2_);
        even = _mm_add_epi32(even, two);
        even = _mm_srai_epi32(even, 2);
        even = _mm_sub_epi32(lf_, even);

        int32_t next_even = _mm_extract_epi32(even, 3);
        __m128i even_m1 = _mm_bslli_si128(even, 4);
        even_m1 = _mm_insert_epi32(even_m1, *prev_even, 0);

        //out[0] + out[2]
        __m128i odd = _mm_add_epi32(even_m1, even);
        odd = _mm_srai_epi32(odd, 1);
        odd = _mm_add_epi32(odd, hf1_);

        const __m128i unpack1 = _mm_unpacklo_epi32(even_m1, odd);
        const __m128i unpack2 = _mm_unpackhi_epi32(even_m1, odd);

        _mm_storeu_si128((__m128i*)(*out_ptr + 0), unpack1);
        _mm_storeu_si128((__m128i*)(*out_ptr + 4), unpack2);

        *prev_even = next_even;

        *out_ptr += 8;
        *lf_ptr += 4;
        *hf_ptr += 4;
    }
    return batch;
}

void idwt_horizontal_line_lf32_hf16_avx512(const int32_t* lf_ptr, const int16_t* hf_ptr, int32_t* out_ptr, uint32_t len,
                                           uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    int32_t prev_even = lf_ptr[0] - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);

    const __m512i permutevar_mask = _mm512_setr_epi32(
        0x10, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e);
    const __m512i store1_perm = _mm512_setr_epi64(0x00, 0x01, 0x08, 0x09, 0x02, 0x03, 0x0a, 0x0b);
    const __m512i store2_perm = _mm512_setr_epi64(0x04, 0x05, 0x0c, 0x0d, 0x06, 0x07, 0x0e, 0x0f);

    const __m512i two = _mm512_set1_epi32(2);

    int32_t simd_batch_512 = (len - 2) / 32;

    for (int32_t i = 0; i < simd_batch_512; i++) {
        const __m512i lf_avx2 = _mm512_loadu_si512((__m512i*)(lf_ptr + 1));
        const __m512i hf1_avx2 = _mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(hf_ptr))), shift);
        const __m512i hf2_avx2 = _mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(hf_ptr + 1))), shift);

        __m512i even = _mm512_add_epi32(hf1_avx2, hf2_avx2);
        even = _mm512_add_epi32(even, two);
        even = _mm512_srai_epi32(even, 2);
        even = _mm512_sub_epi32(lf_avx2, even);

        int32_t next_even = _mm_extract_epi32(_mm512_extracti32x4_epi32(even, 3), 3);

        __m512i duplicate = _mm512_set1_epi32(prev_even);
        __m512i even_m1 = _mm512_permutex2var_epi32(even, permutevar_mask, duplicate);

        //out[0] + out[2]
        __m512i odd = _mm512_add_epi32(even_m1, even);
        odd = _mm512_srai_epi32(odd, 1);
        odd = _mm512_add_epi32(odd, hf1_avx2);

        const __m512i unpack1 = _mm512_unpacklo_epi32(even_m1, odd);
        const __m512i unpack2 = _mm512_unpackhi_epi32(even_m1, odd);

        __m512i store1 = _mm512_permutex2var_epi64(unpack1, store1_perm, unpack2);
        __m512i store2 = _mm512_permutex2var_epi64(unpack1, store2_perm, unpack2);

        _mm512_storeu_si512(out_ptr, store1);
        _mm512_storeu_si512(out_ptr + 16, store2);

        prev_even = next_even;

        out_ptr += 32;
        lf_ptr += 16;
        hf_ptr += 16;
    }

    uint32_t leftover = len - simd_batch_512 * 32;
    if (leftover > 8) {
        leftover -= 8 * loop_short_lf32_hf16(leftover, &lf_ptr, &hf_ptr, &out_ptr, &prev_even, shift);
    }
    out_ptr[0] = prev_even;

    for (uint32_t i = 1; i < (leftover - 2); i += 2) {
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

uint32_t loop_short_lf16_hf16(uint32_t len, const int16_t** lf_ptr, const int16_t** hf_ptr, int32_t** out_ptr, int32_t* prev_even,
                              uint8_t shift) {
    const uint32_t batch = (len - 2) / 8;
    const __m128i two = _mm_set1_epi32(2);

    for (uint32_t i = 0; i < batch; i++) {
        const __m128i lf_ = _mm_slli_epi32(_mm_cvtepi16_epi32(_mm_loadl_epi64((__m128i*)(*lf_ptr + 1))), shift);
        const __m128i hf1_ = _mm_slli_epi32(_mm_cvtepi16_epi32(_mm_loadl_epi64((__m128i*)(*hf_ptr))), shift);
        const __m128i hf2_ = _mm_slli_epi32(_mm_cvtepi16_epi32(_mm_loadl_epi64((__m128i*)(*hf_ptr + 1))), shift);

        __m128i even = _mm_add_epi32(hf1_, hf2_);
        even = _mm_add_epi32(even, two);
        even = _mm_srai_epi32(even, 2);
        even = _mm_sub_epi32(lf_, even);

        int32_t next_even = _mm_extract_epi32(even, 3);
        __m128i even_m1 = _mm_bslli_si128(even, 4);
        even_m1 = _mm_insert_epi32(even_m1, *prev_even, 0);

        //out[0] + out[2]
        __m128i odd = _mm_add_epi32(even_m1, even);
        odd = _mm_srai_epi32(odd, 1);
        odd = _mm_add_epi32(odd, hf1_);

        const __m128i unpack1 = _mm_unpacklo_epi32(even_m1, odd);
        const __m128i unpack2 = _mm_unpackhi_epi32(even_m1, odd);

        _mm_storeu_si128((__m128i*)(*out_ptr + 0), unpack1);
        _mm_storeu_si128((__m128i*)(*out_ptr + 4), unpack2);

        *prev_even = next_even;

        *out_ptr += 8;
        *lf_ptr += 4;
        *hf_ptr += 4;
    }
    return batch;
}

void idwt_horizontal_line_lf16_hf16_avx512(const int16_t* lf_ptr, const int16_t* hf_ptr, int32_t* out_ptr, uint32_t len,
                                           uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    int32_t prev_even = ((int32_t)lf_ptr[0] << shift) - ((((int32_t)hf_ptr[0] << shift) + 1) >> 1);

    const __m512i permutevar_mask = _mm512_setr_epi32(
        0x10, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e);
    const __m512i store1_perm = _mm512_setr_epi64(0x00, 0x01, 0x08, 0x09, 0x02, 0x03, 0x0a, 0x0b);
    const __m512i store2_perm = _mm512_setr_epi64(0x04, 0x05, 0x0c, 0x0d, 0x06, 0x07, 0x0e, 0x0f);

    const __m512i two = _mm512_set1_epi32(2);

    int32_t simd_batch_512 = (len - 2) / 32;

    for (int32_t i = 0; i < simd_batch_512; i++) {
        const __m512i lf_avx2 = _mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(lf_ptr + 1))), shift);
        const __m512i hf1_avx2 = _mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(hf_ptr))), shift);
        const __m512i hf2_avx2 = _mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(hf_ptr + 1))), shift);

        __m512i even = _mm512_add_epi32(hf1_avx2, hf2_avx2);
        even = _mm512_add_epi32(even, two);
        even = _mm512_srai_epi32(even, 2);
        even = _mm512_sub_epi32(lf_avx2, even);

        int32_t next_even = _mm_extract_epi32(_mm512_extracti32x4_epi32(even, 3), 3);

        __m512i duplicate = _mm512_set1_epi32(prev_even);
        __m512i even_m1 = _mm512_permutex2var_epi32(even, permutevar_mask, duplicate);

        //out[0] + out[2]
        __m512i odd = _mm512_add_epi32(even_m1, even);
        odd = _mm512_srai_epi32(odd, 1);
        odd = _mm512_add_epi32(odd, hf1_avx2);

        const __m512i unpack1 = _mm512_unpacklo_epi32(even_m1, odd);
        const __m512i unpack2 = _mm512_unpackhi_epi32(even_m1, odd);

        __m512i store1 = _mm512_permutex2var_epi64(unpack1, store1_perm, unpack2);
        __m512i store2 = _mm512_permutex2var_epi64(unpack1, store2_perm, unpack2);

        _mm512_storeu_si512(out_ptr, store1);
        _mm512_storeu_si512(out_ptr + 16, store2);

        prev_even = next_even;

        out_ptr += 32;
        lf_ptr += 16;
        hf_ptr += 16;
    }

    uint32_t leftover = len - simd_batch_512 * 32;
    if (leftover > 8) {
        leftover -= 8 * loop_short_lf16_hf16(leftover, &lf_ptr, &hf_ptr, &out_ptr, &prev_even, shift);
    }
    out_ptr[0] = prev_even;

    for (uint32_t i = 1; i < (leftover - 2); i += 2) {
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

void idwt_vertical_line_avx512(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4], uint32_t len,
                               int32_t first_precinct, int32_t last_precinct, int32_t height) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    uint32_t i = 0;
    uint32_t simd_batch = len / 16;
    uint32_t simd_leftover = len % 16;
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);

    //Corner case: height is equal to 2
    if (height == 2) {
        int32_t* out_2 = out[2];
        int32_t* out_3 = out[3];
        for (; i < simd_batch; i++) {
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf1 = _mm512_loadu_si512(in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m512i out2 = _mm512_add_epi32(hf1, one);
            out2 = _mm512_srai_epi32(out2, 1);
            out2 = _mm512_sub_epi32(lf, out2);

            //out_3[0] = in_hf1[0] + (out_2[0]);
            __m512i out3 = _mm512_add_epi32(hf1, out2);

            _mm512_storeu_si512(out_2, out2);
            _mm512_storeu_si512(out_3, out3);
            out_2 += 16;
            out_3 += 16;
            in_lf += 16;
            in_hf1 += 16;
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
        int32_t* out_2 = out[2];
        for (; i < simd_batch; i++) {
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf1 = _mm512_loadu_si512(in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m512i out2 = _mm512_add_epi32(hf1, one);
            out2 = _mm512_srai_epi32(out2, 1);
            out2 = _mm512_sub_epi32(lf, out2);

            _mm512_storeu_si512(out_2, out2);
            out_2 += 16;
            in_lf += 16;
            in_hf1 += 16;
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
        int32_t* out_0 = out[0];
        int32_t* out_1 = out[1];
        int32_t* out_2 = out[2];
        for (; i < simd_batch; i++) {
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf0 = _mm512_loadu_si512(in_hf0);

            //out_2[0] = in_lf[0] - ((in_hf0[0] + 1) >> 1);
            __m512i out2 = _mm512_add_epi32(hf0, one);
            out2 = _mm512_srai_epi32(out2, 1);
            out2 = _mm512_sub_epi32(lf, out2);

            //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            __m512i out0 = _mm512_loadu_si512(out_0);
            __m512i out1 = _mm512_add_epi32(out0, out2);
            out1 = _mm512_srai_epi32(out1, 1);
            out1 = _mm512_add_epi32(out1, hf0);

            _mm512_storeu_si512(out_2, out2);
            _mm512_storeu_si512(out_1, out1);
            out_0 += 16;
            out_1 += 16;
            out_2 += 16;
            in_lf += 16;
            in_hf0 += 16;
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
        int32_t* out_0 = out[0];
        int32_t* out_1 = out[1];
        int32_t* out_2 = out[2];
        int32_t* out_3 = out[3];
        for (; i < simd_batch; i++) {
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf0 = _mm512_loadu_si512(in_hf0);
            __m512i hf1 = _mm512_loadu_si512(in_hf1);

            //out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            __m512i out2 = _mm512_add_epi32(hf0, hf1);
            out2 = _mm512_add_epi32(out2, two);
            out2 = _mm512_srai_epi32(out2, 2);
            out2 = _mm512_sub_epi32(lf, out2);

            //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            __m512i out0 = _mm512_loadu_si512(out_0);
            __m512i out1 = _mm512_add_epi32(out0, out2);
            out1 = _mm512_srai_epi32(out1, 1);
            out1 = _mm512_add_epi32(out1, hf0);

            //out_3[0] = in_hf1[0] + (out_2[0]);
            __m512i out3 = _mm512_add_epi32(hf1, out2);

            _mm512_storeu_si512(out_2, out2);
            _mm512_storeu_si512(out_1, out1);
            _mm512_storeu_si512(out_3, out3);

            out_0 += 16;
            out_1 += 16;
            out_2 += 16;
            out_3 += 16;
            in_lf += 16;
            in_hf0 += 16;
            in_hf1 += 16;
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
    int32_t* out_0 = out[0];
    int32_t* out_1 = out[1];
    int32_t* out_2 = out[2];
    for (; i < simd_batch; i++) {
        __m512i lf = _mm512_loadu_si512(in_lf);
        __m512i hf0 = _mm512_loadu_si512(in_hf0);
        __m512i hf1 = _mm512_loadu_si512(in_hf1);

        //out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
        __m512i out2 = _mm512_add_epi32(hf0, hf1);
        out2 = _mm512_add_epi32(out2, two);
        out2 = _mm512_srai_epi32(out2, 2);
        out2 = _mm512_sub_epi32(lf, out2);

        //out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
        __m512i out0 = _mm512_loadu_si512(out_0);
        __m512i out1 = _mm512_add_epi32(out0, out2);
        out1 = _mm512_srai_epi32(out1, 1);
        out1 = _mm512_add_epi32(out1, hf0);

        _mm512_storeu_si512(out_2, out2);
        _mm512_storeu_si512(out_1, out1);

        out_0 += 16;
        out_1 += 16;
        out_2 += 16;
        in_lf += 16;
        in_hf0 += 16;
        in_hf1 += 16;
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

void idwt_vertical_line_recalc_avx512(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                      uint32_t len, uint32_t precinct_line_idx) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    uint32_t i = 0;
    uint32_t simd_batch = len / 16;
    uint32_t simd_leftover = len % 16;
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);

    int32_t* out_0 = out[0];
    if (precinct_line_idx > 1) {
        for (; i < simd_batch; i++) {
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf0 = _mm512_loadu_si512(in_hf0);
            __m512i hf1 = _mm512_loadu_si512(in_hf1);

            //out_0[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            __m512i out0 = _mm512_add_epi32(hf0, hf1);
            out0 = _mm512_add_epi32(out0, two);
            out0 = _mm512_srai_epi32(out0, 2);
            out0 = _mm512_sub_epi32(lf, out0);
            _mm512_storeu_si512(out_0, out0);
            out_0 += 16;
            in_lf += 16;
            in_hf0 += 16;
            in_hf1 += 16;
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
            __m512i lf = _mm512_loadu_si512(in_lf);
            __m512i hf1 = _mm512_loadu_si512(in_hf1);

            //out_0[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            __m512i out0 = _mm512_add_epi32(hf1, one);
            out0 = _mm512_srai_epi32(out0, 1);
            out0 = _mm512_sub_epi32(lf, out0);
            _mm512_storeu_si512(out_0, out0);
            out_0 += 16;
            in_lf += 16;
            in_hf1 += 16;
        }

        for (i = 0; i < simd_leftover; i++) {
            out_0[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_0++;
            in_lf++;
            in_hf1++;
        }
    }
}
