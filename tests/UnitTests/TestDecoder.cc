/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "SampleFramesData.h"
#include "gtest/gtest.h"
#include "SvtJpegxsDec.h"
#include "DecoderSimple.h"
#include "common_dsp_rtcd.h"
#include "SvtJpegxsImageBufferTools.h"

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

#define INVALID_ERROR_CODE_START (100)
#define INVALID_ERROR_CODE_A     (200)
#define INVALID_ERROR_CODE_B     (300)
#define INVALID_ERROR_CODE_C     (400)
#define INVALID_ERROR_CODE_D     (500)
#define INVALID_ERROR_CODE_E     (600)

static int32_t test_decode_frame_lp(uint64_t use_cpu_flags, uint32_t lp, const uint8_t* frame_1, size_t frame_1_size,
                                    const uint8_t* frame_2, size_t frame_2_size) {
    int32_t ret = 0;
    svt_jpeg_xs_image_config_t image_config;
    svt_jpeg_xs_decoder_api_t decoder;
    svt_jpeg_xs_image_buffer_t* image_buffer = NULL;
    memset(&decoder, 0, sizeof(svt_jpeg_xs_decoder_api_t));

    decoder.use_cpu_flags = use_cpu_flags;
    decoder.threads_num = lp;
#if SILENT_OUTPUT
    decoder.verbose = VERBOSE_NONE;
#else
    decoder.verbose = VERBOSE_ERRORS;
#endif

    ret = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &decoder, frame_1, frame_1_size, &image_config);
    if (ret) {
        goto fail;
    }
    image_buffer = svt_jpeg_xs_image_buffer_alloc(&image_config);
    if (!image_buffer) {
        goto fail;
    }

    //Frame 1
    svt_jpeg_xs_frame_t dec_input1;
    dec_input1.user_prv_ctx_ptr = NULL;
    dec_input1.image = *image_buffer;
    dec_input1.bitstream.buffer = (uint8_t*)frame_1;
    dec_input1.bitstream.used_size = (uint32_t)frame_1_size;
    ret = svt_jpeg_xs_decoder_send_frame(&decoder, &dec_input1, 1);
    if (ret) {
        goto fail;
    }

    svt_jpeg_xs_frame_t dec_output2;
    ret = svt_jpeg_xs_decoder_get_frame(&decoder, &dec_output2, 1);
    if (ret) {
        goto fail;
    }

    if (frame_2 != NULL) {
        //Frame 2
        svt_jpeg_xs_frame_t dec_input2;
        dec_input2.image = *image_buffer;
        dec_input2.bitstream.buffer = (uint8_t*)frame_2;
        dec_input2.bitstream.used_size = (uint32_t)frame_2_size;

        ret = svt_jpeg_xs_decoder_send_frame(&decoder, &dec_input2, 1);
        if (ret) {
            goto fail;
        }

        svt_jpeg_xs_frame_t dec_output2;
        ret = svt_jpeg_xs_decoder_get_frame(&decoder, &dec_output2, 1);
        if (ret) {
            goto fail;
        }
    }

fail:
    /* jpeg_xs_out_get to release can be only use
       when was be properly filled by svt_jpeg_xs_decoder_get_frame()
    svt_jpeg_xs_image_buffer_free(&jpeg_xs_out_get); */
    svt_jpeg_xs_image_buffer_free(image_buffer);
    svt_jpeg_xs_decoder_close(&decoder);
    return ret;
}

static int32_t test_decode_frame(uint64_t use_cpu_flags, const uint8_t* frame_1, size_t frame_1_size, const uint8_t* frame_2,
                                 size_t frame_2_size) {
    int32_t ret_lp1 = test_decode_frame_lp(use_cpu_flags, 1, frame_1, frame_1_size, frame_2, frame_2_size);
    int32_t ret_lp5 = test_decode_frame_lp(use_cpu_flags, 5, frame_1, frame_1_size, frame_2, frame_2_size);
    if (ret_lp1 != ret_lp5) {
        /*Result decoder with LP 1 and 5 should always return that same error.*/
        return INVALID_ERROR_CODE_A;
    }
    return ret_lp1;
}

