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
#include "UnpackShared.h"

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t unpack_data_common(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left, unpack_groups_fn groups_sign, unpack_groups_fn groups_nosign);

void unpack_n_groups(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups, uint32_t safe_bytes);
void unpack_n_groups_nosign(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes);

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left);

/* Same as unpack_data_avx2 but spreads a group's planes with PEXT instead of a
 * nibble-split-and-movemask sequence. Needs BMI2 on top of AVX2 - callers must
 * check CPU_FLAGS_BMI2 on the host before selecting this, since PEXT is
 * microcoded and slow on some older CPUs even though it is a single fast
 * instruction on most current ones. */
void unpack_n_groups_bmi2(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                          uint32_t safe_bytes);
void unpack_n_groups_nosign_bmi2(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                 uint32_t safe_bytes);
SvtJxsErrorType_t unpack_data_avx2_bmi2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                        uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                        int32_t* precinct_bits_left);
#ifdef __cplusplus
}
#endif

#endif //__UNPACK_AVX2_H__
