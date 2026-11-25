/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_V210_AVX2_H_
#define _SVT_JPEG_XS_V210_AVX2_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void convert_packed_to_planar_v210_avx2(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width);
void convert_planar_to_packed_v210_avx2(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210,
                                        uint32_t width);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /*_SVT_JPEG_XS_V210_AVX2_H_*/
