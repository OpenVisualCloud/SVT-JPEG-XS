/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <string.h>
#include <chrono>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsImageBufferTools.h"
#include "SampleFramesData.h"

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

TEST(DecoderInit, LosslessRejectsPacketizationMode) {
    // Regression test for a real hang this guards against: packetization mode's slice-boundary
    // framing assumes CBR's uniform, formula-computable slice sizes, and previously spun forever
    // (never converging on a matching size) against a lossless stream's real, data-dependent
    // slice sizes instead of erroring - confirmed not to reproduce on an ordinary CBR bitstream.
    // The encoder itself never produces this combination (see
    // EncoderInit.LosslessRejectsSlicePacketizationMode), but a caller decoding an externally
    // supplied lossless bitstream could still request packetization_mode=1, so the decoder must
    // reject it here too.
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
             SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 64;
    enc.source_height = 64;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    enc.lossless_enable = 1;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_image_config_t image_config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(svt_jpeg_xs_encoder_get_image_config(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &image_config, &bytes_per_frame),
             SvtJxsErrorNone);

    svt_jpeg_xs_image_buffer_t* in_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(in_buf, nullptr);
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        memset(in_buf->data_yuv[c], 0, (size_t)in_buf->stride[c] * image_config.components[c].height);
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
    svt_jpeg_xs_image_buffer_free(in_buf);
    free(out_buf.buffer);

    // packetization_mode=1 must be rejected cleanly (not hang) against this lossless bitstream.
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.packetization_mode = 1;
    svt_jpeg_xs_image_config_t dec_image_config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                                     SVT_JPEGXS_API_VER_MINOR,
                                                     &decoder,
                                                     bitstream.data(),
                                                     bitstream.size(),
                                                     &dec_image_config);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(decoder.private_ptr, nullptr);

    // Sanity: packetization_mode=0 against the SAME bitstream must still succeed - otherwise the
    // rejection above could trivially "pass" for the wrong reason (e.g. a malformed bitstream).
    svt_jpeg_xs_decoder_api_t decoder_ok;
    memset(&decoder_ok, 0, sizeof(decoder_ok));
    decoder_ok.verbose = VERBOSE_NONE;
    decoder_ok.packetization_mode = 0;
    SvtJxsErrorType_t ret_ok = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                                        SVT_JPEGXS_API_VER_MINOR,
                                                        &decoder_ok,
                                                        bitstream.data(),
                                                        bitstream.size(),
                                                        &dec_image_config);
    ASSERT_EQ(ret_ok, SvtJxsErrorNone);
    svt_jpeg_xs_decoder_close(&decoder_ok);
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

/* Encodes one 256x64 8-bit 4:2:2 CBR frame (4 slices of 16 lines, 4 precincts each), used as a
 * well-formed base for the packet-mode robustness tests below. */
