/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_avx2.h"
#include "pack_group_helper.h"

void pack_data_single_group_avx2(bitstream_writer_t* bitstream, uint16_t* buf, uint8_t gcli, uint8_t gtli) {
    pack_data_single_group_sse(bitstream, buf, gcli, gtli);
}
