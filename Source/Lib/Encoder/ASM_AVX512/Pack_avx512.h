/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_AVX512_H__
#define __PACK_AVX512_H__
#include "BitstreamWriter.h"
#include "Codestream.h"

#ifdef __cplusplus
extern "C" {
#endif

void pack_data_single_group_avx512(bitstream_writer_t *bitstream, uint16_t *buf, uint8_t gcli, uint8_t gtli);
uint32_t rate_control_calc_vpred_cost_nosigf_avx512(uint32_t gcli_width, uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr,
                                                    uint8_t *vpred_bits_pack, uint8_t gtli, uint8_t gtli_max);
void rate_control_calc_vpred_cost_sigf_nosigf_avx512(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                     uint32_t significance_group_size, uint8_t *gcli_data_top_ptr,
                                                     uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack,
                                                     uint8_t *vpred_significance, uint8_t gtli, uint8_t gtli_max,
                                                     uint32_t *pack_size_gcli_sigf_reduction, uint32_t *pack_size_gcli_no_sigf);
#ifdef __cplusplus
}
#endif

#endif /*__PACK_AVX512_H__*/
