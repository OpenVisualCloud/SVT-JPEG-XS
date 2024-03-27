/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsEnc.h>

int32_t main(int32_t argc, char *argv[]) {
    if (argc < 2) {
        printf("Not set input file!\n");
        return -1;
    }
    const char *input_file_name = argv[1];
    FILE *input_file = NULL;
#ifdef _WIN32
    fopen_s(&input_file, input_file_name, "rb");
#else
    input_file = fopen(input_file_name, "rb");
#endif
    if (input_file == NULL) {
        printf("Can not open input file: %s!\n", input_file_name);
        return -1;
    }

    svt_jpeg_xs_encoder_api_t enc;
    SvtJxsErrorType_t err = svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);

    if (err != SvtJxsErrorNone) {
        return err;
    }

    enc.source_width = 1920;
    enc.source_height = 1080;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc.bpp_numerator = 3;

    err = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    if (err != SvtJxsErrorNone) {
        return err;
    }

    //8bit YUV422 frame size:
    //luma:      width * height * 1 /*bytes per pixel*/
    //chroma cb: width * height * 1 /*bytes per pixel*/ * 0.5 /*chroma is half the luma size*/
    //chroma cr: width * height * 1 /*bytes per pixel*/ * 0.5 /*chroma is half the luma size*/

    uint32_t pixel_size = enc.input_bit_depth <= 8 ? 1 : 2;
    svt_jpeg_xs_image_buffer_t in_buf;
    in_buf.stride[0] = enc.source_width;
    in_buf.stride[1] = enc.source_width / 2;
    in_buf.stride[2] = enc.source_width / 2;
    for (uint8_t i = 0; i < 3; ++i) {
        in_buf.alloc_size[i] = in_buf.stride[i] * enc.source_height * pixel_size;
        in_buf.data_yuv[i] = malloc(in_buf.alloc_size[i]);
        if (!in_buf.data_yuv[i]) {
            return SvtJxsErrorInsufficientResources;
        }
    }

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    uint32_t bitstream_size = (uint32_t)(
        ((uint64_t)enc.source_width * enc.source_height * enc.bpp_numerator / enc.bpp_denominator + 7) / +8);
    out_buf.allocation_size = bitstream_size;
    out_buf.used_size = 0;
    out_buf.buffer = malloc(out_buf.allocation_size);
    if (!out_buf.buffer) {
        return SvtJxsErrorInsufficientResources;
    }

    { //loop over multiple frames
        //Get YUV 8bit from file or any other location
        size_t read_size = 0;
        read_size += fread(in_buf.data_yuv[0], 1, in_buf.alloc_size[0], input_file); //luma
        read_size += fread(in_buf.data_yuv[1], 1, in_buf.alloc_size[1], input_file); //chroma cb
        read_size += fread(in_buf.data_yuv[2], 1, in_buf.alloc_size[2], input_file); //chroma cr
        printf("Read file %s size: %lu!\n", input_file_name, (unsigned long)read_size);
        fclose(input_file);

        svt_jpeg_xs_frame_t enc_input;
        enc_input.bitstream = out_buf;
        enc_input.image = in_buf;
        enc_input.user_prv_ctx_ptr = NULL;

        err = svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/);
        if (err != SvtJxsErrorNone) {
            return err;
        }

        svt_jpeg_xs_frame_t enc_output;
        err = svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/);
        if (err != SvtJxsErrorNone) {
            return err;
        }
        //Store bitstream to file
        printf("bitstream output: pointer: %p bitstream size: %u\n", enc_output.bitstream.buffer, enc_output.bitstream.used_size);
        //fwrite(enc_output.bitstream.buffer, 1, enc_output.bitstream.used_size, out_file);
    }

    svt_jpeg_xs_encoder_close(&enc);

    free(out_buf.buffer);
    free(in_buf.data_yuv[0]);
    free(in_buf.data_yuv[1]);
    free(in_buf.data_yuv[2]);
    return 0;
}
