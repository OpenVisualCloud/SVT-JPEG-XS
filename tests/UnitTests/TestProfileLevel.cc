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
#include "ProfileLevel.h"
#include "Decoder.h"
#include "DecoderSimple.h"

namespace {

/* Encodes a single, all-zero frame with the given configuration and returns the produced codestream.
 * profile_ppih_override=0 and level_plev_override=0xFFFF mean "auto-derive" (library defaults). */
std::vector<uint8_t> encode_single_frame(uint32_t width, uint32_t height, uint8_t bit_depth, ColourFormat_t colour_format,
                                         uint32_t bpp_numerator, uint32_t bpp_denominator,
                                         uint16_t profile_ppih_override = 0, uint16_t level_plev_override = 0xFFFF) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = colour_format;
    enc.bpp_numerator = bpp_numerator;
    enc.bpp_denominator = bpp_denominator;
    enc.profile_ppih_override = profile_ppih_override;
    enc.level_plev_override = level_plev_override;

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    EXPECT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
              SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t *in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    EXPECT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        memset(in_buf->data_yuv[c], 0, in_buf->alloc_size[c]);
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

/* Parses the produced codestream and returns its picture_header_const_t, so tests can check the
 * literal hdr_Ppih/hdr_Plev bytes that ended up in the stream. */
picture_header_const_t probe_header(const std::vector<uint8_t> &bitstream) {
    picture_header_const_t picture_header_const;
    memset(&picture_header_const, 0, sizeof(picture_header_const));
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_probe(
        bitstream.data(), bitstream.size(), &picture_header_const, NULL, VERBOSE_NONE);
    EXPECT_EQ(ret, SvtJxsErrorNone);
    return picture_header_const;
}

} // namespace

/*
 * derive_stream_profile_ppih(): unit tests across colour format / bit depth combinations.
 */
TEST(StreamProfileDerivation, Yuv420) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV420, 8, VERBOSE_NONE), JXS_PPIH_MAIN_420_12);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV420, 10, VERBOSE_NONE), JXS_PPIH_MAIN_420_12);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV420, 12, VERBOSE_NONE), JXS_PPIH_MAIN_420_12);
}

TEST(StreamProfileDerivation, Yuv422LowBitDepthIsMain422) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV422, 8, VERBOSE_NONE), JXS_PPIH_MAIN_422_10);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV422, 10, VERBOSE_NONE), JXS_PPIH_MAIN_422_10);
}

TEST(StreamProfileDerivation, Yuv422HighBitDepthFallsBackToMain444) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV422, 12, VERBOSE_NONE), JXS_PPIH_MAIN_444_12);
}

TEST(StreamProfileDerivation, Gray400) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV400, 8, VERBOSE_NONE), JXS_PPIH_MAIN_422_10);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV400, 12, VERBOSE_NONE), JXS_PPIH_MAIN_444_12);
}

TEST(StreamProfileDerivation, Yuv444PlanarAndPacked) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 8, VERBOSE_NONE), JXS_PPIH_MAIN_444_12);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 12, VERBOSE_NONE), JXS_PPIH_MAIN_444_12);
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PACKED_YUV444_OR_RGB, 8, VERBOSE_NONE), JXS_PPIH_MAIN_444_12);
}

TEST(StreamProfileDerivation, FourComponents) {
    EXPECT_EQ(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_4_COMPONENTS, 8, VERBOSE_NONE), JXS_PPIH_MAIN_4444_12);
}

TEST(StreamProfileDerivation, NeverReturnsZero) {
    // Ppih=0x0000 is never a valid profile; the auto-derivation must never reintroduce that defect.
    for (int bit_depth = 8; bit_depth <= 14; ++bit_depth) {
        EXPECT_NE(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV420, (uint8_t)bit_depth, VERBOSE_NONE), 0);
        EXPECT_NE(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV422, (uint8_t)bit_depth, VERBOSE_NONE), 0);
        EXPECT_NE(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, (uint8_t)bit_depth, VERBOSE_NONE), 0);
        EXPECT_NE(derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_4_COMPONENTS, (uint8_t)bit_depth, VERBOSE_NONE), 0);
    }
}

/*
 * derive_stream_level_plev(): unit tests across resolution / bpp combinations.
 */
TEST(StreamLevelDerivation, SmallResolutionIs1k1) {
    uint16_t plev = derive_stream_level_plev(640, 480, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_1K_1);
}

TEST(StreamLevelDerivation, FullHdIs2k1) {
    uint16_t plev = derive_stream_level_plev(1920, 1080, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_2K_1);
}

TEST(StreamLevelDerivation, Uhd4kIs4k1) {
    uint16_t plev = derive_stream_level_plev(3840, 2160, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_4K_1);
}

TEST(StreamLevelDerivation, Square4096PicksHigherThroughput4k3) {
    // 4096x4096 exceeds the 4k-1 pixel-count bound; 4k-2/4k-3 share identical bounds, only the
    // higher-throughput (safer, since no fps is known) variant 4k-3 must be picked.
    uint16_t plev = derive_stream_level_plev(4096, 4096, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_4K_3);
}

TEST(StreamLevelDerivation, Uhd8kIs8k1) {
    uint16_t plev = derive_stream_level_plev(7680, 4320, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_8K_1);
}

