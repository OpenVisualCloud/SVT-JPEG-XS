/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "Codestream.h"
#include "NltEnc_avx2.h"

void image_shift_avx2(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset) {
    int32_t* in_ptr_line = in_coeff_32bit;
    uint16_t* out_ptr_line = out_coeff_16bit;

    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    const __m256i sign_mask_epi32 = _mm256_set1_epi32(/*BITSTREAM_MASK_SIGN*/ ((uint32_t)1 << 31));
    const __m256i zero = _mm256_setzero_si256();

    const uint32_t simd_batch = width / 16;
    uint32_t remaining = width % 16;

    for (uint32_t i = 0; i < simd_batch; i++) {
        __m256i data1 = _mm256_loadu_si256((__m256i*)(in_ptr_line));
        __m256i sign1 = _mm256_and_si256(data1, sign_mask_epi32);
        sign1 = _mm256_srli_epi32(sign1, 16);

        __m256i data2 = _mm256_loadu_si256((__m256i*)(in_ptr_line + 8));
        __m256i sign2 = _mm256_and_si256(data2, sign_mask_epi32);
        sign2 = _mm256_srli_epi32(sign2, 16);

        data1 = _mm256_abs_epi32(data1);
        data1 = _mm256_add_epi32(data1, offset_avx2);
        data1 = _mm256_srli_epi32(data1, shift);
        sign1 = _mm256_and_si256(sign1, _mm256_cmpgt_epi32(data1, zero));
        data1 = _mm256_or_si256(data1, sign1);

        data2 = _mm256_abs_epi32(data2);
        data2 = _mm256_add_epi32(data2, offset_avx2);
        data2 = _mm256_srli_epi32(data2, shift);
        sign2 = _mm256_and_si256(sign2, _mm256_cmpgt_epi32(data2, zero));
        data2 = _mm256_or_si256(data2, sign2);

        data1 = _mm256_packus_epi32(data1, data2);
        data1 = _mm256_permute4x64_epi64(data1, 0xd8);

        _mm256_storeu_si256((__m256i*)out_ptr_line, data1);

        in_ptr_line += 16;
        out_ptr_line += 16;
    }

    if (remaining >= 8) {
        __m256i data = _mm256_loadu_si256((__m256i*)(in_ptr_line));
        __m256i sign = _mm256_and_si256(data, sign_mask_epi32);
        sign = _mm256_srli_epi32(sign, 16);

        data = _mm256_abs_epi32(data);
        data = _mm256_add_epi32(data, offset_avx2);
        data = _mm256_srli_epi32(data, shift);
        sign = _mm256_and_si256(sign, _mm256_cmpgt_epi32(data, zero));
        data = _mm256_or_si256(data, sign);
        data = _mm256_packus_epi32(data, data);
        _mm_storel_epi64((__m128i*)out_ptr_line, _mm256_castsi256_si128(data));
        _mm_storel_epi64((__m128i*)(out_ptr_line + 4), _mm256_extracti128_si256(data, 0x1));

        remaining -= 8;
        in_ptr_line += 8;
        out_ptr_line += 8;
    }

    for (uint32_t i = 0; i < remaining; i++) {
        int32_t val = in_ptr_line[i];
        if (val >= 0) {
            val = (val + offset) >> shift;
        }
        else {
            val = ((-val + offset) >> shift);
            if (val) {
                val |= BITSTREAM_MASK_SIGN;
            }
        }
        out_ptr_line[i] = val;
    }
}

void linear_input_scaling_line_8bit_avx2(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset) {
    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    const uint32_t simd_batch = w / 8;
    const uint32_t remaining = w % 8;

    for (uint32_t j = 0; j < simd_batch; j++) {
        __m128i temp_data = _mm_loadl_epi64((const __m128i*)src);
        __m256i data = _mm256_cvtepu8_epi32(temp_data);

        data = _mm256_slli_epi32(data, shift);
        data = _mm256_sub_epi32(data, offset_avx2);

        _mm256_storeu_si256((__m256i*)dst, data);

        src += 8;
        dst += 8;
    }
    for (uint32_t j = 0; j < remaining; j++) {
        dst[j] = ((uint32_t)src[j] << shift) - offset;
    }
}

void linear_input_scaling_line_16bit_avx2(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth) {
    /* each __m512i has 16 int32_t(int32_t) */
    const uint32_t simd_batch = w / 8;
    const uint32_t remaining = w % 8;
    const uint16_t input_mask = (1 << bit_depth) - 1;

    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    const __m128i input_mask_epi16 = _mm_set1_epi16(input_mask);
    for (uint32_t i = 0; i < simd_batch; i++) {
        __m256i reg = _mm256_cvtepi16_epi32(_mm_and_si128(_mm_loadu_si128((__m128i*)src), input_mask_epi16));
        __m256i result = _mm256_sub_epi32(_mm256_slli_epi32(reg, shift), offset_avx2);
        _mm256_storeu_si256((__m256i*)dst, result);
        src += 8;
        dst += 8;
    }
    for (uint32_t i = 0; i < remaining; i++) {
        dst[i] = ((src[i] & input_mask) << shift) - offset;
    }
}
