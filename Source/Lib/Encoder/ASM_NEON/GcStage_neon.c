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

/* The largest coded line index within each significance group - eight code
 * groups, a constant of the format.
 *
 * A pairwise maximum halves the number of values and leaves them in place, so
 * three rounds of it turn sixty-four indices into the eight maxima they belong
 * to, in order and ready to store. x86 gets there by transposing an eight by
 * eight block first; here the reduction is the shuffle. */
void gc_precinct_sigflags_max_neon(uint8_t *significance_data_max_ptr, uint8_t *gcli_data_ptr, uint32_t group_sign_size,
                                   uint32_t gcli_width) {
    UNUSED(group_sign_size);
    assert(group_sign_size == SIGNIFICANCE_GROUP_SIZE);
    const uint32_t group_number = gcli_width / SIGNIFICANCE_GROUP_SIZE;
    const uint32_t size_leftover = gcli_width % SIGNIFICANCE_GROUP_SIZE;
    const uint8_t *in = gcli_data_ptr;
    uint32_t group = 0;

    for (; group + 8 <= group_number; group += 8) {
        const uint8x16_t v0 = vld1q_u8(in);
        const uint8x16_t v1 = vld1q_u8(in + 16);
        const uint8x16_t v2 = vld1q_u8(in + 32);
        const uint8x16_t v3 = vld1q_u8(in + 48);
        const uint8x16_t pairs = vpmaxq_u8(vpmaxq_u8(v0, v1), vpmaxq_u8(v2, v3));
        vst1_u8(significance_data_max_ptr + group, vget_low_u8(vpmaxq_u8(pairs, pairs)));
        in += 8 * SIGNIFICANCE_GROUP_SIZE;
    }

    for (; group < group_number; group++) {
        significance_data_max_ptr[group] = vmaxv_u8(vld1_u8(in));
        in += SIGNIFICANCE_GROUP_SIZE;
    }

    /*Leftover last column*/
    if (size_leftover) {
        uint8_t max = 0;
        for (uint32_t j = 0; j < size_leftover; j++) {
            if (in[j] > max) {
                max = in[j];
            }
        }
        significance_data_max_ptr[group] = max;
    }
}
