/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsDec.h>
#include <stdio.h>
#include <stdlib.h>

int32_t main(int32_t argc, char* argv[]) {
    if (argc < 2) {
        printf("Not set input file!\n");
        return -1;
    }
    const char* input_file_name = argv[1];
    FILE* input_file = NULL;
#ifdef _WIN32
    fopen_s(&input_file, input_file_name, "rb");
#else
    input_file = fopen(input_file_name, "rb");
#endif
    if (input_file == NULL) {
        printf("Can not open input file: %s!\n", input_file_name);
        return -1;
    }

    svt_jpeg_xs_decoder_api_t dec = {0};
    dec.verbose = VERBOSE_SYSTEM_INFO;
    dec.threads_num = 10;
    dec.use_cpu_flags = CPU_FLAGS_ALL;

    svt_jpeg_xs_bitstream_buffer_t bitstream;
    // 1000 bytes is enough to obtain single encoded frame size when fast_search option is enabled
    bitstream.allocation_size = 1000;
    bitstream.buffer = malloc(bitstream.allocation_size);
    if (!bitstream.buffer) {
        return SvtJxsErrorInsufficientResources;
    }
    size_t read_size = fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    printf("Read file %s size: %lu!\n", input_file_name, (unsigned long)read_size);
    //Reset the pointer to the start of the file
    fseek(input_file, 0, SEEK_SET);

    uint32_t frame_size = 0;
    SvtJxsErrorType_t err = svt_jpeg_xs_decoder_get_single_frame_size(
        (uint8_t*)bitstream.buffer, (uint32_t)read_size, NULL, &frame_size, 1);
    if (err) {
        return err;
    }
    if (frame_size == 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    //Allocate buffer with sufficient size to store single frame
    bitstream.allocation_size = frame_size;
    bitstream.buffer = realloc(bitstream.buffer, bitstream.allocation_size);
    if (!bitstream.buffer) {
        return SvtJxsErrorInsufficientResources;
    }
    read_size = fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    if (read_size != bitstream.allocation_size) {
        printf("Provided file does not have full frame\n");
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    bitstream.used_size = read_size;
    fclose(input_file);

    svt_jpeg_xs_image_config_t image_config;
    err = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.buffer, read_size, &image_config);
    if (err) {
        return err;
    }

    svt_jpeg_xs_image_buffer_t image_buffer;
    uint32_t pixel_size = image_config.bit_depth <= 8 ? 1 : 2;
    for (uint8_t i = 0; i < image_config.components_num; ++i) {
        image_buffer.stride[i] = image_config.components[i].width;
        image_buffer.alloc_size[i] = image_buffer.stride[i] * image_config.components[i].height * pixel_size;
        image_buffer.data_yuv[i] = malloc(image_buffer.alloc_size[i]);
        if (!image_buffer.data_yuv[i]) {
            return SvtJxsErrorInsufficientResources;
        }
    }

    { ////loop over multiple frames
        svt_jpeg_xs_frame_t dec_input;
        dec_input.bitstream = bitstream;
        dec_input.image = image_buffer;
        err = svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/);
        if (err) {
            return err;
        }
        svt_jpeg_xs_frame_t dec_output;
        err = svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/);
        if (err) {
            return err;
        }

        //write YUV to file
        uint32_t pixel_size = image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
        for (int32_t c = 0; c < image_config.components_num; c++) {
            uint32_t size = image_config.components[c].width * image_config.components[c].height * pixel_size;
            if (size != image_config.components[c].byte_size) {
                printf("Invalid image config structure!!!\n");
                return -1;
            }
            printf("YUV Component %u resolution: %ux%u bitdepth: %u pointer: %p data_size: %u\n",
                   c,
                   image_config.components[c].width,
                   image_config.components[c].height,
                   image_config.bit_depth,
                   dec_output.image.data_yuv[c],
                   image_config.components[c].byte_size);
            //fwrite(dec_output.image.data_yuv[c], 1, image_config.components[c].byte_size, out_file);
        }

        //Get next bitstream/frame from file
        //fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    }

    free(bitstream.buffer);
    for (uint8_t i = 0; i < image_config.components_num; ++i) {
        free(image_buffer.data_yuv[i]);
    }
    svt_jpeg_xs_decoder_close(&dec);
    return 0;
}
