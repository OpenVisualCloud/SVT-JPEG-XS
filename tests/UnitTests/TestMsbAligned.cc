/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "random.h"

namespace {

/* Encodes a single frame of pseudo-random 10/12-bit content and returns the produced codestream,
 * plus the exact input pixel buffer used (so the caller can compare it against decoded output). */
std::vector<uint8_t> encode_single_frame(uint32_t width, uint32_t height, uint8_t bit_depth, uint8_t msb_aligned,
                                         svt_jpeg_xs_image_config_t *out_image_config) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    enc.bpp_numerator = 4;
    enc.bpp_denominator = 1;
    enc.input_bit_depth_msb_aligned = msb_aligned;

    uint32_t bytes_per_frame = 0;
    EXPECT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, out_image_config, &bytes_per_frame),
              SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t *in_buf = svt_jpeg_xs_image_buffer_alloc(out_image_config);
    EXPECT_NE(in_buf, nullptr);

    svt_jxs_test_tool::SVTRandom rnd(32, false);
    for (int32_t c = 0; c < out_image_config->components_num; ++c) {
        uint16_t *plane = (uint16_t *)in_buf->data_yuv[c];
        uint32_t plane_w = out_image_config->components[c].width;
        uint32_t plane_h = out_image_config->components[c].height;
        uint32_t stride = in_buf->stride[c];
        for (uint32_t y = 0; y < plane_h; y++) {
            for (uint32_t x = 0; x < plane_w; x++) {
                uint16_t pixel = (uint16_t)(rnd.Rand16() % (1 << bit_depth));
                plane[y * stride + x] = msb_aligned ? (uint16_t)(pixel << (16 - bit_depth)) : pixel;
            }
        }
    }

    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    out_buf.allocation_size = bytes_per_frame * 2 + 4096;
    out_buf.used_size = 0;
    out_buf.buffer = (uint8_t *)malloc(out_buf.allocation_size);
    EXPECT_NE(out_buf.buffer, nullptr);

    svt_jpeg_xs_frame_t enc_input;
    enc_input.bitstream = out_buf;
    enc_input.image = *in_buf;
    enc_input.user_prv_ctx_ptr = NULL;
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t enc_output;
    memset(&enc_output, 0, sizeof(enc_output));
    EXPECT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/), SvtJxsErrorNone);

    std::vector<uint8_t> result(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);

    svt_jpeg_xs_encoder_close(&enc);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
    return result;
}

/* Decodes a single frame and returns the output pixel buffer (caller must free with svt_jpeg_xs_image_buffer_free). */
svt_jpeg_xs_image_buffer_t *decode_single_frame(const std::vector<uint8_t> &bitstream, uint8_t msb_aligned) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.output_bit_depth_msb_aligned = msb_aligned;

    svt_jpeg_xs_image_config_t image_config;
    EXPECT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                       SVT_JPEGXS_API_VER_MINOR,
                                       &dec,
                                       bitstream.data(),
                                       bitstream.size(),
                                       &image_config),
              SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t *out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    EXPECT_NE(out_buf, nullptr);

    svt_jpeg_xs_frame_t dec_input;
    dec_input.user_prv_ctx_ptr = NULL;
    dec_input.image = *out_buf;
    dec_input.bitstream.buffer = (uint8_t *)bitstream.data();
    dec_input.bitstream.used_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t dec_output;
    EXPECT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_decoder_close(&dec);
    return out_buf;
}

} // namespace

/* Full round-trip: encode with input_bit_depth_msb_aligned=1, decode with output_bit_depth_msb_aligned=1,
 * verify the low (16-depth) bits of every output sample are zero (proves the pipeline stayed MSB-aligned
 * end-to-end, not just at the API boundary). Uses default Tnlt=0 (linear), the only mode this encoder
 * currently produces. */
TEST(MsbAligned, EncodeMsbDecodeMsbRoundTrip) {
    for (uint8_t bit_depth = 10; bit_depth <= 12; bit_depth += 2) {
        svt_jpeg_xs_image_config_t image_config;
        std::vector<uint8_t> bitstream = encode_single_frame(64, 64, bit_depth, /*msb_aligned=*/1, &image_config);
        ASSERT_GT(bitstream.size(), 0u);

        svt_jpeg_xs_image_buffer_t *out_buf = decode_single_frame(bitstream, /*msb_aligned=*/1);
        uint16_t low_bits_mask = (uint16_t)((1 << (16 - bit_depth)) - 1);
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            uint16_t *plane = (uint16_t *)out_buf->data_yuv[c];
            uint32_t plane_w = image_config.components[c].width;
            uint32_t plane_h = image_config.components[c].height;
            uint32_t stride = out_buf->stride[c];
            for (uint32_t y = 0; y < plane_h; y++) {
                for (uint32_t x = 0; x < plane_w; x++) {
                    ASSERT_EQ(plane[y * stride + x] & low_bits_mask, 0)
                        << "bit_depth=" << (int)bit_depth << " comp=" << c << " (" << x << "," << y << ") not MSB-aligned";
                }
            }
        }
        svt_jpeg_xs_image_buffer_free(out_buf);
    }
}

