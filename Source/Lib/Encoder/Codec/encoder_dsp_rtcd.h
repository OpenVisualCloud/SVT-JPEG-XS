/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// This file is generated. Do not edit.
#ifndef ENCODER_DSP_RTCD_H_
#define ENCODER_DSP_RTCD_H_

#include "common_dsp_rtcd.h"

/**************************************
 * Instruction Set Support
 **************************************/

#ifdef _WIN32
#include <intrin.h>
#endif
#include "SvtType.h"
#include "BitstreamWriter.h"

#undef RTCD_EXTERN
#ifdef ENCODER_RTCD_C
#define RTCD_EXTERN // CHKN RTCD call in effect. declare the function pointers encHandle.
#else
#define RTCD_EXTERN extern // CHKN run time externing the function pointers.
#endif

#ifdef __cplusplus
extern "C" {
#endif

void setup_encoder_rtcd_internal(CPU_FLAGS flags);
void gc_precinct_stage_scalar_loop_ASM(uint32_t line_groups_num, uint16_t* coeff_data_ptr_16bit, uint8_t* gcli_data_ptr);

void msb_x16_ASM(uint16_t* in, uint8_t* out);

RTCD_EXTERN void (*image_shift)(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift,
                                int32_t offset);

RTCD_EXTERN void (*gc_precinct_stage_scalar)(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size,
                                             uint32_t width);

RTCD_EXTERN void (*quantization)(uint16_t* coeff_16bit, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli,
                                 QUANT_TYPE quant_type);

RTCD_EXTERN void (*linear_input_scaling_line_8bit)(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset);
RTCD_EXTERN void (*linear_input_scaling_line_16bit)(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                                    uint8_t bit_depth);

RTCD_EXTERN void (*pack_data_single_group)(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t gcli, uint8_t gtli);
RTCD_EXTERN void (*gc_precinct_stage_scalar_loop)(uint32_t line_groups_num, uint16_t* coeff_data_ptr_16bit,
                                                  uint8_t* gcli_data_ptr);

RTCD_EXTERN void (*dwt_horizontal_line)(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);
RTCD_EXTERN void (*transform_V1_Hx_precinct_recalc_HF_prev)(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                                            const int32_t* line_1, const int32_t* line_2);
RTCD_EXTERN void (*transform_vertical_loop_hf_line_0)(uint32_t width, int32_t* out_hf, const int32_t* line_0,
                                                      const int32_t* line_1);
RTCD_EXTERN void (*transform_vertical_loop_lf_line_0)(uint32_t width, int32_t* out_lf, const int32_t* in_hf,
                                                      const int32_t* line_0);
RTCD_EXTERN void (*transform_vertical_loop_lf_hf_line_0)(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                                         const int32_t* line_1, const int32_t* line_2);
RTCD_EXTERN void (*transform_vertical_loop_lf_hf_line_x_prev)(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                              const int32_t* line_p6, const int32_t* line_p5,
                                                              const int32_t* line_p4, const int32_t* line_p3,
                                                              const int32_t* line_p2);
RTCD_EXTERN void (*transform_vertical_loop_lf_hf_hf_line_x)(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                            const int32_t* in_hf_prev, const int32_t* line_0,
                                                            const int32_t* line_1, const int32_t* line_2);
RTCD_EXTERN void (*transform_vertical_loop_lf_hf_hf_line_last_even)(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                                    const int32_t* in_hf_prev, const int32_t* line_0,
                                                                    const int32_t* line_1);
RTCD_EXTERN void (*gc_precinct_sigflags_max)(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                             uint32_t gcli_width);
RTCD_EXTERN uint32_t (*rate_control_calc_vpred_cost_nosigf)(uint32_t gcli_width, uint8_t* gcli_data_top_ptr,
                                                            uint8_t* gcli_data_ptr, uint8_t* vpred_bits_pack, uint8_t gtli,
                                                            uint8_t gtli_max);

RTCD_EXTERN void (*rate_control_calc_vpred_cost_sigf_nosigf)(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                             uint32_t significance_group_size, uint8_t* gcli_data_top_ptr,
                                                             uint8_t* gcli_data_ptr, uint8_t* vpred_bits_pack,
                                                             uint8_t* vpred_significance, uint8_t gtli, uint8_t gtli_max,
                                                             uint32_t* pack_size_gcli_sigf_reduction,
                                                             uint32_t* pack_size_gcli_no_sigf);

RTCD_EXTERN void (*convert_packed_to_planar_rgb_8bit)(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                                      uint32_t line_width);
RTCD_EXTERN void (*convert_packed_to_planar_rgb_16bit)(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                                       uint32_t line_width);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
