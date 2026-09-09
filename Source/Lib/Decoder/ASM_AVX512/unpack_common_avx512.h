/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_COMMON_AVX512_H__
#define __UNPACK_COMMON_AVX512_H__

#include <immintrin.h>
#include "Definitions.h"
#include "unpack_common.h"

/* unpack_planes_to_lanes used to be defined here as a plain PEXT-based spread,
 * but it needs only BMI2, nothing AVX-512 - it is now unpack_planes_to_lanes_pext
 * in unpack_common.h so the AVX2 tier can use it too on hosts with BMI2 but no
 * AVX-512. See setup_decoder_rtcd_internal for the dispatch. */
static INLINE uint64_t unpack_planes_to_lanes(uint64_t acc, uint8_t gtli) {
    return unpack_planes_to_lanes_pext(acc, gtli);
}

#endif /*__UNPACK_COMMON_AVX512_H__*/
