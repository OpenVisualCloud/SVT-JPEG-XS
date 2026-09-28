/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsEnc.h>
#include <SvtJpegxsDec.h>
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
    uint8_t coding_raw_disable;
    uint8_t cap_compat;
    uint16_t profile_ppih_override;
    uint16_t level_plev_override;
    uint8_t input_bit_depth_msb_aligned;
    uint8_t enable_color_transform;
    uint8_t lossless_enable;
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
    encoder->coding_raw_disable = fuzzer->coding_raw_disable;
    encoder->cap_compat = fuzzer->cap_compat;
    encoder->profile_ppih_override = fuzzer->profile_ppih_override;
    encoder->level_plev_override = fuzzer->level_plev_override;
    encoder->input_bit_depth_msb_aligned = fuzzer->input_bit_depth_msb_aligned;
    encoder->enable_color_transform = fuzzer->enable_color_transform;
    encoder->lossless_enable = fuzzer->lossless_enable;
}

//We can set 2nd parameter to fixed size of 64 (which mean sizeof(config_short_t))
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
    encoder.threads_num = encoder.threads_num > 64 ? 64 : encoder.threads_num;
    encoder.source_width = encoder.source_width > 8000 ? 8000 : encoder.source_width;
    encoder.source_height = encoder.source_height > 8000 ? 8000 : encoder.source_height;

    //bpp is ignored in lossless mode, so do not reject configurations based on it
    if (!encoder.lossless_enable &&
        (((double)encoder.bpp_numerator / encoder.bpp_denominator) < 0.1 ||
        ((double)encoder.bpp_numerator / encoder.bpp_denominator) > 16 * 3)) {
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
    uint8_t num_components = 3; //overridden to 4 for the two 4-component formats below
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
    else if (encoder.colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        in_buf.stride[0] = encoder.source_width * 3;
        in_buf.stride[1] = 0;
        in_buf.stride[2] = 0;
        in_buf.alloc_size[0] = in_buf.stride[0] * encoder.source_height * pixel_size;
        in_buf.alloc_size[1] = 0;
        in_buf.alloc_size[2] = 0;
    }
    else if (encoder.colour_format == COLOUR_FORMAT_PLANAR_4_COMPONENTS) {
        //4:4:4:4 - RGBA/GBRA/YUVA444, all 4 components full resolution
        num_components = 4;
        for (uint8_t i = 0; i < num_components; ++i) {
            in_buf.stride[i] = encoder.source_width;
            in_buf.alloc_size[i] = in_buf.stride[i] * encoder.source_height * pixel_size;
        }
    }
    else if (encoder.colour_format == COLOUR_FORMAT_PLANAR_YUV422_ALPHA) {
        //4:2:2:4 - Y/Cb/Cr at 4:2:2 (comp 1,2 half horizontal res), alpha (comp 3) full resolution
        num_components = 4;
        in_buf.stride[0] = encoder.source_width;
        in_buf.stride[1] = encoder.source_width / 2;
        in_buf.stride[2] = encoder.source_width / 2;
        in_buf.stride[3] = encoder.source_width;
        for (uint8_t i = 0; i < num_components; ++i) {
            in_buf.alloc_size[i] = in_buf.stride[i] * encoder.source_height * pixel_size;
        }
    }
    else {
        svt_jpeg_xs_encoder_close(&encoder);
        return 0;
    }

    if (encoder.colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        //Buffer is intentionally allocated without clearing it
        in_buf.data_yuv[0] = malloc(in_buf.alloc_size[0]);
        in_buf.data_yuv[1] = NULL;
        in_buf.data_yuv[2] = NULL;
    }
    else {
        for (uint8_t i = 0; i < num_components; ++i) {
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
    //Size reported by encoder: bpp based in lossy mode, worst case in lossless mode
    svt_jpeg_xs_bitstream_buffer_t out_buf;
    out_buf.buffer = NULL;
    svt_jpeg_xs_image_config_t image_config;
    uint32_t bitstream_size = 0;
    err = svt_jpeg_xs_encoder_get_image_config(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder, &image_config, &bitstream_size);
    if (err != SvtJxsErrorNone || bitstream_size == 0) {
        goto fail;
    }
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

    //Lossless frames are compacted to their real size and the picture header Lcod is patched
    //after encoding. Both must stay consistent: used_size within the buffer, and Lcod == used_size.
    if (encoder.lossless_enable && err == SvtJxsErrorNone) {
        if (enc_output.bitstream.used_size > enc_output.bitstream.allocation_size) {
            fprintf(stderr,
                    "Lossless used_size %u exceeds allocation_size %u\n",
                    enc_output.bitstream.used_size,
                    enc_output.bitstream.allocation_size);
            abort();
        }
        uint32_t frame_size = 0;
        SvtJxsErrorType_t size_err = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(enc_output.bitstream.buffer,
                                                                                         enc_output.bitstream.used_size,
                                                                                         NULL,
                                                                                         &frame_size,
                                                                                         1 /*fast_search: trust Lcod*/,
                                                                                         proxy_mode_full);
        if (size_err != SvtJxsErrorNone || frame_size != enc_output.bitstream.used_size) {
            fprintf(stderr,
                    "Lossless Lcod mismatch: err %d, frame_size %u, used_size %u\n",
                    size_err,
                    frame_size,
                    enc_output.bitstream.used_size);
            abort();
        }
    }

fail:
    for (uint8_t i = 0; i < num_components; ++i) {
        free(in_buf.data_yuv[i]);
    }
    free(out_buf.buffer);

    svt_jpeg_xs_encoder_close(&encoder);
    return 0; // Values other than 0 and -1 are reserved for future use.
}