static void encode_cbr_packet_mode_base(std::vector<uint8_t>& bitstream) {
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 256;
    enc.source_height = 64;
    enc.input_bit_depth = 8;
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
    for (int32_t c = 0; c < image_config.components_num; ++c) {
        uint8_t* plane = (uint8_t*)in_buf->data_yuv[c];
        for (uint32_t i = 0; i < in_buf->alloc_size[c]; ++i) {
            plane[i] = (uint8_t)(i * 7 + c * 31);
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
    bitstream.assign(enc_output.bitstream.buffer, enc_output.bitstream.buffer + enc_output.bitstream.used_size);

    svt_jpeg_xs_encoder_close(&enc);
    svt_jpeg_xs_image_buffer_free(in_buf);
}

TEST(DecoderPacketMode, LcodSmallerThanHeaderDoesNotStall) {
    // Found by the decoder fuzzer: Lcod sizes the internal frame buffer, so a corrupted Lcod smaller
    // than the headers fills that buffer before the header can be parsed. send_packet() used to keep
    // returning SvtJxsErrorDecoderBitstreamTooShort with bytes_used=0 forever, so a caller feeding
    // the stream (e.g. SvtJpegxsDecApp --packetization-mode 1) looped without progress.
    std::vector<uint8_t> bitstream;
    encode_cbr_packet_mode_base(bitstream);
    ASSERT_FALSE(HasFatalFailure());

    // SOC(2) + CAP(6) + PIH marker(2) + Lpih(2), then the 32-bit Lcod
    const size_t lcod_offset = 12;
    ASSERT_GT(bitstream.size(), lcod_offset + 4);
    ASSERT_EQ(bitstream[8], 0xFF);
    ASSERT_EQ(bitstream[9], 0x12);
    const uint32_t lcod = 20;
    bitstream[lcod_offset + 0] = (uint8_t)(lcod >> 24);
    bitstream[lcod_offset + 1] = (uint8_t)(lcod >> 16);
    bitstream[lcod_offset + 2] = (uint8_t)(lcod >> 8);
    bitstream[lcod_offset + 3] = (uint8_t)lcod;

    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.packetization_mode = 1;
    svt_jpeg_xs_image_config_t image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &decoder, bitstream.data(), bitstream.size(), &image_config),
              SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(out_buf, nullptr);

    svt_jpeg_xs_frame_t dec_input;
    memset(&dec_input, 0, sizeof(dec_input));
    dec_input.image = *out_buf;
    dec_input.bitstream.buffer = bitstream.data();
    dec_input.bitstream.used_size = (uint32_t)bitstream.size();
    dec_input.bitstream.allocation_size = (uint32_t)bitstream.size();
    uint32_t bytes_used = 0;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_send_packet(&decoder, &dec_input, &bytes_used);
    EXPECT_EQ(bytes_used, lcod);
    ASSERT_NE(ret, SvtJxsErrorDecoderBitstreamTooShort);

    // The frame was scheduled with an error, so it is delivered (with that error) instead of hanging.
    if (ret == SvtJxsErrorNone) {
        svt_jpeg_xs_frame_t dec_output;
        EXPECT_NE(svt_jpeg_xs_decoder_get_frame(&decoder, &dec_output, 1 /*blocking*/), SvtJxsErrorNone);
    }
    svt_jpeg_xs_decoder_close(&decoder);
    svt_jpeg_xs_image_buffer_free(out_buf);
}

TEST(DecoderPacketMode, CloseWithPartiallySentFrameDoesNotHang) {
    // Found by the decoder fuzzer: with IDWT synchronized between slices, a slice thread waits for
    // the next slice to be decoded. When a frame is only partially sent in packet mode, that next
    // slice never arrives, and svt_jpeg_xs_decoder_close() used to deadlock joining the thread.
    std::vector<uint8_t> bitstream;
    encode_cbr_packet_mode_base(bitstream);
    ASSERT_FALSE(HasFatalFailure());

    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.threads_num = 4;
    decoder.packetization_mode = 1;
    svt_jpeg_xs_image_config_t image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &decoder, bitstream.data(), bitstream.size(), &image_config),
              SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(out_buf, nullptr);

    // Send about half of the frame: the first slices get scheduled, the remaining ones never do.
    svt_jpeg_xs_frame_t dec_input;
    memset(&dec_input, 0, sizeof(dec_input));
    dec_input.image = *out_buf;
    dec_input.bitstream.buffer = bitstream.data();
    dec_input.bitstream.used_size = (uint32_t)(bitstream.size() / 2);
    dec_input.bitstream.allocation_size = dec_input.bitstream.used_size;
    uint32_t bytes_used = 0;
    EXPECT_EQ(svt_jpeg_xs_decoder_send_packet(&decoder, &dec_input, &bytes_used), SvtJxsErrorDecoderBitstreamTooShort);
    EXPECT_EQ(bytes_used, dec_input.bitstream.used_size);

    // Give the slice threads time to decode the sent slices and block waiting for the missing next slice.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Must return; output buffers are released only after close since slice threads may still write them.
    svt_jpeg_xs_decoder_close(&decoder);
    svt_jpeg_xs_image_buffer_free(out_buf);
}

TEST(DecoderPacketMode, CloseWithQueuedSlicesDoesNotHang) {
    // Found by the decoder fuzzer: closing right after the slices were sent, before get_frame(), shuts
    // down the slice queue while slices are still queued. Those are dropped, so a slice thread waiting
    // for one of them (IDWT between slices) was never woken up and svt_jpeg_xs_decoder_close()
    // deadlocked joining it. The race depends on thread timing, so it is repeated.
    std::vector<uint8_t> bitstream;
    encode_cbr_packet_mode_base(bitstream);
    ASSERT_FALSE(HasFatalFailure());

    for (int iter = 0; iter < 200; iter++) {
        svt_jpeg_xs_decoder_api_t decoder;
        memset(&decoder, 0, sizeof(decoder));
        decoder.verbose = VERBOSE_NONE;
        decoder.threads_num = 4;
        decoder.packetization_mode = 1;
        svt_jpeg_xs_image_config_t image_config;
        ASSERT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                           SVT_JPEGXS_API_VER_MINOR,
                                           &decoder,
                                           bitstream.data(),
                                           bitstream.size(),
                                           &image_config),
                  SvtJxsErrorNone);
        svt_jpeg_xs_image_buffer_t* out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
        ASSERT_NE(out_buf, nullptr);

        svt_jpeg_xs_frame_t dec_input;
        memset(&dec_input, 0, sizeof(dec_input));
        dec_input.image = *out_buf;
        dec_input.bitstream.buffer = bitstream.data();
        dec_input.bitstream.used_size = (uint32_t)bitstream.size();
        dec_input.bitstream.allocation_size = dec_input.bitstream.used_size;
        uint32_t bytes_used = 0;
        EXPECT_EQ(svt_jpeg_xs_decoder_send_packet(&decoder, &dec_input, &bytes_used), SvtJxsErrorNone);

        // Must return even though the frame was never collected with get_frame().
        svt_jpeg_xs_decoder_close(&decoder);
        svt_jpeg_xs_image_buffer_free(out_buf);
    }
}

