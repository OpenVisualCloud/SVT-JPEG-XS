/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_NEON_H__
#define __UNPACK_COMMON_NEON_H__

#include <arm_neon.h>
#include "UnpackShared.h"
#include "NeonMask.h"

/* Spreads count nibbles, right aligned, into four coefficients.
 *
 * The nibbles are first unpacked one per byte (byte j holds nibble j), and
 * then, for each coefficient, the required bit of every byte is collected into
 * a word: bit j of the result is bit (3 - k) of nibble j, that is, the plane
 * with weight 2^j.
 *
 * Nibbles past count are zero in acc, so the result needs no masking. */
static INLINE uint64_t unpack_planes_to_lanes_neon(uint64_t acc, uint8_t gtli) {
    const uint8x8_t packed = vcreate_u8(acc);
    const uint8x8x2_t split = vzip_u8(vand_u8(packed, vdup_n_u8(0x0F)), vshr_n_u8(packed, 4));
    /* byte 2i is the low nibble of source byte i, byte 2i+1 is the high one */
    const uint8x16_t nibbles = vcombine_u8(split.val[0], split.val[1]);

    uint64_t out = 0;
    for (uint32_t coeff = 0; coeff < GROUP_SIZE; coeff++) {
        const uint8x16_t plane = vdupq_n_u8((uint8_t)(1u << (GROUP_SIZE - 1 - coeff)));
        const uint8x16_t taken = vandq_u8(nibbles, plane);
        out |= (uint64_t)neon_mask_from_bytes(vtstq_u8(taken, taken)) << (16 * coeff);
    }
    return out << gtli;
}

/* Nibbles a line of groups costs: the planes of every group whose GCLI exceeds
 * the truncation level, plus one nibble of signs per such group when the line
 * carries them.
 *
 * The saturating subtract yields zero exactly where GCLI does not exceed the
 * level, which is an exact unsigned comparison - a signed one would not be,
 * since a corrupt stream can put any byte value into GCLI.
 *
 * The running total is accumulated in 32-bit lanes. A single group costs at
 * most 256, which a 16-bit lane holds, but a line of a couple of thousand
 * groups does not, and the fold of a wrapped sum reads as a cheap line: a
 * corrupt stream would pass the budget check instead of being rejected. Lines
 * that wide occur at 8K and above. */
static INLINE uint32_t unpack_budget_nibbles_neon(const uint8_t* gclis, uint32_t chunks, uint8_t gtli, const int has_sign) {
    const uint8x16_t gtli_vec = vdupq_n_u8(gtli);
    uint32x4_t sum_u32 = vdupq_n_u32(0);

    for (uint32_t chunk = 0; chunk < chunks; chunk++, gclis += 16) {
        const uint8x16_t planes = vqsubq_u8(vld1q_u8(gclis), gtli_vec);
        uint16x8_t lo = vmovl_u8(vget_low_u8(planes));
        uint16x8_t hi = vmovl_u8(vget_high_u8(planes));
        if (has_sign) {
            /* Widened before the sign nibble is added: a group of 255 planes
             * would carry the sum past what a byte lane holds. */
            const uint8x16_t signs = vandq_u8(vtstq_u8(planes, planes), vdupq_n_u8(1));
            lo = vaddq_u16(lo, vmovl_u8(vget_low_u8(signs)));
            hi = vaddq_u16(hi, vmovl_u8(vget_high_u8(signs)));
        }
        sum_u32 = vpadalq_u16(sum_u32, lo);
        sum_u32 = vpadalq_u16(sum_u32, hi);
    }
    return vaddvq_u32(sum_u32);
}

#endif /*__UNPACK_COMMON_NEON_H__*/
