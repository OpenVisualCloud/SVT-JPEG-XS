/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsImageBufferTools.h"
#include "SampleFramesData.h"

#include <string.h>

/*
 * Tests for svt_jpeg_xs_image_buffer_alloc() validation
 */

TEST(ImageBufferAlloc, NullConfigReturnsNull) {
    svt_jpeg_xs_image_buffer_t* buf = svt_jpeg_xs_image_buffer_alloc(NULL);
    ASSERT_EQ(buf, nullptr);
}

TEST(ImageBufferAlloc, ComponentsNumExceedsMaxReturnsNull) {
    svt_jpeg_xs_image_config_t config;
    memset(&config, 0, sizeof(config));
    config.components_num = MAX_COMPONENTS_NUM + 1;
    svt_jpeg_xs_image_buffer_t* buf = svt_jpeg_xs_image_buffer_alloc(&config);
    ASSERT_EQ(buf, nullptr);
}

TEST(ImageBufferAlloc, ComponentsNumMaxReturnsNonNull) {
    svt_jpeg_xs_image_config_t config;
    memset(&config, 0, sizeof(config));
    config.width = 16;
    config.height = 16;
    config.bit_depth = 8;
    config.format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    config.components_num = 3;
    for (int c = 0; c < 3; c++) {
        config.components[c].width = 16;
        config.components[c].height = 16;
        config.components[c].byte_size = 16 * 16;
    }
    svt_jpeg_xs_image_buffer_t* buf = svt_jpeg_xs_image_buffer_alloc(&config);
    ASSERT_NE(buf, nullptr);
    svt_jpeg_xs_image_buffer_free(buf);
}

TEST(ImageBufferAlloc, ZeroComponentsReturnsEmptyBuffer) {
    svt_jpeg_xs_image_config_t config;
    memset(&config, 0, sizeof(config));
    config.width = 16;
    config.height = 16;
    config.bit_depth = 8;
    config.format = COLOUR_FORMAT_PLANAR_YUV422;
    config.components_num = 0;
    svt_jpeg_xs_image_buffer_t* buf = svt_jpeg_xs_image_buffer_alloc(&config);
    // 0 components is valid (no loop iterations), buffer allocated but empty
    ASSERT_NE(buf, nullptr);
    svt_jpeg_xs_image_buffer_free(buf);
}

/*
 * Tests for svt_jpeg_xs_frame_pool_alloc() validation
 */

TEST(FramePoolAlloc, ComponentsNumExceedsMaxReturnsNull) {
    svt_jpeg_xs_image_config_t config;
    memset(&config, 0, sizeof(config));
    config.components_num = MAX_COMPONENTS_NUM + 1;
    svt_jpeg_xs_frame_pool_t* pool = svt_jpeg_xs_frame_pool_alloc(&config, 0, 1);
    ASSERT_EQ(pool, nullptr);
}

TEST(FramePoolAlloc, NullConfigWithBitstreamSucceeds) {
    svt_jpeg_xs_frame_pool_t* pool = svt_jpeg_xs_frame_pool_alloc(NULL, 1024, 2);
    ASSERT_NE(pool, nullptr);
    svt_jpeg_xs_frame_pool_free(pool);
}

TEST(FramePoolAlloc, NullConfigZeroBitstreamReturnsNull) {
    svt_jpeg_xs_frame_pool_t* pool = svt_jpeg_xs_frame_pool_alloc(NULL, 0, 1);
    ASSERT_EQ(pool, nullptr);
}

/*
 * Tests for decoder init validation and cleanup
 */

TEST(DecoderInit, InvalidPacketizationModeReturnsError) {
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.packetization_mode = 99;

    svt_jpeg_xs_image_config_t image_config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                                     SVT_JPEGXS_API_VER_MINOR,
                                                     &decoder,
                                                     Frame_Sample_1_16x16_8bit_422_bitstream,
                                                     Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                                     &image_config);
    ASSERT_NE(ret, SvtJxsErrorNone);
    // After failed init, private_ptr should be cleaned up
    ASSERT_EQ(decoder.private_ptr, nullptr);
}

TEST(DecoderInit, InvalidProxyModeReturnsError) {
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.packetization_mode = 0;
    decoder.proxy_mode = (proxy_mode_t)99;

    svt_jpeg_xs_image_config_t image_config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                                     SVT_JPEGXS_API_VER_MINOR,
                                                     &decoder,
                                                     Frame_Sample_1_16x16_8bit_422_bitstream,
                                                     Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                                     &image_config);
    ASSERT_NE(ret, SvtJxsErrorNone);
    ASSERT_EQ(decoder.private_ptr, nullptr);
}

TEST(DecoderInit, NullBitstreamReturnsError) {
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;

    svt_jpeg_xs_image_config_t image_config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &decoder, NULL, 0, &image_config);
    ASSERT_NE(ret, SvtJxsErrorNone);
}

TEST(DecoderInit, InvalidApiVersionReturnsError) {
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;

    svt_jpeg_xs_image_config_t image_config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(
        999, 999, &decoder, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size, &image_config);
    ASSERT_EQ(ret, SvtJxsErrorInvalidApiVersion);
}

/*
 * Tests for encoder init validation and cleanup
 */

TEST(EncoderInit, InvalidSlicePacketizationModeReturnsError) {
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 16;
    encoder.source_height = 16;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    encoder.bpp_numerator = 3;
    encoder.slice_packetization_mode = 99;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_NE(ret, SvtJxsErrorNone);
    // After failed init, private_ptr should be cleaned up
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, InvalidApiVersionReturnsError) {
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(999, 999, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorInvalidApiVersion);
}

TEST(EncoderInit, NullEncoderReturnsError) {
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, NULL);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
}