/* Cross-check: encoding the same content once MSB-aligned and once LSB-aligned must produce byte-identical
 * codestreams (the flag only changes host-side buffer interpretation, never the coded bitstream). */
TEST(MsbAligned, MsbAndLsbInputProduceIdenticalBitstream) {
    const uint32_t width = 64, height = 64;
    const uint8_t bit_depth = 10;

    svt_jpeg_xs_image_config_t image_config_lsb;
    svt_jxs_test_tool::SVTRandom rnd(32, false);

    svt_jpeg_xs_encoder_api_t enc_probe;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc_probe);
    enc_probe.source_width = width;
    enc_probe.source_height = height;
    enc_probe.input_bit_depth = bit_depth;
    enc_probe.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    enc_probe.bpp_numerator = 4;
    enc_probe.bpp_denominator = 1;
    uint32_t bytes_per_frame = 0;
    svt_jpeg_xs_encoder_get_image_config(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc_probe, &image_config_lsb, &bytes_per_frame);

    /* Build one shared "logical" pixel buffer, then derive matching LSB- and MSB-packed encoder inputs from it. */
    std::vector<std::vector<uint16_t>> logical(image_config_lsb.components_num);
    for (int32_t c = 0; c < image_config_lsb.components_num; ++c) {
        uint32_t n = image_config_lsb.components[c].width * image_config_lsb.components[c].height;
        logical[c].resize(n);
        for (uint32_t i = 0; i < n; i++) {
            logical[c][i] = (uint16_t)(rnd.Rand16() % (1 << bit_depth));
        }
    }

    auto encode_with_buffer = [&](uint8_t msb_aligned) {
        svt_jpeg_xs_encoder_api_t enc;
        svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
        enc.verbose = VERBOSE_NONE;
        enc.source_width = width;
        enc.source_height = height;
        enc.input_bit_depth = bit_depth;
        enc.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        enc.bpp_numerator = 4;
        enc.bpp_denominator = 1;
        enc.input_bit_depth_msb_aligned = msb_aligned;

        svt_jpeg_xs_image_config_t image_config;
        uint32_t bpf = 0;
        svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bpf);
        svt_jpeg_xs_image_buffer_t *in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);

        for (int32_t c = 0; c < image_config.components_num; ++c) {
            uint16_t *plane = (uint16_t *)in_buf->data_yuv[c];
            uint32_t plane_w = image_config.components[c].width;
            uint32_t plane_h = image_config.components[c].height;
            uint32_t stride = in_buf->stride[c];
            for (uint32_t y = 0; y < plane_h; y++) {
                for (uint32_t x = 0; x < plane_w; x++) {
                    uint16_t pixel = logical[c][y * plane_w + x];
                    plane[y * stride + x] = msb_aligned ? (uint16_t)(pixel << (16 - bit_depth)) : pixel;
                }
            }
        }

        svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);

        svt_jpeg_xs_bitstream_buffer_t out_buf;
        out_buf.allocation_size = bpf * 2 + 4096;
        out_buf.used_size = 0;
        out_buf.buffer = (uint8_t *)malloc(out_buf.allocation_size);

        svt_jpeg_xs_frame_t enc_input;
        enc_input.bitstream = out_buf;
        enc_input.image = *in_buf;
        enc_input.user_prv_ctx_ptr = NULL;
        svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1);

        svt_jpeg_xs_frame_t enc_output;
        memset(&enc_output, 0, sizeof(enc_output));
        svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1);

        std::vector<uint8_t> result(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);

        svt_jpeg_xs_encoder_close(&enc);
        svt_jpeg_xs_image_buffer_free(in_buf);
        free(out_buf.buffer);
        return result;
    };

    std::vector<uint8_t> bitstream_lsb = encode_with_buffer(0);
    std::vector<uint8_t> bitstream_msb = encode_with_buffer(1);

    ASSERT_EQ(bitstream_lsb.size(), bitstream_msb.size());
    ASSERT_EQ(memcmp(bitstream_lsb.data(), bitstream_msb.data(), bitstream_lsb.size()), 0);
}

/* Default (msb_aligned=0) behavior must be provably unchanged: encoding/decoding the same content with
 * the flag left at its default must match a fully-zeroed struct (i.e. not set at all). */
TEST(MsbAligned, DefaultIsZeroAndUnchanged) {
    svt_jpeg_xs_encoder_api_t enc;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    EXPECT_EQ(enc.input_bit_depth_msb_aligned, 0);

    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    EXPECT_EQ(dec.output_bit_depth_msb_aligned, 0);
}

/* Both API fields must reject any value other than 0/1. */
TEST(MsbAligned, RejectsInvalidValues) {
    svt_jpeg_xs_encoder_api_t enc;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    enc.source_width = 64;
    enc.source_height = 64;
    enc.input_bit_depth = 10;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    enc.bpp_numerator = 4;
    enc.bpp_denominator = 1;
    enc.verbose = VERBOSE_NONE;
    enc.input_bit_depth_msb_aligned = 2;
    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorBadParameter);
}
