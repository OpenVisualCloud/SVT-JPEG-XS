/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant_avx2.h"
#include "Quant_sse4_1.h"
#include "Definitions.h"
#include "Quant.h"
#include <immintrin.h>
#include <string.h>

static void quant_deadzone_avx2(uint16_t* buf, uint32_t size, int8_t gtli) {
    uint16_t clamp = (uint16_t)(((unsigned)(uint16_t)(~(uint16_t)0) << gtli) & ~(unsigned)BITSTREAM_MASK_SIGN);
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

/* 32-bit lanes, eight coefficients at a time. quant_uniform_avx2 below leaves to it GTLI 0 and the tail of a line. */
static void quant_uniform_32bit_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
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

/* Uniform quantization in 16-bit lanes, sixteen coefficients (four code groups) at a time.
 *
 * quant_uniform_c computes, for a group above GTLI (G = GCLI, T = GTLI, z = G - T + 1):
 *   q = ((d << z) - d + 2^G) >> (G + 1), and stores q << T.
 * The products need more than sixteen bits, so the version above widens to 32-bit lanes. They are not needed: with
 * u = d >> z and r = d - (u << z), the numerator is 2^z * (d - u + 2^(T-1)) - r, and since 0 <= r < 2^z,
 *   q = (d - u + 2^(T-1) - (r != 0 ? 1 : 0)) >> T,
 * where every term fits in sixteen bits, for any magnitude d and 1 <= T < G <= 15.
 *
 * The shift by z changes from one group to the next and AVX2 has no 16-bit shift by a count per lane, so it is a
 * multiply by 2^(16 - z): the high half of d * 2^(16 - z) is u, and the low half, r * 2^(16 - z), is zero exactly when
 * r is. The multiplier depends only on G - T = z - 1 (1 .. 14 here) and comes from a table.
 *
 * Stored as q << T, that is the line above with its low T bits cleared. */
static INLINE __m256i gcli_above_gtli_x16(const uint8_t* gclis, __m256i gtli_x32) {
    /* Both bytes of lane i take byte i / 4: lanes 0-7 are groups 0 and 1, lanes 8-15 groups 2 and 3 */
    const __m256i spread = _mm256_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3);
    uint32_t gclis4;
    memcpy(&gclis4, gclis, sizeof(gclis4));
    const __m256i above = _mm256_subs_epu8(_mm256_set1_epi32((int32_t)gclis4), gtli_x32);
    return _mm256_shuffle_epi8(above, spread);
}

static void quant_uniform_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli) {
    if (gtli == 0) {
        quant_uniform_32bit_avx2(buf, size, gclis, gtli);
        return;
    }
    /* Low and high bytes of 2^(15 - (G - T)) = 2^(16 - z), for G - T = 0 .. 15 */
    const __m256i lut_lo = _mm256_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0, -128, 64, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, -128, 64, 32, 16, 8, 4, 2, 1);
    const __m256i lut_hi = _mm256_setr_epi8(
        -128, 64, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, -128, 64, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i gtli_x32 = _mm256_set1_epi8((char)gtli);
    const __m256i sign_mask = _mm256_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m256i low_byte = _mm256_set1_epi16(0x00ff);
    /* 2^(T-1) - 1: with the -1 when r is zero, the rounding term of q */
    const __m256i rounding = _mm256_set1_epi16((short)((1 << (gtli - 1)) - 1));
    const __m256i clear_low = _mm256_set1_epi16((short)(uint16_t)(0xffffu << gtli));
    const __m256i zero = _mm256_setzero_si256();
    uint32_t i = 0;

    for (; i + 16 <= size; i += 16) {
        const __m256i above = gcli_above_gtli_x16(gclis + i / 4, gtli_x32);
        const __m256i mult = _mm256_or_si256(_mm256_and_si256(_mm256_shuffle_epi8(lut_lo, above), low_byte),
                                             _mm256_slli_epi16(_mm256_shuffle_epi8(lut_hi, above), 8));
        const __m256i in = _mm256_loadu_si256((const __m256i*)(buf + i));
        const __m256i d = _mm256_andnot_si256(sign_mask, in);
        const __m256i u = _mm256_mulhi_epu16(d, mult);
        /* -1 where r is zero */
        const __m256i r_zero = _mm256_cmpeq_epi16(_mm256_mullo_epi16(d, mult), zero);
        __m256i q = _mm256_sub_epi16(_mm256_add_epi16(_mm256_sub_epi16(d, u), rounding), r_zero);
        /* A group not above GTLI quantizes to zero */
        q = _mm256_andnot_si256(_mm256_cmpeq_epi16(above, zero), _mm256_and_si256(q, clear_low));
        /* The sign stays only on a coefficient that is still non-zero */
        const __m256i sign = _mm256_and_si256(_mm256_and_si256(in, sign_mask), _mm256_cmpgt_epi16(q, zero));
        _mm256_storeu_si256((__m256i*)(buf + i), _mm256_or_si256(q, sign));
    }
    if (i < size) {
        quant_uniform_32bit_avx2(buf + i, size - i, gclis + i / 4, gtli);
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
