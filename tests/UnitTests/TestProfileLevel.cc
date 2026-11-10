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
#include "ProfileLevelNames.h"
#include "Decoder.h"
#include "DecoderSimple.h"

namespace {

/* Encodes a single, all-zero frame with the given configuration and returns the produced codestream.
 * profile_ppih_override=0 and level_plev_override=0xFFFF mean "auto-derive" (library defaults). */
std::vector<uint8_t> encode_single_frame(uint32_t width, uint32_t height, uint8_t bit_depth, ColourFormat_t colour_format,
                                         uint32_t bpp_numerator, uint32_t bpp_denominator, uint16_t profile_ppih_override = 0,
                                         uint16_t level_plev_override = 0xFFFF, uint8_t coding_raw_disable = 0,
                                         uint32_t ndecomp_v = 2) {
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
    enc.coding_raw_disable = coding_raw_disable;
    enc.ndecomp_v = ndecomp_v;

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
 * derive_stream_profile_ppih(): unit tests against ISO/IEC 21122-2 Tables A.1, A.3 (Amd 1:2022) and
 * A.4, and the 3rd edition CHigh 444.12 profile.
 */
namespace {

/* Parameters of a stream that meets every shared restriction: 8 bit, 16-row slices, Lh=0, Rl=0. */
StreamProfileParams conformant_params(ColourFormat_t colour_format, uint8_t ndecomp_v) {
    StreamProfileParams params;
    params.colour_format = colour_format;
    params.width = 1920;
    params.bit_depth = 8;
    params.ndecomp_v = ndecomp_v;
    params.slice_height_rows = 16;
    params.raw_mode_per_packet = 0;
    params.long_header = 0;
    params.lossless = 0;
    params.quantization = 0;
    params.colour_transform = 0;
    return params;
}

uint16_t derive(StreamProfileParams params) {
    return derive_stream_profile_ppih(&params, VERBOSE_NONE);
}

const ColourFormat_t all_colour_formats[] = {COLOUR_FORMAT_PLANAR_RGB,
                                             COLOUR_FORMAT_PLANAR_RGBA,
                                             COLOUR_FORMAT_PLANAR_YUV400,
                                             COLOUR_FORMAT_PLANAR_YUV420,
                                             COLOUR_FORMAT_PLANAR_YUV422,
                                             COLOUR_FORMAT_PLANAR_YUV444_OR_RGB,
                                             COLOUR_FORMAT_PACKED_YUV444_OR_RGB,
                                             COLOUR_FORMAT_PLANAR_4_COMPONENTS,
                                             COLOUR_FORMAT_PLANAR_YUV422_ALPHA};

} // namespace

TEST(StreamProfileDerivation, MainProfilesForUpToOneVerticalDecomposition) {
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV420, 1)), JXS_PPIH_MAIN_420_12);
    for (uint8_t v = 0; v <= 1; ++v) {
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV400, v)), JXS_PPIH_MAIN_422_10);
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV422, v)), JXS_PPIH_MAIN_422_10);
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, v)), JXS_PPIH_MAIN_444_12);
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PACKED_YUV444_OR_RGB, v)), JXS_PPIH_MAIN_444_12);
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_4_COMPONENTS, v)), JXS_PPIH_MAIN_4444_12);
        /* Regression (SDBQ-3776): 4:2:2:4 is a 4444.12 format, not 422.10/444.12. */
        EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV422_ALPHA, v)), JXS_PPIH_MAIN_4444_12);
    }
}

TEST(StreamProfileDerivation, HighProfilesForTwoVerticalDecompositions) {
    /* Main allows at most NL,y = 1 (Main 420.12 exactly 1); High allows 2. */
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV420, 2)), JXS_PPIH_HIGH_420_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV400, 2)), JXS_PPIH_HIGH_444_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 2)), JXS_PPIH_HIGH_444_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 2)), JXS_PPIH_HIGH_444_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PACKED_YUV444_OR_RGB, 2)), JXS_PPIH_HIGH_444_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_4_COMPONENTS, 2)), JXS_PPIH_HIGH_4444_12);
    EXPECT_EQ(derive(conformant_params(COLOUR_FORMAT_PLANAR_YUV422_ALPHA, 2)), JXS_PPIH_HIGH_4444_12);
}

