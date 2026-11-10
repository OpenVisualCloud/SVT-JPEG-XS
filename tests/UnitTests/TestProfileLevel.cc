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
                                         uint32_t ndecomp_v = 2, uint32_t fps_numerator = 0, uint32_t fps_denominator = 1) {
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
    enc.fps_numerator = fps_numerator;
    enc.fps_denominator = fps_denominator;

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
 * derive_stream_level_plev(): unit tests against ISO/IEC 21122-2 Tables A.8, A.9 and A.16.
 */
namespace {

uint16_t level_of(uint32_t width, uint32_t height, uint32_t fps_numerator, uint32_t fps_denominator = 1) {
    StreamLevelParams params = {width, height, fps_numerator, fps_denominator, (uint64_t)width * height * 3 / 8, 0};
    return derive_stream_level_plev(&params, VERBOSE_NONE) & 0xFF00;
}

/* Frame size as the encoder computes it for a bpp target: ceil(floor(w * h * bpp) / 8) bytes. */
uint64_t frame_bytes_for(uint32_t width, uint32_t height, uint32_t bpp_numerator, uint32_t bpp_denominator) {
    return ((uint64_t)width * height * bpp_numerator / bpp_denominator + 7) / 8;
}

uint16_t sublevel_of(uint32_t bpp_numerator, uint32_t bpp_denominator, uint8_t lossless = 0) {
    StreamLevelParams params = {640, 480, 30, 1, frame_bytes_for(640, 480, bpp_numerator, bpp_denominator), lossless};
    return derive_stream_level_plev(&params, VERBOSE_NONE) & 0x00FF;
}

} // namespace

/* Every non-Bayer example resolution of Table A.8 maps to the level it is listed under, except
 * where a lower level already covers it (Table A.8 lists examples, not minima). */
TEST(StreamLevelDerivation, TableA8ExamplesPickLowestLevel) {
    struct Case {
        uint32_t width, height, fps;
        uint16_t level;
    };
    // clang-format off
    const Case cases[] = {
        {1280, 720, 60, JXS_PLEV_LEVEL_1K_1},
        {1280, 720, 120, JXS_PLEV_LEVEL_2K_1},
        {1920, 1080, 30, JXS_PLEV_LEVEL_2K_1},
        {1920, 1080, 60, JXS_PLEV_LEVEL_2K_1},
        {2048, 1536, 30, JXS_PLEV_LEVEL_2K_1},
        {2048, 2048, 30, JXS_PLEV_LEVEL_2K_1},
        {2048, 1080, 60, JXS_PLEV_LEVEL_2K_1},
        {1920, 1080, 120, JXS_PLEV_LEVEL_4K_1},
        {3840, 2160, 30, JXS_PLEV_LEVEL_4K_1},
        {4096, 2160, 30, JXS_PLEV_LEVEL_4K_1},
        {1920, 1080, 240, JXS_PLEV_LEVEL_4K_2},
        {3840, 2160, 60, JXS_PLEV_LEVEL_4K_2},
        {4096, 3072, 30, JXS_PLEV_LEVEL_4K_2},
        {4096, 4096, 30, JXS_PLEV_LEVEL_4K_2},
        {4096, 2160, 60, JXS_PLEV_LEVEL_4K_2},
        {1920, 1080, 480, JXS_PLEV_LEVEL_4K_3},
        {2048, 1080, 480, JXS_PLEV_LEVEL_4K_3},
        {4096, 3072, 60, JXS_PLEV_LEVEL_4K_3},
        {4096, 4096, 60, JXS_PLEV_LEVEL_4K_3},
        {3840, 2160, 120, JXS_PLEV_LEVEL_4K_3},
        {4096, 2160, 120, JXS_PLEV_LEVEL_4K_3},
        {7680, 4320, 30, JXS_PLEV_LEVEL_8K_1},
        {8192, 4320, 30, JXS_PLEV_LEVEL_8K_1},
        {3840, 2160, 240, JXS_PLEV_LEVEL_8K_2},
        {8192, 6144, 30, JXS_PLEV_LEVEL_8K_2},
        {8192, 8192, 30, JXS_PLEV_LEVEL_8K_2},
        {7680, 4320, 60, JXS_PLEV_LEVEL_8K_2},
        {8192, 4320, 60, JXS_PLEV_LEVEL_8K_2},
        {3840, 2160, 480, JXS_PLEV_LEVEL_8K_3},
        {4096, 2160, 480, JXS_PLEV_LEVEL_8K_3},
        {8192, 6144, 60, JXS_PLEV_LEVEL_8K_3},
        {8192, 8192, 60, JXS_PLEV_LEVEL_8K_3},
        {7680, 4320, 120, JXS_PLEV_LEVEL_8K_3},
        {8192, 4320, 120, JXS_PLEV_LEVEL_8K_3},
        {10240, 7680, 30, JXS_PLEV_LEVEL_10K_1},
        {10240, 10240, 30, JXS_PLEV_LEVEL_10K_1},
        {10240, 4320, 60, JXS_PLEV_LEVEL_10K_1},
        {10240, 5400, 60, JXS_PLEV_LEVEL_10K_1},
    };
    // clang-format on
    for (const Case &c : cases) {
        EXPECT_EQ(level_of(c.width, c.height, c.fps), c.level) << c.width << "x" << c.height << "@" << c.fps;
    }
}

TEST(StreamLevelDerivation, SampleRateBoundIsInclusiveAndFractional) {
    /* 2k-1 Rs,max = 133693440 = 2048*1088*60: exactly at the bound stays in 2k-1. */
    EXPECT_EQ(level_of(2048, 1088, 60), JXS_PLEV_LEVEL_2K_1);
    EXPECT_EQ(level_of(2048, 1090, 60), JXS_PLEV_LEVEL_4K_1);
    /* 1920x1080 at 60000/1001 and 120000/1001. */
    EXPECT_EQ(level_of(1920, 1080, 60000, 1001), JXS_PLEV_LEVEL_2K_1);
    EXPECT_EQ(level_of(1920, 1080, 120000, 1001), JXS_PLEV_LEVEL_4K_1);
    /* Interlaced 1080i: each field is a 1920x540 picture, at 60 fields per second. */
    EXPECT_EQ(level_of(1920, 540, 60), JXS_PLEV_LEVEL_2K_1);
}

TEST(StreamLevelDerivation, WidthHeightAndPixelCountBounds) {
    EXPECT_EQ(level_of(1280, 2048, 1), JXS_PLEV_LEVEL_1K_1); /* Wmax and Lmax = 1280*2048 of 1k-1 */
    EXPECT_EQ(level_of(1281, 720, 1), JXS_PLEV_LEVEL_2K_1);  /* wider than 1k-1 */
    EXPECT_EQ(level_of(64, 5121, 1), JXS_PLEV_LEVEL_2K_1);   /* taller than 1k-1 */
    EXPECT_EQ(level_of(4096, 2176, 1), JXS_PLEV_LEVEL_4K_1); /* Lmax of 4k-1 = 4096*2176 */
    EXPECT_EQ(level_of(4096, 2177, 1), JXS_PLEV_LEVEL_4K_2);
    EXPECT_EQ(level_of(10240, 10240, 1), JXS_PLEV_LEVEL_10K_1);
    EXPECT_EQ(level_of(10241, 64, 1), JXS_PLEV_LEVEL_UNRESTRICTED);
    EXPECT_EQ(level_of(64, 40961, 1), JXS_PLEV_LEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, FiveKWidthUses8k1Not5k1) {
    /* 5k-1 (2024/Amd 1) is rejected by the edition 3 reference decoder; 8k-1 also bounds it. */
    EXPECT_EQ(level_of(5120, 2880, 30), JXS_PLEV_LEVEL_8K_1);
    EXPECT_EQ(level_of(4097, 2160, 30), JXS_PLEV_LEVEL_8K_1);
}

TEST(StreamLevelDerivation, RateBeyondEveryLevelIsUnrestricted) {
    EXPECT_EQ(level_of(1920, 1080, 4000), JXS_PLEV_LEVEL_UNRESTRICTED); /* 8.3 G/s > 8k-3 4.28 G/s */
    EXPECT_EQ(level_of(10240, 10240, 60), JXS_PLEV_LEVEL_UNRESTRICTED);
    /* Extreme frame rates: pixels (at most Lmax) * fps_numerator and Rs,max * fps_denominator both
     * stay within 64 bits. */
    EXPECT_EQ(level_of(64, 64, 0xFFFFFFFFu, 1), JXS_PLEV_LEVEL_UNRESTRICTED);
    EXPECT_EQ(level_of(64, 64, 1, 0xFFFFFFFFu), JXS_PLEV_LEVEL_1K_1);
    EXPECT_EQ(level_of(8192, 8192, 0xFFFFFFFFu, 0xFFFFFFFFu), JXS_PLEV_LEVEL_8K_2);
    EXPECT_EQ(level_of(10240, 10240, 1, 0xFFFFFFFFu), JXS_PLEV_LEVEL_10K_1);
}

TEST(StreamLevelDerivation, UnknownFrameRateIsUnrestricted) {
    EXPECT_EQ(level_of(640, 480, 0), JXS_PLEV_LEVEL_UNRESTRICTED);
    EXPECT_EQ(level_of(640, 480, 30, 0), JXS_PLEV_LEVEL_UNRESTRICTED);
    /* The sublevel does not depend on the frame rate. */
    StreamLevelParams params = {640, 480, 0, 1, frame_bytes_for(640, 480, 3, 1), 0};
    EXPECT_EQ(derive_stream_level_plev(&params, VERBOSE_NONE), (uint16_t)JXS_PLEV_SUBLEVEL_3BPP);
}

TEST(StreamLevelDerivation, SublevelBucketsPickSmallestBoundThatCovers) {
    EXPECT_EQ(sublevel_of(2, 1), JXS_PLEV_SUBLEVEL_2BPP);
    EXPECT_EQ(sublevel_of(5, 2), JXS_PLEV_SUBLEVEL_3BPP); // 2.5bpp -> 3bpp bucket
    EXPECT_EQ(sublevel_of(3, 1), JXS_PLEV_SUBLEVEL_3BPP);
    EXPECT_EQ(sublevel_of(301, 100), JXS_PLEV_SUBLEVEL_4BPP);
    EXPECT_EQ(sublevel_of(4, 1), JXS_PLEV_SUBLEVEL_4BPP);
    EXPECT_EQ(sublevel_of(6, 1), JXS_PLEV_SUBLEVEL_6BPP);
    EXPECT_EQ(sublevel_of(9, 1), JXS_PLEV_SUBLEVEL_9BPP);
    EXPECT_EQ(sublevel_of(12, 1), JXS_PLEV_SUBLEVEL_12BPP);
    EXPECT_EQ(sublevel_of(15, 1), JXS_PLEV_SUBLEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, SublevelUsesTheRoundedUpFrameSize) {
    /* 1922x1081 at 2 bpp: 4155364 bits round up to 519421 bytes, 4 bits over 2 bpp. The ISO/IEC
     * 21122-5 reference decoder rejects such a stream declared as Sublev2bpp. */
    StreamLevelParams params = {1922, 1081, 30, 1, frame_bytes_for(1922, 1081, 2, 1), 0};
    EXPECT_EQ(params.frame_bytes, 519421u);
    EXPECT_EQ(derive_stream_level_plev(&params, VERBOSE_NONE) & 0x00FF, JXS_PLEV_SUBLEVEL_3BPP);
    /* 1920x1080 at 2 bpp is a whole number of bytes and stays in Sublev2bpp. */
    params = {1920, 1080, 30, 1, frame_bytes_for(1920, 1080, 2, 1), 0};
    EXPECT_EQ(derive_stream_level_plev(&params, VERBOSE_NONE) & 0x00FF, JXS_PLEV_SUBLEVEL_2BPP);
    /* Unknown size: Unrestricted sublevel. */
    params = {1920, 1080, 30, 1, 0, 0};
    EXPECT_EQ(derive_stream_level_plev(&params, VERBOSE_NONE) & 0x00FF, JXS_PLEV_SUBLEVEL_UNRESTRICTED);
}

TEST(StreamLevelDerivation, LosslessForcesUnrestrictedSublevelRegardlessOfBpp) {
    // bpp_numerator/bpp_denominator are meaningless for lossless (Table A.9 NOTE 3); a low bpp
    // fraction here must not narrow the declared sublevel, since real lossless output far exceeds it.
    EXPECT_EQ(sublevel_of(2, 1, 1), JXS_PLEV_SUBLEVEL_UNRESTRICTED);
    EXPECT_EQ(sublevel_of(12, 1, 1), JXS_PLEV_SUBLEVEL_UNRESTRICTED);
    EXPECT_EQ(sublevel_of(12, 1, 0), JXS_PLEV_SUBLEVEL_12BPP);
}

/*
 * check_stream_level_plev(): level_plev_override is written verbatim, but problems are reported.
 */
TEST(StreamLevelOverrideCheck, LevelBounds) {
    /* BirdDog 2026-11-10 sample: 3840x2160 declared 2k-1. */
    StreamLevelParams params = {3840, 2160, 0, 1, frame_bytes_for(3840, 2160, 2, 1), 0};
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_3BPP, JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE),
              1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_4K_1 | JXS_PLEV_SUBLEVEL_3BPP, JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE),
              0u);
    /* With the frame rate known, 4k-1 is too low for 2160p60. */
    params.fps_numerator = 60;
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_4K_1 | JXS_PLEV_SUBLEVEL_3BPP, JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE),
              1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_4K_2 | JXS_PLEV_SUBLEVEL_3BPP, JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE),
              0u);
    /* 5k-1 is a valid override, checked with its own bounds. */
    params = {5120, 2880, 30, 1, frame_bytes_for(5120, 2880, 3, 1), 0};
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_5K_1 | JXS_PLEV_SUBLEVEL_3BPP, 0, &params, VERBOSE_NONE), 0u);
    /* 5k-1 bounds: 5121 wide, or 5120x2880 above its pixel rate. */
    params.width = 5121;
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_5K_1 | JXS_PLEV_SUBLEVEL_3BPP, 0, &params, VERBOSE_NONE), 1u);
    params.width = 5120;
    params.fps_numerator = 120;
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_5K_1 | JXS_PLEV_SUBLEVEL_3BPP, 0, &params, VERBOSE_NONE), 1u);
    params.fps_numerator = 30;
    /* Unknown level codes (incl. bit 15); the Unrestricted level always holds. */
    EXPECT_EQ(check_stream_level_plev(0x0C00, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(0x8000 | JXS_PLEV_LEVEL_2K_1, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_UNRESTRICTED, 0, &params, VERBOSE_NONE), 0u);
}

