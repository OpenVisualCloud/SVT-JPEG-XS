/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __ENCODER_DWT_AVX2_H__
#define __ENCODER_DWT_AVX2_H__

#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

void dwt_horizontal_line_avx2(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_avx2(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1);
void transform_vertical_loop_lf_line_0_avx2(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0);
void transform_vertical_loop_lf_hf_line_0_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                               const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_line_x_prev_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                    const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                    const int32_t* line_p2);
void transform_vertical_loop_lf_hf_hf_line_x_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                                  const int32_t* line_0, const int32_t* line_1, const int32_t* line_2);
void transform_vertical_loop_lf_hf_hf_line_last_even_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                          const int32_t* in_hf_prev, const int32_t* line_0,
                                                          const int32_t* line_1);
void transform_V1_Hx_precinct_recalc_HF_prev_avx2(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                                  const int32_t* line_1, const int32_t* line_2);

#ifdef __cplusplus
}
#endif

#endif /*__ENCODER_DWT_AVX2_H__*/
