/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_AVX2_H__
#define __PACK_AVX2_H__

#include "Definitions.h"
#include "BitstreamWriter.h"

#ifdef __cplusplus
extern "C" {
#endif

void pack_data_groups_avx2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups, uint8_t gtli,
                           uint8_t sign_flag);

/* Same as pack_data_groups_avx2 but packs a group's planes with PDEP instead of
 * a per-plane loop. Needs BMI2 on top of AVX2 - callers must check
 * CPU_FLAGS_BMI2 on the host before selecting this, since PDEP is microcoded
 * and slow on some older CPUs even though it is a single fast instruction on
 * most current ones. */
void pack_data_groups_avx2_bmi2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups,
                                uint8_t gtli, uint8_t sign_flag);

void pack_data_single_group_avx2(bitstream_writer_t* bitstream, uint16_t* buf, uint8_t gcli, uint8_t gtli);

#ifdef __cplusplus
}
#endif

#endif /*__PACK_AVX2_H__*/
