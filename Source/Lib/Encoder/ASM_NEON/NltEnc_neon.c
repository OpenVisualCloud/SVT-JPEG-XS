/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltEnc_neon.h"
#include "Definitions.h"
#include "Codestream.h"
#include <arm_neon.h>

/* Input scaling: every sample is widened to 32 bits, shifted up to the working
 * depth and biased by half of it.
 *
 * The shift and the subtraction are one multiply-accumulate: the sample times
 * two to the shift, added to minus the offset. Unsigned arithmetic wraps to
 * exactly the signed result, so a widening multiply does per output vector what
 * widening, shifting and subtracting took three instructions to do - and three
 * was no better than the plain loop, which the compiler vectorizes on its own.
 *
 * The factor has to fit sixteen positions, which is the same as saying the
 * shift is under sixteen. It always is: the shift only brings a sample up to
 * the working depth, and a sample shifted by sixteen would not fit the
 * thirty-two the reference computes in either. The line falls back to the
 * scalar loop if it ever does not - one prediction per line that always holds. */

static INLINE int32x4_t scale_mla(uint32x4_t base, uint16x4_t samples, uint16x4_t scale) {
    return vreinterpretq_s32_u32(vmlal_u16(base, samples, scale));
}

#define SCALE_SHIFT_MAX 16

void linear_input_scaling_line_8bit_neon(const uint8_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset) {
    uint32_t j = 0;

    if (shift < SCALE_SHIFT_MAX) {
        const uint16x4_t scale = vdup_n_u16((uint16_t)(1u << shift));
        const uint32x4_t base = vdupq_n_u32((uint32_t)(-offset));
        for (; j + 16 <= w; j += 16) {
            const uint8x16_t bytes = vld1q_u8(src + j);
            const uint16x8_t lo = vmovl_u8(vget_low_u8(bytes));
            const uint16x8_t hi = vmovl_u8(vget_high_u8(bytes));
            vst1q_s32(dst + j, scale_mla(base, vget_low_u16(lo), scale));
            vst1q_s32(dst + j + 4, scale_mla(base, vget_high_u16(lo), scale));
            vst1q_s32(dst + j + 8, scale_mla(base, vget_low_u16(hi), scale));
            vst1q_s32(dst + j + 12, scale_mla(base, vget_high_u16(hi), scale));
        }
        if (j + 8 <= w) {
            const uint16x8_t wide = vmovl_u8(vld1_u8(src + j));
            vst1q_s32(dst + j, scale_mla(base, vget_low_u16(wide), scale));
            vst1q_s32(dst + j + 4, scale_mla(base, vget_high_u16(wide), scale));
            j += 8;
        }
    }

    for (; j < w; j++) {
        dst[j] = (int32_t)((uint32_t)src[j] << shift) - (int32_t)offset;
    }
}

void linear_input_scaling_line_16bit_neon(const uint16_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth) {
    const uint16_t input_mask = (uint16_t)((1 << bit_depth) - 1);
    uint32_t j = 0;

    if (shift < SCALE_SHIFT_MAX) {
        const uint16x8_t mask_vec = vdupq_n_u16(input_mask);
        const uint16x4_t scale = vdup_n_u16((uint16_t)(1u << shift));
        const uint32x4_t base = vdupq_n_u32((uint32_t)(-offset));
        for (; j + 8 <= w; j += 8) {
            const uint16x8_t masked = vandq_u16(vld1q_u16(src + j), mask_vec);
            vst1q_s32(dst + j, scale_mla(base, vget_low_u16(masked), scale));
            vst1q_s32(dst + j + 4, scale_mla(base, vget_high_u16(masked), scale));
        }
    }

    for (; j < w; j++) {
        dst[j] = ((src[j] & input_mask) << shift) - offset;
    }
}

void linear_input_scaling_line_16bit_msb_neon(const uint16_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset,
                                              uint8_t bit_depth) {
    (void)bit_depth;
    uint32_t j = 0;

    if (shift < SCALE_SHIFT_MAX) {
        const uint16x4_t scale = vdup_n_u16((uint16_t)(1u << shift));
        const uint32x4_t base = vdupq_n_u32((uint32_t)(-offset));
        for (; j + 8 <= w; j += 8) {
            const uint16x8_t val = vld1q_u16(src + j);
            vst1q_s32(dst + j, scale_mla(base, vget_low_u16(val), scale));
            vst1q_s32(dst + j + 4, scale_mla(base, vget_high_u16(val), scale));
        }
    }

    for (; j < w; j++) {
        dst[j] = ((uint32_t)src[j] << shift) - offset;
    }
}

/* The last step of the transform: a coefficient leaves the 32-bit pipeline as a
 * sign and a magnitude in sixteen bits.
 *
 * The branch the reference takes on the sign of the coefficient is what makes
 * this worth vectorizing - it is taken about half the time and cannot be
 * learned. Vectorized there is no branch at all: the magnitude is an absolute
 * value, the shift is the same for the whole line, and the sign bit is put back
 * by a mask.
 *
 * The sign is dropped on a magnitude that rounded to zero, exactly as the
 * reference does - negative zero is not a value the stream can carry. */
void image_shift_neon(uint16_t *out_coeff_16bit, int32_t *in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset) {
    const int32x4_t voffset = vdupq_n_s32(offset);
    /* a negative count is a shift to the right, and the operand is a magnitude,
     * so this is the reference's arithmetic shift of a non-negative value */
    const int32x4_t vshift = vdupq_n_s32(-shift);
    const uint16x8_t sign_bit = vdupq_n_u16(BITSTREAM_MASK_SIGN);

    uint32_t i = 0;
    for (; i + 8 <= width; i += 8) {
        const int32x4_t lo = vld1q_s32(in_coeff_32bit + i);
        const int32x4_t hi = vld1q_s32(in_coeff_32bit + i + 4);
        const int32x4_t mag_lo = vshlq_s32(vaddq_s32(vabsq_s32(lo), voffset), vshift);
        const int32x4_t mag_hi = vshlq_s32(vaddq_s32(vabsq_s32(hi), voffset), vshift);
        const uint16x8_t mag = vcombine_u16(vmovn_u32(vreinterpretq_u32_s32(mag_lo)),
                                            vmovn_u32(vreinterpretq_u32_s32(mag_hi)));
        /* all ones where the coefficient was negative */
        const uint16x8_t negative = vcombine_u16(vmovn_u32(vcltq_s32(lo, vdupq_n_s32(0))),
                                                 vmovn_u32(vcltq_s32(hi, vdupq_n_s32(0))));
        const uint16x8_t keeps_sign = vandq_u16(negative, vtstq_u16(mag, mag));
        vst1q_u16(out_coeff_16bit + i, vorrq_u16(mag, vandq_u16(keeps_sign, sign_bit)));
    }

    for (; i < width; i++) {
        const int32_t val = in_coeff_32bit[i];
        int32_t mag = ((val < 0 ? -val : val) + offset) >> shift;
        if ((val < 0) && mag) {
            mag |= BITSTREAM_MASK_SIGN;
        }
        out_coeff_16bit[i] = (uint16_t)mag;
    }
}
