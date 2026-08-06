/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Tests for 4-component (alpha channel) format support added for SDBQ-3776 / GitHub issue #34:
 * COLOUR_FORMAT_PLANAR_4_COMPONENTS (4:4:4:4, e.g. RGBA/YUVA444) and
 * COLOUR_FORMAT_PLANAR_YUV422_ALPHA (4:2:2:4, YUV422 + full-resolution alpha). */

#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "Decoder.h"

namespace {

/* Encodes a single, all-zero frame with the given configuration and returns the produced
 * codestream. Returns an empty vector if encoder_init itself is expected to fail (caller checks
 * via encoder_init_result out-param instead of relying on a non-empty result). */
std::vector<uint8_t> encode_single_frame(uint32_t width, uint32_t height, uint8_t bit_depth, ColourFormat_t colour_format,
                                         uint32_t decomp_h, uint32_t decomp_v, uint32_t bpp_numerator,
                                         SvtJxsErrorType_t *encoder_init_result = nullptr) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = colour_format;
    enc.ndecomp_h = decomp_h;
    enc.ndecomp_v = decomp_v;
    enc.bpp_numerator = bpp_numerator;
    enc.bpp_denominator = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    if (encoder_init_result) {
        *encoder_init_result = ret;
    }
    if (ret != SvtJxsErrorNone) {
        return {};
    }

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    EXPECT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
              SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t *in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    EXPECT_NE(in_buf, nullptr);
    if (!in_buf) {
        svt_jpeg_xs_encoder_close(&enc);
        return {};
    }
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        EXPECT_NE(in_buf->data_yuv[c], nullptr);
        if (!in_buf->data_yuv[c]) {
            svt_jpeg_xs_encoder_close(&enc);
            svt_jpeg_xs_image_buffer_free(in_buf);
            return {};
        }
        memset(in_buf->data_yuv[c], 0, in_buf->alloc_size[c]);
    }

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    out_buf.allocation_size = bytes_per_frame * 2 + 4096;
    out_buf.used_size = 0;
    out_buf.buffer = (uint8_t *)malloc(out_buf.allocation_size);
    EXPECT_NE(out_buf.buffer, nullptr);
    if (!out_buf.buffer) {
        svt_jpeg_xs_encoder_close(&enc);
        svt_jpeg_xs_image_buffer_free(in_buf);
        return {};
    }

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

} // namespace

/*
 * Encoder accepts both new 4-component formats across the full decomposition range.
 */
class FourComponentEncodeParam : public ::testing::TestWithParam<std::tuple<ColourFormat_t, uint32_t, uint32_t>> {};

TEST_P(FourComponentEncodeParam, EncodesSuccessfully) {
    ColourFormat_t format = std::get<0>(GetParam());
    uint32_t decomp_h = std::get<1>(GetParam());
    uint32_t decomp_v = std::get<2>(GetParam());
    if (decomp_v > decomp_h) {
        GTEST_SKIP() << "vertical decomposition must not exceed horizontal (pre-existing, unrelated rule)";
    }
    SvtJxsErrorType_t init_result = SvtJxsErrorNone;
    std::vector<uint8_t> bitstream = encode_single_frame(64, 64, 8, format, decomp_h, decomp_v, 4, &init_result);
    EXPECT_EQ(init_result, SvtJxsErrorNone) << "decomp_h=" << decomp_h << " decomp_v=" << decomp_v;
    EXPECT_GT(bitstream.size(), 0u);
}

INSTANTIATE_TEST_SUITE_P(Yuv444_4444, FourComponentEncodeParam,
                         ::testing::Combine(::testing::Values(COLOUR_FORMAT_PLANAR_4_COMPONENTS), ::testing::Range(1u, 6u),
                                            ::testing::Range(0u, 3u)));

INSTANTIATE_TEST_SUITE_P(Yuv422Alpha, FourComponentEncodeParam,
                         ::testing::Combine(::testing::Values(COLOUR_FORMAT_PLANAR_YUV422_ALPHA), ::testing::Range(1u, 6u),
                                            ::testing::Range(0u, 3u)));

/*
 * Reject cases: EncHandle.c validation parity for the new 4:2:2:4 format.
 */
TEST(FourComponentReject, Yuv422AlphaOddWidthRejected) {
    SvtJxsErrorType_t init_result = SvtJxsErrorNone;
    encode_single_frame(65, 64, 8, COLOUR_FORMAT_PLANAR_YUV422_ALPHA, 2, 1, 4, &init_result);
    EXPECT_EQ(init_result, SvtJxsErrorBadParameter);
}

TEST(FourComponentReject, Yuv444EvenOrOddWidthAccepted) {
    // 4:4:4:4 has no chroma subsampling - odd width must NOT be rejected (unlike 4:2:2:4).
    SvtJxsErrorType_t init_result = SvtJxsErrorNone;
    encode_single_frame(65, 64, 8, COLOUR_FORMAT_PLANAR_4_COMPONENTS, 2, 1, 4, &init_result);
    EXPECT_EQ(init_result, SvtJxsErrorNone);
}

/*
 * Decoder-side format detection: svt_jpeg_xs_get_format_from_params() comps_num==4 branch.
 */
