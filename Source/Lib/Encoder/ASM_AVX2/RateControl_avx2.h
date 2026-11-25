/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __RATE_CONTROL_AVX2_H__
#define __RATE_CONTROL_AVX2_H__

#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t rate_control_calc_vpred_cost_nosigf_avx2(uint32_t gcli_width, uint8_t* gcli_data_top_ptr, uint8_t* gcli_data_ptr,
                                                  uint8_t* vpred_bits_pack, uint8_t gtli, uint8_t gtli_max);
void rate_control_calc_vpred_cost_sigf_nosigf_avx2(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                   uint32_t significance_group_size, uint8_t* gcli_data_top_ptr,
                                                   uint8_t* gcli_data_ptr, uint8_t* vpred_bits_pack, uint8_t* vpred_significance,
                                                   uint8_t gtli, uint8_t gtli_max, uint32_t* pack_size_gcli_sigf_reduction,
                                                   uint32_t* pack_size_gcli_no_sigf);

void gc_histogram_16_avx2(const uint8_t* data, uint32_t width, uint16_t* hist);
void gc_precinct_sigflags_max_avx2(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                   uint32_t gcli_width);
void forward_rct_line_avx2(int32_t* comp0, int32_t* comp1, int32_t* comp2, uint32_t width);
void gc_lossless_stats_gtli0_avx2(const uint8_t* data, uint32_t width, uint32_t* out_sum, uint16_t* out_zero);
void rc_lut_derive_avx2(uint16_t* gc_hist, uint32_t* no_sign_tbl, uint32_t* sign_tbl, uint16_t* sig_hist);

void gc_precinct_stage_scalar_avx2(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size, uint32_t width);

void convert_packed_to_planar_rgb_8bit_avx2(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                            uint32_t line_width);
void convert_packed_to_planar_rgb_16bit_avx2(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                             uint32_t line_width);

#ifdef __cplusplus
}
#endif

#endif /*__RATE_CONTROL_AVX2_H__*/
