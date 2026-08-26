/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_H__
#define __UNPACK_COMMON_H__

#include <string.h>
#include "UnPack_avx2.h"
#include "SvtUtility.h"
#include "UnpackShared.h"

/* Spreads count nibbles, right aligned, into four coefficients.
 *
 * PEXT would do this in four instructions, but this path also runs on CPUs
 * where it is microcoded and costs tens of cycles. So the nibbles are first
 * unpacked one per byte (byte j holds nibble j), and then, for each
 * coefficient, the required bit of every byte is raised into the sign position
 * and collected by VPMOVMSKB. The value is exactly the same: bit j of the
 * result is bit (3 - k) of nibble j, that is, the plane with weight 2^j.
 *
 * Nibbles past count are zero in acc, so the result needs no masking. */
static INLINE uint64_t unpack_planes_to_lanes_sse(uint64_t acc, uint8_t gtli) {
    const __m128i lo_mask = _mm_set1_epi8(0x0F);
    const __m128i packed = _mm_cvtsi64_si128((int64_t)acc);
    const __m128i lo = _mm_and_si128(packed, lo_mask);
    const __m128i hi = _mm_and_si128(_mm_srli_epi16(packed, 4), lo_mask);
    /* byte 2i is the low nibble of source byte i, byte 2i+1 is the high one */
    const __m128i nibbles = _mm_unpacklo_epi8(lo, hi);

    const uint64_t v0 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 4));
    const uint64_t v1 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 5));
    const uint64_t v2 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 6));
    const uint64_t v3 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 7));
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

/* Spreads count nibbles, right aligned, into four coefficients, using PEXT
 * instead of the nibble-split-and-movemask sequence above.
 *
 * PEXT gathers the bits selected by a mask into the low positions, so each
 * coefficient is one instruction instead of a slli+movemask pair; the whole
 * thing is four PEXTs plus three shifts and ORs instead of nine-odd SSE
 * instructions. It needs only BMI2, nothing AVX-512 about it - see
 * unpack_common_avx512.h, which used to define this before it moved here so
 * the AVX2 tier could use it too on a host with BMI2 but no AVX-512. PEXT is
 * microcoded and slow on some older CPUs even though it is a single fast
 * instruction on most current ones; callers must gate this on CPU_FLAGS_BMI2
 * (see setup_decoder_rtcd_internal). */
TARGET_BMI2 static INLINE uint64_t unpack_planes_to_lanes_pext(uint64_t acc, uint8_t gtli) {
    const uint64_t v0 = _pext_u64(acc, 0x8888888888888888ULL);
    const uint64_t v1 = _pext_u64(acc, 0x4444444444444444ULL);
    const uint64_t v2 = _pext_u64(acc, 0x2222222222222222ULL);
    const uint64_t v3 = _pext_u64(acc, 0x1111111111111111ULL);
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

/* Horizontal sum of eight 32-bit lanes.
 *
 * The budget of a band line is accumulated in 32-bit lanes and not in 16-bit
 * ones. A single group costs at most 256, which a 16-bit lane holds, but a line
 * of a couple of thousand groups does not, and the fold of a wrapped sum reads
 * as a cheap line: a corrupt stream would pass the budget check instead of
 * being rejected. Lines that wide occur at 8K and above. */
static INLINE uint32_t unpack_budget_hsum_epi32(__m256i sum_epi32) {
    __m128i sum_sse = _mm_add_epi32(_mm256_castsi256_si128(sum_epi32), _mm256_extracti128_si256(sum_epi32, 0x1));
    sum_sse = _mm_add_epi32(sum_sse, _mm_shuffle_epi32(sum_sse, 0x4E)); /* lanes 0+2 and 1+3 */
    sum_sse = _mm_add_epi32(sum_sse, _mm_shuffle_epi32(sum_sse, 0xB1)); /* all four */
    return (uint32_t)_mm_cvtsi128_si32(sum_sse);
}

#endif /*__UNPACK_COMMON_H__*/
