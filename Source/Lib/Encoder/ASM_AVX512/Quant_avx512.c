/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant_avx512.h"
#include "Quant_sse4_1.h"
#include "Definitions.h"
#include "Quant.h"
#include <immintrin.h>

static void quant_deadzone_avx512(uint16_t* buf, uint32_t size, int8_t gtli) {
    uint16_t clamp = ((uint16_t)(~(uint16_t)0) << gtli) & (~BITSTREAM_MASK_SIGN);

    const __m512i zero = _mm512_setzero_si512();
    const __m512i mask_sign = _mm512_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m512i clamp_avx = _mm512_set1_epi16(clamp);
    const __m512i m1 = _mm512_set1_epi16(-1);

    for (uint32_t coeff_idx = 0; coeff_idx < (size / 32); coeff_idx++) {
        __m512i buff = _mm512_loadu_si512((__m512i*)(buf));
        __m512i sign = _mm512_and_si512(buff, mask_sign);
        buff = _mm512_and_si512(buff, clamp_avx);
        uint32_t mask = _mm512_cmpgt_epi16_mask(buff, zero);
        sign = _mm512_and_si512(sign, _mm512_mask_mov_epi16(zero, mask, m1));
        buff = _mm512_or_si512(buff, sign);
        _mm512_storeu_si512((__m512i*)(buf), buff);
        buf += 32;
    }

    if (size % 32) {
        uint32_t leftover = size % 32;
        const __m128i zero_sse = _mm_setzero_si128();
        const __m128i mask_sign_sse = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
        const __m128i clamp_sse = _mm_set1_epi16(clamp);

        for (uint32_t coeff_idx = 0; coeff_idx < (leftover / 8); coeff_idx++) {
            __m128i buff = _mm_loadu_si128((__m128i*)(buf));
            __m128i sign = _mm_and_si128(buff, mask_sign_sse);
            buff = _mm_and_si128(buff, clamp_sse);
            sign = _mm_and_si128(sign, _mm_cmpgt_epi16(buff, zero_sse));
            buff = _mm_or_si128(buff, sign);
            _mm_storeu_si128((__m128i*)(buf), buff);
            buf += 8;
        }

        for (uint32_t i = 0; i < (leftover % 8); i++) {
            uint16_t sign = buf[i] & BITSTREAM_MASK_SIGN;
            //remove sign and quantize
            buf[i] &= clamp;
            //if coeff after quantization have data, then restore sign
            if (buf[i]) {
                buf[i] |= sign;
            }
        }
    }
}

void quant_uniform_avx512(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i mask_sign = _mm256_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m256i clamp_sse = _mm256_set1_epi16((short)~BITSTREAM_MASK_SIGN);
    const __m256i gtli_avx = _mm256_set1_epi16(gtli);
    const __m256i one_avx = _mm256_set1_epi16(1);
    const __m512i permute = _mm512_setr_epi64(0, 2, 4, 6, 0, 0, 0, 0);

    for (uint32_t coeff_idx = 0; coeff_idx < size / 16; coeff_idx++) {
        __m256i buff = _mm256_loadu_si256((__m256i*)(buf));
        __m256i sign = _mm256_and_si256(buff, mask_sign);
        buff = _mm256_and_si256(buff, clamp_sse);

        //d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1); this calculation have to be 32bits
        const __m512i buff32 = _mm512_cvtepi16_epi32(buff);

        uint64_t gcli1 = gclis[0] * 0x0001000100010001;
        uint64_t gcli2 = gclis[1] * 0x0001000100010001;
        uint64_t gcli3 = gclis[2] * 0x0001000100010001;
        uint64_t gcli4 = gclis[3] * 0x0001000100010001;
        const __m256i gclis_avx16 = _mm256_set_epi64x(gcli4, gcli3, gcli2, gcli1);
        const __m256i gclis_avx16_p1 = _mm256_adds_epu16(gclis_avx16, one_avx);
        const __m256i scal_value = _mm256_subs_epu16(gclis_avx16_p1, gtli_avx);

        __m512i d = _mm512_sllv_epi32(buff32, _mm512_cvtepi16_epi32(scal_value));
        d = _mm512_add_epi32(_mm512_sub_epi32(d, buff32), _mm512_cvtepi16_epi32(_mm256_sllv_epi16(one_avx, gclis_avx16)));
        d = _mm512_srlv_epi32(d, _mm512_cvtepi16_epi32(gclis_avx16_p1));
        d = _mm512_slli_epi32(d, gtli);
        d = _mm512_packs_epi32(d, d);

        __m256i d_avx = _mm512_castsi512_si256(_mm512_permutexvar_epi64(permute,d));

        sign = _mm256_and_si256(sign, _mm256_cmpgt_epi16(d_avx, zero));
        d_avx = _mm256_or_si256(d_avx, sign);

        _mm256_storeu_si256((__m256i*)(buf), d_avx);

        gclis += 4;
        buf += 16;
    }
    if (size % 16) {
        quant_uniform_sse4_1(buf, size % 16, gclis, gtli);
    }
}

void quantization_avx512(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    UNUSED(group_size);
    assert(group_size == 4);
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_avx512(buf, size, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_avx512(buf, size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