TEST(StreamLevelOverrideCheck, SublevelBounds) {
    StreamLevelParams params = {1922, 1081, 30, 1, frame_bytes_for(1922, 1081, 2, 1), 0};
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_2BPP, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_3BPP, 0, &params, VERBOSE_NONE), 0u);
    /* Unknown sublevel code, FBB-level bits, Full sublevel without and with a named profile. */
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | 0x05, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | 0x0300, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_FULL, 0, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_FULL, JXS_PPIH_MAIN_422_10, &params, VERBOSE_NONE),
              0u);
    /* 24 bpp is beyond Full of Main 422.10 (20 bpp). */
    params.frame_bytes = frame_bytes_for(1922, 1081, 24, 1);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_FULL, JXS_PPIH_MAIN_422_10, &params, VERBOSE_NONE),
              1u);
    /* Full with a profile this encoder cannot produce: the profile check reports it, not this one. */
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_FULL, 0xB340, &params, VERBOSE_NONE), 0u);
    /* Several problems at once are all counted: level too small, sublevel too small, FBB bits. */
    params.frame_bytes = frame_bytes_for(1922, 1081, 24, 1);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_1K_1 | 0x0300 | JXS_PLEV_SUBLEVEL_2BPP, 0, &params, VERBOSE_NONE), 3u);
    /* Lossless output is content-dependent: a named sublevel cannot be guaranteed. */
    params = {1920, 1080, 30, 1, 0, 1};
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_12BPP, JXS_PPIH_MLS_12, &params, VERBOSE_NONE), 1u);
    EXPECT_EQ(check_stream_level_plev(JXS_PLEV_LEVEL_2K_1, JXS_PPIH_MLS_12, &params, VERBOSE_NONE), 0u);
}

