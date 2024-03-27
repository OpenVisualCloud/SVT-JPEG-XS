/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DECODER_NLT_AVX512_H__
#define __DECODER_NLT_AVX512_H__

#include "SvtJpegxsDec.h"
#include "Pi.h"
#include <immintrin.h>
#include "Codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

void linear_output_scaling_8bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w);
void linear_output_scaling_16bit_line_avx512(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w);

#ifdef __cplusplus
}
#endif

#endif //__DECODER_NLT_AVX512_H__
