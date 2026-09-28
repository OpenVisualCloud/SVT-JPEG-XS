/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Encode/decode correctness tests for true lossless coding (lossless_enable, Fq=0).
 * Unlike TestColorTransform.cc's PSNR-based "visually plausible" check, lossless promises
 * bit-exact reproduction, so these compare decoded bytes against the source directly. */

#include <cstring>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "Decoder.h"

namespace {

/* Deterministic pseudo-random fill (not just a smooth gradient) - a smooth pattern would never
 * exercise the high-magnitude / high-frequency wavelet coefficients that stress the Fq=0 coding
 * path (no quantization, no rounding shift) the hardest. */
void fill_noisy(uint8_t* plane, uint32_t width, uint32_t height, uint32_t stride, uint8_t bit_depth,
                uint32_t component_seed) {
    uint16_t mask = (uint16_t)((1u << bit_depth) - 1);
    uint32_t state = 0x9E3779B9u ^ (component_seed * 0x85EBCA6Bu);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            state = state * 1664525u + 1013904223u; //LCG, deterministic across runs/platforms.
            uint16_t val = (uint16_t)(state >> 16) & mask;
            if (bit_depth <= 8) {
                plane[y * stride + x] = (uint8_t)val;
            }
            else {
                ((uint16_t*)plane)[y * stride + x] = val;
            }
        }
    }
}

void fill_flat(uint8_t* plane, uint32_t width, uint32_t height, uint32_t stride, uint8_t bit_depth, uint16_t value) {
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            if (bit_depth <= 8) {
                plane[y * stride + x] = (uint8_t)value;
            }
            else {
                ((uint16_t*)plane)[y * stride + x] = value;
            }
        }
    }
}

bool planes_bit_exact(const svt_jpeg_xs_image_buffer_t* a, const svt_jpeg_xs_image_buffer_t* b,
                      const svt_jpeg_xs_image_config_t& cfg, uint8_t bit_depth) {
    uint32_t pixel_size = bit_depth <= 8 ? 1 : 2;
    for (int32_t c = 0; c < cfg.components_num; ++c) {
        uint32_t width = cfg.components[c].width;
        uint32_t height = cfg.components[c].height;
        for (uint32_t y = 0; y < height; y++) {
            const uint8_t* row_a = (const uint8_t*)a->data_yuv[c] + (size_t)y * a->stride[c] * pixel_size;
            const uint8_t* row_b = (const uint8_t*)b->data_yuv[c] + (size_t)y * b->stride[c] * pixel_size;
            if (memcmp(row_a, row_b, (size_t)width * pixel_size) != 0) {
                return false;
            }
        }
    }
    return true;
}

/* Lossless frame size is data-dependent, so the encoder patches the real compacted size into
 * the picture header's Lcod field after encoding. File-based consumers (SvtJpegxsDecApp, ffmpeg
 * demuxing) split frames by Lcod, so it must match the returned used_size exactly - the in-memory
 * decode used by these tests would not notice a stale (worst-case) Lcod on its own. */
void expect_lcod_matches_used_size(const std::vector<uint8_t>& bitstream) {
    uint32_t frame_size = 0;
    EXPECT_EQ(svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
                  bitstream.data(), bitstream.size(), NULL, &frame_size, 1 /*fast_search: trust Lcod*/, proxy_mode_full),
              SvtJxsErrorNone);
    EXPECT_EQ(frame_size, bitstream.size());
}

} // namespace

/* Parameterized over (bit_depth, colour_format): sweeps the full supported bit-depth range
 * (8-14) including the 14-bit case, which has the least headroom against TRUNCATION_MAX before
 * the wavelet transform's coefficient growth could in principle overflow the 16-bit coefficient
 * storage now that Fq=0 removes the compression right-shift that normally provides that margin. */
struct LosslessCase {
    uint8_t bit_depth;
    ColourFormat_t format;
};

class LosslessDepthFormat : public ::testing::TestWithParam<LosslessCase> {};

