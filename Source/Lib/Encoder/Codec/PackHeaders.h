/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_HEADERS_ENCODER_H__
#define __PACK_HEADERS_ENCODER_H__
#include "BitstreamWriter.h"
#include "Pi.h"
#include "Encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SequenceControlSet;

void write_header(bitstream_writer_t* bitstream, svt_jpeg_xs_encoder_common_t* enc_common);
void write_capabilities_marker(bitstream_writer_t* bitstream, svt_jpeg_xs_encoder_common_t* enc_common);
void write_picture_header(bitstream_writer_t* bitstream, pi_t* pi, svt_jpeg_xs_encoder_common_t* enc_common);
void write_weight_table(bitstream_writer_t* bitstream, pi_t* pi);
void write_component_table(bitstream_writer_t* bitstream, pi_t* pi, uint8_t bit_depth);
void write_slice_header(bitstream_writer_t* bitstream, int slice_idx);
uint32_t write_packet_header(bitstream_writer_t* bitstream, uint32_t long_hdr, uint8_t raw_coding, uint64_t data_size_bytes,
                             uint64_t bitplane_count_size_bytes, uint64_t sign_size_bytes);
void write_tail(bitstream_writer_t* bitstream);

#ifdef __cplusplus
}
#endif

#endif /*__PACK_HEADERS_ENCODER_H__*/