static int32_t test_decode_frame_simple(const uint8_t* frame_1, size_t frame_1_size, const uint8_t* frame_2,
                                        size_t frame_2_size) {
    int32_t ret = 0;

    DecoderSimple_t decoder_simple;
#if SILENT_OUTPUT
    decoder_simple.verbose = VERBOSE_NONE;
#else
    decoder_simple.verbose = VERBOSE_ERRORS;
#endif

    ret = decoder_simple_alloc(&decoder_simple, frame_1, frame_1_size);
    if (ret < 0) {
        goto fail;
    }

    //Frame 1
    ret = decoder_simple_get_frame(&decoder_simple, frame_1, frame_1_size);
    if (ret < 0) {
        goto fail;
    }
    if (frame_2 != NULL) {
        //Frame 2
        ret = decoder_simple_get_frame(&decoder_simple, frame_2, frame_2_size);
    }

fail:
    decoder_simple_free(&decoder_simple);

    if (ret > 0) {
        //When decode correctly, return size of frame bitstream. This function only return error code or 0.
        ret = 0;
    }
    return ret;
}

static int32_t test_bitstream_simple(const uint8_t* frame_valid, size_t frame_valid_size, const uint8_t* frame_corrupted,
                                     size_t frame_corrupted_size) {
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));

    /*Test size for corrupted bitstream. Valgrind should handle possible errors. */
    uint32_t frame_size = 0;
    SvtJxsErrorType_t frame_size_ret = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        frame_corrupted, frame_corrupted_size, &image_config, &frame_size, 0, proxy_mode_full);

    /*Test direct corrupted frame*/
    int32_t ret_simple_1 = test_decode_frame_simple(frame_corrupted, frame_corrupted_size, NULL, 0);
    /*Test init with correct frame and then decode corrupted frame*/
    int32_t ret_simple_2 = test_decode_frame_simple(frame_valid, frame_valid_size, frame_corrupted, frame_corrupted_size);

    if ((frame_size_ret != SvtJxsErrorNone || frame_size == 0) && ((ret_simple_1 == 0) || (ret_simple_2 == 0))) {
        /*Never should happens for corrupted bitstream that frame correct decode.*/
        return INVALID_ERROR_CODE_B;
    }

    if ((ret_simple_1 != 0) || (ret_simple_2 != 0)) {
        if ((!!ret_simple_1) != (!!ret_simple_2)) {
            if (!(ret_simple_1 == 0 && ret_simple_2 == SvtJxsErrorDecoderConfigChange)) {
                /*Corrupted bitstream can not be decoded,
                 *But bitstream with config change can be still decodable.*/
                return INVALID_ERROR_CODE_D;
            }
        }
    }

    if (ret_simple_2) {
        return ret_simple_2;
    }

    if (ret_simple_1) {
        return ret_simple_1;
    }

    return 0;
}

static int32_t test_bitstream_full(uint64_t use_cpu_flags, const uint8_t* frame_valid, size_t frame_valid_size,
                                   const uint8_t* frame_corrupted, size_t frame_corrupted_size) {
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));

    /*Test size for corrupted bitstream. Valgrind should handle possible errors. */
    uint32_t frame_size = 0;
    SvtJxsErrorType_t frame_size_ret = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        frame_corrupted, frame_corrupted_size, &image_config, &frame_size, 0, proxy_mode_full);

    /*Test direct corrupted frame*/
    int32_t ret_simple = test_bitstream_simple(frame_valid, frame_valid_size, frame_corrupted, frame_corrupted_size);
    if (ret_simple >= INVALID_ERROR_CODE_START) {
        return ret_simple;
    }

    /*Test direct corrupted frame*/
    int32_t ret_1 = test_decode_frame(use_cpu_flags, frame_corrupted, frame_corrupted_size, NULL, 0);
    if (ret_1 >= INVALID_ERROR_CODE_START) {
        return ret_1;
    }

    /*Test init with correct frame and then decode corrupted frame*/
    int32_t ret_2 = test_decode_frame(use_cpu_flags, frame_valid, frame_valid_size, frame_corrupted, frame_corrupted_size);
    if (ret_2 >= INVALID_ERROR_CODE_START) {
        return ret_2;
    }

    if ((frame_size_ret != SvtJxsErrorNone || frame_size == 0) && ((ret_1 == 0) || (ret_2 == 0) || (ret_simple == 0))) {
        /*Never should happens for corrupted bitstream that frame correct decode.*/
        return INVALID_ERROR_CODE_B;
    }

    if ((ret_1 != 0) || (ret_2 != 0)) {
        if ((!!ret_1) != (!!ret_2)) {
            if (!(ret_1 == 0 && ret_2 == SvtJxsErrorDecoderConfigChange)) {
                /*Corrupted bitstream can not be decoded,
                 *But bitstream with config change can be still decodable.*/
                return INVALID_ERROR_CODE_C;
            }
        }
    }

    if ((ret_2 != 0) || (ret_simple != 0)) {
        if ((!!ret_2) != (!!ret_simple)) {
            /*Corrupted bitstream can not be decoded.*/
            return INVALID_ERROR_CODE_E;
        }
    }

    if (ret_2) {
        return ret_2;
    }

    if (ret_1) {
        return ret_1;
    }

    return 0;
}

