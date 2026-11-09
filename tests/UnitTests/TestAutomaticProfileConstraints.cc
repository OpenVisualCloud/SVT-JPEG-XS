/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "gtest/gtest.h"
#include "SvtJpegxsEnc.h"
#include "EncHandle.h"
#include "ProfileLevel.h"

static svt_jpeg_xs_encoder_api_t profile_config(ColourFormat_t format, uint8_t depth,
                                                uint8_t vertical, bool lossless, bool raw) {
    svt_jpeg_xs_encoder_api_t enc = {};
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);
    enc.source_width = 128; enc.source_height = 48;
    enc.input_bit_depth = depth; enc.colour_format = format;
    enc.ndecomp_v = vertical; enc.ndecomp_h = 5; enc.slice_height = 16;
    enc.lossless_enable = lossless; enc.coding_raw_disable = !raw;
    enc.bpp_numerator = 8; enc.bpp_denominator = 1;
    enc.threads_num = 1; enc.use_cpu_flags = 0; enc.verbose = VERBOSE_NONE;
    return enc;
}

TEST(AutomaticProfileConstraints, EveryAcceptedPrecisionAndSampling) {
    const ColourFormat_t formats[] = { COLOUR_FORMAT_PLANAR_YUV400, COLOUR_FORMAT_PLANAR_YUV420,
        COLOUR_FORMAT_PLANAR_YUV422, COLOUR_FORMAT_PLANAR_YUV444_OR_RGB,
        COLOUR_FORMAT_PACKED_YUV444_OR_RGB, COLOUR_FORMAT_PLANAR_4_COMPONENTS,
        COLOUR_FORMAT_PLANAR_YUV422_ALPHA };
    for (auto f : formats) for (uint8_t depth = 8; depth <= 14; ++depth)
    for (uint8_t v = 0; v <= 2; ++v) for (bool lossless : {false,true}) for (bool raw : {false,true}) {
        auto enc = profile_config(f,depth,v,lossless,raw);
        SCOPED_TRACE(::testing::Message() << "format=" << f << " depth=" << +depth << " v=" << +v
                                         << " lossless=" << lossless << " raw=" << raw);
        auto ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,&enc);
        if (f == COLOUR_FORMAT_PLANAR_YUV400 || (f == COLOUR_FORMAT_PLANAR_YUV420 && v == 0)) {
            EXPECT_EQ(ret,SvtJxsErrorBadParameter);
        } else {
            ASSERT_EQ(ret,SvtJxsErrorNone);
            const auto *prv = static_cast<svt_jpeg_xs_encoder_api_prv_t *>(enc.private_ptr);
            uint16_t expected = 0; // Unrestricted; no false named-profile claim.
            if (depth == 8 || depth == 10 || depth == 12) {
                if (lossless && !raw) expected = JXS_PPIH_MLS_12;
                if (!lossless && !raw) {
                    if (f == COLOUR_FORMAT_PLANAR_YUV420)
                        expected = v == 2 ? JXS_PPIH_HIGH_420_12 : JXS_PPIH_MAIN_420_12;
                    else if (f == COLOUR_FORMAT_PLANAR_4_COMPONENTS || f == COLOUR_FORMAT_PLANAR_YUV422_ALPHA)
                        expected = v == 2 ? JXS_PPIH_HIGH_4444_12 : JXS_PPIH_MAIN_4444_12;
                    else if (v == 2) expected = JXS_PPIH_HIGH_444_12;
                    else if ((f == COLOUR_FORMAT_PLANAR_YUV422 || f == COLOUR_FORMAT_PLANAR_YUV400) && depth <= 10)
                        expected = JXS_PPIH_MAIN_422_10;
                    else expected = JXS_PPIH_MAIN_444_12;
                }
            }
            EXPECT_EQ(prv->enc_common.hdr_Ppih,expected);
        }
        svt_jpeg_xs_encoder_close(&enc);
    }
}

TEST(AutomaticProfileConstraints, NonstandardSliceAndExplicitOverride) {
    auto enc = profile_config(COLOUR_FORMAT_PLANAR_YUV422,8,2,false,false);
    enc.slice_height = 32;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,&enc), SvtJxsErrorNone);
    EXPECT_EQ(static_cast<svt_jpeg_xs_encoder_api_prv_t *>(enc.private_ptr)->enc_common.hdr_Ppih,0);
    svt_jpeg_xs_encoder_close(&enc);
    enc = profile_config(COLOUR_FORMAT_PLANAR_YUV422,14,2,false,true);
    enc.profile_ppih_override = JXS_PPIH_MAIN_422_10;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,&enc), SvtJxsErrorNone);
    EXPECT_EQ(static_cast<svt_jpeg_xs_encoder_api_prv_t *>(enc.private_ptr)->enc_common.hdr_Ppih,JXS_PPIH_MAIN_422_10);
    svt_jpeg_xs_encoder_close(&enc);
}

TEST(AutomaticProfileConstraints, LongHeaderFallsBackToUnrestricted) {
    for (uint32_t width : {10916u,10924u}) {
        auto enc=profile_config(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB,8,1,false,false);
        enc.source_width=width;
        ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,&enc),SvtJxsErrorNone);
        const auto *prv=static_cast<svt_jpeg_xs_encoder_api_prv_t *>(enc.private_ptr);
        EXPECT_EQ(prv->enc_common.hdr_Ppih,width==10916 ? JXS_PPIH_MAIN_444_12 : 0);
        svt_jpeg_xs_encoder_close(&enc);
    }
}