TEST(StreamProfileDerivation, TwelveBit422And400UseMain444) {
    /* Main 422.10 only allows B in {8, 10}; Main 444.12 also covers 4:0:0 and 4:2:2. */
    StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 1);
    params.bit_depth = 10;
    EXPECT_EQ(derive(params), JXS_PPIH_MAIN_422_10);
    params.bit_depth = 12;
    EXPECT_EQ(derive(params), JXS_PPIH_MAIN_444_12);
    params.colour_format = COLOUR_FORMAT_PLANAR_YUV400;
    EXPECT_EQ(derive(params), JXS_PPIH_MAIN_444_12);
}

TEST(StreamProfileDerivation, RawModeUsesCHighOnlyFor400_422_444) {
    for (uint8_t v = 0; v <= 2; ++v) {
        for (ColourFormat_t colour_format : all_colour_formats) {
            StreamProfileParams params = conformant_params(colour_format, v);
            params.raw_mode_per_packet = 1;
            const bool chigh = colour_format == COLOUR_FORMAT_PLANAR_RGB || colour_format == COLOUR_FORMAT_PLANAR_YUV400 ||
                colour_format == COLOUR_FORMAT_PLANAR_YUV422 || colour_format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB ||
                colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB;
            EXPECT_EQ(derive(params), chigh ? JXS_PPIH_CHIGH_444_12 : JXS_PPIH_UNRESTRICTED)
                << "colour_format " << colour_format << " ndecomp_v " << (int)v;
        }
    }
}

TEST(StreamProfileDerivation, LosslessIsMls12OnlyWithoutRawMode) {
    for (ColourFormat_t colour_format : all_colour_formats) {
        StreamProfileParams params = conformant_params(colour_format, 2);
        params.lossless = 1;
        EXPECT_EQ(derive(params), JXS_PPIH_MLS_12) << "colour_format " << colour_format;
        params.raw_mode_per_packet = 1; /* Table A.4: MLS.12 requires Rl=0; CHigh is lossy only. */
        EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED) << "colour_format " << colour_format;
    }
}

TEST(StreamProfileDerivation, BitDepthOutsideDiscreteSetIsUnrestricted) {
    /* Every selectable profile allows B in {8, 10, 12} only. */
    const uint8_t bit_depths[] = {9, 11, 13, 14};
    for (uint8_t bit_depth : bit_depths) {
        for (ColourFormat_t colour_format : all_colour_formats) {
            for (uint8_t raw = 0; raw <= 1; ++raw) {
                for (uint8_t lossless = 0; lossless <= 1; ++lossless) {
                    StreamProfileParams params = conformant_params(colour_format, 2);
                    params.bit_depth = bit_depth;
                    params.raw_mode_per_packet = raw;
                    params.lossless = lossless;
                    EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED) << "bit_depth " << (int)bit_depth;
                }
            }
        }
    }
}

TEST(StreamProfileDerivation, SliceHeightOtherThan16RowsIsUnrestricted) {
    const uint32_t heights[] = {4, 8, 32, 64, 1080};
    for (uint32_t rows : heights) {
        StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 1);
        params.slice_height_rows = rows;
        EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED) << "rows " << rows;
        params.raw_mode_per_packet = 1;
        EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED) << "rows " << rows;
    }
}

TEST(StreamProfileDerivation, LongHeaderEnforcementIsUnrestricted) {
    StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_4_COMPONENTS, 1);
    params.long_header = 1;
    EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED);
    params = conformant_params(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 2);
    params.long_header = 1;
    params.raw_mode_per_packet = 1;
    EXPECT_EQ(derive(params), JXS_PPIH_UNRESTRICTED);
}

/*
 * check_stream_profile_ppih(): profile_ppih_override is written verbatim, but every restriction the
 * stream breaks is reported.
 */
TEST(StreamProfileOverrideCheck, AutoSelectionAlwaysPassesItsOwnCheck) {
    for (ColourFormat_t colour_format : all_colour_formats) {
        for (uint8_t v = 0; v <= 2; ++v) {
            for (uint8_t raw = 0; raw <= 1; ++raw) {
                for (uint8_t lossless = 0; lossless <= 1; ++lossless) {
                    for (uint8_t bit_depth : {8, 10, 12, 14}) {
                        StreamProfileParams params = conformant_params(colour_format, v);
                        params.raw_mode_per_packet = raw;
                        params.lossless = lossless;
                        params.bit_depth = bit_depth;
                        const uint16_t ppih = derive(params);
                        EXPECT_EQ(check_stream_profile_ppih(ppih, &params, VERBOSE_NONE), 0u)
                            << std::hex << ppih << std::dec << " format " << colour_format << " v " << (int)v;
                    }
                }
            }
        }
    }
}

