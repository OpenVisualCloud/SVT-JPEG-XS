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
    const __m256i sign_mask_epi32 = _mm256_set1_epi32(/*BITSTREAM_MASK_SIGN*/ (-2147483647 - 1));
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

/* The input picture is read once, a line at a time, and when the encoder gets to it, it is usually in DRAM. A 1080p
 * line of 16-bit samples is 3840 bytes, about a page, so the hardware prefetcher, which does not follow a stream across
 * pages, starts over at almost every line. The input scaling prefetches a page ahead of what it reads, which, as the
 * lines of a plane usually follow each other, is the start of the next line (two lines ahead for 8-bit samples).
 * Near the end of the plane that address is past it, which is harmless for a prefetch. */
#define INPUT_PREFETCH_DISTANCE 4096

void linear_input_scaling_line_8bit_avx2(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset) {
    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    uint32_t i = 0;

    for (; i + 64 <= w; i += 64) {
        _mm_prefetch((const char*)(src + i) + INPUT_PREFETCH_DISTANCE, _MM_HINT_T0);
        for (uint32_t k = 0; k < 64; k += 8) {
            const __m256i data = _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(src + i + k)));
            _mm256_storeu_si256((__m256i*)(dst + i + k), _mm256_sub_epi32(_mm256_slli_epi32(data, shift), offset_avx2));
        }
    }
    for (; i + 8 <= w; i += 8) {
        const __m256i data = _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(src + i)));
        _mm256_storeu_si256((__m256i*)(dst + i), _mm256_sub_epi32(_mm256_slli_epi32(data, shift), offset_avx2));
    }
    for (; i < w; i++) {
        dst[i] = (int32_t)((uint32_t)src[i] << shift) - (int32_t)offset;
    }
}

void linear_input_scaling_line_16bit_avx2(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth) {
    const uint16_t input_mask = (1 << bit_depth) - 1;
    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    const __m128i input_mask_epi16 = _mm_set1_epi16(input_mask);
    uint32_t i = 0;

    for (; i + 32 <= w; i += 32) {
        _mm_prefetch((const char*)(src + i) + INPUT_PREFETCH_DISTANCE, _MM_HINT_T0);
        for (uint32_t k = 0; k < 32; k += 8) {
            const __m256i reg = _mm256_cvtepi16_epi32(
                _mm_and_si128(_mm_loadu_si128((const __m128i*)(src + i + k)), input_mask_epi16));
            _mm256_storeu_si256((__m256i*)(dst + i + k), _mm256_sub_epi32(_mm256_slli_epi32(reg, shift), offset_avx2));
        }
    }
    for (; i + 8 <= w; i += 8) {
        const __m256i reg = _mm256_cvtepi16_epi32(_mm_and_si128(_mm_loadu_si128((const __m128i*)(src + i)), input_mask_epi16));
        _mm256_storeu_si256((__m256i*)(dst + i), _mm256_sub_epi32(_mm256_slli_epi32(reg, shift), offset_avx2));
    }
    for (; i < w; i++) {
        dst[i] = ((src[i] & input_mask) << shift) - offset;
    }
}

/* MSB-aligned: no masking needed, values use the full 16-bit range so zero-extend (not sign-extend). */
void linear_input_scaling_line_16bit_msb_avx2(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                              uint8_t bit_depth) {
    (void)bit_depth;
    const __m256i offset_avx2 = _mm256_set1_epi32(offset);
    uint32_t i = 0;

    for (; i + 32 <= w; i += 32) {
        _mm_prefetch((const char*)(src + i) + INPUT_PREFETCH_DISTANCE, _MM_HINT_T0);
        for (uint32_t k = 0; k < 32; k += 8) {
            const __m256i reg = _mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i*)(src + i + k)));
            _mm256_storeu_si256((__m256i*)(dst + i + k), _mm256_sub_epi32(_mm256_slli_epi32(reg, shift), offset_avx2));
        }
    }
    for (; i + 8 <= w; i += 8) {
        const __m256i reg = _mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i*)(src + i)));
        _mm256_storeu_si256((__m256i*)(dst + i), _mm256_sub_epi32(_mm256_slli_epi32(reg, shift), offset_avx2));
    }
    for (; i < w; i++) {
        dst[i] = ((uint32_t)src[i] << shift) - offset;
    }
}
