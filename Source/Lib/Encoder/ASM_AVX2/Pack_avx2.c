/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_avx2.h"
#include "pack_group_helper.h"

void pack_data_single_group_avx2(bitstream_writer_t* bitstream, uint16_t* buf, uint8_t gcli, uint8_t gtli) {
    pack_data_single_group_sse(bitstream, buf, gcli, gtli);
}

void pack_data_groups_avx2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups, uint8_t gtli,
                           uint8_t sign_flag) {
    pack_data_groups_sse(bitstream, buf_16bit, gclis, groups, gtli, sign_flag);
}

void pack_data_groups_avx2_bmi2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups,
                                uint8_t gtli, uint8_t sign_flag) {
    nib_writer_t w;
    nibw_init(&w, bitstream);
    if (sign_flag == 0) {
        pack_groups_masked_pdep(&w, buf_16bit, gclis, groups, gtli, 1);
    }
    else {
        pack_groups_masked_pdep(&w, buf_16bit, gclis, groups, gtli, 0);
    }
    nibw_finish(&w, bitstream);
}
