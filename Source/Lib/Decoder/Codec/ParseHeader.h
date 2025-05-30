/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PARSE_CODESTREAM_HEADER_H_
#define __PARSE_CODESTREAM_HEADER_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DecHandle.h"
#include "BitstreamReader.h"
#include "SvtJpegxsDec.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct packet_header {
    uint8_t raw_mode_flag; // 1 bit
    uint32_t data_len;     // max 20 bits
    uint32_t gcli_len;     // max 20 bits
    uint16_t sign_len;     // max 15bits
} packet_header_t;

SvtJxsErrorType_t get_header(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                             picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose);
SvtJxsErrorType_t get_tail(bitstream_reader_t* bitstream);
SvtJxsErrorType_t get_slice_header(bitstream_reader_t* bitstream, uint16_t* slice_idx);
void get_packet_header(bitstream_reader_t* bitstream, int32_t use_long_header, packet_header_t* out_pkt);

SvtJxsErrorType_t static_get_single_frame_size(const uint8_t* bitstream_buf, size_t bitstream_buf_size,
                                               svt_jpeg_xs_image_config_t* out_image_config, uint32_t* frame_size,
                                               uint32_t fast_search, proxy_mode_t proxy_mode);

#ifdef __cplusplus
}
#endif
#endif /* __PARSE_CODESTREAM_HEADER_H_ */
