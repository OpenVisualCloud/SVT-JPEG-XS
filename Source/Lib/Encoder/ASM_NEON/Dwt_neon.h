/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DWT_NEON_H__
#define __DWT_NEON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dwt_horizontal_line_neon(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /*__DWT_NEON_H__*/
