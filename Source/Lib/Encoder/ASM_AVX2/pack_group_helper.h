/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_GROUP_HELPER_H__
#define __PACK_GROUP_HELPER_H__

#include <immintrin.h>
#include "Definitions.h"
#include "Codestream.h"
#include "BitstreamWriter.h"
#include "SvtUtility.h"
#include <string.h>

/* Packing one group of four coefficients is a transposition of bit planes: each
 * plane contributes one nibble to the stream, assembled from the same-numbered
 * bits of the four values, most significant plane first.
 *
 * The key trick: reverse the lane order once on input and the required nibble
 * becomes exactly the mask of sign bits. PACKSSWB saturates 16-bit values to
 * 8-bit ones keeping the sign, and VPMOVMSKB gathers the top bits of the bytes
 * into an integer where bit i is the top bit of lane i. No horizontal summing
 * is needed at all.
 *
 * The former implementation extracted the bit with a shift and folded the lanes
 * with two PHADDW per plane; those two instructions took about forty per cent
 * of the function's time - PHADDW is three uops and has a long latency.
 *
 * Only SSE2 here, nothing wider than 128 bits: there are just 64 bits of data
 * and nothing to widen the register with. That is why one implementation serves
 * both AVX2 and AVX-512. */
/* Lanes are reversed once: lane 0 receives buf[3], lane 3 receives buf[0]. */
static INLINE __m128i pack_group_load_reversed(const uint16_t* buf) {
    return _mm_shufflelo_epi16(_mm_loadl_epi64((const __m128i*)buf), _MM_SHUFFLE(0, 1, 2, 3));
}

/* Nibble made of the top bits of the four lanes. */
static INLINE uint32_t pack_group_nibble(__m128i reversed) {
    return (uint32_t)_mm_movemask_epi8(_mm_packs_epi16(reversed, reversed)) & 0xF;
}

static INLINE void pack_data_single_group_sse(bitstream_writer_t* bitstream, const uint16_t* buf, uint8_t gcli, uint8_t gtli) {
    /* the top significant plane is moved into the sign bit */
    __m128i tmp = _mm_slli_epi16(pack_group_load_reversed(buf), 16 - gcli);

    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        const uint32_t nibble = pack_group_nibble(tmp);
        tmp = _mm_slli_epi16(tmp, 1);
        write_4_bits_align4(bitstream, (uint8_t)nibble);
    }
}

/* The planes of a group collected into one word: the top plane is the top
 * nibble. Returning a word beats handing out nibbles one by one: the writer
 * gets a single call instead of a chain of short dependent steps. */
static INLINE uint64_t pack_group_planes_word(__m128i reversed, uint8_t gcli, uint8_t gtli) {
    __m128i tmp = _mm_slli_epi16(reversed, 16 - gcli);
    uint64_t word = 0;
    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        word = (word << 4) | pack_group_nibble(tmp);
        tmp = _mm_slli_epi16(tmp, 1);
    }
    return word;
}

/* Walk over the groups of a line driven by a mask of the non-empty ones.
 *
 * Empty groups - those whose GCLI does not exceed the truncation level - are
 * the vast majority: about eighty-seven per cent on 1080p at four bits per
 * pixel. With such a skew the per-group "is there anything to write" branch
 * predicts badly, and it costs more than the write itself. Here the mask is
 * built for thirty-two groups at a time and the walk follows only its set bits:
 * an empty group costs nothing.
 *
 * The mask comes from a saturating subtract: subs_epu8 yields zero exactly
 * where GCLI does not exceed the truncation level. A signed compare against
 * zero is valid afterwards because GCLI never exceeds fifteen.
 *
 * emit_signs is a parameter rather than a field: both call sites pass a
 * constant, so the compiler expands the function into two variants with no
 * branch inside the loop. */
static INLINE uint64_t pack_nonempty_mask(const uint8_t* gclis, uint32_t chunk, uint8_t gtli) {
    __m256i gcli_vec;
    if (chunk >= 32) {
        gcli_vec = _mm256_loadu_si256((const __m256i*)gclis);
    }
    else {
        uint8_t padded[32] = {0};
        memcpy(padded, gclis, chunk);
        gcli_vec = _mm256_loadu_si256((const __m256i*)padded);
    }
    return (uint32_t)_mm256_movemask_epi8(
        _mm256_cmpgt_epi8(_mm256_subs_epu8(gcli_vec, _mm256_set1_epi8((char)gtli)), _mm256_setzero_si256()));
}

static INLINE void pack_groups_masked(nib_writer_t* w, const uint16_t* buf_16bit, const uint8_t* gclis, uint32_t groups,
                                      uint8_t gtli, const int emit_signs) {
    for (uint32_t base = 0; base < groups; base += 32) {
        const uint32_t chunk = MIN(groups - base, 32u);
        uint64_t todo = pack_nonempty_mask(gclis + base, chunk, gtli);
        while (todo) {
            const uint32_t group = base + svt_first_set_bit(todo);
            todo &= todo - 1;
            const __m128i reversed = pack_group_load_reversed(buf_16bit + (size_t)group * GROUP_SIZE);
            const uint32_t planes = (uint32_t)gclis[group] - gtli;
            uint64_t word = pack_group_planes_word(reversed, gclis[group], gtli);
            uint32_t count = planes;
            if (emit_signs) {
                /* the signs are the same top bits of the same lanes, and they go first */
                word |= (uint64_t)pack_group_nibble(reversed) << (4 * planes);
                count = planes + 1;
            }
            nibw_put_group(w, word, count);
        }
    }
}

/* The whole loop over the full groups of a line.
 *
 * It is dispatched as a whole for two reasons: the nibble writer must live from
 * the start of the line to its end rather than being set up per group, and the
 * per-group indirect call disappears along the way - there are hundreds of them
 * per line. */
static INLINE void pack_data_groups_sse(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups,
                                        uint8_t gtli, uint8_t sign_flag) {
    nib_writer_t w;
    nibw_init(&w, bitstream);
    if (sign_flag == 0) {
        pack_groups_masked(&w, buf_16bit, gclis, groups, gtli, 1);
    }
    else {
        pack_groups_masked(&w, buf_16bit, gclis, groups, gtli, 0);
    }
    nibw_finish(&w, bitstream);
}

#endif /*__PACK_GROUP_HELPER_H__*/
