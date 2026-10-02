/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Multithreaded decode of streams whose last slice is shorter than 2 precinct lines.
 *
 * With decom_v > 0, more than one universal thread and more than 2 precincts per slice the decoder
 * runs in sync_slices_idwt mode: each slice thread finishes the vertical IDWT across the boundary
 * with the next slice itself, which needs the next slice's first 2 precinct lines and waits on that
 * slice's map_slices_decode_done flag. A slice raised that flag only after unpacking its 2nd line,
 * so a last slice holding a single precinct line never raised it and the frame deadlocked.
 *
 * Lossless coding makes the round trip bit-exact, so a wrong overlap is caught as well as a hang.
 * get_frame() is polled with a deadline instead of blocking, so a deadlock fails the test instead
 * of hanging the whole test binary. */

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "Decoder.h"

namespace {

void fill_noisy(uint8_t* plane, uint32_t width, uint32_t height, uint32_t stride, uint8_t bit_depth, uint32_t component_seed) {
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

} // namespace

struct SliceSyncCase {
    uint32_t height;
    uint32_t ndecomp_v;
    uint32_t slice_height; //In luma lines, a multiple of the precinct height (1 << ndecomp_v).
    uint8_t bit_depth;
    uint8_t rct;
};

std::ostream& operator<<(std::ostream& os, const SliceSyncCase& c) {
    return os << "height=" << c.height << " ndecomp_v=" << c.ndecomp_v << " slice_height=" << c.slice_height
              << " bit_depth=" << (int)c.bit_depth << " rct=" << (int)c.rct;
}

class SliceSyncLastSlice : public ::testing::TestWithParam<SliceSyncCase> {};

TEST_P(SliceSyncLastSlice, MultithreadedDecodeBitExact) {
    const SliceSyncCase param = GetParam();
    const uint32_t width = 64;
    //8 threads leave 6 universal (slice) threads - sync_slices_idwt needs more than one.
    const uint32_t threads_num = 8;
    const uint32_t frames_num = 3;
    const auto timeout = std::chrono::seconds(20);

    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = param.height;
    enc.input_bit_depth = param.bit_depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    enc.lossless_enable = 1;
    enc.enable_color_transform = param.rct;
    enc.ndecomp_v = param.ndecomp_v;
    enc.slice_height = param.slice_height;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
              SvtJxsErrorNone);

    std::vector<svt_jpeg_xs_image_buffer_t*> in_bufs;
    std::vector<std::vector<uint8_t>> bitstreams;
    for (uint32_t frame = 0; frame < frames_num; frame++) {
        svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
        ASSERT_NE(in_buf, nullptr);
        in_bufs.push_back(in_buf);
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            fill_noisy((uint8_t*)in_buf->data_yuv[c],
                       image_config.components[c].width,
                       image_config.components[c].height,
                       in_buf->stride[c],
                       param.bit_depth,
                       (uint32_t)(c + frame * 7));
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
        bitstreams.emplace_back(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);
        free(out_buf.buffer);
    }
    svt_jpeg_xs_encoder_close(&enc);