//Polls get_frame for up to 5 seconds, so a frame that is never delivered fails the test instead of hanging it.
static SvtJxsErrorType_t get_frame_with_timeout(svt_jpeg_xs_decoder_api_t* decoder, svt_jpeg_xs_frame_t* dec_output) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    SvtJxsErrorType_t ret;
    while ((ret = svt_jpeg_xs_decoder_get_frame(decoder, dec_output, 0 /*non-blocking*/)) == SvtJxsErrorNoErrorEmptyQueue &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ret;
}

TEST(DecoderPacketMode, EocAfterPartiallySentFrameDeliversErrorFrame) {
    // A frame that was only partially sent in packet mode used to stay pending forever after
    // svt_jpeg_xs_decoder_send_eoc(): the EOC was queued behind it in the output ring buffer, so
    // get_frame() never returned either the truncated frame or the end of codestream.
    std::vector<uint8_t> bitstream;
    encode_cbr_packet_mode_base(bitstream);
    ASSERT_FALSE(HasFatalFailure());

    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.threads_num = 4;
    decoder.packetization_mode = 1;
    svt_jpeg_xs_image_config_t image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &decoder, bitstream.data(), bitstream.size(), &image_config),
              SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* out_buf = svt_jpeg_xs_image_buffer_alloc(&image_config);
    ASSERT_NE(out_buf, nullptr);

    svt_jpeg_xs_frame_t dec_input;
    memset(&dec_input, 0, sizeof(dec_input));
    dec_input.image = *out_buf;
    dec_input.bitstream.buffer = bitstream.data();
    dec_input.bitstream.used_size = (uint32_t)(bitstream.size() / 2);
    dec_input.bitstream.allocation_size = dec_input.bitstream.used_size;
    uint32_t bytes_used = 0;
    EXPECT_EQ(svt_jpeg_xs_decoder_send_packet(&decoder, &dec_input, &bytes_used), SvtJxsErrorDecoderBitstreamTooShort);

    EXPECT_EQ(svt_jpeg_xs_decoder_send_eoc(&decoder), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t dec_output;
    EXPECT_EQ(get_frame_with_timeout(&decoder, &dec_output), SvtJxsErrorDecoderBitstreamTooShort);
    EXPECT_EQ(get_frame_with_timeout(&decoder, &dec_output), SvtJxsDecoderEndOfCodestream);

    svt_jpeg_xs_decoder_close(&decoder);
    svt_jpeg_xs_image_buffer_free(out_buf);
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

TEST(EncoderInit, ColorTransformRequiresLowLatencyProfileReturnsError) {
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 16;
    encoder.source_height = 16;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    encoder.bpp_numerator = 3;
    encoder.cpu_profile = 1; //CPU_PROFILE_CPU
    encoder.enable_color_transform = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, ColorTransformWithCpuProfileRejectedEvenWhenNdecompVZeroForcesLowLatency) {
    // ndecomp_v == 0 forces enc_common->cpu_profile to CPU_PROFILE_LOW_LATENCY internally (it never
    // makes sense to run CPU_PROFILE_CPU threading with no vertical decomposition). Regression guard:
    // that internal downgrade must not let an explicit CPU_PROFILE_CPU request bypass the
    // enable_color_transform + cpu_profile validation - the caller's original request must still be
    // rejected, matching the documented "requires Low latency" contract.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 16;
    encoder.source_height = 16;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    encoder.bpp_numerator = 3;
    encoder.ndecomp_v = 0;
    encoder.cpu_profile = 1; //CPU_PROFILE_CPU
    encoder.enable_color_transform = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, ColorTransformRequiresPlanar444FormatReturnsError) {
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 16;
    encoder.source_height = 16;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    encoder.bpp_numerator = 3;
    encoder.cpu_profile = 0; //CPU_PROFILE_LOW_LATENCY
    encoder.enable_color_transform = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, LosslessRejectsMsbAlignedInput) {
    // NltEnc.c's MSB-aligned input path computes shift = hdr_Bw - 16, which is only valid (a
    // non-negative left shift) for the non-lossless Bw in {18,20}; lossless sets Bw=bit_depth
    // (8-14), so this combination is rejected rather than risking an undefined shift.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 10;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    encoder.bpp_numerator = 3;
    encoder.lossless_enable = 1;

    // Control: the same configuration without input_bit_depth_msb_aligned must initialize, so the rejection below
    // is caused by input_bit_depth_msb_aligned and not by some other parameter.
    svt_jpeg_xs_encoder_api_t control = encoder;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &control), SvtJxsErrorNone);
    svt_jpeg_xs_encoder_close(&control);

    encoder.input_bit_depth_msb_aligned = 1;
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, LosslessRejectsSlicePacketizationMode) {
    // Slice packetization mode needs hdr_Lcod known before the header packet is released, but
    // lossless's true final size is only known after every slice has finished packing.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    encoder.bpp_numerator = 3;
    encoder.lossless_enable = 1;

    // Control: the same configuration without slice_packetization_mode must initialize, so the rejection below
    // is caused by slice_packetization_mode and not by some other parameter.
    svt_jpeg_xs_encoder_api_t control = encoder;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &control), SvtJxsErrorNone);
    svt_jpeg_xs_encoder_close(&control);

    encoder.slice_packetization_mode = 1;
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, LosslessRejectsRctAboveBitDepth12) {
    // RCT (Cpih=1) grows coefficient magnitude by roughly a bit on top of the wavelet transform's
    // own growth; combined with 13/14-bit lossless input (Bw=bit_depth, Fq=0, no margin from
    // quantization) that can overflow TRUNCATION_MAX=15 bits and wrap into the sign bit undetected
    // in a release build (NltEnc.c only asserts the bound). Reject the combination instead.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 13;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    encoder.bpp_numerator = 3;
    encoder.lossless_enable = 1;
    encoder.enable_color_transform = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorBadParameter);
    ASSERT_EQ(encoder.private_ptr, nullptr);
}

