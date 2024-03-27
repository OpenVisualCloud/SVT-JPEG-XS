/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// Command line argument parsing

#ifndef _DECODER_PARAMETERS_PARSE_H_
#define _DECODER_PARAMETERS_PARSE_H_

/***************************************
 * Includes
 ***************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"

#define UNUSED(x) (void)(x)

typedef struct DecoderConfig {
    char in_filename[1024];
    char out_filename[1024];

    FILE* in_file;
    FILE* out_file;

    int autodetect_bitstream_header;
    uint8_t* bitstream_buf_ref;
    size_t bitstream_buf_size;
    size_t bitstream_offset;

    uint32_t frames_count;
    uint32_t force_decode;
    uint32_t limit_fps;

    svt_jpeg_xs_decoder_api_t decoder;
    svt_jpeg_xs_image_config_t image_config;

    svt_jpeg_xs_frame_pool_t* frame_pool;
} DecoderConfig_t;

SvtJxsErrorType_t read_command_line(int32_t argc, char* const argv[], DecoderConfig_t* configs);
extern uint32_t get_help(int32_t argc, char* const argv[]);
void show_help();

#endif /*_DECODER_PARAMETERS_PARSE_H_*/