TEST(FourComponentFormatDetection, Yuv444FourComponents) {
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 1, 1, 1};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1, 1};
    EXPECT_EQ(svt_jpeg_xs_get_format_from_params(4, sx, sy), COLOUR_FORMAT_PLANAR_4_COMPONENTS);
}

TEST(FourComponentFormatDetection, Yuv422Alpha) {
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 2, 2, 1};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1, 1};
    EXPECT_EQ(svt_jpeg_xs_get_format_from_params(4, sx, sy), COLOUR_FORMAT_PLANAR_YUV422_ALPHA);
}

TEST(FourComponentFormatDetection, UnrecognizedSamplingShapeIsInvalid) {
    // Not a shape produced by this encoder or a known convention (e.g. not 4:4:4:4 nor 4:2:2:4).
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 2, 1, 2};
    uint32_t sy[MAX_COMPONENTS_NUM] = {2, 1, 2, 1};
    EXPECT_EQ(svt_jpeg_xs_get_format_from_params(4, sx, sy), COLOUR_FORMAT_INVALID);
}

/*
 * Full encode-decode round trip for both new formats.
 */
class FourComponentRoundTripParam : public ::testing::TestWithParam<ColourFormat_t> {};

TEST_P(FourComponentRoundTripParam, DecodesSuccessfullyAndReportsCorrectFormat) {
    ColourFormat_t format = GetParam();
    std::vector<uint8_t> bitstream = encode_single_frame(64, 64, 8, format, 2, 1, 4);
    ASSERT_GT(bitstream.size(), 0u);

    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    svt_jpeg_xs_image_config_t image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), &image_config),
              SvtJxsErrorNone);
    EXPECT_EQ(image_config.format, format);
    EXPECT_EQ(image_config.components_num, 4);

    svt_jpeg_xs_image_buffer_t *out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(out_buf, nullptr);

    svt_jpeg_xs_bitstream_buffer_t in_bitstream;
    in_bitstream.buffer = bitstream.data();
    in_bitstream.allocation_size = (uint32_t)bitstream.size();
    in_bitstream.used_size = (uint32_t)bitstream.size();

    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream = in_bitstream;
    dec_input.image = *out_buf;
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t dec_output;
    EXPECT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_buf);
}

INSTANTIATE_TEST_SUITE_P(FourComponentFormats, FourComponentRoundTripParam,
                         ::testing::Values(COLOUR_FORMAT_PLANAR_4_COMPONENTS, COLOUR_FORMAT_PLANAR_YUV422_ALPHA));

/*
 * Security regression test (found while implementing SDBQ-3776, unrelated to alpha feature but
 * on the same touched decoder code path): static_get_single_frame_size() previously validated
 * comps_num only against JPEGXS_SPEC_MAX_COMPONENTS_NUM(8), then wrote
 * out_image_config->components[c] (sized MAX_COMPONENTS_NUM=4) for c in [0, comps_num) - a
 * crafted stream claiming comps_num in [5..8] caused an out-of-bounds write (CWE-787). Fixed by
 * additionally bounding comps_num against MAX_COMPONENTS_NUM before those writes. This test
 * mutates a real encoded stream's Nc field and asserts rejection, not a crash/OOB write.
 */
class ParseHeaderCompsNumOobParam : public ::testing::TestWithParam<uint8_t> {};

TEST_P(ParseHeaderCompsNumOobParam, MutatedCompsNumRejected) {
    uint8_t bad_comps_num = GetParam();
    std::vector<uint8_t> bitstream = encode_single_frame(64, 64, 8, COLOUR_FORMAT_PLANAR_YUV422, 2, 1, 3);
    ASSERT_GT(bitstream.size(), 0u);

    // Locate the Nc byte: PIH marker (0xFF12) header, Nc is at offset 20 from the start of the
    // PIH marker (i.e. including its 2-byte marker code) - same layout static_get_single_frame_size()
    // itself parses. Find the PIH marker by scanning rather than hardcoding a stream-wide offset
    // (the offset varies with preceding marker sizes, e.g. CAP marker length).
    size_t pih_offset = std::string::npos;
    for (size_t i = 0; i + 1 < bitstream.size(); ++i) {
        if (bitstream[i] == 0xFF && bitstream[i + 1] == 0x12) {
            pih_offset = i;
            break;
        }
    }
    ASSERT_NE(pih_offset, std::string::npos);
    size_t nc_offset = pih_offset + 20;
    ASSERT_LT(nc_offset, bitstream.size());
    ASSERT_EQ(bitstream[nc_offset], 3); // sanity: real yuv422 stream has Nc=3 here before mutation

    bitstream[nc_offset] = bad_comps_num;

    svt_jpeg_xs_image_config_t out_config;
    memset(&out_config, 0, sizeof(out_config));
    uint32_t frame_size = 0;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_get_single_frame_size(
        bitstream.data(), bitstream.size(), &out_config, &frame_size, 0);
    EXPECT_EQ(ret, SvtJxsErrorDecoderInvalidBitstream) << "comps_num=" << (int)bad_comps_num;
}

INSTANTIATE_TEST_SUITE_P(BeyondImplementationLimit, ParseHeaderCompsNumOobParam,
                         ::testing::Values((uint8_t)5, (uint8_t)6, (uint8_t)7, (uint8_t)8));
