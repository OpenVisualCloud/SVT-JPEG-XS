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
    const uint16_t clamp = (uint16_t)(((unsigned)(uint16_t)(~(uint16_t)0) << gtli) & ~(unsigned)BITSTREAM_MASK_SIGN);
    const __m512i clamp_avx = _mm512_set1_epi16((short)clamp);
    const __m512i clamp_sign_avx = _mm512_set1_epi16((short)(clamp | BITSTREAM_MASK_SIGN));

    uint32_t i = 0;
    for (; i + 32 <= size; i += 32) {
        const __m512i buff = _mm512_loadu_si512((const void*)(buf + i));
        const __mmask32 nz = _mm512_test_epi16_mask(buff, clamp_avx);
        const __m512i res = _mm512_maskz_mov_epi16(nz, _mm512_and_si512(buff, clamp_sign_avx));
        _mm512_storeu_si512((void*)(buf + i), res);
    }
    if (i < size) {
        const __mmask32 m = ((__mmask32)1 << (size - i)) - 1;
        const __m512i buff = _mm512_maskz_loadu_epi16(m, buf + i);
        const __mmask32 nz = _mm512_test_epi16_mask(buff, clamp_avx);
        const __m512i res = _mm512_maskz_mov_epi16(nz, _mm512_and_si512(buff, clamp_sign_avx));
        _mm512_mask_storeu_epi16(buf + i, m, res);
    }
}

void quant_uniform_avx512(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i mask_sign = _mm256_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m256i clamp_sse = _mm256_set1_epi16((short)~BITSTREAM_MASK_SIGN);
    const __m256i gtli_avx = _mm256_set1_epi16(gtli);
    const __m256i one_avx = _mm256_set1_epi16(1);
    const __m512i one_avx512 = _mm512_set1_epi32(1);
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
        /* 1 << gcli in 32-bit lanes: in 16-bit lanes 1 << 15 is 0x8000, which the widening would sign-extend */
        d = _mm512_add_epi32(_mm512_sub_epi32(d, buff32), _mm512_sllv_epi32(one_avx512, _mm512_cvtepi16_epi32(gclis_avx16)));
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