TEST_P(LosslessDepthFormat, EncodeDecodeBitExact) {
    const uint32_t width = 64;
    const uint32_t height = 64;
    const LosslessCase param = GetParam();
    const uint8_t bit_depth = param.bit_depth;
    const ColourFormat_t format = param.format;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = format;
    enc.lossless_enable = 1;
    //Leave ndecomp_v/ndecomp_h at their (deepest) defaults - the margin concern above is worst at
    //maximum decomposition depth, not at some reduced depth.

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        fill_noisy((uint8_t*)in_buf->data_yuv[c], image_config.components[c].width, image_config.components[c].height,
                   in_buf->stride[c], bit_depth, (uint32_t)c);
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
    expect_lcod_matches_used_size(bitstream);

    svt_jpeg_xs_encoder_close(&enc);

    //Fq=0 / Bw=bit_depth were actually signaled (ISO/IEC 21122-1 Table A.8 lossless row).
    picture_header_const_t picture_header_const;
    picture_header_dynamic_t picture_header_dynamic;
    memset(&picture_header_const, 0, sizeof(picture_header_const));
    memset(&picture_header_dynamic, 0, sizeof(picture_header_dynamic));
    ASSERT_EQ(svt_jpeg_xs_decoder_probe(
                  bitstream.data(), bitstream.size(), &picture_header_const, &picture_header_dynamic, VERBOSE_NONE),
             SvtJxsErrorNone);
    EXPECT_EQ(picture_header_dynamic.hdr_Fq, 0);
    EXPECT_EQ(picture_header_dynamic.hdr_Bw, bit_depth);

    //Full decode, bit-exact round trip.
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

    EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, bit_depth)) << "bit_depth=" << (int)bit_depth;

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

INSTANTIATE_TEST_SUITE_P(BitDepthSweep, LosslessDepthFormat,
                        ::testing::Values(LosslessCase{8, COLOUR_FORMAT_PLANAR_YUV420},
                                          LosslessCase{10, COLOUR_FORMAT_PLANAR_YUV422},
                                          LosslessCase{12, COLOUR_FORMAT_PLANAR_YUV444_OR_RGB},
                                          LosslessCase{14, COLOUR_FORMAT_PLANAR_YUV420}));

/* Multi-frame: proves the PictureControlSet pool (slice_real_bytes_arr etc.) doesn't leak state
 * between frames sharing the same encoder instance, and that flat frames really do end up smaller
 * than noisy ones (the actual "true variable-size" claim), not just bit-exact. */
TEST(Lossless, MultipleFramesDifferentCompressibilityBitExactAndVariableSize) {
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint8_t bit_depth = 10;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    enc.lossless_enable = 1;

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    //flat, noisy, flat - so a leftover-from-previous-frame bug would show up as frame 2 (flat)
    //incorrectly inheriting frame 1's (noisy) size, or any frame failing to round-trip.
    const bool kNoisy[3] = {false, true, false};
    uint32_t used_size[3] = {0, 0, 0};

    for (int frame = 0; frame < 3; frame++) {
        svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
        ASSERT_NE(in_buf, nullptr) << "frame " << frame;
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            if (kNoisy[frame]) {
                fill_noisy((uint8_t*)in_buf->data_yuv[c], image_config.components[c].width,
                          image_config.components[c].height, in_buf->stride[c], bit_depth, (uint32_t)(c + frame * 7));
            }
            else {
                fill_flat((uint8_t*)in_buf->data_yuv[c], image_config.components[c].width,
                         image_config.components[c].height, in_buf->stride[c], bit_depth, 128);
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
        ASSERT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/), SvtJxsErrorNone) << "frame " << frame;

        svt_jpeg_xs_frame_t enc_output;
        memset(&enc_output, 0, sizeof(enc_output));
        ASSERT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/), SvtJxsErrorNone) << "frame " << frame;

        std::vector<uint8_t> bitstream(enc_output.bitstream.buffer,
                                       enc_output.bitstream.buffer + enc_output.bitstream.used_size);
        ASSERT_GT(bitstream.size(), 0u) << "frame " << frame;
        used_size[frame] = (uint32_t)bitstream.size();
        expect_lcod_matches_used_size(bitstream);

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

        EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, bit_depth)) << "frame " << frame;

        svt_jpeg_xs_decoder_close(&dec);
        svt_jpeg_xs_image_buffer_free(out_img);
        svt_jpeg_xs_image_buffer_free(in_buf);
        free(out_buf.buffer);
    }

    svt_jpeg_xs_encoder_close(&enc);

    //The actual "true variable-size" claim: flat frames end up smaller than the noisy one, and
    //the two flat frames (frame 0 and frame 2) don't inherit any leftover noisy-frame sizing.
    EXPECT_LT(used_size[0], used_size[1]) << "flat frame 0 should be smaller than noisy frame 1";
    EXPECT_LT(used_size[2], used_size[1]) << "flat frame 2 should be smaller than noisy frame 1";
}