TEST(StreamProfileOverrideCheck, ReportsEveryBrokenRestriction) {
    /* BirdDog 2026-11-10 samples: 4:2:2 10 bit, NL,y = 2, Rl=1 declared as High 444.12. */
    StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 2);
    params.bit_depth = 10;
    params.raw_mode_per_packet = 1;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_CHIGH_444_12, &params, VERBOSE_NONE), 0u);
    /* Main 422.10 at NL,y = 2 with Rl=1: two problems. */
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_MAIN_422_10, &params, VERBOSE_NONE), 2u);
    /* CHigh with Rl=0. */
    params.raw_mode_per_packet = 0;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_CHIGH_444_12, &params, VERBOSE_NONE), 1u);
    /* MLS.12 for a lossy stream; Main for a lossless one. */
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_MLS_12, &params, VERBOSE_NONE), 1u);
    params.lossless = 1;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE), 1u);
}

TEST(StreamProfileOverrideCheck, ColourFormatRctQuantizerAndColumnWidth) {
    StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_YUV420, 1);
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_MAIN_422_10, &params, VERBOSE_NONE), 1u);
    params = conformant_params(COLOUR_FORMAT_PLANAR_RGB, 1);
    params.colour_transform = 1;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_MAIN_444_12, &params, VERBOSE_NONE), 0u);
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_LIGHT_444_12, &params, VERBOSE_NONE), 0u);
    /* Light 422.10 allows neither 4:4:4 nor the colour transform. */
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_LIGHT_422_10, &params, VERBOSE_NONE), 2u);
    /* Light profiles other than Light-Subline only allow the deadzone quantizer. */
    params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 1);
    params.quantization = 1;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_LIGHT_422_10, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_MAIN_422_10, &params, VERBOSE_NONE), 0u);
    /* Light-Subline: NL,y = 0 and a column of at most 2048 grid points (Cw=0 makes it Wf). */
    params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 0);
    params.width = 2048;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_LIGHT_SUBLINE_422_10, &params, VERBOSE_NONE), 0u);
    params.width = 2050;
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_LIGHT_SUBLINE_422_10, &params, VERBOSE_NONE), 1u);
}

