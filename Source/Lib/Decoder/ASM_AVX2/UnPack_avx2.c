/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnPack_avx2.h"
#include "unpack_common.h"
#include "SvtUtility.h"
#include "EncDec.h" /* TRUNCATION_MAX: the single load holds fifteen planes at most */

/* Shared walk for unpack_n_groups/unpack_n_groups_nosign: the two differ only in
 * whether a sign nibble is read alongside the planes. has_sign is a parameter
 * rather than two separate bodies, but both call sites below pass a compile-time
 * constant (0 or 1), so the compiler specializes this into the same two variants
 * that used to be written out by hand, with no runtime branch inside either loop -
 * the same reasoning pack_groups_masked's emit_signs parameter already relies on, on
 * the encoder side (see pack_group_helper.h). */
static INLINE void unpack_n_groups_impl(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                        uint32_t safe_bytes, const int has_sign) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* The vast majority of groups are empty: about eighty-seven per cent on
     * 1080p at 4 bits per pixel. Each of them used to cost a GCLI load, a
     * compare, a poorly predicted branch and a store of eight zeroes. Now the
     * output is zeroed in one pass and the walk follows the bits of a non-empty
     * mask, so an empty group costs nothing and the per-group branch is gone.
     *
     * The mask comes from a saturating subtract: subs_epu8 yields zero exactly
     * where GCLI does not exceed the truncation level. Testing that result for
     * equality with zero and inverting the mask is therefore an exact unsigned
     * comparison, which a signed compare against zero would not be: a corrupt
     * stream can put any byte value into GCLI, and AVX-512 tests it unsigned. */
    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    /* Fast path: a group's position in the stream comes from adding up lengths
     * rather than from finishing the previous group, so the group loads are
     * independent of each other. */
    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 32u);
        __m256i gcli_vec;
        if (chunk >= 32) {
            gcli_vec = _mm256_loadu_si256((const __m256i*)(gclis + group));
        }
        else {
            uint8_t padded[32] = {0};
            memcpy(padded, gclis + group, chunk);
            gcli_vec = _mm256_loadu_si256((const __m256i*)padded);
        }
        uint64_t todo = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(gcli_vec, gtli_vec), zero));
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
            uint64_t out = unpack_planes_to_lanes_sse(unpack_load_nibbles(base, nib + (uint32_t)has_sign, size), gtli);
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
            /* A corrupt stream can carry a GCLI far above the truncation maximum: the
             * unary code yields up to thirty-one, and vertical prediction can wrap it
             * to anything up to two hundred and fifty-five. The fast path above
             * already declines to touch such a group at all; this sequential reader
             * has to decline the same way, or it reads size nibbles - unbounded by
             * anything - straight past the end of whatever buffer backs it. */
            const int32_t read_planes = (size > TRUNCATION_MAX) ? TRUNCATION_MAX : size;
            uint64_t acc = 0;
            for (int32_t i = 0; i < read_planes; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes_sse(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups, uint32_t safe_bytes) {
    unpack_n_groups_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 1);
}

void unpack_n_groups_nosign(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes) {
    unpack_n_groups_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 0);
}

SvtJxsErrorType_t unpack_data_common(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left, unpack_groups_fn groups_sign, unpack_groups_fn groups_nosign) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    const uint32_t group_num = w / GROUP_SIZE;
    const uint32_t leftover = w % GROUP_SIZE;
    const __m128i gtli_const = _mm_set1_epi8(gtli);
    const uint32_t safe_bytes = unpack_safe_byte_count(bitstream->size > bitstream->offset ? bitstream->size - bitstream->offset
                                                                                           : 0);

    if (sign_flag == 0) {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            const __m256i zero = _mm256_setzero_si256();
            __m256i sum_epi32 = zero;
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                /* Unsigned widening: subs_epu8 is an unsigned saturating subtract, so on a
                 * corrupt stream the byte can be anything up to 255. Widening it as signed
                 * would make such a group subtract from the budget instead of adding to it,
                 * and the bitstream would not be rejected. */
                __m256i bits_epu16 = _mm256_cvtepu8_epi16(bits_epu8);
                __m256i signs = _mm256_cmpgt_epi16(bits_epu16, zero);
                signs = _mm256_srli_epi16(signs, 15);
                /* Cost of one group: its planes plus one nibble of signs. At most 256, so
                 * the per-group value still fits a 16-bit lane; the running total does not
                 * and is widened before it is added. */
                __m256i group_bits = _mm256_add_epi16(bits_epu16, signs);
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpacklo_epi16(group_bits, zero));
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpackhi_epi16(group_bits, zero));
                gclis_ptr += 16;
            }
            uint32_t bits_sum = unpack_budget_hsum_epi32(sum_epi32);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli) + 1;
                }
            }
            /* The cost is compared before it is subtracted, so that the widened sum
             * cannot be lost again in the last step: a line whose nibble count exceeds
             * what int32_t holds is rejected rather than wrapped into a small debit. */
            if (*precinct_bits_left < 0 || (uint64_t)bits_sum * 4 > (uint64_t)*precinct_bits_left) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_sign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_sign(gclis, gtli, &reader, buf_tmp, 1, 0);
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }
    else {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            const __m256i zero = _mm256_setzero_si256();
            __m256i sum_epi32 = zero;
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                /* Unsigned widening, and 32-bit accumulation, for the same reasons as in
                 * the branch above. */
                __m256i group_bits = _mm256_cvtepu8_epi16(bits_epu8);
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpacklo_epi16(group_bits, zero));
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpackhi_epi16(group_bits, zero));
                gclis_ptr += 16;
            }
            uint32_t bits_sum = unpack_budget_hsum_epi32(sum_epi32);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli);
                }
            }
            /* The cost is compared before it is subtracted, so that the widened sum
             * cannot be lost again in the last step: a line whose nibble count exceeds
             * what int32_t holds is rejected rather than wrapped into a small debit. */
            if (*precinct_bits_left < 0 || (uint64_t)bits_sum * 4 > (uint64_t)*precinct_bits_left) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_nosign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_nosign(gclis, gtli, &reader, buf_tmp, 1, 0);
            *leftover_signs_num = 0;
            for (uint32_t leftover_id = leftover; leftover_id < GROUP_SIZE; leftover_id++) {
                *leftover_signs_num += !!buf_tmp[leftover_id];
            }
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    return unpack_data_common(bitstream,
                              buf,
                              w,
                              gclis,
                              group_size,
                              gtli,
                              sign_flag,
                              leftover_signs_num,
                              precinct_bits_left,
                              unpack_n_groups,
                              unpack_n_groups_nosign);
}
