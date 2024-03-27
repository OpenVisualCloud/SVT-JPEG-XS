/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _APP_ENCODER_CONFIG_H_
#define _APP_ENCODER_CONFIG_H_

#include <stdio.h>
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsImageBufferTools.h"

#define MAX_NUM_TOKENS 210

typedef struct EncoderConfig {
    /****************************************
     * File I/O
     ****************************************/
    char in_filename[1024];
    char out_filename[1024];

    FILE *in_file;
    FILE *out_file;

    uint8_t progress; // 0 = no progress output, 1 = normal, verbose progress
    uint32_t frames_count;
    uint32_t limit_fps;

    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_image_config_t image_config;

    svt_jpeg_xs_frame_pool_t *frame_pool;
} EncoderConfig_t;

extern SvtJxsErrorType_t read_command_line(int32_t argc, char *const argv[], EncoderConfig_t *configs);
SvtJxsErrorType_t verify_settings(EncoderConfig_t *config);
extern uint32_t get_help(int32_t argc, char *const argv[]);
extern uint32_t show_help();
#endif /*_APP_ENCODER_CONFIG_H_*/
