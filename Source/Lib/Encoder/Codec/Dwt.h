/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DWT_ENCODER_H__
#define __DWT_ENCODER_H__

#include <stdint.h>
#include "Pi.h"
#include "PiEnc.h"

#ifdef __cplusplus
extern "C" {
#endif

void dwt_horizontal_line_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);

/*DWT transform_V0_H[1,2,3,4,5]() calculate precinct with 1 line.
* When (input_bit_depth == 0) do not convert input.
* buffer_tmp - Need size: (width *3/2)
*/
typedef void (*transform_V0_ptr_t)(const pi_component_t* const component, const pi_enc_component_t* const component_enc,
                                   const int32_t* buff_in, uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr,
                                   uint16_t* buffer_out_16bit, uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

transform_V0_ptr_t transform_V0_get_function_ptr(uint8_t decom_h);

void transform_V0_H1(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

void transform_V0_H2(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

void transform_V0_H3(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

void transform_V0_H4(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

void transform_V0_H5(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp);

void transform_V1_Hx_precinct_recalc_HF_prev_c(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                               const int32_t* line_1, const int32_t* line_2);

void transform_V1_Hx_precinct(const pi_component_t* const component, const pi_enc_component_t* const component_enc,
                              transform_V0_ptr_t transform_v0_hx, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                              uint32_t height, const int32_t* line_0, const int32_t* line_1, const int32_t* line_2,
                              uint16_t* buffer_out_16bit, uint8_t param_out_Fq, int32_t* in_tmp_line_HF_prev,
                              int32_t* out_tmp_line_HF_next, int32_t* buffer_tmp);

void transform_V2_Hx_precinct_recalc_prec_0(uint32_t width, const int32_t* line_0, const int32_t* line_1, const int32_t* line_2,
                                            int32_t* buffer_next, int32_t* buffer_on_place, int32_t* buffer_tmp);

void transform_V2_Hx_precinct_recalc(const pi_component_t* const component, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                                     uint32_t height, const int32_t* line_p6, const int32_t* line_p5, const int32_t* line_p4,
                                     const int32_t* line_p3, const int32_t* line_p2, const int32_t* line_p1,
                                     const int32_t* line_0, const int32_t* line_1, const int32_t* line_2, int32_t* buffer_next,
                                     int32_t* buffer_on_place, int32_t* buffer_tmp);

void transform_V2_Hx_precinct(const pi_component_t* const component, const pi_enc_component_t* const component_enc,
                              transform_V0_ptr_t transform_V0_Hn_sub_1, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                              uint32_t height, const int32_t* line_2, const int32_t* line_3, const int32_t* line_4,
                              const int32_t* line_5, const int32_t* line_6, uint16_t* buffer_out_16bit, uint8_t param_out_Fq,
                              int32_t* buffer_prev, int32_t* buffer_next, int32_t* buffer_on_place, int32_t* buffer_tmp);

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_c(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1);
void transform_vertical_loop_lf_line_0_c(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0);
void transform_vertical_loop_lf_hf_line_0_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                            const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_line_x_prev_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                 const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                 const int32_t* line_p2);
void transform_vertical_loop_lf_hf_hf_line_x_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                               const int32_t* line_0, const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_hf_line_last_even_c(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                       const int32_t* in_hf_prev, const int32_t* line_0, const int32_t* line_1);

#ifdef __cplusplus
}
#endif

#endif /*__DWT_ENCODER_H__*/
