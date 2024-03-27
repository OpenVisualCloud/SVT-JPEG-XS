/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _NLT_H
#define _NLT_H

#include "DecHandle.h"
#include "SvtJpegxsDec.h"
#include "decoder_dsp_rtcd.h"

#ifdef __cplusplus
extern "C" {
#endif

void linear_output_scaling_8bit_c(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                  svt_jpeg_xs_image_buffer_t* out);
void linear_output_scaling_16bit_c(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                   svt_jpeg_xs_image_buffer_t* out);
void nlt_inverse_transform(int32_t* comps[MAX_COMPONENTS_NUM], const pi_t* pi, const picture_header_const_t* picture_header_const,
                           const picture_header_dynamic_t* picture_header_dynamic, svt_jpeg_xs_image_buffer_t* out);

void nlt_inverse_transform_line_8bit(int32_t* in, uint32_t depth, const picture_header_dynamic_t* picture_header_dynamic,
                                     uint8_t* out, int32_t w);
void nlt_inverse_transform_line_16bit(int32_t* in, uint32_t depth, const picture_header_dynamic_t* picture_header_dynamic,
                                      uint16_t* out, int32_t w);
void linear_output_scaling_8bit_line_c(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w);
void linear_output_scaling_16bit_line_c(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w);

#ifdef __cplusplus
}
#endif

#endif /*_NLT_H*/
