/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dequant_neon.h"
#include "Definitions.h"
#include "Codestream.h"
#include <arm_neon.h>

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

/* Two code groups per vector. Their scale values differ, which on x86 is what
 * forces one group at a time: the shift amount there is a scalar. NEON shifts by
 * a vector, so both groups are shifted by their own amount in one instruction. */
static INLINE void inv_quant_uniform_neon(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint8_t gtli) {
    const uint16x8_t sign_mask = vdupq_n_u16(BITSTREAM_MASK_SIGN);
    const uint16x8_t magnitude_mask = vdupq_n_u16((uint16_t)~BITSTREAM_MASK_SIGN);
    const uint32_t groups = buf_len / GROUP_SIZE;
    const uint32_t pairs = groups / 2;

    uint32_t group = 0;
    for (uint32_t pair = 0; pair < pairs; pair++, group += 2) {
        const int16_t zeta_lo = (int16_t)gclis[group] - gtli + 1;
        const int16_t zeta_hi = (int16_t)gclis[group + 1] - gtli + 1;

        if (zeta_lo > 1 || zeta_hi > 1) {
            const uint16x8_t dq = vld1q_u16(buf);
            const uint16x8_t sign = vandq_u16(dq, sign_mask);
            /* A group with nothing to do is masked off rather than skipped: with
             * its magnitudes zeroed it neither contributes to the sum nor holds
             * the loop below, which runs until every lane is empty. */
            const uint16x8_t touched = vcombine_u16(vdup_n_u16(zeta_lo > 1 ? 0xFFFF : 0),
                                                    vdup_n_u16(zeta_hi > 1 ? 0xFFFF : 0));
            const int16x8_t scale = vcombine_s16(vdup_n_s16((int16_t)-zeta_lo), vdup_n_s16((int16_t)-zeta_hi));

            uint16x8_t phi = vandq_u16(vandq_u16(dq, magnitude_mask), touched);
            uint16x8_t rho = vdupq_n_u16(0);
            while (vmaxvq_u16(phi)) {
                rho = vaddq_u16(rho, phi);
                phi = vshlq_u16(phi, scale);
            }
            vst1q_u16(buf, vbslq_u16(touched, vorrq_u16(rho, sign), dq));
        }
        buf += 2 * GROUP_SIZE;
    }

    /* An odd number of groups leaves one behind, and buf_len need not be a whole
     * number of groups either. */
    if (group < groups) {
        if (gclis[group] > gtli) {
            for (uint32_t i = 0; i < GROUP_SIZE; i++) {
                inv_quant_uniform_leftover(buf + i, gclis[group], gtli);
            }
        }
        buf += GROUP_SIZE;
        group++;
    }
    if ((buf_len % GROUP_SIZE) && (gclis[group] > gtli)) {
        for (uint32_t i = 0; i < (buf_len % GROUP_SIZE); i++) {
            inv_quant_uniform_leftover(buf + i, gclis[group], gtli);
        }
    }
}

static INLINE void inv_quant_deadzone_neon(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint8_t gtli) {
    const uint16x8_t magnitude_mask = vdupq_n_u16((uint16_t)~BITSTREAM_MASK_SIGN);
    const uint16x8_t gtli_bit = vdupq_n_u16((uint16_t)(1 << (gtli - 1)));
    const uint16x8_t gtli_vec = vdupq_n_u16(gtli);
    const uint32_t groups = buf_len / GROUP_SIZE;
    const uint32_t pairs = groups / 2;

    uint32_t group = 0;
    for (uint32_t pair = 0; pair < pairs; pair++, group += 2) {
        const uint16x8_t gcli = vcombine_u16(vdup_n_u16(gclis[group]), vdup_n_u16(gclis[group + 1]));
        const uint16x8_t dq = vld1q_u16(buf);
        const uint16x8_t magnitude = vandq_u16(dq, magnitude_mask);
        const uint16x8_t mask = vandq_u16(vcgtq_u16(gcli, gtli_vec), vtstq_u16(magnitude, magnitude));

        vst1q_u16(buf, vorrq_u16(dq, vandq_u16(gtli_bit, mask)));
        buf += 2 * GROUP_SIZE;
    }

    if (group < groups) {
        if (gclis[group] > gtli) {
            for (uint32_t i = 0; i < GROUP_SIZE; i++) {
                inv_quant_deadzone_leftover(buf + i, gtli);
            }
        }
        buf += GROUP_SIZE;
        group++;
    }
    if ((buf_len % GROUP_SIZE) && (gclis[group] > gtli)) {
        for (uint32_t i = 0; i < (buf_len % GROUP_SIZE); i++) {
            inv_quant_deadzone_leftover(buf + i, gtli);
        }
    }
}

void dequant_neon(uint16_t *buf, uint32_t buf_len, uint8_t *gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    if (gtli == 0) {
        return;
    }

    switch (dq_type) {
    case QUANT_TYPE_UNIFORM:
        inv_quant_uniform_neon(buf, buf_len, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        inv_quant_deadzone_neon(buf, buf_len, gclis, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
