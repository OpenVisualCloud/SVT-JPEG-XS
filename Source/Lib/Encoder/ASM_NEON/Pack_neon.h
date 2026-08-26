/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_NEON_H__
#define __PACK_NEON_H__

#include "Definitions.h"
#include "BitstreamWriter.h"

#ifdef __cplusplus
extern "C" {
#endif

void pack_data_groups_neon(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups, uint8_t gtli,
                           uint8_t sign_flag);

#ifdef __cplusplus
}
#endif

#endif /*__PACK_NEON_H__*/
