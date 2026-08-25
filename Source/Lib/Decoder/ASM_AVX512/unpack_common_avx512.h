/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_AVX512_H__
#define __UNPACK_COMMON_AVX512_H__

#include <immintrin.h>
#include "Definitions.h"

/* Spreads count nibbles, right aligned, into four coefficients. */
static INLINE uint64_t unpack_planes_to_lanes(uint64_t acc, uint8_t gtli) {
    const uint64_t v0 = _pext_u64(acc, 0x8888888888888888ULL);
    const uint64_t v1 = _pext_u64(acc, 0x4444444444444444ULL);
    const uint64_t v2 = _pext_u64(acc, 0x2222222222222222ULL);
    const uint64_t v3 = _pext_u64(acc, 0x1111111111111111ULL);
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

#endif /*__UNPACK_COMMON_AVX512_H__*/
