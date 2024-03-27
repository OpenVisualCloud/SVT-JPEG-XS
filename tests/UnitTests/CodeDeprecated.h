/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _UT_CODE_DEPRECATED_H_
#define _UT_CODE_DEPRECATED_H_
#include "SvtJpegxs.h"

#ifndef RTC_EXTERN
#define RTC_EXTERN extern
#endif

RTC_EXTERN int (*dwt_horizontal_depricated)(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                            uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);
RTC_EXTERN int (*dwt_vertical_depricated)(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                          uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);
RTC_EXTERN void (*idwt_deprecated_vertical)(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width,
                                            uint32_t height, uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);
RTC_EXTERN void (*idwt_deprecated_horizontal)(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width,
                                              uint32_t height, uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);

void setup_depricated_test_rtcd_internal(CPU_FLAGS flags);

void dwt_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len, uint32_t stride_lf, uint32_t stride_hf,
                      uint32_t stride_in);

int dwt_vertical_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                              uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);

int dwt_horizontal_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in);

void idwt_deprecated_vertical_c(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);

void idwt_deprecated_horizontal_c(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                  uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);

void idwt_deprecated_vertical_avx2(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                   uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);

void idwt_deprecated_horizontal_avx2(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                     uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out);

void linear_input_scaling_8bit_depricated(uint8_t Bw, uint8_t* src, int32_t* dst, uint32_t w, uint32_t h, uint32_t src_stride,
                                          uint32_t dst_stride, uint8_t input_bit_depth);

void linear_input_scaling_16bit_depricated(uint8_t Bw, uint16_t* src, int32_t* dst, uint32_t w, uint32_t h, uint32_t src_stride,
                                           uint32_t dst_stride, uint8_t input_bit_depth);

#endif /*_UT_CODE_DEPRECATED_H_*/
