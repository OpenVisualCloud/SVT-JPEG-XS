/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _MCT_ENC_H
#define _MCT_ENC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Table F.2 - Forward reversible colour transformation (Cpih=1), the exact inverse of the
 * decoder's inverse_rct() (Source/Lib/Decoder/Codec/Mct.c). Operates in place on one row of
 * already-NLT-scaled (post nlt_input_scaling_line) int32 wavelet-domain samples, one row at
 * a time, matching the nlt_input_scaling_line() per-line convention. */
void forward_rct_line(int32_t* comp0, int32_t* comp1, int32_t* comp2, uint32_t width);

#ifdef __cplusplus
}
#endif

#endif /*_MCT_ENC_H*/
