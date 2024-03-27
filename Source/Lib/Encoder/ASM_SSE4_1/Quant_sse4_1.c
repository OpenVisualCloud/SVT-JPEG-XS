/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant_sse4_1.h"
#include "Definitions.h"
#include "Quant.h"
#include <immintrin.h>

static void quant_deadzone_sse4_1(uint16_t* buf, uint32_t size, int8_t gtli) {
    uint16_t clamp = ((uint16_t)(~(uint16_t)0) << gtli) & (~BITSTREAM_MASK_SIGN);
    uint32_t coeff_idx = 0;

    __m128i zero = _mm_setzero_si128();
    __m128i mask_sign = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
    __m128i clamp_sse = _mm_set1_epi16(clamp);

    for (; coeff_idx < (size / 8); coeff_idx++) {
        __m128i buff = _mm_loadu_si128((__m128i*)(buf));
        __m128i sign = _mm_and_si128(buff, mask_sign);
        buff = _mm_and_si128(buff, clamp_sse);
        sign = _mm_and_si128(sign, _mm_cmpgt_epi16(buff, zero));
        buff = _mm_or_si128(buff, sign);
        _mm_storeu_si128((__m128i*)(buf), buff);
        buf += 8;
    }

    for (uint32_t i = 0; i < (size % 8); i++) {
        uint16_t sign = buf[i] & BITSTREAM_MASK_SIGN;
        //remove sign and quantize
        buf[i] &= clamp;
        //if coeff after quantization have data, then restore sign
        if (buf[i]) {
            buf[i] |= sign;
        }
    }
}

void quant_uniform_sse4_1(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
    uint32_t coeff_idx = 0;
    __m128i zero = _mm_setzero_si128();
    __m128i mask_sign = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
    __m128i clamp_sse = _mm_set1_epi16((short)~BITSTREAM_MASK_SIGN);

    for (; coeff_idx < size / 4; coeff_idx++) {
        uint8_t gcli = gclis[coeff_idx];
        if (gcli > gtli) {
            __m128i buff = _mm_loadl_epi64((__m128i*)(buf));
            __m128i sign = _mm_and_si128(buff, mask_sign);
            buff = _mm_and_si128(buff, clamp_sse);
            uint8_t scale_value = gcli - gtli + 1;

            //d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1); this calculation have to be 32bits
            const __m128i buff32 = _mm_cvtepi16_epi32(buff);
            __m128i d = _mm_slli_epi32(buff32, scale_value);
            d = _mm_add_epi32(_mm_sub_epi32(d, buff32), _mm_set1_epi32(1 << gcli));
            d = _mm_srli_epi32(d, gcli + 1);
            d = _mm_slli_epi32(d, gtli);
            d = _mm_packs_epi32(d, d);

            sign = _mm_and_si128(sign, _mm_cmpgt_epi16(d, zero));
            d = _mm_or_si128(d, sign);

            _mm_storel_epi64((__m128i*)(buf), d);
        }
        else {
            _mm_storel_epi64((__m128i*)(buf), zero);
        }
        buf += 4;
    }
    if (size % 4) {
        uint8_t gcli = gclis[coeff_idx];
        if (gcli > gtli) {
            for (uint32_t i = 0; i < (size % 4); i++) {
                uint16_t sign = buf[i] & BITSTREAM_MASK_SIGN;
                uint8_t scale_value = gcli - gtli + 1;
                uint16_t d = buf[i] & ~BITSTREAM_MASK_SIGN;
                d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1);
                buf[i] = d << gtli;
                //if coeff after quantization have data, then restore sign
                if (buf[i]) {
                    buf[i] |= sign;
                }
            }
        }
        else {
            for (uint32_t i = 0; i < (size % 4); i++) {
                buf[i] = 0;
            }
        }
    }
}

void quantization_sse4_1(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    UNUSED(group_size);
    assert(group_size == 4);
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_sse4_1(buf, size, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_sse4_1(buf, size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
