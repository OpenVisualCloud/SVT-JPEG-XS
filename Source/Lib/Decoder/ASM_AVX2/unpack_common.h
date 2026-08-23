/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_H__
#define __UNPACK_COMMON_H__

#include <string.h>
#include "UnPack_avx2.h"

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

/* Index of the lowest set bit. The wrapper exists for MSVC, which has no
 * __builtin_ctzll, and TZCNT cannot be assumed either - BMI1 is not enabled for
 * this target, only -mbmi2. */
static INLINE uint32_t unpack_first_set_bit(uint64_t mask) {
    assert(mask != 0);
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, mask);
    return (uint32_t)index;
#else
    return (uint32_t)__builtin_ctzll(mask);
#endif
}

#endif /*__UNPACK_COMMON_H__*/
