/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* One bit per byte of a comparison result - what x86 gets from a single
 * MOVMSKB. Advanced SIMD has no such instruction, and both the group parser and
 * the group packer need it, so the replacement lives here rather than twice. */

#ifndef __NEON_MASK_H__
#define __NEON_MASK_H__

#ifdef ARCH_AARCH64

#include <arm_neon.h>
#include "SvtType.h"

/* Each byte is weighted by its position and the two halves are reduced
 * separately: ADDV sums a whole vector, and the sixteen weights would not fit
 * one byte otherwise. Low byte of the result comes from the low half. */
static INLINE uint32_t neon_mask_from_bytes(uint8x16_t nonzero) {
    static const uint8_t weights_tbl[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t weighted = vandq_u8(nonzero, vld1q_u8(weights_tbl));
    return (uint32_t)vaddv_u8(vget_low_u8(weighted)) | ((uint32_t)vaddv_u8(vget_high_u8(weighted)) << 8);
}

/* Mask of the groups whose GCLI exceeds the truncation level, sixteen at a
 * time. The saturating subtract yields zero exactly where it does not, which is
 * an exact unsigned comparison - a signed one would not be, since a corrupt
 * stream can put any byte value into GCLI. */
static INLINE uint32_t neon_nonempty_group_mask(const uint8_t* gclis, uint32_t chunk, uint8_t gtli) {
    uint8x16_t gcli_vec;
    if (chunk >= 16) {
        gcli_vec = vld1q_u8(gclis);
    }
    else {
        uint8_t padded[16] = {0};
        memcpy(padded, gclis, chunk);
        gcli_vec = vld1q_u8(padded);
    }
    const uint8x16_t planes = vqsubq_u8(gcli_vec, vdupq_n_u8(gtli));
    return neon_mask_from_bytes(vtstq_u8(planes, planes));
}

#endif /* ARCH_AARCH64 */

#endif /*__NEON_MASK_H__*/
