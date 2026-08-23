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

/* The bound up to which an over-reading load stays inside the line's data. */
static INLINE uint32_t unpack_fast_byte_limit(uint32_t nib0, uint32_t total_nibbles) {
    const uint32_t last_byte = (nib0 + total_nibbles + 1) >> 1;
    return last_byte >= 8 ? last_byte - 8 : 0;
}

#endif /*__UNPACK_COMMON_H__*/
