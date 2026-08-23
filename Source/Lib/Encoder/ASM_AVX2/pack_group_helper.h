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
static INLINE void pack_data_single_group_sse(bitstream_writer_t* bitstream, const uint16_t* buf, uint8_t gcli, uint8_t gtli) {
    __m128i tmp = _mm_loadl_epi64((const __m128i*)buf);
    /* lane 0 receives buf[3], lane 3 receives buf[0] */
    tmp = _mm_shufflelo_epi16(tmp, _MM_SHUFFLE(0, 1, 2, 3));
    /* the top significant plane is moved into the sign bit */
    tmp = _mm_slli_epi16(tmp, 16 - gcli);

    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        const uint32_t nibble = (uint32_t)_mm_movemask_epi8(_mm_packs_epi16(tmp, tmp)) & 0xF;
        tmp = _mm_slli_epi16(tmp, 1);
        write_4_bits_align4(bitstream, (uint8_t)nibble);
    }
}

#endif /*__PACK_GROUP_HELPER_H__*/
