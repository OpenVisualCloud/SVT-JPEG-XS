/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DWT53DECODER_AVX2_H_
#define _DWT53DECODER_AVX2_H_

#include "DwtDecoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void idwt_horizontal_line_lf16_hf16_avx2(const int16_t* lf_ptr, const int16_t* hf_ptr, int32_t* out_ptr, uint32_t len,
                                         uint8_t shift);
void idwt_horizontal_line_lf32_hf16_avx2(const int32_t* lf_ptr, const int16_t* hf_ptr, int32_t* out_ptr, uint32_t len,
                                         uint8_t shift);
void idwt_vertical_line_avx2(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4], uint32_t len,
                             int32_t first_precinct, int32_t last_precinct, int32_t height);

void idwt_vertical_line_recalc_avx2(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                    uint32_t len, uint32_t precinct_line_idx);
void idwt_vertical_line_output_8bit_avx2(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1,
                                         const int32_t* in_out0, int32_t* out2, uint8_t* img_odd, uint8_t* img_even, uint32_t len,
                                         uint32_t bw, uint32_t depth);
void idwt_vertical_line_output_16bit_avx2(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1,
                                          const int32_t* in_out0, int32_t* out2, uint16_t* img_odd, uint16_t* img_even,
                                          uint32_t len, uint32_t bw, uint32_t depth, uint8_t msb_aligned);

#ifdef __cplusplus
}
#endif
#endif /*_DWT53DECODER_AVX2_H_*/