TEST(StreamLevelOverrideCheck, AutoSelectionAlwaysPassesItsOwnCheck) {
    const uint32_t sizes[][2] = {{640, 480}, {1920, 1080}, {1922, 1081}, {3840, 2160}, {5120, 2880}, {7680, 4320}, {10240, 4320}};
    const uint32_t rates[][2] = {{0, 1}, {25, 1}, {60000, 1001}, {120, 1}, {480, 1}};
    for (const auto &size : sizes) {
        for (const auto &rate : rates) {
            for (uint32_t bpp : {1, 2, 3, 5, 12, 20}) {
                StreamLevelParams params = {size[0], size[1], rate[0], rate[1], frame_bytes_for(size[0], size[1], bpp, 1), 0};
                const uint16_t plev = derive_stream_level_plev(&params, VERBOSE_NONE);
                EXPECT_EQ(check_stream_level_plev(plev, JXS_PPIH_HIGH_444_12, &params, VERBOSE_NONE), 0u)
                    << size[0] << "x" << size[1] << " plev " << std::hex << plev;
            }
        }
    }
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
    /* No frame rate given: Unrestricted level, sublevel from the 8 bpp target. */
    EXPECT_EQ(phc.hdr_Plev, (uint16_t)(JXS_PLEV_LEVEL_UNRESTRICTED | JXS_PLEV_SUBLEVEL_9BPP));
}

