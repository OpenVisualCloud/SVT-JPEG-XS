/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _NON_LINEAR_TRANSFORM_ENCODER_H_
#define _NON_LINEAR_TRANSFORM_ENCODER_H_
#include <Definitions.h>
#include <Pi.h>

#ifdef __cplusplus
extern "C" {
#endif

void image_shift_c(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset);
void nlt_input_scaling_line(const void* src, int32_t* dst, uint32_t width, picture_header_dynamic_t* hdr,
                            uint8_t input_bit_depth);
void linear_input_scaling_line_8bit_c(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset);
void linear_input_scaling_line_16bit_c(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                       uint8_t bit_depth);

#ifdef __cplusplus
}
#endif

#endif /*_NON_LINEAR_TRANSFORM_ENCODER_H_*/
