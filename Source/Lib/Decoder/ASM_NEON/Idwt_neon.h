/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __IDWT_NEON_H__
#define __IDWT_NEON_H__

#include <stdint.h>
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif

void idwt_horizontal_line_lf16_hf16_neon(const int16_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);
void idwt_horizontal_line_lf32_hf16_neon(const int32_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);

void idwt_vertical_line_neon(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4], uint32_t len,
                             int32_t first_precinct, int32_t last_precinct, int32_t height);

void idwt_vertical_line_recalc_neon(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                    uint32_t len, uint32_t precinct_line_idx);

#ifdef __cplusplus
}
#endif

#endif /*__IDWT_NEON_H__*/
