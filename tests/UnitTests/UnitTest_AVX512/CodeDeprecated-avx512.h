/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _UT_CODE_DEPRECATED_AVX512_H_
#define _UT_CODE_DEPRECATED_AVX512_H_

#include "SvtJpegxs.h"

int dwt_vertical_depricated_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                   uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);

int dwt_horizontal_depricated_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                     uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);

#endif /*_UT_CODE_DEPRECATED_AVX512_H_*/
