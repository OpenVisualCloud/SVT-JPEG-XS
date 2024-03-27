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

void quantization_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    UNUSED(group_size);
    assert(group_size == 4);
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_sse4_1(buf, size, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_avx2(buf, size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
