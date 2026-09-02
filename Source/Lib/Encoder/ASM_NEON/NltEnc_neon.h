/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __NLT_ENC_NEON_H__
#define __NLT_ENC_NEON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void image_shift_neon(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift,
                      int32_t offset);

void linear_input_scaling_line_8bit_neon(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset);
void linear_input_scaling_line_16bit_neon(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                          uint8_t bit_depth);
void linear_input_scaling_line_16bit_msb_neon(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                              uint8_t bit_depth);

#ifdef __cplusplus
}
#endif

#endif /*__NLT_ENC_NEON_H__*/
