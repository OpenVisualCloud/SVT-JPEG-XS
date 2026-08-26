/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __UNPACK_AVX512_H__
#define __UNPACK_AVX512_H__

#include "SvtJpegxsDec.h"
#include "BitstreamReader.h"
#include "UnPack_avx2.h" /* reader_short_t, shared with the AVX2 parser */

#ifdef __cplusplus
extern "C" {
#endif

void unpack_n_groups_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes);
void unpack_n_groups_nosign_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                   uint32_t safe_bytes);

SvtJxsErrorType_t unpack_data_avx512(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left);

#ifdef __cplusplus
}
#endif

#endif //__UNPACK_AVX512_H__