TEST(StreamProfileOverrideCheck, UnknownAndUnsupportedCodepoints) {
    StreamProfileParams params = conformant_params(COLOUR_FORMAT_PLANAR_YUV422, 2);
    EXPECT_EQ(check_stream_profile_ppih(JXS_PPIH_UNRESTRICTED, &params, VERBOSE_NONE), 0u);
    EXPECT_EQ(check_stream_profile_ppih(0x1234, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_profile_ppih(0xB340, &params, VERBOSE_NONE), 1u); /* MainBayer */
    EXPECT_EQ(check_stream_profile_ppih(0x4A45, &params, VERBOSE_NONE), 1u); /* TDC 444.12 */
}

/*
 * derive_stream_level_plev(): unit tests across resolution / bpp combinations.
 */
TEST(StreamLevelDerivation, SmallResolutionIs1k1) {
    uint16_t plev = derive_stream_level_plev(640, 480, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_1K_1);
}

TEST(StreamLevelDerivation, FullHdIs2k1) {
    uint16_t plev = derive_stream_level_plev(1920, 1080, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_2K_1);
}

TEST(StreamLevelDerivation, Uhd4kIs4k1) {
    uint16_t plev = derive_stream_level_plev(3840, 2160, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_4K_1);
}

TEST(StreamLevelDerivation, Square4096PicksHigherThroughput4k3) {
    // 4096x4096 exceeds the 4k-1 pixel-count bound; 4k-2/4k-3 share identical bounds, only the
    // higher-throughput (safer, since no fps is known) variant 4k-3 must be picked.
    uint16_t plev = derive_stream_level_plev(4096, 4096, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_4K_3);
}

TEST(StreamLevelDerivation, Uhd8kIs8k1) {
    uint16_t plev = derive_stream_level_plev(7680, 4320, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_8K_1);
}

TEST(StreamLevelDerivation, HugeResolutionFallsBackToUnrestrictedLevel) {
    uint16_t plev = derive_stream_level_plev(20000, 20000, 8, 1, 0);
    EXPECT_EQ(plev & 0xFC00, JXS_PLEV_LEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, SublevelBucketsPickSmallestBoundThatCovers) {
    EXPECT_EQ(derive_stream_level_plev(640, 480, 2, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_2BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 5, 2, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_3BPP); // 2.5bpp -> 3bpp bucket
    EXPECT_EQ(derive_stream_level_plev(640, 480, 4, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_4BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 6, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_6BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 9, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_9BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 12, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_12BPP);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 15, 1, 0) & 0x00FF, JXS_PLEV_SUBLEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, LosslessForcesUnrestrictedSublevelRegardlessOfBpp) {
    // bpp_numerator/bpp_denominator are meaningless for lossless (Table A.9 NOTE 3); a low bpp
    // fraction here must not narrow the declared sublevel, since real lossless output far exceeds it.
    EXPECT_EQ(derive_stream_level_plev(640, 480, 0, 1, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_UNRESTRICTED);
    EXPECT_EQ(derive_stream_level_plev(640, 480, 2, 1, 1) & 0x00FF, JXS_PLEV_SUBLEVEL_UNRESTRICTED);
}

/*
 * End-to-end: encode a single frame and verify the actual bytes in the produced codestream carry the
 * expected auto-derived Ppih/Plev (regression test for the original Ppih=0/Plev=0 defect).
 */
TEST(StreamProfileLevelEncodeIntegration, DefaultYuv422IsCHigh444) {
    /* Library defaults: raw-mode enabled (Rl=1), ndecomp_v=2, 16-row slices. */
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_CHIGH_444_12);
    EXPECT_EQ(phc.hdr_Plev, derive_stream_level_plev(256, 256, 8, 1, 0));
}

TEST(StreamProfileLevelEncodeIntegration, DefaultYuv420IsUnrestricted) {
    /* No profile covers 4:2:0 with raw-mode selection per packet (Rl=1). */
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV420, 6, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_UNRESTRICTED);
    EXPECT_EQ(phc.hdr_Plev, derive_stream_level_plev(256, 256, 6, 1, 0));
}

TEST(StreamProfileLevelEncodeIntegration, RawModeDisabledSelectsMainOrHigh) {
    struct Case {
        ColourFormat_t colour_format;
        uint8_t bit_depth;
        uint32_t ndecomp_v;
        uint16_t expected;
    };
    const Case cases[] = {
        {COLOUR_FORMAT_PLANAR_YUV422, 10, 1, JXS_PPIH_MAIN_422_10},
        {COLOUR_FORMAT_PLANAR_YUV422, 10, 2, JXS_PPIH_HIGH_444_12},
        {COLOUR_FORMAT_PLANAR_YUV420, 8, 1, JXS_PPIH_MAIN_420_12},
        {COLOUR_FORMAT_PLANAR_YUV420, 8, 2, JXS_PPIH_HIGH_420_12},
        {COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 12, 1, JXS_PPIH_MAIN_444_12},
        {COLOUR_FORMAT_PLANAR_4_COMPONENTS, 8, 2, JXS_PPIH_HIGH_4444_12},
    };
    for (const Case &c : cases) {
        std::vector<uint8_t> bitstream = encode_single_frame(
            256, 256, c.bit_depth, c.colour_format, 6, 1, 0, 0xFFFF, 1 /*coding_raw_disable*/, c.ndecomp_v);
        ASSERT_GT(bitstream.size(), 0u);
        EXPECT_EQ(probe_header(bitstream).hdr_Ppih, c.expected)
            << "colour_format " << c.colour_format << " ndecomp_v " << c.ndecomp_v;
    }
}

TEST(StreamProfileLevelEncodeIntegration, ShortPictureSliceIsUnrestricted) {
    /* A picture shorter than 16 rows clamps the slice height below the 16 rows every profile needs. */
    std::vector<uint8_t> bitstream = encode_single_frame(256, 8, 8, COLOUR_FORMAT_PLANAR_YUV422, 6, 1, 0, 0xFFFF, 1, 1);
    ASSERT_GT(bitstream.size(), 0u);
    EXPECT_EQ(probe_header(bitstream).hdr_Ppih, JXS_PPIH_UNRESTRICTED);
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
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV422, 8, 1, 0, 0x0000);
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

INSTANTIATE_TEST_SUITE_P(VariousProfilesAndLevels, StreamProfileLevelDecodeStillWorks,
                         ::testing::Values(
                             // Both auto-derived (baseline).
                             ProfileLevelParam{0, 0xFFFF},
                             // Profile override only, level auto-derived.
                             ProfileLevelParam{JXS_PPIH_MAIN_422_10, 0xFFFF}, ProfileLevelParam{JXS_PPIH_HIGH_444_12, 0xFFFF},
                             ProfileLevelParam{JXS_PPIH_LIGHT_422_10, 0xFFFF},
                             // Level override only, profile auto-derived.
                             ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_10K_1 | JXS_PLEV_SUBLEVEL_UNRESTRICTED)},
                             ProfileLevelParam{0, 0x0000}, // Explicit Unrestricted level+sublevel+FBB.
                             ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_1K_1 | JXS_PLEV_SUBLEVEL_12BPP)},
                             ProfileLevelParam{0, (uint16_t)(JXS_PLEV_LEVEL_8K_1 | JXS_PLEV_SUBLEVEL_2BPP)},
                             // Profile and level overridden together.
                             ProfileLevelParam{JXS_PPIH_HIGH_444_12, (uint16_t)JXS_PLEV_LEVEL_8K_1},
                             ProfileLevelParam{JXS_PPIH_LIGHT_422_10, (uint16_t)(JXS_PLEV_LEVEL_1K_1 | JXS_PLEV_SUBLEVEL_3BPP)}));

