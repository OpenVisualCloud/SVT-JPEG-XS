/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsEnc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct config_short {
    uint16_t source_width;
    uint16_t source_height;
    uint8_t input_bit_depth;
    ColourFormat_t colour_format;
    uint32_t bpp_numerator;
    uint32_t bpp_denominator;
    uint8_t ndecomp_v;
    uint8_t ndecomp_h;
    uint8_t quantization;
    uint32_t slice_height;
    CPU_FLAGS use_cpu_flags;
    uint16_t threads_num;
    uint8_t cpu_profile;
    uint8_t print_bands_info;
    uint8_t coding_signs_handling;
    uint8_t coding_significance;
    uint8_t rate_control_mode;
    uint8_t coding_vertical_prediction_mode;
    uint8_t slice_packetization_mode;
    uint8_t verbose;
} config_short_t;

void copy_fuzzer_params(config_short_t *fuzzer, svt_jpeg_xs_encoder_api_t *encoder) {
    encoder->source_width = fuzzer->source_width;
    encoder->source_height = fuzzer->source_height;
    encoder->input_bit_depth = fuzzer->input_bit_depth;
    encoder->colour_format = fuzzer->colour_format;
    encoder->bpp_numerator = fuzzer->bpp_numerator;
    encoder->bpp_denominator = fuzzer->bpp_denominator;
    encoder->ndecomp_v = fuzzer->ndecomp_v;
    encoder->ndecomp_h = fuzzer->ndecomp_h;
    encoder->quantization = fuzzer->quantization;
    encoder->slice_height = fuzzer->slice_height;
    encoder->use_cpu_flags = fuzzer->use_cpu_flags;
    encoder->threads_num = fuzzer->threads_num;
    encoder->cpu_profile = fuzzer->cpu_profile;
    encoder->print_bands_info = fuzzer->print_bands_info;
    encoder->coding_signs_handling = fuzzer->coding_signs_handling;
    encoder->coding_significance = fuzzer->coding_significance;
    encoder->rate_control_mode = fuzzer->rate_control_mode;
    encoder->coding_vertical_prediction_mode = fuzzer->coding_vertical_prediction_mode;
    encoder->slice_packetization_mode = fuzzer->slice_packetization_mode;
    encoder->verbose = fuzzer->verbose;
}

