/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_GROUP_HELPER_AVX512_H__
#define __PACK_GROUP_HELPER_AVX512_H__

#include <immintrin.h>
#include "pack_group_helper.h"

/* All sixteen bit planes of a group in one go.
 *
 * Packing a group is a transposition of a "four values by sixteen positions"
 * matrix: the nibble of plane j consists of the j-th bits of the four values,
 * with the first value contributing the top bit of the nibble. PDEP scatters
 * the bits of a number over the set bits of a mask, so the mask 0x8888... lays
 * the positions of the first value exactly on the top bits of all sixteen
 * nibbles, 0x4444... does the same for the second, and so on; the four results
 * only have to be ORed together.
 *
 * The gain is that the cost stops depending on the number of planes: each one
 * used to take a saturating pack, a sign-mask extraction and a shift, and a
 * group can have up to fifteen planes. Here it is always four PDEPs.
 *
 * The sign nibble falls out of the same computation for free: the sign is bit
 * 15, that is the top nibble of the word.
 *
 * PDEP is used without hesitation: it is microcoded and slow only on Zen 1 and
 * Zen 2, and this path is only reached when AVX-512 is present - that is on
 * Skylake-X and newer or Zen 4 and newer, where it is a single uop. */
static INLINE uint64_t pack_group_planes_pdep(const uint16_t *buf) {
    return _pdep_u64(buf[0], 0x8888888888888888ULL) | _pdep_u64(buf[1], 0x4444444444444444ULL) |
        _pdep_u64(buf[2], 0x2222222222222222ULL) | _pdep_u64(buf[3], 0x1111111111111111ULL);
}

static INLINE void pack_groups_masked_pdep(nib_writer_t *w, const uint16_t *buf_16bit, const uint8_t *gclis, uint32_t groups,
                                           uint8_t gtli, const int emit_signs) {
    for (uint32_t base = 0; base < groups; base += 32) {
        const uint32_t chunk = MIN(groups - base, 32u);
        uint64_t todo = pack_nonempty_mask(gclis + base, chunk, gtli);
        while (todo) {
            const uint32_t group = base + svt_first_set_bit(todo);
            todo &= todo - 1;
            const uint64_t all = pack_group_planes_pdep(buf_16bit + (size_t)group * GROUP_SIZE);
            const uint32_t planes = (uint32_t)gclis[group] - gtli;
            uint64_t word = (all >> (4 * gtli)) & (((uint64_t)1 << (4 * planes)) - 1);
            uint32_t count = planes;
            if (emit_signs) {
                word |= (all >> 60) << (4 * planes);
                count = planes + 1;
            }
            nibw_put_group(w, word, count);
        }
    }
}

#endif /*__PACK_GROUP_HELPER_AVX512_H__*/
