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

/* Splits the planes of a group and feeds the nibbles to the shared writer. */
static INLINE void pack_group_planes_nibw(nib_writer_t* w, __m128i reversed, uint8_t gcli, uint8_t gtli) {
    __m128i tmp = _mm_slli_epi16(reversed, 16 - gcli);
    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        const uint32_t nibble = pack_group_nibble(tmp);
        tmp = _mm_slli_epi16(tmp, 1);
        nibw_put(w, nibble);
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
        for (uint32_t group = 0; group < groups; group++) {
            if (gclis[group] > gtli) {
                const __m128i reversed = pack_group_load_reversed(buf_16bit);
                /* the signs are the same top bits of the same lanes */
                nibw_put(&w, pack_group_nibble(reversed));
                pack_group_planes_nibw(&w, reversed, gclis[group], gtli);
            }
            buf_16bit += GROUP_SIZE;
        }
    }
    else {
        for (uint32_t group = 0; group < groups; group++) {
            if (gclis[group] > gtli) {
                pack_group_planes_nibw(&w, pack_group_load_reversed(buf_16bit), gclis[group], gtli);
            }
            buf_16bit += GROUP_SIZE;
        }
    }
    nibw_finish(&w, bitstream);
}

#endif /*__PACK_GROUP_HELPER_H__*/