static void Test_Bitstream_NULL_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));
    //Decode full bitstream
    int32_t ret = test_bitstream_full(use_cpu_flags, NULL, Frame_Sample_1_16x16_8bit_422_bitstream_size, NULL, 0);
    ASSERT_EQ(ret, SvtJxsErrorDecoderInvalidPointer);
    uint32_t frame_size = 0;
    SvtJxsErrorType_t ret2 = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        NULL, Frame_Sample_1_16x16_8bit_422_bitstream_size, &image_config, &frame_size, 0, proxy_mode_full);
    ASSERT_EQ(ret2, SvtJxsErrorDecoderInvalidPointer);
}

TEST(Decoder, Bitstream_NULL_valgrind_C) {
    Test_Bitstream_NULL_valgrind(0);
}

TEST(Decoder, Bitstream_NULL_valgrind_AVX2) {
    Test_Bitstream_NULL_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_NULL_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_NULL_valgrind(CPU_FLAGS_ALL);
    }
}

static void Test_Bitstream_ZeroAlloc_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));
    uint8_t zero_alloc;
    //Decode full bitstream
    int32_t ret = test_bitstream_full(use_cpu_flags, &zero_alloc, 0, NULL, 0);
    ASSERT_EQ(ret, SvtJxsErrorDecoderInvalidPointer);
    uint32_t frame_size = 0;
    SvtJxsErrorType_t ret2 = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        &zero_alloc, 0, &image_config, &frame_size, 0, proxy_mode_full);
    ASSERT_EQ(ret2, SvtJxsErrorDecoderInvalidPointer);
}

TEST(Decoder, Bitstream_ZeroAlloc_valgrind_C) {
    Test_Bitstream_ZeroAlloc_valgrind(0);
}

TEST(Decoder, Bitstream_ZeroAlloc_valgrind_AVX2) {
    Test_Bitstream_ZeroAlloc_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_ZeroAlloc_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_ZeroAlloc_valgrind(CPU_FLAGS_ALL);
    }
}

static void Test_Bitstream_1_Correct_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));
    //Decode full bitstream
    int32_t ret = test_bitstream_full(use_cpu_flags,
                                      Frame_Sample_1_16x16_8bit_422_bitstream,
                                      Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                      Frame_Sample_1_16x16_8bit_422_bitstream,
                                      Frame_Sample_1_16x16_8bit_422_bitstream_size);
    ASSERT_EQ(ret, SvtJxsErrorNone);
    uint32_t frame_size = 0;
    SvtJxsErrorType_t ret2 = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size, &image_config, &frame_size, 0, proxy_mode_full);
    ASSERT_EQ(ret2, SvtJxsErrorNone);
    ASSERT_EQ(frame_size, Frame_Sample_1_16x16_8bit_422_bitstream_size);
}

TEST(Decoder, Bitstream_1_Correct_valgrind_C) {
    Test_Bitstream_1_Correct_valgrind(0);
}

TEST(Decoder, Bitstream_1_Correct_valgrind_AVX2) {
    Test_Bitstream_1_Correct_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_1_Correct_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_1_Correct_valgrind(CPU_FLAGS_ALL);
    }
}

static void Test_Bitstream_1_TruncationSize(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    uint8_t* buffer = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
    //Decode truncation bitstream
    for (uint32_t size = 1; size < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++size) {
        //for (uint32_t size = Frame_Sample_1_16x16_8bit_422_bitstream_size - 1; size > 0; --size) {
        printf("Size: %u\n", size);
        memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);
        int32_t ret = test_bitstream_full(
            use_cpu_flags, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size, buffer, size);
        ASSERT_EQ(ret, SvtJxsErrorDecoderBitstreamTooShort);
    }
    free(buffer);
}

TEST(Decoder, Bitstream_1_TruncationSize_C) {
    Test_Bitstream_1_TruncationSize(0);
}

TEST(Decoder, Bitstream_1_TruncationSize_AVX2) {
    Test_Bitstream_1_TruncationSize(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_1_TruncationSize_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_1_TruncationSize(CPU_FLAGS_ALL);
    }
}

