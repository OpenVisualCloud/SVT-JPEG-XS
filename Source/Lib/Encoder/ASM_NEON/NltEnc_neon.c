/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltEnc_neon.h"
#include "Definitions.h"
#include <arm_neon.h>

/* Input scaling: every sample is widened to 32 bits, shifted up to the working
 * depth and biased by half of it. The shift is the same for the whole line, so
 * it is a scalar duplicated once outside the loop. */

static INLINE void scale_store_u32(int32_t *dst, uint32x4_t widened, int32x4_t shift, int32x4_t offset) {
    vst1q_s32(dst, vsubq_s32(vreinterpretq_s32_u32(vshlq_u32(widened, shift)), offset));
}

void linear_input_scaling_line_8bit_neon(const uint8_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset) {
    const int32x4_t shift_vec = vdupq_n_s32(shift);
    const int32x4_t offset_vec = vdupq_n_s32(offset);
    uint32_t j = 0;

    for (; j + 8 <= w; j += 8) {
        const uint16x8_t wide = vmovl_u8(vld1_u8(src + j));
        scale_store_u32(dst + j, vmovl_u16(vget_low_u16(wide)), shift_vec, offset_vec);
        scale_store_u32(dst + j + 4, vmovl_u16(vget_high_u16(wide)), shift_vec, offset_vec);
    }
    for (; j < w; j++) {
        dst[j] = (int32_t)((uint32_t)src[j] << shift) - (int32_t)offset;
    }
}

void linear_input_scaling_line_16bit_neon(const uint16_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth) {
    const uint16_t input_mask = (uint16_t)((1 << bit_depth) - 1);
    const uint16x8_t mask_vec = vdupq_n_u16(input_mask);
    const int32x4_t shift_vec = vdupq_n_s32(shift);
    const int32x4_t offset_vec = vdupq_n_s32(offset);
    uint32_t j = 0;

    for (; j + 8 <= w; j += 8) {
        const uint16x8_t masked = vandq_u16(vld1q_u16(src + j), mask_vec);
        scale_store_u32(dst + j, vmovl_u16(vget_low_u16(masked)), shift_vec, offset_vec);
        scale_store_u32(dst + j + 4, vmovl_u16(vget_high_u16(masked)), shift_vec, offset_vec);
    }
    for (; j < w; j++) {
        dst[j] = ((src[j] & input_mask) << shift) - offset;
    }
}

void linear_input_scaling_line_16bit_msb_neon(const uint16_t *src, int32_t *dst, uint32_t w, uint8_t shift, int32_t offset,
                                              uint8_t bit_depth) {
    (void)bit_depth;
    const int32x4_t shift_vec = vdupq_n_s32(shift);
    const int32x4_t offset_vec = vdupq_n_s32(offset);
    uint32_t j = 0;

    for (; j + 8 <= w; j += 8) {
        const uint16x8_t val = vld1q_u16(src + j);
        scale_store_u32(dst + j, vmovl_u16(vget_low_u16(val)), shift_vec, offset_vec);
        scale_store_u32(dst + j + 4, vmovl_u16(vget_high_u16(val)), shift_vec, offset_vec);
    }
    for (; j < w; j++) {
        dst[j] = ((uint32_t)src[j] << shift) - offset;
    }
}