TEST(StreamLevelDerivation, HugeResolutionFallsBackToUnrestrictedLevel) {
    uint16_t plev = derive_stream_level_plev(20000, 20000, 8, 1);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, SublevelBucketsPickSmallestBoundThatCovers) {
    EXPECT_EQ(derive_stream_level_plev(640, 480, 2, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_2BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 5, 2) & 0x00FF, JXS_PLEV_SUBLEVEL_3BPP); // 2.5bpp -> 3bpp bucket
    EXPECT_EQ(derive_stream_level_plev(640, 480, 4, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_4BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 6, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_6BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 9, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_9BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 12, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_12BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 15, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_UNRESTRICTED);
}

/*
 * End-to-end: encode a single frame and verify the actual bytes in the produced codestream carry the
 * expected auto-derived Ppih/Plev (regression test for the original Ppih=0/Plev=0 defect).
 */
TEST(StreamProfileLevelEncodeIntegration, AutoDerivedYuv422MatchesDerivationFunction) {
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, derive_stream_profile_ppih(COLOUR_FORMAT_PLANAR_YUV422, 8, VERBOSE_NONE));
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_MAIN_422_10);
    EXPECT_NE(phc.hdr_Ppih, 0u); // The original defect: Ppih was hardcoded to 0.
    EXPECT_EQ(phc.hdr_Plev, derive_stream_level_plev(256, 256, 8, 1));
}

TEST(StreamProfileLevelEncodeIntegration, AutoDerivedYuv420MatchesDerivationFunction) {
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV420, 6, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_MAIN_420_12);
    EXPECT_EQ(phc.hdr_Plev, derive_stream_level_plev(256, 256, 6, 1));
}

TEST(StreamProfileLevelEncodeIntegration, AutoDerivedYuv444_12bitMatchesDerivationFunction) {
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 12, COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 12, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_MAIN_444_12);
}

TEST(StreamProfileLevelEncodeIntegration, ProfileOverrideIsUsedVerbatim) {
    std::vector<uint8_t> bitstream = encode_single_frame(
        256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1, JXS_PPIH_HIGH_444_12, 0xFFFF);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_HIGH_444_12);
}

TEST(StreamProfileLevelEncodeIntegration, LevelOverrideIsUsedVerbatim) {
    std::vector<uint8_t> bitstream = encode_single_frame(
        256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1, 0, (uint16_t)JXS_PLEV_LEVEL_10K_1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Plev, (uint16_t)JXS_PLEV_LEVEL_10K_1);
}

TEST(StreamProfileLevelEncodeIntegration, LevelOverrideCanExplicitlyForceUnrestricted) {
    // Plev=0x0000 is a legitimate explicit override (distinct from the 0xFFFF "auto" sentinel), even
    // though the 256x256 resolution would otherwise auto-derive a tighter level.
    std::vector<uint8_t> bitstream = encode_single_frame(
        256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1, 0, 0x0000);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Plev, 0u);
}

/*
 * Regression safety: the decoder must still successfully decode full frames once Ppih/Plev carry
 * real, non-zero values (the decoder never validated these fields, but this guards against
 * accidental breakage in the parsing path itself). Covers profile overrides, level overrides, and
 * combinations of both, since either field independently changes the picture header bytes the
 * decoder parses.
 */
struct ProfileLevelParam {
    uint16_t profile_override;
    uint16_t level_override;
};

class StreamProfileLevelDecodeStillWorks : public ::testing::TestWithParam<ProfileLevelParam> {};

TEST_P(StreamProfileLevelDecodeStillWorks, DecodesSuccessfully) {
    ProfileLevelParam param = GetParam();
    std::vector<uint8_t> bitstream = encode_single_frame(
        256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1, param.profile_override, param.level_override);
    ASSERT_GT(bitstream.size(), 0u);

    DecoderSimple_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder_simple_init_rtcd(CPU_FLAGS_ALL);

    ASSERT_EQ(decoder_simple_alloc(&decoder, bitstream.data(), bitstream.size()), SvtJxsErrorNone);
    SvtJxsErrorType_t ret = decoder_simple_get_frame(&decoder, bitstream.data(), bitstream.size());
    EXPECT_GE(ret, 0); // decoder_simple_get_frame returns the decoded frame size (>=0) on success.
    decoder_simple_free(&decoder);
}

INSTANTIATE_TEST_SUITE_P(
    VariousProfilesAndLevels, StreamProfileLevelDecodeStillWorks,
    ::testing::Values(
        // Both auto-derived (baseline).
        ProfileLevelParam{0, 0xFFFF},
        // Profile override only, level auto-derived.
        ProfileLevelParam{JXS_PPIH_MAIN_422_10, 0xFFFF},
        ProfileLevelParam{JXS_PPIH_HIGH_444_12, 0xFFFF},
        ProfileLevelParam{JXS_PPIH_LIGHT_422_10, 0xFFFF},
        // Level override only, profile auto-derived.
        ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_10K_1 | JXS_PLEV_SUBLEVEL_UNRESTRICTED)},
        ProfileLevelParam{0, 0x0000}, // Explicit Unrestricted level+sublevel+FBB.
        ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_1K_1 | JXS_PLEV_SUBLEVEL_12BPP)},
        ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_8K_1 | JXS_PLEV_SUBLEVEL_2BPP)},
        // Profile and level overridden together.
        ProfileLevelParam{JXS_PPIH_HIGH_444_12, (uint16_t)JXS_PLEV_LEVEL_8K_1},
        ProfileLevelParam{JXS_PPIH_LIGHT_422_10, (uint16_t)(JXS_PLEV_LEVEL_1K_1 | JXS_PLEV_SUBLEVEL_3BPP)}));

