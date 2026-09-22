/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Encode/decode smoke test for the encoder-side forward RCT (Cpih=1, enable_color_transform).
 * Not a bit-exact test - this codec is lossy-only, so exact pixel match isn't meaningful.
 * Checks: encode/decode both succeed, Cpih=1 was actually signaled in the produced
 * bitstream, and the round trip is visually plausible (PSNR above a generous threshold).
 * See TestMctEnc.cc for the bit-exact transform-correctness test. */

#include <cmath>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "Decoder.h"

namespace {

double compute_psnr(const uint8_t* a, const uint8_t* b, size_t count) {
    double sum_sq_err = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = (double)a[i] - (double)b[i];
        sum_sq_err += diff * diff;
    }
    if (sum_sq_err == 0.0) {
        return 99.0; //Identical - avoid log(0).
    }
    double mse = sum_sq_err / (double)count;
    return 10.0 * log10((255.0 * 255.0) / mse);
}

} // namespace

/* Parameterized over ndecomp_v so all three encoder DWT variants touched by the RCT prepass
 * (V0, V1, V2 in GcStageProcess.c) get exercised through the real encode/decode path, not
 * just whichever variant the default happens to pick. */
class ColorTransformDecompV : public ::testing::TestWithParam<uint32_t> {};

TEST_P(ColorTransformDecompV, EncodeDecodeSignalsCpihAndIsVisuallyPlausible) {
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint32_t ndecomp_v = GetParam();

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.cpu_profile = 0; //CPU_PROFILE_LOW_LATENCY
    enc.ndecomp_v = ndecomp_v;
    enc.ndecomp_h = 5;
    enc.bpp_numerator = 8;
    enc.bpp_denominator = 1;
    enc.enable_color_transform = 1;

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    //Non-degenerate gradient per component, distinct per component, so RCT actually mixes non-trivial values.
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        uint8_t* plane = (uint8_t*)in_buf->data_yuv[c];
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                plane[y * in_buf->stride[c] + x] = (uint8_t)((x * 4 + y * 2 + c * 37) & 0xFF);
            }
        }
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
    ASSERT_GT(bitstream.size(), 0u);

    svt_jpeg_xs_encoder_close(&enc);

    //Cpih=1 was actually signaled.
    picture_header_const_t picture_header_const;
    memset(&picture_header_const, 0, sizeof(picture_header_const));
    ASSERT_EQ(svt_jpeg_xs_decoder_probe(bitstream.data(), bitstream.size(), &picture_header_const, NULL, VERBOSE_NONE),
             SvtJxsErrorNone);
    EXPECT_EQ(picture_header_const.hdr_Cpih, 1);

    //Full decode, visually plausible round trip.
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    svt_jpeg_xs_image_config_t dec_image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                       SVT_JPEGXS_API_VER_MINOR,
                                       &dec,
                                       bitstream.data(),
                                       bitstream.size(),
                                       &dec_image_config),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* out_img = svt_jpeg_xs_image_buffer_alloc(&dec_image_config);
    ASSERT_NE(out_img, nullptr);

    svt_jpeg_xs_bitstream_buffer_t in_bitstream;
    in_bitstream.buffer = bitstream.data();
    in_bitstream.allocation_size = (uint32_t)bitstream.size();
    in_bitstream.used_size = (uint32_t)bitstream.size();

    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream = in_bitstream;
    dec_input.image = *out_img;
    ASSERT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t dec_output;
    ASSERT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/), SvtJxsErrorNone);

    for (int32_t c = 0; c < image_config.components_num; ++c) {
        double psnr = compute_psnr((const uint8_t*)in_buf->data_yuv[c], (const uint8_t*)dec_output.image.data_yuv[c],
                                   (size_t)in_buf->stride[c] * height);
        EXPECT_GT(psnr, 20.0) << "component " << c << " PSNR too low: " << psnr << " dB";
    }

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

INSTANTIATE_TEST_SUITE_P(V0V1V2, ColorTransformDecompV, ::testing::Values(0u, 1u, 2u));