/* Every codeword the encoder can auto-derive must have a printable name, and the formatted string
 * must keep the raw hex value, only adding the name in parentheses when the codeword is known. */
TEST(ProfileLevelNames, KnownCodewordsAreNamed) {
    const uint16_t profiles[] = {JXS_PPIH_UNRESTRICTED,
                                 JXS_PPIH_LIGHT_422_10,
                                 JXS_PPIH_LIGHT_444_12,
                                 JXS_PPIH_LIGHT_SUBLINE_422_10,
                                 JXS_PPIH_MAIN_420_12,
                                 JXS_PPIH_MAIN_422_10,
                                 JXS_PPIH_MAIN_444_12,
                                 JXS_PPIH_MAIN_4444_12,
                                 JXS_PPIH_HIGH_420_12,
                                 JXS_PPIH_HIGH_444_12,
                                 JXS_PPIH_CHIGH_444_12,
                                 JXS_PPIH_HIGH_4444_12,
                                 JXS_PPIH_MLS_12};
    for (uint16_t ppih : profiles) {
        EXPECT_NE(jxs_profile_name(ppih), nullptr) << std::hex << ppih;
    }
    const uint16_t levels[] = {JXS_PLEV_LEVEL_UNRESTRICTED,
                               JXS_PLEV_LEVEL_1K_1,
                               JXS_PLEV_LEVEL_2K_1,
                               JXS_PLEV_LEVEL_4K_1,
                               JXS_PLEV_LEVEL_4K_2,
                               JXS_PLEV_LEVEL_4K_3,
                               JXS_PLEV_LEVEL_5K_1,
                               JXS_PLEV_LEVEL_8K_1,
                               JXS_PLEV_LEVEL_8K_2,
                               JXS_PLEV_LEVEL_8K_3,
                               JXS_PLEV_LEVEL_10K_1};
    const uint16_t sublevels[] = {JXS_PLEV_SUBLEVEL_UNRESTRICTED,
                                  JXS_PLEV_SUBLEVEL_12BPP,
                                  JXS_PLEV_SUBLEVEL_9BPP,
                                  JXS_PLEV_SUBLEVEL_6BPP,
                                  JXS_PLEV_SUBLEVEL_4BPP,
                                  JXS_PLEV_SUBLEVEL_3BPP,
                                  JXS_PLEV_SUBLEVEL_2BPP};
    char buf[64];
    for (uint16_t level : levels) {
        for (uint16_t sublevel : sublevels) {
            EXPECT_NE(jxs_level_name((uint16_t)(level | sublevel), buf, sizeof(buf)), nullptr)
                << std::hex << (level | sublevel);
        }
    }
}

TEST(ProfileLevelNames, FormattedStringKeepsHexAndAddsNamesOnlyWhenKnown) {
    char buf[JXS_PROFILE_LEVEL_STR_SIZE];
    EXPECT_STREQ(jxs_profile_level_str(JXS_PPIH_MLS_12, JXS_PLEV_LEVEL_2K_1, buf, sizeof(buf)),
                 "0x6EC0 (MLS.12) / 0x1000 (2k-1, Sublevel Unrestricted)");
    EXPECT_STREQ(jxs_profile_level_str(JXS_PPIH_MAIN_422_10, (uint16_t)(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_3BPP), buf, sizeof(buf)),
                 "0x3540 (Main 422.10) / 0x1004 (2k-1, Sublev3bpp)");
    EXPECT_STREQ(jxs_profile_level_str(0x1234, 0x0301, buf, sizeof(buf)), "0x1234 / 0x0301");
}
