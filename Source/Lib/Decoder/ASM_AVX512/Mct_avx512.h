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
void inverse_rct_output_8bit_line_avx512(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                         uint8_t* out0, uint8_t* out1, uint8_t* out2, uint32_t width);
void inverse_rct_output_16bit_line_avx512(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                          uint16_t* out0, uint16_t* out1, uint16_t* out2, uint32_t width, uint8_t msb_aligned);

#ifdef __cplusplus
}
#endif

#endif //__DECODER_MCT_AVX512_H__
