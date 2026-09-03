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

typedef struct reader_short {
    uint8_t* mem;
    uint8_t bits_used;
} reader_short_t;

/* The group parser differs between instruction sets, but the surrounding logic
 * (counting the consumed bits, the end of the line, handing the position back
 * to the common reader) does not. So the surrounding logic exists once and the
 * parser is passed in: it is called once per line, which makes the indirect
 * call free. */
typedef void (*unpack_groups_fn)(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                 uint32_t safe_bytes);

uint8_t read_4_bits_align4_fast(reader_short_t* r);

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