/* Regression test for a real heap buffer overflow (confirmed via a Debug build: SIGABRT on
 * BitstreamWriter.c's bounds assert; silently corrupts memory in a Release/NDEBUG build instead).
 *
 * Root cause: EncHandle.c's slice_sizes[] distribution (shared with, and unchanged from, the
 * pre-existing CBR code) splits the frame's total byte budget EVENLY BY PRECINCT COUNT, not by
 * each precinct's actual sample count. When the image height isn't a multiple of the precinct
 * height (1 << ndecomp_v), the trailing precinct row has fewer samples than the others. Sizing
 * the lossless worst-case budget against the *true* (smaller-weighted) total let that discount
 * get spread evenly across every full-size precinct too, under-allocating each of them below its
 * own real 2-bytes/sample worst case - at high bit depth, where real coefficient magnitudes are
 * close enough to that worst case for the shortfall to matter, the encoder then wrote past its
 * pre-reserved slice window. Fixed by rounding height up to a precinct-height multiple only for
 * the purpose of sizing that budget (EncHandle.c's lossless_total_samples()), so the even-by-count
 * split always assumes uniform, full-size precincts and never shortchanges any of them.
 *
 * height=5 with slice_height=4 forces exactly this: a 4-line full slice followed by a 1-line
 * trailing slice, each getting an equal share of a budget sized for 5 true lines - a maximal,
 * easily reproducible case of the dilution above. 14-bit is the bit depth with the least
 * (empirically, none) margin to spare once Fq=0 removes the usual compression right-shift. */
TEST(Lossless, NonPrecinctAlignedHeightDoesNotOverflowSliceWindow) {
    const uint32_t width = 2048;
    const uint32_t height = 5;
    const uint8_t bit_depth = 14;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc.lossless_enable = 1;
    enc.slice_height = 4; //Forces a 4-line slice followed by a 1-line trailing slice.
    enc.ndecomp_h = 3;

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        fill_noisy((uint8_t*)in_buf->data_yuv[c], image_config.components[c].width, image_config.components[c].height,
                   in_buf->stride[c], bit_depth, (uint32_t)c);
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
    expect_lcod_matches_used_size(bitstream);

    svt_jpeg_xs_encoder_close(&enc);

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

    EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, bit_depth));

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

/* Regression coverage for a residual theoretical concern: image_shift_c() (NltEnc.c) asserts every
 * packed coefficient's magnitude fits TRUNCATION_MAX (15 bits) - a debug-only assert, so a release
 * build would silently truncate instead of failing if a coefficient ever exceeded it. Fq=0
 * (lossless) removes the compression right-shift that normally provides headroom against this
 * ceiling, and 14-bit is the bit depth with the least headroom left (worst case ~1-2 bits of
 * wavelet growth budget at max decomposition, going by hdr_Bw's margin against Bw=20/Fq=8's proven
 * 4-bit-growth-tolerant baseline). Reversible 5/3 wavelet lifting only grows the dynamic range by a
 * small, bounded amount regardless of input (a well-known property of the filter, and why ISO/IEC
 * 21122-1 Table A.8 legalizes Bw=B[0]/Fq=0 as lossless at all), so this should hold by construction
 * - but pseudo-random content (as used by LosslessDepthFormat above) doesn't specifically try to
 * maximize that growth. A checkerboard is the classic worst case for transform coding: alternating
 * extremes at the highest representable frequency, zero redundancy for the lifting steps to
 * exploit. Passing here is added confidence, not a mathematical proof - if this ever starts
 * failing, treat it as a real signal that the growth bound assumption above needs re-examining. */
TEST(Lossless, AdversarialCheckerboardAtMaxDecompositionDoesNotOverflowCoefficients) {
    const uint32_t width = 256;
    const uint32_t height = 64;
    const uint8_t bit_depth = 14;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.lossless_enable = 1;
    //Leave ndecomp_v/ndecomp_h at their (deepest) defaults - maximum decomposition depth is where
    //accumulated wavelet growth is largest.

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    const uint16_t max_val = (uint16_t)((1u << bit_depth) - 1);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        uint32_t comp_width = image_config.components[c].width;
        uint32_t comp_height = image_config.components[c].height;
        uint32_t stride = in_buf->stride[c];
        uint16_t* plane = (uint16_t*)in_buf->data_yuv[c];
        for (uint32_t y = 0; y < comp_height; y++) {
            for (uint32_t x = 0; x < comp_width; x++) {
                plane[y * stride + x] = ((x + y) % 2 == 0) ? max_val : 0;
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
    expect_lcod_matches_used_size(bitstream);

    svt_jpeg_xs_encoder_close(&enc);

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

    EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, bit_depth));

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

/* Regression test for a real heap-buffer-overflow: coding_raw_disable=1 (hdr_Rl=0, mandatory for the
 * MLS.12 lossless profile) means RateControl.c:803's raw-vs-normal comparison can never select the
 * fixed-cost raw GCLI fallback, only the unary-coded "normal" path, which can cost up to
 * TRUNCATION_MAX+1=16 bits per GROUP_SIZE=4-sample group (4 bits/sample) - double the 2-bits/sample
 * margin lossless_worst_case_bytes_per_frame() used to budget regardless of hdr_Rl. Confirmed via
 * this exact repro (64x64, 14-bit, YUV444, pseudo-random noise, exact-size output buffer - no
 * padding, so the shortfall isn't masked): a Debug build aborted on
 * align_bitstream_writer_to_next_byte()'s assert, and a Release+ASan build showed
 * "lossless slice N overflowed its window" with BitstreamWriter.c's unchecked mem[]=... writes
 * having already landed past the slice's allotted region. Fixed by widening the metadata margin to
 * 4 bits/sample when coding_raw_disable is set, plus adding real (non-assert, non-NDEBUG-only)
 * bounds checks to BitstreamWriter.c/.h so a future misestimate fails safely instead of corrupting
 * memory. */
TEST(Lossless, RawDisableAdversarialNoiseDoesNotExceedWorstCaseBound) {
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint8_t bit_depth = 14;

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = height;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.lossless_enable = 1;
    enc.coding_raw_disable = 1;

    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        fill_noisy((uint8_t*)in_buf->data_yuv[c], image_config.components[c].width, image_config.components[c].height,
                   in_buf->stride[c], bit_depth, (uint32_t)c);
    }

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    out_buf.allocation_size = bytes_per_frame; //EXACT bound - no padding.
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
    ASSERT_LE(enc_output.bitstream.used_size, bytes_per_frame);

    std::vector<uint8_t> bitstream(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);
    ASSERT_GT(bitstream.size(), 0u);
    expect_lcod_matches_used_size(bitstream);

    svt_jpeg_xs_encoder_close(&enc);

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

    EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, bit_depth));

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);
}

