/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "Dwt.h"
#include "Idwt.h"
#include "Enc_avx512.h"
#include "Dwt53Decoder_AVX2.h"
#include "SvtUtility.h"
#include "CodeDeprecated.h"
#include "CodeDeprecated-avx512.h"

typedef ::testing::tuple<int, int> size_param_t;

// clang-format off
static size_param_t params_block_sizes[] = {
    {30, 30}, {60, 60}, {120, 120}, {240, 240},
    {270,270}, {480, 480}, {540, 540}, {960, 960},
    /* Test to not squares */
    {30, 60}, {240, 120},
    {270,480}, {960, 540},
    /* Untypical sizes */
    {30, 31}, {30, 32}, {30, 33}, {30, 34}, {30, 35}, {30, 36}, {30, 37}, {30, 38}, {30, 39}, {30, 40}, {30, 41}, {30, 42},
    {30, 43}, {30, 44}, {30, 45}, {30, 46}, {32, 31}, {32, 32}, {32, 33}, {32, 34}, {32, 35}, {32, 36}, {32, 37}, {32, 38},
    {32, 39}, {32, 40}, {32, 41}, {32, 42}, {32, 43}, {32, 44}, {32, 45}, {32, 46}, {30, 32}, {31, 32}, {32, 32}, {33, 32},
    {34, 32}, {35, 32}, {36, 32}, {37, 32}, {38, 32}, {39, 32}, {40, 32}, {41, 32}, {42, 32}, {43, 32}, {44, 32}, {45, 32},
    {46, 32}, {31, 62}, {243, 124},
    {275,486}, {967, 548} , {967, 549},
    {2, 2},{3, 3},{4, 4},{5, 5},{6, 6},{7, 7},{8, 8},{9, 9},{10, 10},
    {11, 11},{12, 12},{13, 13},{14, 14},{15, 15},{16, 16},{17, 17},{18, 18},{19, 19},{20, 20},
    {21, 21},{22, 22},{23, 23},{24, 24},{25, 25},{26, 26},{27, 27},{28, 28},{29, 29},{30, 30},
    {31, 31},{32, 32},{33, 33},{34, 34},{35, 35},{36, 36},{37, 37},{38, 38},{39, 39},{40, 40},
    {41, 41},{42, 42},{43, 43},{44, 44},{45, 45},{46, 46},{47, 47},{48, 48},{49, 49},{50, 50},
    {51, 51},{52, 52},{53, 53},{54, 54},{55, 55},{56, 56},{57, 57},{58, 58},{59, 59},{60, 60},
    {61, 61},{62, 62},{63, 63},{64, 64},{65, 65},{66, 66},{67, 67},{68, 68},{69, 69},{70, 70},
};
// clang-format on

typedef ::testing::tuple<size_param_t> fixture_param_t;

class DWT_IDWT : public ::testing::TestWithParam<fixture_param_t> {
  protected:
    virtual void SetUp() {
        rnd = new svt_jxs_test_tool::SVTRandom(32, false);

        fixture_param_t params = GetParam();
        size_param_t param_size = std::get<0>(params);
        width = std::get<0>(param_size);
        height = std::get<1>(param_size);
        stride = width + (rand() % 1024);
        lenght = (height)*stride; //w*h
        buffer_len = lenght * sizeof(int32_t);

        lf_buf_c = (int32_t*)malloc(buffer_len);
        hf_buf_c = (int32_t*)malloc(buffer_len);
        lf_buf_avx = (int32_t*)malloc(buffer_len);
        hf_buf_avx = (int32_t*)malloc(buffer_len);
        data_in = (int32_t*)malloc(buffer_len);
        data_out_c = (int32_t*)malloc(buffer_len);
        data_out_avx = (int32_t*)malloc(buffer_len);
    }

    virtual void TearDown() {
        free(lf_buf_c);
        free(hf_buf_c);
        free(lf_buf_avx);
        free(hf_buf_avx);
        free(data_in);
        free(data_out_c);
        free(data_out_avx);
        delete rnd;
    }

    void SetRandData() {
        for (int32_t i = 0; i < lenght; i++) {
            data_in[i] = rnd->random() % 0xFFFF;
        }
    }

    void run_test_dwt_vertical_avx512() {
        memset(lf_buf_c, 0, buffer_len);
        memset(hf_buf_c, 0, buffer_len);
        memset(lf_buf_avx, 0, buffer_len);
        memset(hf_buf_avx, 0, buffer_len);

        for (int i = 0; i < 5; ++i) {
            SetRandData();

            dwt_vertical_depricated_c(lf_buf_c, hf_buf_c, data_in, width, height, stride, stride, stride);
            dwt_vertical_depricated_avx512(lf_buf_avx, hf_buf_avx, data_in, width, height, stride, stride, stride);

            ASSERT_EQ(0, memcmp(lf_buf_c, lf_buf_avx, buffer_len));
            ASSERT_EQ(0, memcmp(hf_buf_c, hf_buf_avx, buffer_len));
        }
    }

