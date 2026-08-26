/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant_neon.h"
#include <arm_neon.h>

/* Two code groups per vector, the way dequant does it. The scale of the uniform
 * mode depends on the group's GCLI, but the two groups sit in the two halves of
 * the vector, so each half is shifted by a scalar of its own and no per-lane
 * shift vector is needed.
 *
 * A group whose GCLI does not exceed the truncation level is zeroed rather than
 * skipped: that is what the C implementation does, and a branch per group would
 * cost more than the store. */

static const uint16_t quant_sign_mask = BITSTREAM_MASK_SIGN;

/* d = ((d << scale) - d + (1 << gcli)) >> (gcli + 1), computed in 32 bits the
 * way the C implementation computes it: the shift carries the value past what a
 * 16-bit lane holds before the shift back brings it down again. */
static INLINE uint16x4_t quant_uniform_half(uint16x4_t magnitude, uint8_t gcli, uint8_t gtli) {
    const uint32x4_t d = vmovl_u16(magnitude);
    const int32_t scale = (int32_t)gcli - (int32_t)gtli + 1;
    const uint32x4_t scaled = vshlq_u32(d, vdupq_n_s32(scale));
    const uint32x4_t rounded = vaddq_u32(vsubq_u32(scaled, d), vdupq_n_u32(1u << gcli));
    return vmovn_u32(vshlq_u32(rounded, vdupq_n_s32(-((int32_t)gcli + 1))));
}

static INLINE void quant_uniform_neon(uint16_t *buf, uint32_t size, uint8_t *gclis, uint8_t gtli) {
    const uint16x8_t sign_mask = vdupq_n_u16(quant_sign_mask);
    const uint16x8_t magnitude_mask = vdupq_n_u16((uint16_t)~quant_sign_mask);
    const uint32_t groups = size / GROUP_SIZE;
    const uint32_t pairs = groups / 2;

    uint32_t group = 0;
    for (uint32_t pair = 0; pair < pairs; pair++, group += 2) {
        const uint8_t gcli_lo = gclis[group];
        const uint8_t gcli_hi = gclis[group + 1];

        if (gcli_lo <= gtli && gcli_hi <= gtli) {
            vst1q_u16(buf, vdupq_n_u16(0));
            buf += 2 * GROUP_SIZE;
            continue;
        }

        const uint16x8_t val = vld1q_u16(buf);
        const uint16x8_t sign = vandq_u16(val, sign_mask);
        const uint16x8_t magnitude = vandq_u16(val, magnitude_mask);

        uint16x8_t quantized = vcombine_u16(quant_uniform_half(vget_low_u16(magnitude), gcli_lo, gtli),
                                            quant_uniform_half(vget_high_u16(magnitude), gcli_hi, gtli));
        quantized = vshlq_u16(quantized, vdupq_n_s16((int16_t)gtli));

        /* the sign comes back only where something survived the quantization */
        quantized = vorrq_u16(quantized, vandq_u16(sign, vtstq_u16(quantized, quantized)));

        const uint16x8_t touched = vcombine_u16(vdup_n_u16(gcli_lo > gtli ? 0xFFFF : 0),
                                                vdup_n_u16(gcli_hi > gtli ? 0xFFFF : 0));
        vst1q_u16(buf, vandq_u16(quantized, touched));
        buf += 2 * GROUP_SIZE;
    }

    for (uint32_t coeff = group * GROUP_SIZE; coeff < size; coeff++) {
        const uint8_t gcli = gclis[coeff / GROUP_SIZE];
        if (gcli > gtli) {
            const uint16_t sign = buf[0] & quant_sign_mask;
            const uint8_t scale_value = gcli - gtli + 1;
            uint16_t d = buf[0] & ~quant_sign_mask;
            d = (uint16_t)(((d << scale_value) - d + (1 << gcli)) >> (gcli + 1));
            buf[0] = (uint16_t)(d << gtli);
            if (buf[0]) {
                buf[0] |= sign;
            }
        }
        else {
            buf[0] = 0;
        }
        buf++;
    }
}

static INLINE void quant_deadzone_neon(uint16_t *buf, uint32_t size, uint8_t *gclis, uint8_t gtli) {
    const uint16x8_t sign_mask = vdupq_n_u16(quant_sign_mask);
    const uint16x8_t magnitude_mask = vdupq_n_u16((uint16_t)~quant_sign_mask);
    const int16x8_t gtli_down = vdupq_n_s16(-(int16_t)gtli);
    const int16x8_t gtli_up = vdupq_n_s16((int16_t)gtli);
    const uint32_t groups = size / GROUP_SIZE;
    const uint32_t pairs = groups / 2;

    uint32_t group = 0;
    for (uint32_t pair = 0; pair < pairs; pair++, group += 2) {
        const uint8_t gcli_lo = gclis[group];
        const uint8_t gcli_hi = gclis[group + 1];

        if (gcli_lo <= gtli && gcli_hi <= gtli) {
            vst1q_u16(buf, vdupq_n_u16(0));
            buf += 2 * GROUP_SIZE;
            continue;
        }

        const uint16x8_t val = vld1q_u16(buf);
        const uint16x8_t sign = vandq_u16(val, sign_mask);
        /* the low gtli bit planes are dropped, the value keeps its place */
        uint16x8_t quantized = vshlq_u16(vshlq_u16(vandq_u16(val, magnitude_mask), gtli_down), gtli_up);
        quantized = vorrq_u16(quantized, vandq_u16(sign, vtstq_u16(quantized, quantized)));

        const uint16x8_t touched = vcombine_u16(vdup_n_u16(gcli_lo > gtli ? 0xFFFF : 0),
                                                vdup_n_u16(gcli_hi > gtli ? 0xFFFF : 0));
        vst1q_u16(buf, vandq_u16(quantized, touched));
        buf += 2 * GROUP_SIZE;
    }

    for (uint32_t coeff = group * GROUP_SIZE; coeff < size; coeff++) {
        const uint8_t gcli = gclis[coeff / GROUP_SIZE];
        if (gcli > gtli) {
            const uint16_t sign = buf[0] & quant_sign_mask;
            buf[0] = (uint16_t)(((buf[0] & ~quant_sign_mask) >> gtli) << gtli);
            if (buf[0]) {
                buf[0] |= sign;
            }
        }
        else {
            buf[0] = 0;
        }
        buf++;
    }
}

void quantization_neon(uint16_t *buf, uint32_t size, uint8_t *gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);

    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_neon(buf, size, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_neon(buf, size, gclis, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
