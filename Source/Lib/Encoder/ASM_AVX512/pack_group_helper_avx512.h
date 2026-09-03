/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_GROUP_HELPER_AVX512_H__
#define __PACK_GROUP_HELPER_AVX512_H__

#include <immintrin.h>
#include "pack_group_helper.h"

/* pack_group_planes_pdep / pack_groups_masked_pdep used to live here, but they
 * need only AVX2 (for pack_nonempty_mask) plus BMI2 (for PDEP) - nothing
 * AVX-512 about them. They are now in pack_group_helper.h so the AVX2 tier can
 * use them too on hosts with BMI2 but no AVX-512. See setup_encoder_rtcd_internal
 * for the dispatch that picks this path. */

#endif /*__PACK_GROUP_HELPER_AVX512_H__*/