/* The RCT prepass caches NLT-scaled+RCT'd lines across precinct_calculate_data calls, keyed
 * by absolute line index modulo a 13-slot ring, to avoid recomputing lines that reappear in
 * the sliding window. That cache is invalidated on frame_number change (see
 * mct_forward_rct_prepass_planar) - otherwise frame N+1 could reuse frame N's stale RCT'd
 * data for the same line index, since every frame has the same width/height/decom_v and
 * therefore the same absolute line index range.
 *
 * height is deliberately <= 13 (the ring size): with a taller frame, every ring residue is
 * naturally revisited several times within one frame (each visit correctly overwrites the
 * previous), which "self-heals" the cache before any cross-frame collision could matter, so
 * a bug here wouldn't be caught. With height <= 13, each residue is written exactly once per
 * frame, so a stale leftover from the previous frame is never overwritten until this
 * invalidation-on-frame-change logic runs - this is confirmed empirically (not just by
 * inspection): temporarily disabling the frame-number check makes this exact test fail. */
TEST(ColorTransform, MultipleFramesWithDifferentContentEachDecodePlausibly) {
    const uint32_t width = 64;
    const uint32_t height = 8;
    const int kNumFrames = 3;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.cpu_profile = 0; //CPU_PROFILE_LOW_LATENCY
    enc.ndecomp_v = 2;   //Deepest sliding window (13 slots) - most likely to expose cache bugs.
    enc.ndecomp_h = 5;
    enc.bpp_numerator = 8;
    enc.bpp_denominator = 1;
    enc.enable_color_transform = 1;

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    for (int frame = 0; frame < kNumFrames; frame++) {
        svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
        ASSERT_NE(in_buf, nullptr);
        //Different pattern per frame (frame index folded in), so a stale cross-frame cache hit
        //would produce a decode that doesn't match THIS frame's actual content.
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            uint8_t* plane = (uint8_t*)in_buf->data_yuv[c];
            for (uint32_t y = 0; y < height; y++) {
                for (uint32_t x = 0; x < width; x++) {
                    plane[y * in_buf->stride[c] + x] = (uint8_t)((x * 4 + y * 2 + c * 37 + frame * 61) & 0xFF);
                }
            }
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
        ASSERT_GT(bitstream.size(), 0u) << "frame " << frame;

        svt_jpeg_xs_decoder_api_t dec;
        memset(&dec, 0, sizeof(dec));
        svt_jpeg_xs_image_config_t dec_image_config;
        ASSERT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                           SVT_JPEGXS_API_VER_MINOR,
                                           &dec,
                                           bitstream.data(),
                                           bitstream.size(),
                                           &dec_image_config),
                 SvtJxsErrorNone)
            << "frame " << frame;

        svt_jpeg_xs_image_buffer_t* out_img = svt_jpeg_xs_image_buffer_alloc(&dec_image_config);
        ASSERT_NE(out_img, nullptr);

        svt_jpeg_xs_bitstream_buffer_t in_bitstream;
        in_bitstream.buffer = bitstream.data();
        in_bitstream.allocation_size = (uint32_t)bitstream.size();
        in_bitstream.used_size = (uint32_t)bitstream.size();

        svt_jpeg_xs_frame_t dec_input;
        dec_input.bitstream = in_bitstream;
        dec_input.image = *out_img;
        ASSERT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone) << "frame " << frame;

        svt_jpeg_xs_frame_t dec_output;
        ASSERT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/), SvtJxsErrorNone) << "frame " << frame;

        for (int32_t c = 0; c < image_config.components_num; ++c) {
            double psnr = compute_psnr((const uint8_t*)in_buf->data_yuv[c], (const uint8_t*)dec_output.image.data_yuv[c],
                                       (size_t)in_buf->stride[c] * height);
            EXPECT_GT(psnr, 20.0) << "frame " << frame << " component " << c << " PSNR too low: " << psnr << " dB";
        }

        svt_jpeg_xs_decoder_close(&dec);
        svt_jpeg_xs_image_buffer_free(out_img);
        svt_jpeg_xs_image_buffer_free(in_buf);
        free(out_buf.buffer);
    }

    svt_jpeg_xs_encoder_close(&enc);
}
