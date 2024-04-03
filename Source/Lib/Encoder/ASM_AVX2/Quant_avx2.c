/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant_avx2.h"
#include "Quant_sse4_1.h"
#include "Definitions.h"
#include "Quant.h"
#include <immintrin.h>

static void quant_deadzone_avx2(uint16_t* buf, uint32_t size, int8_t gtli) {
    uint16_t clamp = ((uint16_t)(~(uint16_t)0) << gtli) & (~BITSTREAM_MASK_SIGN);
    uint32_t coeff_idx = 0;

    __m256i zero = _mm256_setzero_si256();
    __m256i mask_sign = _mm256_set1_epi16((short)BITSTREAM_MASK_SIGN);
    __m256i clamp_sse = _mm256_set1_epi16(clamp);

    for (; coeff_idx < (size / 16); coeff_idx++) {
        __m256i buff = _mm256_loadu_si256((__m256i*)(buf));
        __m256i sign = _mm256_and_si256(buff, mask_sign);
        buff = _mm256_and_si256(buff, clamp_sse);
        sign = _mm256_and_si256(sign, _mm256_cmpgt_epi16(buff, zero));
        buff = _mm256_or_si256(buff, sign);
        _mm256_storeu_si256((__m256i*)(buf), buff);
        buf += 16;
    }

    for (uint32_t i = 0; i < (size % 16); i++) {
        uint16_t sign = buf[i] & BITSTREAM_MASK_SIGN;
        //remove sign and quantize
        buf[i] &= clamp;
        //if coeff after quantization have data, then restore sign
        if (buf[i]) {
            buf[i] |= sign;
        }
    }
}

void quant_uniform_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i mask_sign = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m128i clamp_sse = _mm_set1_epi16((short)~BITSTREAM_MASK_SIGN);
    const __m128i gtli_sse = _mm_set1_epi16(gtli);
    const __m128i one_sse = _mm_set1_epi16(1);
    const __m256i one_avx = _mm256_set1_epi32(1);

    for (uint32_t coeff_idx = 0; coeff_idx < size / 8; coeff_idx++) {
        __m128i buff = _mm_loadu_si128((__m128i*)(buf));
        __m128i sign = _mm_and_si128(buff, mask_sign);
        buff = _mm_and_si128(buff, clamp_sse);

        //d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1); this calculation have to be 32bits
        const __m256i buff32 = _mm256_cvtepi16_epi32(buff);

        uint64_t gcli1 = gclis[0] * 0x0001000100010001;
        uint64_t gcli2 = gclis[1] * 0x0001000100010001;
        const __m128i gclis_sse16 = _mm_set_epi64x(gcli2, gcli1);
        const __m128i gclis_sse16_p1 = _mm_adds_epu16(gclis_sse16, one_sse);
        const __m128i scal_value = _mm_subs_epu16(gclis_sse16_p1, gtli_sse);

        __m256i d = _mm256_sllv_epi32(buff32, _mm256_cvtepi16_epi32(scal_value));
        d = _mm256_add_epi32(_mm256_sub_epi32(d, buff32), _mm256_sllv_epi32(one_avx, _mm256_cvtepi16_epi32(gclis_sse16)));
        d = _mm256_srlv_epi32(d, _mm256_cvtepi16_epi32(gclis_sse16_p1));
        d = _mm256_slli_epi32(d, gtli);
        d = _mm256_packs_epi32(d, d);

        __m128i d_sse = _mm256_castsi256_si128(_mm256_permute4x64_epi64(d, 0xd8));

        sign = _mm_and_si128(sign, _mm_cmpgt_epi16(d_sse, zero));
        d_sse = _mm_or_si128(d_sse, sign);

        _mm_storeu_si128((__m128i*)(buf), d_sse);

        gclis += 2;
        buf += 8;
    }
    if (size % 8) {
        quant_uniform_sse4_1(buf, size % 8, gclis, gtli);
    }
}

void quantization_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    UNUSED(group_size);
    assert(group_size == 4);
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_avx2(buf, size, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_avx2(buf, size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
