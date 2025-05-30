/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsDec.h>
#include <stdio.h>
#include <stdlib.h>

void decode_data(svt_jpeg_xs_decoder_api_t* dec, const uint8_t* Data, size_t Size) {
    svt_jpeg_xs_bitstream_buffer_t bitstream = {0};
    bitstream.allocation_size = Size;
    bitstream.buffer = (uint8_t*)Data;
    bitstream.used_size = Size;

    svt_jpeg_xs_image_buffer_t image_buffer = {0};
    for (uint8_t i = 0; i < MAX_COMPONENTS_NUM; i++) {
        image_buffer.stride[i] = 0;
        image_buffer.alloc_size[i] = 0;
        image_buffer.data_yuv[i] = NULL;
    }

    svt_jpeg_xs_image_config_t image_config = {0};

    SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, dec, bitstream.buffer, bitstream.used_size, &image_config);
    if (err) {
        svt_jpeg_xs_decoder_close(dec);
        return;
    }

    uint32_t pixel_size = image_config.bit_depth <= 8 ? 1 : 2;
    for (uint8_t i = 0; i < image_config.components_num; i++) {
        image_buffer.stride[i] = image_config.components[i].width;
        image_buffer.alloc_size[i] = image_buffer.stride[i] * image_config.components[i].height * pixel_size;
        image_buffer.data_yuv[i] = malloc(image_buffer.alloc_size[i]);
        if (!image_buffer.data_yuv[i]) {
            for (uint8_t j = 0; j < i; j++) {
                free(image_buffer.data_yuv[j]);
            }
            svt_jpeg_xs_decoder_close(dec);
            return;
        }
    }

    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream = bitstream;
    dec_input.image = image_buffer;
    dec_input.user_prv_ctx_ptr = NULL;
    err = svt_jpeg_xs_decoder_send_frame(dec, &dec_input, 1 /*blocking*/);
    if (err == SvtJxsErrorNone) {
        svt_jpeg_xs_frame_t dec_output;
        svt_jpeg_xs_decoder_get_frame(dec, &dec_output, 1 /*blocking*/);
    }

    for (uint8_t i = 0; i < image_config.components_num; i++) {
        if (image_buffer.data_yuv[i]) {
            free(image_buffer.data_yuv[i]);
        }
    }
    svt_jpeg_xs_decoder_close(dec);
}

int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    svt_jpeg_xs_decoder_api_t dec = {0};

    dec.verbose = VERBOSE_NONE;
    dec.threads_num = 6;
    dec.use_cpu_flags = CPU_FLAGS_ALL;
    dec.packetization_mode = 0;

    dec.proxy_mode = proxy_mode_full;
    decode_data(&dec, Data, Size);
    dec.proxy_mode = proxy_mode_half;
    decode_data(&dec, Data, Size);
    dec.proxy_mode = proxy_mode_quarter;
    decode_data(&dec, Data, Size);

    return 0;
}
