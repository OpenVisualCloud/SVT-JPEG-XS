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

void dequant_sse4_1(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    if (gtli == 0) {
        return;
    }
    const __m128i sign_mask_neg = _mm_set1_epi16((uint16_t)(~BITSTREAM_MASK_SIGN));

    if (dq_type == QUANT_TYPE_UNIFORM) {
        const uint32_t grup_numbers = buf_len / 4;
        const __m128i sign_mask = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);

        uint32_t group = 0;
        for (; group < grup_numbers; group++) {
            const int16_t zeta = gclis[group] - gtli + 1;
            if (zeta > 1) {
                const __m128i dq = _mm_loadl_epi64((__m128i const *)buf);
                const __m128i sign = _mm_and_si128(dq, sign_mask);
                __m128i phi = _mm_and_si128(dq, sign_mask_neg);
                __m128i rho = _mm_setzero_si128();
                do {
                    rho = _mm_add_epi16(rho, phi);
                    phi = _mm_srli_epi16(phi, zeta);
                } while (!_mm_testz_si128(phi, phi));
                _mm_storel_epi64((__m128i *)buf, _mm_or_si128(rho, sign));
            }
            buf += 4;
        }

        //Leftover
        if ((buf_len % 4) && (gclis[group] > gtli)) {
            for (uint32_t i = 0; i < (buf_len % 4); i++) {
                inv_quant_uniform_leftover(buf + i, gclis[group], gtli);
            }
        }
    }
    else if (dq_type == QUANT_TYPE_DEADZONE) {
        const uint32_t grup_numbers = buf_len / 8;
        const __m128i gtli_move = _mm_set1_epi16((1 << (gtli - 1)));
        const __m128i gtli_sse = _mm_set1_epi16(gtli);
        const uint64_t magic_val = 0x0001000100010001;

        uint32_t group = 0;
        for (; group < grup_numbers; group++) {
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
            //}
            gclis += 2;
            buf += 8;
        }
        //Leftover
        if ((buf_len % 8) >= 4) {
            if (gclis[0] > gtli) {
                for (uint32_t i = 0; i < 4; i++) {
                    inv_quant_deadzone_leftover(buf + i, gtli);
                }
            }
            buf += 4;
            gclis++;
        }
        if ((buf_len % 4) && (gclis[0] > gtli)) {
            for (uint32_t i = 0; i < (buf_len % 4); i++) {
                inv_quant_deadzone_leftover(buf + i, gtli);
            }
        }
    }
    else {
        assert("unknown quantization");
        return;
    }
}