/* Narrow bands pay for the padding coefficients of their last code group (a 4-wide picture with
 * ndecomp_h=2 codes 12 coefficients for every 4 samples per line), and ndecomp_v=0 packs many small
 * precincts, each with its own precinct and packet headers, into one slice. The worst-case bound
 * used to be charged per sample with a flat header margin per slice, so these configurations
 * overflowed the slice window and wrote past the end of the output buffer. */
struct LosslessNarrowCase {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint32_t ndecomp_h;
    uint32_t ndecomp_v;
    uint8_t raw_disable; //0: raw GCLI fallback allowed, bound charges 4 bits of GCLI per group
};

class LosslessNarrow : public ::testing::TestWithParam<LosslessNarrowCase> {};

TEST_P(LosslessNarrow, NoiseFitsWorstCaseBoundAndIsBitExact) {
    const LosslessNarrowCase p = GetParam();

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = p.width;
    enc.source_height = p.height;
    enc.input_bit_depth = p.bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.ndecomp_h = p.ndecomp_h;
    enc.ndecomp_v = p.ndecomp_v;
    enc.lossless_enable = 1;
    enc.coding_raw_disable = p.raw_disable;

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
                   p.bit_depth,
                   (uint32_t)c);
    }

    std::vector<uint8_t> out(bytes_per_frame); //Exact bound, no padding
    svt_jpeg_xs_frame_t enc_input;
    enc_input.bitstream.buffer = out.data();
    enc_input.bitstream.allocation_size = bytes_per_frame;
    enc_input.bitstream.used_size = 0;
    enc_input.image = *in_buf;
    enc_input.user_prv_ctx_ptr = NULL;
    ASSERT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t enc_output;
    memset(&enc_output, 0, sizeof(enc_output));
    ASSERT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/), SvtJxsErrorNone);
    ASSERT_LE(enc_output.bitstream.used_size, bytes_per_frame);
    std::vector<uint8_t> bitstream(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);
    svt_jpeg_xs_encoder_close(&enc);
    expect_lcod_matches_used_size(bitstream);

    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    svt_jpeg_xs_image_config_t dec_image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
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
    EXPECT_TRUE(planes_bit_exact(in_buf, &dec_output.image, image_config, p.bit_depth));

    svt_jpeg_xs_decoder_close(&dec);
    svt_jpeg_xs_image_buffer_free(out_img);
    svt_jpeg_xs_image_buffer_free(in_buf);
}

INSTANTIATE_TEST_SUITE_P(PaddedGroupsAndSmallPrecincts, LosslessNarrow,
                         ::testing::Values(LosslessNarrowCase{4, 16, 14, 2, 1, 1},
                                           LosslessNarrowCase{4, 16, 12, 2, 1, 1},
                                           LosslessNarrowCase{6, 16, 14, 2, 1, 1},
                                           LosslessNarrowCase{4, 64, 14, 2, 0, 1},
                                           LosslessNarrowCase{9, 33, 14, 3, 1, 1},
                                           LosslessNarrowCase{4, 16, 14, 2, 1, 0},
                                           LosslessNarrowCase{4, 64, 14, 2, 0, 0},
                                           LosslessNarrowCase{64, 64, 14, 5, 2, 0}));
