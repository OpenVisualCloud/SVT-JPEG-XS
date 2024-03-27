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

void gc_precinct_stage_scalar_avx2(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size, uint32_t width);

void convert_packed_to_planar_rgb_8bit_avx2(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                            uint32_t line_width);
void convert_packed_to_planar_rgb_16bit_avx2(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                             uint32_t line_width);

#ifdef __cplusplus
}
#endif

#endif /*__RATE_CONTROL_AVX2_H__*/
