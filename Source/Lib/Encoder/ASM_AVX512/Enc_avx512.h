/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _ENCODER_AVX512_H_
#define _ENCODER_AVX512_H_

#include "Definitions.h"
#include "SvtType.h"

#ifdef __cplusplus
extern "C" {
#endif

void dwt_horizontal_line_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);
void linear_input_scaling_line_8bit_avx512(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset);
void linear_input_scaling_line_16bit_avx512(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                            uint8_t bit_depth);
void linear_input_scaling_line_16bit_msb_avx512(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                                uint8_t bit_depth);
void image_shift_avx512(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset);

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_avx512(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1);
void transform_vertical_loop_lf_line_0_avx512(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0);
void transform_vertical_loop_lf_hf_line_0_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                                 const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_line_x_prev_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                      const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                      const int32_t* line_p2);
void transform_vertical_loop_lf_hf_hf_line_x_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                                    const int32_t* line_0, const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_hf_line_last_even_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                            const int32_t* in_hf_prev, const int32_t* line_0,
                                                            const int32_t* line_1);
void transform_V1_Hx_precinct_recalc_HF_prev_avx512(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                                    const int32_t* line_1, const int32_t* line_2);
void convert_packed_to_planar_rgb_8bit_avx512(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                              uint32_t line_width);
void convert_packed_to_planar_rgb_16bit_avx512(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                               uint32_t line_width);

void gc_histogram_16_avx512(const uint8_t* data, uint32_t width, uint16_t* hist);

void gc_precinct_stage_scalar_avx512(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size, uint32_t width);
void gc_precinct_sigflags_max_avx512(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                     uint32_t gcli_width);
void forward_rct_line_avx512(int32_t* comp0, int32_t* comp1, int32_t* comp2, uint32_t width);
void gc_lossless_stats_gtli0_avx512(const uint8_t* data, uint32_t width, uint32_t* out_sum, uint16_t* out_zero);
void rc_lut_derive_avx512(uint16_t* gc_hist, uint32_t* no_sign_tbl, uint32_t* sign_tbl, uint16_t* sig_hist);
#ifdef __cplusplus
}
#endif

#endif /*_ENCODER_AVX512_H_*/
