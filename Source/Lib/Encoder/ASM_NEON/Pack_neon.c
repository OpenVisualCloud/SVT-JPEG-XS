/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_neon.h"
#include "Codestream.h"
#include "SvtUtility.h"
#include "NeonMask.h"
#include <arm_neon.h>
#include <string.h>

/* Packing one group of four coefficients is a transposition of bit planes: each
 * plane contributes one nibble to the stream, assembled from the same-numbered
 * bits of the four values, most significant plane first.
 *
 * The same trick the x86 packer uses applies here: reverse the lane order once
 * on input and the required nibble becomes exactly the mask of the top bits of
 * the four lanes. What differs is how that mask is taken - there is no MOVMSKB,
 * so the top bits are shifted into their lane positions and summed with ADDV.
 *
 * Nothing wider than four lanes is used: there are only four coefficients in a
 * group, and the plane loop is what the width would have to be spent on. */

/* Lanes are reversed once: lane 0 receives buf[3], lane 3 receives buf[0]. */
static INLINE uint16x4_t pack_group_load_reversed_neon(const uint16_t *buf) {
    return vrev64_u16(vld1_u16(buf));
}

/* Nibble made of the top bits of the four lanes. */
static INLINE uint32_t pack_group_nibble_neon(uint16x4_t reversed) {
    static const int16_t lane_shift_tbl[4] = {0, 1, 2, 3};
    return (uint32_t)vaddv_u16(vshl_u16(vshr_n_u16(reversed, 15), vld1_s16(lane_shift_tbl)));
}

/* The planes of a group collected into one word: the top plane is the top
 * nibble. Returning a word beats handing out nibbles one by one: the writer
 * gets a single call instead of a chain of short dependent steps. */
static INLINE uint64_t pack_group_planes_word_neon(uint16x4_t reversed, uint8_t gcli, uint8_t gtli) {
    /* the top significant plane is moved into the sign bit */
    uint16x4_t tmp = vshl_u16(reversed, vdup_n_s16((int16_t)(16 - gcli)));
    uint64_t word = 0;
    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        word = (word << 4) | pack_group_nibble_neon(tmp);
        tmp = vshl_n_u16(tmp, 1);
    }
    return word;
}

/* Walk over the groups of a line driven by a mask of the non-empty ones. Empty
 * groups - those whose GCLI does not exceed the truncation level - are the vast
 * majority, about eighty-seven per cent on 1080p at four bits per pixel, and
 * with such a skew the per-group branch predicts badly and costs more than the
 * write itself. Following the set bits of a mask makes an empty group free.
 *
 * emit_signs is a parameter rather than a field: both call sites pass a
 * constant, so the compiler expands the function into two variants with no
 * branch inside the loop. */
static INLINE void pack_groups_masked_neon(nib_writer_t *w, const uint16_t *buf_16bit, const uint8_t *gclis, uint32_t groups,
                                           uint8_t gtli, const int emit_signs) {
    for (uint32_t base = 0; base < groups; base += 16) {
        const uint32_t chunk = MIN(groups - base, 16u);
        uint64_t todo = neon_nonempty_group_mask(gclis + base, chunk, gtli);
        while (todo) {
            const uint32_t group = base + svt_first_set_bit(todo);
            todo &= todo - 1;
            const uint16x4_t reversed = pack_group_load_reversed_neon(buf_16bit + (size_t)group * GROUP_SIZE);
            const uint32_t planes = (uint32_t)gclis[group] - gtli;
            uint64_t word = pack_group_planes_word_neon(reversed, gclis[group], gtli);
            uint32_t count = planes;
            if (emit_signs) {
                /* the signs are the same top bits of the same lanes, and they go first */
                word |= (uint64_t)pack_group_nibble_neon(reversed) << (4 * planes);
                count = planes + 1;
            }
            nibw_put_group(w, word, count);
        }
    }
}

void pack_data_groups_neon(bitstream_writer_t *bitstream, uint16_t *buf_16bit, uint8_t *gclis, uint32_t groups, uint8_t gtli,
                           uint8_t sign_flag) {
    nib_writer_t w;
    nibw_init(&w, bitstream);
    if (sign_flag == 0) {
        pack_groups_masked_neon(&w, buf_16bit, gclis, groups, gtli, 1);
    }
    else {
        pack_groups_masked_neon(&w, buf_16bit, gclis, groups, gtli, 0);
    }
    nibw_finish(&w, bitstream);
}