    //The stream really has the slice layout under test.
    picture_header_const_t picture_header_const;
    picture_header_dynamic_t picture_header_dynamic;
    memset(&picture_header_const, 0, sizeof(picture_header_const));
    memset(&picture_header_dynamic, 0, sizeof(picture_header_dynamic));
    ASSERT_EQ(svt_jpeg_xs_decoder_probe(
                  bitstreams[0].data(), bitstreams[0].size(), &picture_header_const, &picture_header_dynamic, VERBOSE_NONE),
              SvtJxsErrorNone);
    const uint32_t precinct_height = 1u << param.ndecomp_v;
    const uint32_t precinct_lines = (param.height + precinct_height - 1) / precinct_height;
    EXPECT_EQ(picture_header_const.hdr_decom_v, (int32_t)param.ndecomp_v);
    EXPECT_EQ(picture_header_const.hdr_Hsl, param.slice_height / precinct_height);
    EXPECT_EQ(picture_header_const.hdr_Cpih, param.rct);
    ASSERT_GT(picture_header_const.hdr_Hsl, 2u); //Otherwise sync_slices_idwt is off.

    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.threads_num = threads_num;
    svt_jpeg_xs_image_config_t dec_image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                       SVT_JPEGXS_API_VER_MINOR,
                                       &dec,
                                       bitstreams[0].data(),
                                       bitstreams[0].size(),
                                       &dec_image_config),
              SvtJxsErrorNone);

    std::vector<svt_jpeg_xs_image_buffer_t*> out_imgs;
    for (uint32_t frame = 0; frame < frames_num; frame++) {
        svt_jpeg_xs_image_buffer_t* out_img = svt_jpeg_xs_image_buffer_alloc(&dec_image_config);
        ASSERT_NE(out_img, nullptr);
        out_imgs.push_back(out_img);

        svt_jpeg_xs_frame_t dec_input;
        memset(&dec_input, 0, sizeof(dec_input));
        dec_input.bitstream.buffer = bitstreams[frame].data();
        dec_input.bitstream.allocation_size = (uint32_t)bitstreams[frame].size();
        dec_input.bitstream.used_size = (uint32_t)bitstreams[frame].size();
        dec_input.image = *out_img;
        dec_input.user_prv_ctx_ptr = (void*)(uintptr_t)frame;
        ASSERT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/), SvtJxsErrorNone);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (uint32_t frame = 0; frame < frames_num; frame++) {
        svt_jpeg_xs_frame_t dec_output;
        SvtJxsErrorType_t ret;
        for (;;) {
            memset(&dec_output, 0, sizeof(dec_output));
            ret = svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 0 /*non-blocking*/);
            if (ret != SvtJxsErrorNoErrorEmptyQueue || std::chrono::steady_clock::now() > deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (ret == SvtJxsErrorNoErrorEmptyQueue) {
            //The decoder threads are stuck: svt_jpeg_xs_decoder_close() would wait on them forever,
            //so leave the decoder and its buffers behind and only report the failure.
            FAIL() << "frame " << frame << " not decoded within " << timeout.count() << " s (" << param << ", last slice has "
                   << (precinct_lines % picture_header_const.hdr_Hsl) << " precinct lines)";
        }
        ASSERT_EQ(ret, SvtJxsErrorNone) << "frame " << frame;
        ASSERT_EQ((uintptr_t)dec_output.user_prv_ctx_ptr, (uintptr_t)frame);
        EXPECT_TRUE(planes_bit_exact(in_bufs[frame], &dec_output.image, image_config, param.bit_depth))
            << "frame " << frame << " " << param;
    }

    svt_jpeg_xs_decoder_close(&dec);
    for (auto* img : out_imgs) {
        svt_jpeg_xs_image_buffer_free(img);
    }
    for (auto* img : in_bufs) {
        svt_jpeg_xs_image_buffer_free(img);
    }
}

/* Each layout is run without and with RCT (Cpih=1), whose per-precinct inverse transform shares the
 * same slice overlap path, and at 8 and 10 bits for the two output line writers.
 *   - 1 precinct line in the last slice: the deadlock case. 66 lines also has a partial last precinct.
 *   - 2 precinct lines in the last slice: the shortest last slice that worked before.
 *   - No short last slice: all slices full. */
static std::vector<SliceSyncCase> slice_sync_cases() {
    const SliceSyncCase layouts[] = {
        {68, 2, 16, 0, 0}, //17 precinct lines, 4 per slice: last slice has 1.
        {66, 2, 16, 0, 0}, //17 precinct lines, the last one partial: last slice has 1.
        {18, 1, 8, 0, 0},  //9 precinct lines, 4 per slice: last slice has 1.
        {68, 1, 8, 0, 0},  //34 precinct lines, 4 per slice: last slice has 2.
        {72, 2, 16, 0, 0}, //18 precinct lines, 4 per slice: last slice has 2.
        {64, 2, 16, 0, 0}, //16 precinct lines, 4 per slice: all slices full.
    };
    std::vector<SliceSyncCase> cases;
    for (const SliceSyncCase& layout : layouts) {
        for (uint8_t bit_depth : {8, 10}) {
            for (uint8_t rct : {0, 1}) {
                SliceSyncCase c = layout;
                c.bit_depth = bit_depth;
                c.rct = rct;
                cases.push_back(c);
            }
        }
    }
    return cases;
}

INSTANTIATE_TEST_SUITE_P(LastSliceLayouts, SliceSyncLastSlice, ::testing::ValuesIn(slice_sync_cases()));
