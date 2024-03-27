/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __ENCODER_NLT_AVX2_H__
#define __ENCODER_NLT_AVX2_H__

#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

void image_shift_avx2(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset);
void linear_input_scaling_line_8bit_avx2(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset);
void linear_input_scaling_line_16bit_avx2(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth);

#ifdef __cplusplus
}
#endif

#endif /*__ENCODER_NLT_AVX2_H__*/
