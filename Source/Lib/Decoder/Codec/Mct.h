/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _MCT_H
#define _MCT_H

#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif
int32_t get_cfa_pattern(const picture_header_dynamic_t* picture_header_dynamic);
void inverse_rct_c(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h);
void inverse_rct_output_8bit_line_c(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                    uint8_t* out0, uint8_t* out1, uint8_t* out2, uint32_t width);
void inverse_rct_output_16bit_line_c(const int32_t* in0, const int32_t* in1, const int32_t* in2, uint32_t bw, uint32_t depth,
                                     uint16_t* out0, uint16_t* out1, uint16_t* out2, uint32_t width, uint8_t msb_aligned);
void mct_inverse_transform(int32_t* out_comps[MAX_COMPONENTS_NUM], const pi_t* pi,
                           const picture_header_dynamic_t* picture_header_dynamic, uint8_t hdr_Cpih);

void mct_inverse_transform_precinct(int32_t* out_comps[MAX_COMPONENTS_NUM],
                                    const picture_header_dynamic_t* picture_header_dynamic, int32_t w, int32_t h,
                                    uint8_t hdr_Cpih);

#ifdef __cplusplus
}
#endif

#endif /*_MCT_H*/
