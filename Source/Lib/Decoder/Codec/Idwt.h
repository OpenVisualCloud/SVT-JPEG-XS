/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __IDWT_H__
#define __IDWT_H__

#include <stdint.h>
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif

void idwt_horizontal_line_lf16_hf16_c(const int16_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);
void idwt_horizontal_line_lf32_hf16_c(const int32_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);

typedef void (*inv_transform_V0_ptr_t)(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM],
                                       int32_t* buf_out, int32_t* buf_out_tmp, uint8_t shift);
void inv_transform_V0_H1(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift);
void inv_transform_V0_H2(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift);
void inv_transform_V0_H3(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift);
void inv_transform_V0_H4(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift);
void inv_transform_V0_H5(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift);

void idwt_vertical_line_c(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4], uint32_t len,
                          int32_t first_precinct, int32_t last_precinct, int32_t height);

void idwt_vertical_line_recalc_c(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                 uint32_t len, uint32_t precinct_line_idx);

#ifdef __cplusplus
}
#endif

#endif /*__IDWT_H__*/
