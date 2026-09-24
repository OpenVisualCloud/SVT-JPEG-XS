/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DECODER_MCT_AVX512_H__
#define __DECODER_MCT_AVX512_H__

#include "SvtJpegxsDec.h"
#include "Pi.h"
#include <immintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

void inverse_rct_avx512(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif

#endif //__DECODER_MCT_AVX512_H__
