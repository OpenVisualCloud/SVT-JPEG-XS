/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dequant.h"
#include "Dequant_avx512.h"
#include "NltDec_avx512.h"
#include "Definitions.h"
#include <immintrin.h>
#include "Codestream.h"

static INLINE void dequant_avx512_impl(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli,
                                       QUANT_TYPE dq_type, const int apply_inv_sign) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    if (gtli == 0) {
        if (apply_inv_sign) {
            inv_sign_avx512(buf, buf_len);
        }
        return;
    }
    const __m512i sign_mask_neg_avx = _mm512_set1_epi16((short)(~BITSTREAM_MASK_SIGN));
    const __m512i zero = _mm512_setzero_si512();
    const __m512i spread_idx = _mm512_setr_epi64(0x0000000000000000LL,
                                                 0x0001000100010001LL,
                                                 0x0002000200020002LL,
                                                 0x0003000300030003LL,
                                                 0x0004000400040004LL,
                                                 0x0005000500050005LL,
                                                 0x0006000600060006LL,
                                                 0x0007000700070007LL);

    if (dq_type == QUANT_TYPE_UNIFORM) {
        const __m512i two_avx = _mm512_set1_epi16(2);
        const __m512i max_avx = _mm512_set1_epi16(-1);
        const __m512i gtli_minus1_avx = _mm512_set1_epi16(gtli - 1);
        const __m512i sign_mask_avx = _mm512_set1_epi16((short)BITSTREAM_MASK_SIGN);

        uint32_t i = 0;
        for (; i + 32 <= buf_len; i += 32, gclis += 8) {
            const __m128i g8 = _mm_loadl_epi64((const __m128i *)gclis);
            __m512i zeta = _mm512_permutexvar_epi16(spread_idx, _mm512_cvtepu8_epi16(_mm256_castsi128_si256(g8)));
            zeta = _mm512_subs_epu16(zeta, gtli_minus1_avx);
            __mmask32 mask = _mm512_cmplt_epi16_mask(zeta, two_avx);
            zeta = _mm512_mask_mov_epi16(zeta, mask, max_avx);

            const __m512i dq = _mm512_loadu_si512((const void *)(buf + i));
            __m512i phi = _mm512_and_si512(dq, sign_mask_neg_avx);
            __m512i rho = zero;
            do {
                rho = _mm512_add_epi16(rho, phi);
                phi = _mm512_srlv_epi16(phi, zeta);
            } while (_mm512_test_epi16_mask(phi, phi));
            if (apply_inv_sign) {
                const __mmask32 neg = _mm512_movepi16_mask(dq);
                _mm512_storeu_si512((void *)(buf + i), _mm512_mask_sub_epi16(rho, neg, zero, rho));
            }
            else {
                _mm512_storeu_si512((void *)(buf + i), _mm512_or_si512(rho, _mm512_and_si512(dq, sign_mask_avx)));
            }
        }

        const uint32_t leftover = buf_len - i;
        if (leftover) {
            const uint32_t g_rem = (leftover + GROUP_SIZE - 1) >> 2;
            const __mmask16 gm = (__mmask16)((1u << g_rem) - 1);
            const __m128i g8 = _mm_maskz_loadu_epi8(gm, gclis);
            const __mmask32 bm = ((__mmask32)1 << leftover) - 1;
            __m512i zeta = _mm512_permutexvar_epi16(spread_idx, _mm512_cvtepu8_epi16(_mm256_castsi128_si256(g8)));
            zeta = _mm512_subs_epu16(zeta, gtli_minus1_avx);
            __mmask32 mask = _mm512_cmplt_epi16_mask(zeta, two_avx);
            zeta = _mm512_mask_mov_epi16(zeta, mask, max_avx);

            const __m512i dq = _mm512_maskz_loadu_epi16(bm, buf + i);
            __m512i phi = _mm512_and_si512(dq, sign_mask_neg_avx);
            __m512i rho = zero;
            do {
                rho = _mm512_add_epi16(rho, phi);
                phi = _mm512_srlv_epi16(phi, zeta);
            } while (_mm512_test_epi16_mask(phi, phi));
            if (apply_inv_sign) {
                const __mmask32 neg = _mm512_movepi16_mask(dq);
                _mm512_mask_storeu_epi16(buf + i, bm, _mm512_mask_sub_epi16(rho, neg, zero, rho));
            }
            else {
                _mm512_mask_storeu_epi16(buf + i, bm, _mm512_or_si512(rho, _mm512_and_si512(dq, sign_mask_avx)));
            }
        }
    }
    else if (dq_type == QUANT_TYPE_DEADZONE) {
        const __m512i gtli_move_avx = _mm512_set1_epi16((short)(1 << (gtli - 1)));
        const __m128i gtli_u8 = _mm_set1_epi8((char)gtli);

        uint32_t i = 0;
        for (; i + 64 <= buf_len; i += 64, gclis += 16) {
            const uint32_t gmask16 = (uint32_t)_mm_cmpgt_epu8_mask(_mm_loadu_si128((const __m128i *)gclis), gtli_u8);
            const uint64_t cond64 = _pdep_u64((uint64_t)gmask16, 0x1111111111111111ULL) * 0xFULL;
            const __mmask32 cond0 = (__mmask32)cond64;
            const __mmask32 cond1 = (__mmask32)(cond64 >> 32);
            const __m512i dq0 = _mm512_loadu_si512((const void *)(buf + i));
            const __m512i dq1 = _mm512_loadu_si512((const void *)(buf + i + 32));
            if (apply_inv_sign) {
                const __m512i mag0 = _mm512_and_si512(dq0, sign_mask_neg_avx);
                const __m512i mag1 = _mm512_and_si512(dq1, sign_mask_neg_avx);
                const __mmask32 mask0 = _mm512_mask_test_epi16_mask(cond0, mag0, mag0);
                const __mmask32 mask1 = _mm512_mask_test_epi16_mask(cond1, mag1, mag1);
                const __m512i mag_dq0 = _mm512_mask_mov_epi16(mag0, mask0, _mm512_or_si512(mag0, gtli_move_avx));
                const __m512i mag_dq1 = _mm512_mask_mov_epi16(mag1, mask1, _mm512_or_si512(mag1, gtli_move_avx));
                const __mmask32 neg0 = _mm512_movepi16_mask(dq0);
                const __mmask32 neg1 = _mm512_movepi16_mask(dq1);
                _mm512_storeu_si512((void *)(buf + i), _mm512_mask_sub_epi16(mag_dq0, neg0, zero, mag_dq0));
                _mm512_storeu_si512((void *)(buf + i + 32), _mm512_mask_sub_epi16(mag_dq1, neg1, zero, mag_dq1));
            }
            else {
                const __mmask32 mask0 = _mm512_mask_test_epi16_mask(cond0, dq0, sign_mask_neg_avx);
                const __mmask32 mask1 = _mm512_mask_test_epi16_mask(cond1, dq1, sign_mask_neg_avx);
                _mm512_storeu_si512((void *)(buf + i), _mm512_mask_mov_epi16(dq0, mask0, _mm512_or_si512(dq0, gtli_move_avx)));
                _mm512_storeu_si512((void *)(buf + i + 32),
                                    _mm512_mask_mov_epi16(dq1, mask1, _mm512_or_si512(dq1, gtli_move_avx)));
            }
        }
        for (; i + 32 <= buf_len; i += 32, gclis += 8) {
            const uint32_t gmask8 = (uint32_t)_mm_cmpgt_epu8_mask(_mm_loadl_epi64((const __m128i *)gclis), gtli_u8) & 0xFFu;
            const __mmask32 cond1 = (__mmask32)(_pdep_u32(gmask8, 0x11111111u) * 0xFu);
            const __m512i dq = _mm512_loadu_si512((const void *)(buf + i));
            if (apply_inv_sign) {
                const __m512i mag = _mm512_and_si512(dq, sign_mask_neg_avx);
                const __mmask32 mask = _mm512_mask_test_epi16_mask(cond1, mag, mag);
                const __m512i mag_dq = _mm512_mask_mov_epi16(mag, mask, _mm512_or_si512(mag, gtli_move_avx));
                const __mmask32 neg = _mm512_movepi16_mask(dq);
                _mm512_storeu_si512((void *)(buf + i), _mm512_mask_sub_epi16(mag_dq, neg, zero, mag_dq));
            }
            else {
                const __mmask32 mask = _mm512_mask_test_epi16_mask(cond1, dq, sign_mask_neg_avx);
                _mm512_storeu_si512((void *)(buf + i), _mm512_mask_mov_epi16(dq, mask, _mm512_or_si512(dq, gtli_move_avx)));
            }
        }

        const uint32_t leftover = buf_len - i;
        if (leftover) {
            const uint32_t g_rem = (leftover + GROUP_SIZE - 1) >> 2;
            const __mmask16 gm = (__mmask16)((1u << g_rem) - 1);
            const uint32_t gmask8 = (uint32_t)_mm_mask_cmpgt_epu8_mask(gm, _mm_maskz_loadu_epi8(gm, gclis), gtli_u8);
            const __mmask32 cond1 = (__mmask32)(_pdep_u32(gmask8, 0x11111111u) * 0xFu);
            const __mmask32 bm = ((__mmask32)1 << leftover) - 1;
            const __m512i dq = _mm512_maskz_loadu_epi16(bm, buf + i);
            if (apply_inv_sign) {
                const __m512i mag = _mm512_and_si512(dq, sign_mask_neg_avx);
                const __mmask32 mask = _mm512_mask_test_epi16_mask(cond1, mag, mag);
                const __m512i mag_dq = _mm512_mask_mov_epi16(mag, mask, _mm512_or_si512(mag, gtli_move_avx));
                const __mmask32 neg = _mm512_movepi16_mask(dq);
                _mm512_mask_storeu_epi16(buf + i, bm, _mm512_mask_sub_epi16(mag_dq, neg, zero, mag_dq));
            }
            else {
                const __mmask32 mask = _mm512_mask_test_epi16_mask(cond1, dq, sign_mask_neg_avx);
                _mm512_mask_storeu_epi16(buf + i, bm, _mm512_mask_mov_epi16(dq, mask, _mm512_or_si512(dq, gtli_move_avx)));
            }
        }
    }
    else {
        assert("unknown quantization");
        return;
    }
}

void dequant_avx512(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type) {
    dequant_avx512_impl(buf, buf_len, gclis, group_size, gtli, dq_type, 0);
}

void dequant_inv_sign_avx512(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli,
                             QUANT_TYPE dq_type) {
    dequant_avx512_impl(buf, buf_len, gclis, group_size, gtli, dq_type, 1);
}
