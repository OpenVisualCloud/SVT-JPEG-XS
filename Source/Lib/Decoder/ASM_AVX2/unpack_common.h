/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_H__
#define __UNPACK_COMMON_H__

#include <string.h>
#include "UnPack_avx2.h"
#include "SvtUtility.h"

#if defined(_MSC_VER)
#define UNPACK_BSWAP64(x) _byteswap_uint64(x)
#else
#define UNPACK_BSWAP64(x) __builtin_bswap64(x)
#endif

/* Sign nibble: its bit (3 - i) belongs to coefficient i and has to become the
 * top bit of the i-th 16-bit word. The table replaces four shifts. */
static const uint64_t unpack_sign_spread[16] = {
    0x0000000000000000ULL,
    0x8000000000000000ULL,
    0x0000800000000000ULL,
    0x8000800000000000ULL,
    0x0000000080000000ULL,
    0x8000000080000000ULL,
    0x0000800080000000ULL,
    0x8000800080000000ULL,
    0x0000000000008000ULL,
    0x8000000000008000ULL,
    0x0000800000008000ULL,
    0x8000800000008000ULL,
    0x0000000080008000ULL,
    0x8000000080008000ULL,
    0x0000800080008000ULL,
    0x8000800080008000ULL,
};

/* Reads count nibbles starting at nibble number nib and returns them right
 * aligned: nibble nib becomes the most significant of the count, that is the
 * top bit plane, and the low nibble of the result is the lowest plane.
 *
 * The point is that the whole group is taken with a single eight-byte load
 * instead of a chain of per-nibble accesses. Bytes arrive high nibble first, so
 * the word is reversed with BSWAP. The shift inside a byte is at most one
 * nibble and count is at most fifteen, so 4 + 60 = 64 bits fit exactly into one
 * word.
 *
 * The load reads up to eight bytes ahead, so it may only be used where that
 * many bytes are known to follow the start of the group; the end of the line is
 * read sequentially. */
static INLINE uint64_t unpack_load_nibbles(const uint8_t* mem, uint32_t nib, uint32_t count) {
    uint64_t w;
    memcpy(&w, mem + (nib >> 1), sizeof(w));
    w = UNPACK_BSWAP64(w);
    w <<= (nib & 1) * 4;
    return w >> (64 - 4 * count);
}

static INLINE uint32_t unpack_one_nibble(const uint8_t* mem, uint32_t nib) {
    return (uint32_t)((mem[nib >> 1] >> (4 - (nib & 1) * 4)) & 0xF);
}

/* How many leading bytes from the current position may start an over-reading
 * load: eight readable bytes must remain past the start of the group.
 *
 * The bound comes from the end of the buffer, not from the length of the line
 * being parsed. Extra bytes that land in the word are masked off by the plane
 * count anyway, so the parser never leaves its own data; the only question here
 * is the right to touch the memory. The former per-line bound pushed forty per
 * cent of all groups onto the slow path, because bands of the upper
 * decomposition levels are shorter than eight bytes in their entirety.
 *
 * A count is returned rather than the last allowed index: zero has to mean "not
 * at all", whereas index zero would also mean "byte zero is allowed". */
static INLINE uint32_t unpack_safe_byte_count(uint32_t bytes_left) {
    return bytes_left >= 8 ? bytes_left - 7 : 0;
}

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
