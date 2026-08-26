/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "GcStage_neon.h"
#include "Definitions.h"
#include "SvtUtility.h"
#include <arm_neon.h>

/* The greatest coded line index of a group is the position of the highest set
 * bit among its four coefficients, sign excluded. Two instructions do most of
 * it here: the four-register load hands over the four coefficients of eight
 * groups already separated by their position within a group, so the reduction
 * is three plain ORs rather than a horizontal fold, and CLZ gives the bit
 * position directly - x86 has to reach for a float conversion or a lookup. */
void gc_precinct_stage_scalar_loop_neon(uint32_t line_groups_num, uint16_t *coeff_data_ptr_16bit, uint8_t *gcli_data_ptr) {
    uint32_t group = 0;

    for (; group + 8 <= line_groups_num; group += 8) {
        const uint16x8x4_t lanes = vld4q_u16(coeff_data_ptr_16bit);
        uint16x8_t merge_or = vorrq_u16(vorrq_u16(lanes.val[0], lanes.val[1]), vorrq_u16(lanes.val[2], lanes.val[3]));
        merge_or = vshlq_n_u16(merge_or, 1); //Remove sign bit

        /* 15 - clz is the index of the highest set bit; an empty group would give
         * 15 - 16, so it is masked back to zero. */
        const uint16x8_t msb = vsubq_u16(vdupq_n_u16(15), vclzq_u16(merge_or));
        vst1_u8(gcli_data_ptr, vmovn_u16(vandq_u16(msb, vtstq_u16(merge_or, merge_or))));

        coeff_data_ptr_16bit += 8 * GROUP_SIZE;
        gcli_data_ptr += 8;
    }

    for (; group < line_groups_num; group++) {
        uint16_t merge_or = coeff_data_ptr_16bit[0];
        merge_or |= coeff_data_ptr_16bit[1];
        merge_or |= coeff_data_ptr_16bit[2];
        merge_or |= coeff_data_ptr_16bit[3];
        merge_or <<= 1; //Remove sign bit

        if (merge_or) {
            gcli_data_ptr[0] = svt_log2_32(merge_or); //MSB
            assert(gcli_data_ptr[0] <= TRUNCATION_MAX);
        }
        else {
            gcli_data_ptr[0] = 0;
        }
        coeff_data_ptr_16bit += GROUP_SIZE;
        gcli_data_ptr++;
    }
}