//We can set 2nd parameter to fixed size of 56 (which mean sizeof(config_short_t))
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    svt_jpeg_xs_encoder_api_t encoder;
    memset(&encoder, 0, sizeof(svt_jpeg_xs_encoder_api_t));

    config_short_t config_fuzzer;
    memset(&config_fuzzer, 0, sizeof(config_short_t));

    size_t copy_size = Size > sizeof(config_short_t) ? sizeof(config_short_t) : Size;
    memcpy(&config_fuzzer, Data, copy_size);

    copy_fuzzer_params(&config_fuzzer, &encoder);
    //Enforce verbose to minimum to not to flood the console/logs
    encoder.verbose = VERBOSE_NONE;
    encoder.threads_num = encoder.threads_num > 64 ? 64: encoder.threads_num;
    encoder.source_width = encoder.source_width > 8000 ? 8000 : encoder.source_width;
    encoder.source_height = encoder.source_height > 8000 ? 8000 : encoder.source_height;

    if (((double)encoder.bpp_numerator / encoder.bpp_denominator) < 0.1 ||
        ((double)encoder.bpp_numerator / encoder.bpp_denominator) > 16 * 3) {
        return 0;
    }

    if (encoder.source_width * encoder.source_height > 7680 * 4320) {
        return 0;
    }

    SvtJxsErrorType_t err = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    if (err != SvtJxsErrorNone) {
        svt_jpeg_xs_encoder_close(&encoder);
        return 0;
    }

    //allocate input buffers
    uint32_t pixel_size = encoder.input_bit_depth <= 8 ? 1 : 2;
    svt_jpeg_xs_image_buffer_t in_buf;

    if (encoder.colour_format == COLOUR_FORMAT_PLANAR_YUV420) {
        in_buf.stride[0] = encoder.source_width;
        in_buf.stride[1] = encoder.source_width / 2;
        in_buf.stride[2] = encoder.source_width / 2;
        in_buf.alloc_size[0] = in_buf.stride[0] * encoder.source_height * pixel_size;
        in_buf.alloc_size[1] = in_buf.stride[1] * encoder.source_height / 2 * pixel_size;
        in_buf.alloc_size[2] = in_buf.stride[2] * encoder.source_height / 2 * pixel_size;
    }
    else if (encoder.colour_format == COLOUR_FORMAT_PLANAR_YUV422) {
        in_buf.stride[0] = encoder.source_width;
        in_buf.stride[1] = encoder.source_width / 2;
        in_buf.stride[2] = encoder.source_width / 2;
        in_buf.alloc_size[0] = in_buf.stride[0] * encoder.source_height * pixel_size;
        in_buf.alloc_size[1] = in_buf.stride[1] * encoder.source_height * pixel_size;
        in_buf.alloc_size[2] = in_buf.stride[2] * encoder.source_height * pixel_size;
    }
    else if (encoder.colour_format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB) {
        in_buf.stride[0] = encoder.source_width;
        in_buf.stride[1] = encoder.source_width;
        in_buf.stride[2] = encoder.source_width;
        in_buf.alloc_size[0] = in_buf.stride[0] * encoder.source_height * pixel_size;
        in_buf.alloc_size[1] = in_buf.stride[1] * encoder.source_height * pixel_size;
        in_buf.alloc_size[2] = in_buf.stride[2] * encoder.source_height * pixel_size;
    }
    else if (encoder.colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB){
        in_buf.stride[0] = encoder.source_width * 3;
        in_buf.stride[1] = 0;
        in_buf.stride[2] = 0;
        in_buf.alloc_size[0] = in_buf.stride[0] * encoder.source_height * pixel_size;
        in_buf.alloc_size[1] = 0;
        in_buf.alloc_size[2] = 0;
    }
    else{
        svt_jpeg_xs_encoder_close(&encoder);
        return 0;
    }

    if (encoder.colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB){
        //Buffer is intentionally allocated without clearing it
        in_buf.data_yuv[0] = malloc(in_buf.alloc_size[0]);
        in_buf.data_yuv[1] = NULL;
        in_buf.data_yuv[2] = NULL;
    }
    else{
        for (uint8_t i = 0; i < 3; ++i) {
            //Buffer is intentionally allocated without clearing it
            in_buf.data_yuv[i] = malloc(in_buf.alloc_size[i]);
            if (!in_buf.data_yuv[i]) {
                for (uint8_t j = 0; j < i; ++j) {
                    free(in_buf.data_yuv[j]);
                }
                svt_jpeg_xs_encoder_close(&encoder);
                return 0;
            }
        }
    }

    //allocate output buffers
    svt_jpeg_xs_bitstream_buffer_t out_buf;
    uint32_t bitstream_size =(uint32_t)(((uint64_t)encoder.source_width * encoder.source_height * encoder.bpp_numerator / encoder.bpp_denominator + 7) / 8);
    out_buf.allocation_size = bitstream_size;
    out_buf.used_size = 0;
    out_buf.buffer = malloc(out_buf.allocation_size);
    if (!out_buf.buffer) {
        goto fail;
    }

    svt_jpeg_xs_frame_t enc_input;
    enc_input.bitstream = out_buf;
    enc_input.image = in_buf;
    enc_input.user_prv_ctx_ptr = NULL;

    err = svt_jpeg_xs_encoder_send_picture(&encoder, &enc_input, 1 /*blocking*/);
    if (err != SvtJxsErrorNone) {
        goto fail;
    }

    svt_jpeg_xs_frame_t enc_output;
    do {
        err |= svt_jpeg_xs_encoder_get_packet(&encoder, &enc_output, 1 /*blocking*/);
    } while (enc_output.bitstream.last_packet_in_frame == 0);

fail:
    for (uint8_t i = 0; i < 3; ++i) {
        free(in_buf.data_yuv[i]);
    }
    free(out_buf.buffer);

     svt_jpeg_xs_encoder_close(&encoder);
    return 0; // Values other than 0 and -1 are reserved for future use.
}
