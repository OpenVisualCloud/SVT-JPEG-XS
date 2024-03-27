/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_AVX2_H__
#define __UNPACK_AVX2_H__

#include "SvtJpegxsDec.h"
#include <immintrin.h>
#include "Codestream.h"
#include "BitstreamReader.h"

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left);
#ifdef __cplusplus
}
#endif

#endif //__UNPACK_AVX2_H__
