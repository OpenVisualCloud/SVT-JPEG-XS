/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Packet header size selection (ISO/IEC 21122-1 Table C.3 and NOTE 2).
 * Decoders read short packet headers only when Wf*Nc < 32752 and Lh=0. Short headers hold
 * Ldat < 2^15, Lcnt < 2^13 and Lsgn < 2^11 bytes. With NL,y = 0 the first packet carries a full line
 * of every component, so the encoder forces long headers with Lh=1 once Wf*Nc >= 16376. For
 * Wf*Nc >= 32752 long headers are implied, and Lh must stay 0 (every profile requires Lh=0). */

#include <cstring>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "Decoder.h"

namespace {

struct HeaderCase {
    uint32_t width;
    ColourFormat_t format;
    uint32_t ndecomp_v;
    uint8_t lossless;
    uint32_t bpp;           /* lossy only */
    uint8_t signs_handling; /* lossy only, Fs=1 when non-zero */
    uint8_t expected_lh;
};

void PrintTo(const HeaderCase& c, std::ostream* os) {
    *os << "{width " << c.width << ", format " << (int)c.format << ", ndecomp_v " << c.ndecomp_v << ", lossless "
        << (int)c.lossless << ", bpp " << c.bpp << ", signs " << (int)c.signs_handling << "}";
}

/* Deterministic noise: high-magnitude, high-frequency coefficients produce the largest packets. */
void fill_noisy(uint8_t* plane, uint32_t width, uint32_t height, uint32_t stride, uint32_t seed) {
    uint32_t state = 0x9E3779B9u ^ (seed * 0x85EBCA6Bu);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            state = state * 1664525u + 1013904223u;
            plane[y * stride + x] = (uint8_t)(state >> 24);
        }
    }
}

class PacketHeaderSize : public ::testing::TestWithParam<HeaderCase> {};

TEST_P(PacketHeaderSize, LhAndRoundTrip) {
    const HeaderCase param = GetParam();
    const uint32_t height = 16;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = param.width;
    enc.source_height = height;
    enc.input_bit_depth = 8;
    enc.colour_format = param.format;
    enc.ndecomp_v = param.ndecomp_v;
    enc.coding_raw_disable = 1;
    enc.lossless_enable = param.lossless;
    enc.bpp_numerator = param.bpp;
    enc.bpp_denominator = 1;
    enc.coding_signs_handling = param.signs_handling;

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
              SvtJxsErrorNone);
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        fill_noisy((uint8_t*)in_buf->data_yuv[c],
                   image_config.components[c].width,
                   image_config.components[c].height,
                   in_buf->stride[c],
                   (uint32_t)c);
    }

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    out_buf.allocation_size = bytes_per_frame * 2 + 4096;
    out_buf.used_size = 0;
    out_buf.buffer = (uint8_t*)malloc(out_buf.allocation_size);
    ASSERT_NE(out_buf.buffer, nullptr);

    svt_jpeg_xs_frame_t enc_input;
    enc_input.bitstream = out_buf;
    enc_input.image = *in_buf;
    enc_input.user_prv_ctx_ptr = NULL;
    ASSERT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t enc_output;
    memset(&enc_output, 0, sizeof(enc_output));
    ASSERT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/), SvtJxsErrorNone);
    std::vector<uint8_t> bitstream(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);
    svt_jpeg_xs_encoder_close(&enc);
    ASSERT_GT(bitstream.size(), 0u);

    picture_header_const_t phc;
    picture_header_dynamic_t phd;
    memset(&phc, 0, sizeof(phc));
    memset(&phd, 0, sizeof(phd));
    ASSERT_EQ(svt_jpeg_xs_decoder_probe(bitstream.data(), bitstream.size(), &phc, &phd, VERBOSE_NONE), SvtJxsErrorNone);
    EXPECT_EQ(phd.hdr_Lh, param.expected_lh);

    /* The stream must decode, and lossless streams bit-exactly: an overflowing short header size
     * field makes the decoder fail. */
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    svt_jpeg_xs_image_config_t dec_image_config;
    ASSERT_EQ(
        svt_jpeg_xs_decoder_init(
            SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), &dec_image_config),
        SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* out_img = svt_jpeg_xs_image_buffer_alloc(&dec_image_config);
    ASSERT_NE(out_img, nullptr);
    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream.buffer = bitstream.data();
    dec_input.bitstream.allocation_size = (uint32_t)bitstream.size();
    dec_input.bitstream.used_size = (uint32_t)bitstream.size();
    dec_input.image = *out_img;
    ASSERT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t dec_output;
    ASSERT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/), SvtJxsErrorNone);

    if (param.lossless) {
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            for (uint32_t y = 0; y < image_config.components[c].height; ++y) {
                ASSERT_EQ(memcmp((uint8_t*)in_buf->data_yuv[c] + (size_t)y * in_buf->stride[c],
                                 (uint8_t*)dec_output.image.data_yuv[c] + (size_t)y * dec_output.image.stride[c],
                                 image_config.components[c].width),
                          0)
                    << "component " << c << " row " << y;
            }
        }
    }

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

/* All cases use 8-bit input, Rl=0 and 16-row slices. Wf*Nc: 4093*4 = 16372 < 16376 <= 4094*4 = 16376; 5458*3 = 16374 < 16376 < 5459*3 = 16377;
 * 10916*3 = 32748 < 32752 <= 10918*3 = 32754; 8192*4 = 32768. */
INSTANTIATE_TEST_SUITE_P(WidthBoundaries, PacketHeaderSize,
                         ::testing::Values(
                             /* NL,y = 0: long headers forced with Lh=1 from Wf*Nc >= 16376 up to 32752. */
                             HeaderCase{4093, COLOUR_FORMAT_PLANAR_4_COMPONENTS, 0, 1, 0, 0, 0},
                             HeaderCase{4094, COLOUR_FORMAT_PLANAR_4_COMPONENTS, 0, 1, 0, 0, 1},
                             HeaderCase{5458, COLOUR_FORMAT_PLANAR_YUV444, 0, 1, 0, 0, 0},
                             HeaderCase{5459, COLOUR_FORMAT_PLANAR_YUV444, 0, 1, 0, 0, 1},
                             HeaderCase{10916, COLOUR_FORMAT_PLANAR_YUV444, 0, 1, 0, 0, 1},
                             HeaderCase{5459, COLOUR_FORMAT_PLANAR_YUV444, 0, 0, 20, 1, 1},
                             HeaderCase{10916, COLOUR_FORMAT_PLANAR_YUV444, 0, 0, 8, 2, 1},
                             /* Wf*Nc >= 32752: long headers are implied, so Lh=0. */
                             HeaderCase{10918, COLOUR_FORMAT_PLANAR_YUV444, 0, 1, 0, 0, 0},
                             HeaderCase{8192, COLOUR_FORMAT_PLANAR_4_COMPONENTS, 0, 1, 0, 0, 0},
                             HeaderCase{8192, COLOUR_FORMAT_PLANAR_4_COMPONENTS, 1, 0, 6, 0, 0},
                             HeaderCase{10918, COLOUR_FORMAT_PLANAR_YUV444, 2, 0, 6, 0, 0},
                             /* NL,y >= 1 below 32752 keeps short headers. */
                             HeaderCase{10916, COLOUR_FORMAT_PLANAR_YUV444, 1, 1, 0, 0, 0},
                             HeaderCase{10916, COLOUR_FORMAT_PLANAR_YUV444, 2, 1, 0, 0, 0}));

} // namespace