static void Test_Bitstream_1_TruncationBufferFillZero(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    uint8_t* buffer = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
    //Decode truncation bitstream
    for (uint32_t size = 1; size < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++size) {
        //for (uint32_t size = Frame_Sample_1_16x16_8bit_422_bitstream_size - 1; size > 0; --size) {
        printf("Size: %u\n", size);
        memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);
        memset(buffer + size, 0, Frame_Sample_1_16x16_8bit_422_bitstream_size - size);

        int32_t ret = test_bitstream_full(use_cpu_flags,
                                          Frame_Sample_1_16x16_8bit_422_bitstream,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                          buffer,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size);
        ASSERT_EQ(ret, SvtJxsErrorDecoderInvalidBitstream);
    }
    free(buffer);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillZero_C) {
    Test_Bitstream_1_TruncationBufferFillZero(0);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillZero_AVX2) {
    Test_Bitstream_1_TruncationBufferFillZero(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillZero_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_1_TruncationBufferFillZero(CPU_FLAGS_ALL);
    }
}

static void Test_Bitstream_1_TruncationBufferFillOne(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    uint8_t* buffer = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
    //Decode truncation bitstream
    for (uint32_t size = 1; size < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++size) {
        //for (uint32_t size = Frame_Sample_1_16x16_8bit_422_bitstream_size - 1; size > 0; --size) {
        printf("Size: %u\n", size);
        memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);
        memset(buffer + size, 1, Frame_Sample_1_16x16_8bit_422_bitstream_size - size);

        int32_t ret = test_bitstream_full(use_cpu_flags,
                                          Frame_Sample_1_16x16_8bit_422_bitstream,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                          buffer,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size);
        ASSERT_TRUE((ret == SvtJxsErrorDecoderBitstreamTooShort) || (ret == SvtJxsErrorDecoderInvalidBitstream));
    }
    free(buffer);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillOne_C) {
    Test_Bitstream_1_TruncationBufferFillOne(0);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillOne_AVX2) {
    Test_Bitstream_1_TruncationBufferFillOne(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_1_TruncationBufferFillOne_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_1_TruncationBufferFillOne(CPU_FLAGS_ALL);
    }
}

/*To detect all memory corruption, this test should be run with Valgrind tool.*/
static void Test_Bitstream_1_TruncationBufferCut_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);
    svt_jpeg_xs_image_config_t image_config;
    memset(&image_config, 0, sizeof(svt_jpeg_xs_image_config_t));

    //Decode truncation bitstream
    for (uint32_t size = 1; size < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++size) {
        //for (uint32_t size = Frame_Sample_1_16x16_8bit_422_bitstream_size - 1; size > 0; --size) {
        printf("Size: %u\n", size);
        uint8_t* buffer = (uint8_t*)malloc(size);
        memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, size);
        uint32_t frame_size = 0;
        SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
            buffer, size, &image_config, &frame_size, 0, proxy_mode_full);
        if (size > 0) {
            ASSERT_LT(ret, 0);
        }
        else {
            ASSERT_EQ(ret, 0);
        }

        int32_t ret2 = test_bitstream_full(
            use_cpu_flags, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size, buffer, size);
        ASSERT_EQ(ret2, SvtJxsErrorDecoderBitstreamTooShort);
        free(buffer);
    }
}

TEST(Decoder, Bitstream_1_TruncationBufferCut_valgrind_C) {
    Test_Bitstream_1_TruncationBufferCut_valgrind(0);
}

TEST(Decoder, Bitstream_1_TruncationBufferCut_valgrind_AVX2) {
    Test_Bitstream_1_TruncationBufferCut_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_1_TruncationBufferCut_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_1_TruncationBufferCut_valgrind(CPU_FLAGS_ALL);
    }
}

/*To detect all memory corruption, this test should be run with Valgrind tool.*/
static void Test_Bitstream_Corruprion_xor_bytes_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);

    //Corruption Bytes
    for (uint32_t i = 0; i < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++i) {
        printf("Size: %u\n", i);
        uint8_t* buffer = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
        memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);
        buffer[i] = ~buffer[i];

        int32_t ret = test_bitstream_full(use_cpu_flags,
                                          Frame_Sample_1_16x16_8bit_422_bitstream,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                          buffer,
                                          Frame_Sample_1_16x16_8bit_422_bitstream_size);

        ASSERT_TRUE(ret < INVALID_ERROR_CODE_START);
        free(buffer);
    }
}

TEST(Decoder, Bitstream_Corruprion_xor_bytes_valgrind_C) {
    Test_Bitstream_Corruprion_xor_bytes_valgrind(0);
}

TEST(Decoder, Bitstream_Corruprion_xor_bytes_valgrind_AVX2) {
    Test_Bitstream_Corruprion_xor_bytes_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_Corruprion_xor_bytes_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_Corruprion_xor_bytes_valgrind(CPU_FLAGS_ALL);
    }
}