TEST(StreamProfileLevelEncodeIntegration, DefaultYuv420IsUnrestricted) {
    /* No profile covers 4:2:0 with raw-mode selection per packet (Rl=1). */
    std::vector<uint8_t> bitstream = encode_single_frame(256, 256, 8, COLOUR_FORMAT_PLANAR_YUV420, 6, 1);
    ASSERT_GT(bitstream.size(), 0u);
    picture_header_const_t phc = probe_header(bitstream);
    EXPECT_EQ(phc.hdr_Ppih, JXS_PPIH_UNRESTRICTED);
    EXPECT_EQ(phc.hdr_Plev, (uint16_t)(JXS_PLEV_LEVEL_UNRESTRICTED | JXS_PLEV_SUBLEVEL_6BPP));
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

TEST(StreamProfileLevelEncodeIntegration, FrameRateSelectsLevel) {
    /* 1280x720 at 60 fps is within 1k-1, at 120 fps it needs 2k-1 (Table A.8 examples). */
    std::vector<uint8_t> bitstream = encode_single_frame(1280, 720, 8, COLOUR_FORMAT_PLANAR_YUV422, 3, 1, 0, 0xFFFF, 0, 2, 60, 1);
    ASSERT_GT(bitstream.size(), 0u);
    EXPECT_EQ(probe_header(bitstream).hdr_Plev, (uint16_t)(JXS_PLEV_LEVEL_1K_1 | JXS_PLEV_SUBLEVEL_3BPP));
    bitstream = encode_single_frame(1280, 720, 8, COLOUR_FORMAT_PLANAR_YUV422, 3, 1, 0, 0xFFFF, 0, 2, 120000, 1001);
    ASSERT_GT(bitstream.size(), 0u);
    EXPECT_EQ(probe_header(bitstream).hdr_Plev, (uint16_t)(JXS_PLEV_LEVEL_2K_1 | JXS_PLEV_SUBLEVEL_3BPP));
}

TEST(StreamProfileLevelEncodeIntegration, ZeroFpsDenominatorIsRejected) {
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    EXPECT_EQ(enc.fps_numerator, 0u);
    EXPECT_EQ(enc.fps_denominator, 1u);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 256;
    enc.source_height = 256;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    enc.fps_numerator = 30;
    enc.fps_denominator = 0;
    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorBadParameter);
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
