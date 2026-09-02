/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnPack_neon.h"
#include "unpack_common_neon.h"
#include "SvtUtility.h"
#include "EncDec.h" /* TRUNCATION_MAX: the single load holds fifteen planes at most */

/* Shared walk for unpack_n_groups_neon/unpack_n_groups_nosign_neon: the two
 * differ only in whether a sign nibble is read alongside the planes. has_sign is
 * a parameter rather than two separate bodies, but both call sites below pass a
 * compile-time constant (0 or 1), so the compiler specializes this into two
 * variants with no runtime branch inside either loop. */
static INLINE void unpack_n_groups_impl_neon(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                             uint32_t safe_bytes, const int has_sign) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* The vast majority of groups are empty: about eighty-seven per cent on
     * 1080p at 4 bits per pixel. The output is zeroed in one pass and the walk
     * follows the bits of a non-empty mask, so an empty group costs nothing and
     * the per-group branch is gone. */
    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    /* Fast path: a group's position in the stream comes from adding up lengths
     * rather than from finishing the previous group, so the group loads are
     * independent of each other. */
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 16u);
        uint64_t todo = neon_nonempty_group_mask(gclis + group, chunk, gtli);
        while (todo) {
            const uint32_t k = svt_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            /* A corrupt stream can carry a GCLI above the truncation maximum:
             * the unary code yields up to thirty-one. Such a group holds more
             * bit planes than the single load can take - fifteen - and the
             * shift that would extract it would be undefined, so the group is
             * left to the sequential reader below. */
            if (size > TRUNCATION_MAX || ((nib + (uint32_t)has_sign) >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            uint64_t out = unpack_planes_to_lanes_neon(unpack_load_nibbles(base, nib + (uint32_t)has_sign, size), gtli);
            if (has_sign) {
                out |= unpack_sign_spread[unpack_one_nibble(base, nib)];
            }
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size + (uint32_t)has_sign;
            todo &= todo - 1;
        }
        group += chunk;
    }

tail:
    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);
    buf += (size_t)group * GROUP_SIZE;

    /* End of the line is read sequentially so the load never runs past the data.
     * Zeroes need not be written: the buffer is already cleared. */
    for (; group < n_groups; group++) {
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            uint64_t signs = 0;
            if (has_sign) {
                signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            }
            /* The fast path above declines to touch a group whose plane count
             * is above the truncation maximum; this sequential reader has to
             * decline the same way, or it reads size nibbles - unbounded by
             * anything - straight past the end of whatever buffer backs it. */
            const int32_t read_planes = (size > TRUNCATION_MAX) ? TRUNCATION_MAX : size;
            uint64_t acc = 0;
            for (int32_t i = 0; i < read_planes; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes_neon(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_neon(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                          uint32_t safe_bytes) {
    unpack_n_groups_impl_neon(gclis, gtli, r, buf, n_groups, safe_bytes, 1);
}

void unpack_n_groups_nosign_neon(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                 uint32_t safe_bytes) {
    unpack_n_groups_impl_neon(gclis, gtli, r, buf, n_groups, safe_bytes, 0);
}

/* The budget of the line, then the line itself. Kept next to the NEON parsers
 * rather than shared with the x86 one because the budget is vectorized too, and
 * the two vector bodies cannot live in the same translation unit. */
static INLINE SvtJxsErrorType_t unpack_charge_budget(const uint8_t* gclis, uint32_t group_num, uint32_t leftover, uint8_t gtli,
                                                     int32_t* precinct_bits_left, const int has_sign) {
    const uint32_t chunks = group_num / 16;
    uint32_t bits_sum = unpack_budget_nibbles_neon(gclis, chunks, gtli, has_sign);

    const uint8_t* rest = gclis + chunks * 16;
    for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
        if (rest[group] > gtli) {
            bits_sum += (uint32_t)(rest[group] - gtli) + (uint32_t)has_sign;
        }
    }
    /* The cost is compared before it is subtracted, so that the widened sum
     * cannot be lost again in the last step: a line whose nibble count exceeds
     * what int32_t holds is rejected rather than wrapped into a small debit. */
    if (*precinct_bits_left < 0 || (uint64_t)bits_sum * 4 > (uint64_t)*precinct_bits_left) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    *precinct_bits_left -= (int32_t)(bits_sum * 4);
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t unpack_data_neon(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                   uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                   int32_t* precinct_bits_left) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    const uint32_t group_num = w / GROUP_SIZE;
    const uint32_t leftover = w % GROUP_SIZE;
    const uint32_t safe_bytes = unpack_safe_byte_count(bitstream->size > bitstream->offset ? bitstream->size - bitstream->offset
                                                                                          : 0);
    const int has_sign = (sign_flag == 0);

    const SvtJxsErrorType_t budget = has_sign
        ? unpack_charge_budget(gclis, group_num, leftover, gtli, precinct_bits_left, 1)
        : unpack_charge_budget(gclis, group_num, leftover, gtli, precinct_bits_left, 0);
    if (budget != SvtJxsErrorNone) {
        return budget;
    }

    reader_short_t reader;
    reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
    reader.bits_used = bitstream->bits_used;

    const unpack_groups_fn groups = has_sign ? unpack_n_groups_neon : unpack_n_groups_nosign_neon;
    groups(gclis, gtli, &reader, buf, group_num, safe_bytes);
    if (leftover) {
        buf += group_num * GROUP_SIZE;
        gclis += group_num;
        uint16_t buf_tmp[GROUP_SIZE];
        groups(gclis, gtli, &reader, buf_tmp, 1, 0);
        if (!has_sign) {
            *leftover_signs_num = 0;
            for (uint32_t leftover_id = leftover; leftover_id < GROUP_SIZE; leftover_id++) {
                *leftover_signs_num += !!buf_tmp[leftover_id];
            }
        }
        memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
    }
    bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
    bitstream->bits_used = reader.bits_used;

    return SvtJxsErrorNone;
}
