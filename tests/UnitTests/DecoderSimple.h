/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_SIMPLE_H_
#define _DECODER_SIMPLE_H_
#include "stdint.h"
#include "DecHandle.h"

typedef struct DecoderSimple {
    uint32_t verbose;
    svt_jpeg_xs_decoder_common_t dec_common; /*Common decoder*/
    svt_jpeg_xs_decoder_instance_t* instance_ctx;
    svt_jpeg_xs_image_config image_config;
    svt_jpeg_xs_image_buffer_t* image_out;
    svt_jpeg_xs_decoder_thread_context* dec_thread_context;
} DecoderSimple_t;

void decoder_simple_init_rtcd(CPU_FLAGS use_cpu_flags);
SvtJxsErrorType_t decoder_simple_alloc(DecoderSimple_t* decoder, const uint8_t* bitstream_buf, size_t bitstream_length);
void decoder_simple_free(DecoderSimple_t* decoder);
SvtJxsErrorType_t decoder_simple_get_frame(DecoderSimple_t* decoder, const uint8_t* bitstream_buf, size_t bitstream_buf_size);

/*Tools*/
int32_t find_bitstream_header(uint8_t* bitstream_buf_ref, size_t bitstream_length);

#endif /*_DECODER_SIMPLE_H_*/