TEST(EncoderInit, LosslessAcceptsRctAtBitDepth12) {
    // Sanity check paired with the rejection above: RCT+lossless at exactly bit_depth=12 (the MLS.12
    // profile's own ceiling, Table A.4) must still succeed.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 12;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    encoder.bpp_numerator = 3;
    encoder.lossless_enable = 1;
    encoder.enable_color_transform = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorNone);
    ASSERT_NE(encoder.private_ptr, nullptr);
    svt_jpeg_xs_encoder_close(&encoder);
}

TEST(EncoderInit, LosslessAcceptsPlainConfiguration) {
    // Sanity check paired with the two rejections above: lossless_enable alone (no MSB-aligned,
    // no packetization) must succeed - otherwise those two tests could trivially "pass" for the
    // wrong reason if lossless_enable were unconditionally rejected.
    svt_jpeg_xs_encoder_api_t encoder;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    encoder.verbose = VERBOSE_NONE;
    // 16x16 is too small for the default ndecomp_h=5 (fails independently of lossless_enable -
    // see EncoderTest.sh's "Unsuported Decomposition"/"Too big decomposition" cases), so use a
    // size that comfortably fits the default decomposition depth.
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    encoder.bpp_numerator = 3;
    encoder.lossless_enable = 1;

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
    ASSERT_EQ(ret, SvtJxsErrorNone);
    ASSERT_NE(encoder.private_ptr, nullptr);
    svt_jpeg_xs_encoder_close(&encoder);
}

TEST(EncoderInit, LosslessRejectsZeroResolution) {
    // Found by the encoder fuzzer: slice_height is clamped to source_height, so source_height=0 made
    // the lossless worst-case output sizing divide by zero (SIGFPE) instead of failing validation.
    for (int zero_height = 0; zero_height < 2; ++zero_height) {
        svt_jpeg_xs_encoder_api_t encoder;
        svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder);
        encoder.verbose = VERBOSE_NONE;
        encoder.source_width = zero_height ? 64 : 0;
        encoder.source_height = zero_height ? 0 : 64;
        encoder.input_bit_depth = 8;
        encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        encoder.lossless_enable = 1;

        svt_jpeg_xs_image_config_t image_config;
        uint32_t bytes_per_frame = 0;
        EXPECT_EQ(svt_jpeg_xs_encoder_get_image_config(
                      SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder, &image_config, &bytes_per_frame),
                  SvtJxsErrorBadParameter);
        EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder),
                  SvtJxsErrorBadParameter);
        svt_jpeg_xs_encoder_close(&encoder);
    }
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