    void run_test_dwt_horizontal_avx512() {
        memset(lf_buf_c, 0, buffer_len);
        memset(hf_buf_c, 0, buffer_len);
        memset(lf_buf_avx, 0, buffer_len);
        memset(hf_buf_avx, 0, buffer_len);

        for (int i = 0; i < 5; ++i) {
            SetRandData();

            dwt_horizontal_depricated_c(lf_buf_c, hf_buf_c, data_in, width, height, stride, stride, stride);
            dwt_horizontal_depricated_avx512(lf_buf_avx, hf_buf_avx, data_in, width, height, stride, stride, stride);

            ASSERT_EQ(0, memcmp(lf_buf_c, lf_buf_avx, buffer_len));
            ASSERT_EQ(0, memcmp(hf_buf_c, hf_buf_avx, buffer_len));
        }
    }

    void run_test_idwt_vertical_avx2() {
        memset(lf_buf_c, 0, buffer_len);
        memset(hf_buf_c, 0, buffer_len);
        memset(lf_buf_avx, 0, buffer_len);
        memset(hf_buf_avx, 0, buffer_len);
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx, 0, buffer_len);

        for (int i = 0; i < 5; ++i) {
            SetRandData();

            idwt_deprecated_vertical_c(lf_buf_c, hf_buf_c, data_out_c, width, height, stride, stride, stride);
            idwt_deprecated_vertical_avx2(lf_buf_c, hf_buf_c, data_out_avx, width, height, stride, stride, stride);

            ASSERT_EQ(0, memcmp(data_out_c, data_out_avx, buffer_len));
        }
    }

    void run_test_idwt_horizontal_avx2() {
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx, 0, buffer_len);

        SetRandData();

        idwt_deprecated_horizontal_avx2(lf_buf_c, hf_buf_c, data_out_c, width, height, stride, stride, stride);
        idwt_deprecated_horizontal_c(lf_buf_c, hf_buf_c, data_out_c, width, height, stride, stride, stride);

        ASSERT_EQ(0, memcmp(data_out_c, data_out_avx, buffer_len));
    }

    void run_test_vertical_c() {
        for (int i = 0; i < 5; ++i) {
            SetRandData();

            dwt_vertical_depricated_c(lf_buf_c, hf_buf_c, data_in, width, height, stride, stride, stride);
            idwt_deprecated_vertical_c(lf_buf_c, hf_buf_c, data_out_c, width, height, stride, stride, stride);

            for (int j = 0; j < width; j++) {
                for (int k = 0; k < height; k++) {
                    int idx = j + k * stride;
                    int tmp = abs(data_out_c[idx] - data_in[idx]);
                    if (tmp > 0) // May be that 0 should be changed to 1 because of approximations in dwt/idwt calculation results
                    {
                        ASSERT_EQ(0, tmp);
                    }
                }
            }
        }
    }

    void run_test_horizontal_c() {
        for (int i = 0; i < 5; ++i) {
            SetRandData();

            dwt_horizontal_depricated_c(lf_buf_c, hf_buf_c, data_in, width, height, stride, stride, stride);
            idwt_deprecated_horizontal_c(lf_buf_c, hf_buf_c, data_out_c, width, height, stride, stride, stride);

            for (int k = 0; k < height; k++) {
                for (int j = 0; j < width; j++) {
                    int idx = j + k * stride;
                    int tmp = abs(data_out_c[idx] - data_in[idx]);
                    if (tmp > 0) // May be that 0 should be changed to 1 because of approximations in dwt/idwt calculation results
                    {
                        ASSERT_EQ(0, tmp);
                    }
                }
            }
        }
    }

  protected:
    int width;
    int height;
    int stride;
    int lenght;
    int buffer_len;
    int32_t* data_in;
    int32_t* lf_buf_c;
    int32_t* hf_buf_c;
    int32_t* lf_buf_avx;
    int32_t* hf_buf_avx;
    int32_t* data_out_c;
    int32_t* data_out_avx;
    svt_jxs_test_tool::SVTRandom* rnd;
};

TEST_P(DWT_IDWT, VERTICAL_DWT_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        run_test_dwt_vertical_avx512();
    }
}

TEST_P(DWT_IDWT, HORIZONTAL_DWT_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        run_test_dwt_horizontal_avx512();
    }
}

TEST_P(DWT_IDWT, VERTICAL_IDWT_AVX2) {
    run_test_idwt_vertical_avx2();
}

TEST_P(DWT_IDWT, DISABLED_HORIZONTAL_IDWT_AVX2) {
    run_test_idwt_horizontal_avx2();
}

TEST_P(DWT_IDWT, VERTICAL_ALL_C) {
    run_test_vertical_c();
}

TEST_P(DWT_IDWT, HORIZONTAL_ALL_C) {
    run_test_horizontal_c();
}

INSTANTIATE_TEST_SUITE_P(DWT_IDWT, DWT_IDWT, ::testing::Combine(::testing::ValuesIn(params_block_sizes)));
