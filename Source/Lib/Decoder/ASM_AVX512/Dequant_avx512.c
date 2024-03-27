/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dequant.h"
#include "Definitions.h"
#include <immintrin.h>
#include "Codestream.h"

static INLINE void inv_quant_uniform_leftover(uint16_t *buf, uint8_t gcli, uint8_t gtli) {
    if (*buf & ~BITSTREAM_MASK_SIGN) {
        uint16_t sign = *buf & BITSTREAM_MASK_SIGN;
        uint16_t val = (*buf & ~BITSTREAM_MASK_SIGN);
        uint8_t scale_value = gcli - gtli + 1;
        *buf = 0;
        for (; val > 0; val >>= scale_value) {
            *buf += val;
        }
        //insert sign
        *buf |= sign;
    }
}

static INLINE void inv_quant_deadzone_leftover(uint16_t *buf, uint8_t gtli) {
    if (*buf & ~BITSTREAM_MASK_SIGN) {
        *buf |= (1 << (gtli - 1));
    }
}

void dequant_avx512(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    if (gtli == 0) {
        return;
    }
    const uint64_t magic_val = 0x0001000100010001;
    const __m512i sign_mask_neg_avx = _mm512_set1_epi16((uint16_t)(~BITSTREAM_MASK_SIGN));
    const __m128i sign_mask_neg = _mm_set1_epi16((uint16_t)(~BITSTREAM_MASK_SIGN));

    if (dq_type == QUANT_TYPE_UNIFORM) {
        const __m512i two_avx = _mm512_set1_epi16(2);
        const __m512i max_avx = _mm512_set1_epi16(-1);
        const __m512i gtli_minus1_avx = _mm512_set1_epi16(gtli - 1);
        const __m512i sign_mask_avx = _mm512_set1_epi16((short)BITSTREAM_MASK_SIGN);

        for (uint32_t group = 0; group < buf_len / 32; group++) {
            __m256i gclis_avx1 = _mm256_setr_epi64x(
                gclis[0] * magic_val, gclis[1] * magic_val, gclis[2] * magic_val, gclis[3] * magic_val);
            gclis += 4;
            __m256i gclis_avx2 = _mm256_setr_epi64x(
                gclis[0] * magic_val, gclis[1] * magic_val, gclis[2] * magic_val, gclis[3] * magic_val);
            gclis += 4;
            __m512i zeta = _mm512_inserti64x4(_mm512_castsi256_si512(gclis_avx1), gclis_avx2, 0x1);
            zeta = _mm512_subs_epu16(zeta, gtli_minus1_avx);
            __mmask32 mask = _mm512_cmplt_epi16_mask(zeta, two_avx);
            zeta = _mm512_mask_mov_epi16(zeta, mask, max_avx);

            const __m512i dq = _mm512_loadu_si512((__m512i const *)buf);
            const __m512i sign = _mm512_and_si512(dq, sign_mask_avx);
            __m512i phi = _mm512_and_si512(dq, sign_mask_neg_avx);
            __m512i rho = _mm512_setzero_si512();
            do {
                rho = _mm512_add_epi16(rho, phi);
                phi = _mm512_srlv_epi16(phi, zeta);
            } while (_mm512_test_epi16_mask(phi, phi));
            _mm512_storeu_si512((__m512i *)buf, _mm512_or_si512(rho, sign));
            buf += 32;
        }

        const uint32_t leftover = buf_len % 32;
        const __m128i two_sse = _mm_set1_epi16(2);
        const __m128i gtli_minus1_sse = _mm_set1_epi16(gtli - 1);
        const __m128i sign_mask = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
        for (uint32_t group = 0; group < leftover / 8; group++) {
            __m128i zeta_sse = _mm_set_epi64x(gclis[1] * magic_val, gclis[0] * magic_val);
            zeta_sse = _mm_subs_epu16(zeta_sse, gtli_minus1_sse);
            __m128i cond = _mm_cmplt_epi16(zeta_sse, two_sse);
            zeta_sse = _mm_or_si128(zeta_sse, cond);

            const __m128i dq = _mm_loadu_si128((__m128i const *)buf);
            const __m128i sign = _mm_and_si128(dq, sign_mask);
            __m128i phi = _mm_and_si128(dq, sign_mask_neg);
            __m128i rho = _mm_setzero_si128();
            do {
                rho = _mm_add_epi16(rho, phi);
                phi = _mm_srlv_epi16(phi, zeta_sse);
            } while (_mm_test_epi16_mask(phi, phi)); //_mm512_test_epi16_mask
            _mm_storeu_si128((__m128i *)buf, _mm_or_si128(rho, sign));
            gclis += 2;
            buf += 8;
        }

        //Leftover
        if ((leftover % 8) >= 4) {
            if (gclis[0] > gtli) {
                for (uint32_t i = 0; i < 4; i++) {
                    inv_quant_uniform_leftover(buf + i, gclis[0], gtli);
                }
            }
            buf += 4;
            gclis++;
        }
        if ((leftover % 4) && (gclis[0] > gtli)) {
            for (uint32_t i = 0; i < (leftover % 4); i++) {
                inv_quant_uniform_leftover(buf + i, gclis[0], gtli);
            }
        }
    }
    else if (dq_type == QUANT_TYPE_DEADZONE) {
        const __m512i gtli_move_avx = _mm512_set1_epi16((1 << (gtli - 1)));
        const __m512i gtli_avx = _mm512_set1_epi16(gtli);
        for (uint32_t group = 0; group < buf_len / 32; group++) {
            __m256i gclis_avx1 = _mm256_setr_epi64x(
                gclis[0] * magic_val, gclis[1] * magic_val, gclis[2] * magic_val, gclis[3] * magic_val);
            gclis += 4;
            __m256i gclis_avx2 = _mm256_setr_epi64x(
                gclis[0] * magic_val, gclis[1] * magic_val, gclis[2] * magic_val, gclis[3] * magic_val);
            gclis += 4;
            __m512i gclis_avx512 = _mm512_inserti64x4(_mm512_castsi256_si512(gclis_avx1), gclis_avx2, 0x1);

            //cond 1: (gclis[group] > gtli)
            __mmask32 cond1 = _mm512_cmpgt_epi16_mask(gclis_avx512, gtli_avx);

            //cond 2:(*dq_in & ~BITSTREAM_MASK_SIGN)
            const __m512i dq = _mm512_loadu_si512((__m512i const *)buf);
            __m512i cond2_avx = _mm512_and_si512(dq, sign_mask_neg_avx);
            __mmask32 cond2 = _mm512_cmpgt_epi16_mask(cond2_avx, _mm512_setzero_si512());

            __mmask32 mask = cond1 & cond2;
            //sigmag_data |= (1 << (gtli - 1));
            __m512i gtli_end = _mm512_mask_mov_epi16(_mm512_setzero_si512(), mask, gtli_move_avx);
            _mm512_storeu_si512((__m512i *)buf, _mm512_or_si512(dq, gtli_end));
            buf += 32;
        }

        const uint32_t leftover = buf_len % 32;
        const __m128i gtli_move = _mm_set1_epi16((1 << (gtli - 1)));
        const __m128i gtli_sse = _mm_set1_epi16(gtli);
        for (uint32_t group = 0; group < leftover / 8; group++) {
            __m128i gclis_sse = _mm_set_epi64x(gclis[1] * magic_val, gclis[0] * magic_val);
            //cond 1: (gclis[group] > gtli)
            __m128i cond1 = _mm_cmpgt_epi16(gclis_sse, gtli_sse);

            //cond 2:(*dq_in & ~BITSTREAM_MASK_SIGN)
            const __m128i dq = _mm_loadu_si128((__m128i const *)buf);
            __m128i cond2 = _mm_and_si128(dq, sign_mask_neg);
            cond2 = _mm_cmpgt_epi16(cond2, _mm_setzero_si128());

            __m128i mask = _mm_and_si128(cond1, cond2);
            //sigmag_data |= (1 << (gtli - 1));
            const __m128i gtli_end = _mm_and_si128(gtli_move, mask);
            _mm_storeu_si128((__m128i *)buf, _mm_or_si128(gtli_end, dq));
            gclis += 2;
            buf += 8;
        }

        //Leftover
        if ((leftover % 8) >= 4) {
            if (gclis[0] > gtli) {
                for (uint32_t i = 0; i < 4; i++) {
                    inv_quant_deadzone_leftover(buf + i, gtli);
                }
            }
            buf += 4;
            gclis++;
        }
        if ((leftover % 4) && (gclis[0] > gtli)) {
            for (uint32_t i = 0; i < (leftover % 4); i++) {
                inv_quant_deadzone_leftover(buf + i, gtli);
            }
        }
    }
    else {
        assert("unknown quantization");
        return;
    }
}
