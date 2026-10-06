/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* With verbose >= VERBOSE_WARNINGS a slice thread warns when decoding a slice used a different number of
 * bytes than the slice holds. It compared the bytes with svt_jpeg_xs_decode_slice()'s error code, and in
 * frame mode each slice was given the rest of the frame instead of its own size, so every slice of every
 * valid frame warned. A valid multi-slice stream decoded in frame and packet mode must log no such warning. */

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "SvtLog.h"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

namespace {

//Only the size warning: other decoder warnings are not what this test is about
void count_log_callback(void* context, SvtLogLevel level, const char* tag, const char* fmt, va_list args) {
    (void)tag;
    std::atomic<int>* size_warnings = static_cast<std::atomic<int>*>(context);
    char message[512];
    vsnprintf(message, sizeof(message), fmt, args);
    if (level == SVT_LOG_WARN && strstr(message, "Unexpected size")) {
        (*size_warnings)++;
    }
}

//4 slices of 16 lines, 3 frames with different content
void encode_multi_slice(std::vector<std::vector<uint8_t>>& bitstreams) {
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 256;
    enc.source_height = 64;
    enc.input_bit_depth = 10;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc.bpp_numerator = 3;
    enc.slice_height = 16;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
              SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);

    for (uint32_t frame = 0; frame < 3; frame++) {
        for (int32_t c = 0; c < image_config.components_num; ++c) {
            uint16_t* plane = (uint16_t*)in_buf->data_yuv[c];
            for (uint32_t i = 0; i < in_buf->alloc_size[c] / 2; ++i) {
                plane[i] = (uint16_t)((i * 7 + c * 31 + frame * 97) & 0x3FF);
            }
        }
        std::vector<uint8_t> out(bytes_per_frame);
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
        bitstreams.emplace_back(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);
    }

    svt_jpeg_xs_image_buffer_free(in_buf);
    svt_jpeg_xs_encoder_close(&enc);
}

//Returns the number of frames decoded without error, or -1 if decoding could not start
int decode_with_warnings(const std::vector<std::vector<uint8_t>>& bitstreams, uint8_t packetization_mode) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_WARNINGS;
    dec.threads_num = 4;
    dec.packetization_mode = packetization_mode;
    svt_jpeg_xs_image_config_t image_config;
    if (svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                 SVT_JPEGXS_API_VER_MINOR,
                                 &dec,
                                 bitstreams[0].data(),
                                 bitstreams[0].size(),
                                 &image_config) != SvtJxsErrorNone) {
        return -1;
    }

    int frames_ok = 0;
    for (const auto& bitstream : bitstreams) {
        svt_jpeg_xs_image_buffer_t* out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
        if (!out_buf) {
            break;
        }
        svt_jpeg_xs_frame_t dec_input;
        memset(&dec_input, 0, sizeof(dec_input));
        dec_input.image = *out_buf;
        dec_input.bitstream.buffer = (uint8_t*)bitstream.data();
        dec_input.bitstream.used_size = (uint32_t)bitstream.size();
        dec_input.bitstream.allocation_size = (uint32_t)bitstream.size();
        SvtJxsErrorType_t ret;
        if (packetization_mode) {
            uint32_t bytes_used = 0;
            ret = svt_jpeg_xs_decoder_send_packet(&dec, &dec_input, &bytes_used);
            if (ret == SvtJxsErrorNone && bytes_used != bitstream.size()) {
                ret = SvtJxsErrorUndefined;
            }
        }
        else {
            ret = svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/);
        }
        if (ret == SvtJxsErrorNone) {
            svt_jpeg_xs_frame_t dec_output;
            memset(&dec_output, 0, sizeof(dec_output));
            if (svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/) == SvtJxsErrorNone) {
                frames_ok++;
            }
        }
        svt_jpeg_xs_image_buffer_free(out_buf);
        if (ret != SvtJxsErrorNone) {
            break;
        }
    }
    svt_jpeg_xs_decoder_close(&dec);
    return frames_ok;
}

struct ChildResult {
    int frames_ok;
    int size_warnings;
};

} // namespace

class DecoderSliceSize : public ::testing::TestWithParam<uint8_t> {};

/* The logger takes a callback only before its first use in the process, so decode in a forked child with
 * a reset logger, as TestSvtLog.cc does. */
TEST_P(DecoderSliceSize, ValidStreamLogsNoSizeWarning) {
    const uint8_t packetization_mode = GetParam();
    std::vector<std::vector<uint8_t>> bitstreams;
    encode_multi_slice(bitstreams);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(bitstreams.size(), 3u);

    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(pipefd[0]);
        svt_jxs_log_reset_for_testing();
        static std::atomic<int> size_warnings(0);
        svt_jpeg_xs_set_log_callback(count_log_callback, &size_warnings);
        ChildResult result;
        result.frames_ok = decode_with_warnings(bitstreams, packetization_mode);
        result.size_warnings = size_warnings;
        ssize_t written = write(pipefd[1], &result, sizeof(result));
        (void)written;
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    ChildResult result;
    memset(&result, 0, sizeof(result));
    ssize_t bytes_read = read(pipefd[0], &result, sizeof(result));
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT_EQ(bytes_read, static_cast<ssize_t>(sizeof(result)));
    EXPECT_EQ(result.frames_ok, 3);
    EXPECT_EQ(result.size_warnings, 0);
}

INSTANTIATE_TEST_SUITE_P(FrameAndPacketMode, DecoderSliceSize, ::testing::Values((uint8_t)0, (uint8_t)1));

#endif // _WIN32