static void xor_next_bits(uint8_t* buffer, uint32_t i, uint32_t max_size, uint32_t bit, uint8_t nbits) {
#define XOR_BIT(x, bit) x = ((x) & (~(1 << (bit)))) | ((~x) & ((1 << (bit))))

    uint32_t ki = i;
    uint32_t kj = bit;
    for (uint8_t bit_number = 0; bit_number < nbits; ++bit_number) {
        XOR_BIT(buffer[ki], kj);
        if (kj == 0) {
            kj = 7;
            if (++ki >= max_size) {
                break;
            }
        }
        else {
            kj--;
        }
    }
}

/*To detect all memory corruption, this test should be run with Valgrind tool.*/
static void Test_Bitstream_Corruprion_xor_bits_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);

    //Corruption Bits
    for (uint8_t test_bits = 1; test_bits < 8; ++test_bits) {
        for (uint32_t i = 0; i < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++i) {
            for (uint32_t j = 0; j < 8; ++j) {
                printf("test_bits %u, Start Byte: %u Start Bit: %u\n", test_bits, i, j);
                uint8_t* buffer = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
                memcpy(buffer, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);
                xor_next_bits(buffer, i, Frame_Sample_1_16x16_8bit_422_bitstream_size, 7 - j, 1);

                int32_t ret = test_bitstream_simple(Frame_Sample_1_16x16_8bit_422_bitstream,
                                                    Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                                    buffer,
                                                    Frame_Sample_1_16x16_8bit_422_bitstream_size);
                /*Should not return invalid error code, -100, -200.. etc.*/
                ASSERT_TRUE(ret < INVALID_ERROR_CODE_START);
                free(buffer);
            }
        }
    }
}

TEST(Decoder, Bitstream_Corruprion_xor_bits_valgrind_C) {
    Test_Bitstream_Corruprion_xor_bits_valgrind(0);
}

TEST(Decoder, Bitstream_Corruprion_xor_bits_valgrind_AVX2) {
    Test_Bitstream_Corruprion_xor_bits_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_Corruprion_xor_bits_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_Corruprion_xor_bits_valgrind(CPU_FLAGS_ALL);
    }
}

/*To detect all memory corruption, this test should be run with Valgrind tool.*/
static void Test_Bitstream_Corruprion_max_bits_valgrind(uint64_t use_cpu_flags) {
    decoder_simple_init_rtcd(use_cpu_flags);

    int changes = 0;
    uint8_t* buffer_decodable = (uint8_t*)malloc(Frame_Sample_1_16x16_8bit_422_bitstream_size);
    memcpy(buffer_decodable, Frame_Sample_1_16x16_8bit_422_bitstream, Frame_Sample_1_16x16_8bit_422_bitstream_size);

    //Corruption Bits
    for (uint32_t i = 0; i < Frame_Sample_1_16x16_8bit_422_bitstream_size; ++i) {
        for (uint32_t j = 0; j < 8; ++j) {
            printf("Start Byte: %u Start Bit: %u\n", i, j);
            xor_next_bits(buffer_decodable, i, Frame_Sample_1_16x16_8bit_422_bitstream_size, 7 - j, 1);
            changes++;

            int32_t ret = test_bitstream_simple(Frame_Sample_1_16x16_8bit_422_bitstream,
                                                Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                                buffer_decodable,
                                                Frame_Sample_1_16x16_8bit_422_bitstream_size);
            /*Should not return invalid error code, -100, -200.. etc.*/
            ASSERT_TRUE(ret < INVALID_ERROR_CODE_START);
            if (ret < 0) {
                changes--;
                xor_next_bits(buffer_decodable, i, Frame_Sample_1_16x16_8bit_422_bitstream_size, 7 - j, 1);
            }
        }
    }

    int32_t ret = test_bitstream_full(use_cpu_flags,
                                      Frame_Sample_1_16x16_8bit_422_bitstream,
                                      Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                      buffer_decodable,
                                      Frame_Sample_1_16x16_8bit_422_bitstream_size);
    /*Should not return invalid error code, -100, -200.. etc.*/
    ASSERT_TRUE(ret == 0);

    free(buffer_decodable);
}

TEST(Decoder, Bitstream_Corruprion_max_bits_valgrind_C) {
    Test_Bitstream_Corruprion_max_bits_valgrind(0);
}

TEST(Decoder, Bitstream_Corruprion_max_bits_valgrind_AVX2) {
    Test_Bitstream_Corruprion_max_bits_valgrind(CPU_FLAGS_AVX2);
}

TEST(Decoder, Bitstream_Corruprion_max_bits_valgrind_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        Test_Bitstream_Corruprion_max_bits_valgrind(CPU_FLAGS_ALL);
    }
}
