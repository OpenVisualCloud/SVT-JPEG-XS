/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include <immintrin.h>
#include "Dwt53Decoder_AVX2.h"
#include "SvtUtility.h"
#include "Dwt.h"
#include "Idwt.h"
#include "CodeDeprecated.h"

typedef ::testing::tuple<int, int> size_param_t;

// clang-format off
static size_param_t params_block_sizes[] = {
    {8, 8}, {32, 32},
    {30, 30}, {60, 60}, {120, 120}, {240, 240},
    {270,270}, {480, 480}, {540, 540}, {960, 960},
    /* Test to not squares */
    {30, 60}, {240, 120},
    {270,480}, {960, 540},
    /* Untypical sizes */
    {31, 62}, {243, 124},
    {275,486}, {967, 548}, {967, 549}
};
// clang-format on

typedef ::testing::tuple<size_param_t> fixture_param_t;

enum RAND_TYPE { RAND_CUSTOM = 0, RAND_ONE, RAND_ZERO, RAND_FULL };

class TransformIDWT53 : public ::testing::TestWithParam<fixture_param_t> {
  protected:
    static void SetUpTestCase() {
    } //Prepare data for all tests
    static void TearDownTestCase() {
    } //Release data for all tests
    virtual void SetUp() {
        rnd = new svt_jxs_test_tool::SVTRandom(32, false);

        fixture_param_t params = GetParam();
        size_param_t param_size = std::get<0>(params);
        len_width = std::get<0>(param_size);
        len_height = std::get<1>(param_size);
        stride_in = len_width + (rand() % 512);
        stride_out = stride_in + (rand() % 512);
        lenght = (len_height)*stride_out; //w*h
        buffer_len = lenght * sizeof(int32_t);

        lf_in = (int32_t*)malloc(buffer_len);
        hf_in = (int32_t*)malloc(buffer_len);
        data_out_c = (int32_t*)malloc(buffer_len);
        data_out_avx2 = (int32_t*)malloc(buffer_len);
    }

    virtual void TearDown() {
        free(lf_in);
        free(hf_in);
        free(data_out_c);
        free(data_out_avx2);
        delete rnd;
    }

    void SetRandData(RAND_TYPE type) {
        switch (type) {
        case RAND_ZERO:
            memset(lf_in, 0, buffer_len);
            memset(hf_in, 0, buffer_len);
            break;
        case RAND_ONE:
            for (int i = 0; i < lenght; ++i) {
                lf_in[i] = 0;
                hf_in[i] = 0;
            }
            break;
        case RAND_CUSTOM:
            for (int i = 0; i < lenght; ++i) {
                lf_in[i] = i;
                hf_in[i] = i + buffer_len;
            }
            break;
        case RAND_FULL:
            for (int i = 0; i < lenght; ++i) {
                lf_in[i] = rnd->random();
                hf_in[i] = rnd->random();
            }
            break;
        };
    }

    void run_test_vertical() {
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx2, 0, buffer_len);

        for (int i = 0; i < 5; ++i) {
            RAND_TYPE type = (RAND_TYPE)i;
            if (type > RAND_FULL) {
                type = RAND_FULL;
            }
            SetRandData(type);

            idwt_deprecated_vertical_c(lf_in, hf_in, data_out_c, len_width, len_height, stride_in, stride_in, stride_out);
            idwt_deprecated_vertical_avx2(lf_in, hf_in, data_out_avx2, len_width, len_height, stride_in, stride_in, stride_out);

            /*if (0 != memcmp(data_out_c, data_out_avx2, buffer_len)) {
                printf("Error: %i %i %i\n", len_width, len_height, param_stride);
            }*/
            ASSERT_EQ(0, memcmp(data_out_c, data_out_avx2, buffer_len));
        }
    }

    void run_test_vertical_speed() {
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx2, 0, buffer_len);
        SetRandData(RAND_FULL);
        int repeat = 600000 * 1000 / (len_width * len_height);
        uint64_t start_time_s, start_time_us, medium_time_s, medium_time_us, finish_time_s, finish_time_us;

        get_current_time(&start_time_s, &start_time_us);

        for (int i = 0; i < repeat; i++) {
            idwt_deprecated_vertical_c(lf_in, hf_in, data_out_c, len_width, len_height, stride_in, stride_in, stride_out);
        }

        get_current_time(&medium_time_s, &medium_time_us);

        for (int i = 0; i < repeat; i++) {
            idwt_deprecated_vertical_avx2(lf_in, hf_in, data_out_avx2, len_width, len_height, stride_in, stride_in, stride_out);
        }

        get_current_time(&finish_time_s, &finish_time_us);

        double time_c = compute_elapsed_time_in_ms(start_time_s, start_time_us, medium_time_s, medium_time_us);
        double time_o = compute_elapsed_time_in_ms(medium_time_s, medium_time_us, finish_time_s, finish_time_us);

        printf("idwt_vertical_avx2(%3dx%3d): %6.2f  time_c: %6.2f ms  time_o: %6.2f ms\n",
               len_width,
               len_height,
               time_c / time_o,
               time_c,
               time_o);

        ASSERT_EQ(0, memcmp(data_out_c, data_out_avx2, buffer_len));
    }

    void run_test_horizontal() {
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx2, 0, buffer_len);

        int32_t* buffer_temp = (int32_t*)malloc(sizeof(int32_t) * (len_width));

        for (int i = 0; i < 5; ++i) {
            RAND_TYPE type = (RAND_TYPE)i;
            if (type > RAND_FULL) {
                type = RAND_FULL;
            }
            SetRandData(type);

            idwt_deprecated_horizontal_c(lf_in, hf_in, data_out_c, len_width, len_height, stride_in, stride_in, stride_out);
            idwt_deprecated_horizontal_avx2(lf_in, hf_in, data_out_avx2, len_width, len_height, stride_in, stride_in, stride_out);

            /*if (0 != memcmp(data_out_c, data_out_avx2, buffer_len)) {
                printf("Error: %i %i %i\n", len_width, len_height, param_stride);
            }*/

            EXPECT_EQ(0, memcmp(data_out_c, data_out_avx2, buffer_len));
        }

        free(buffer_temp);
    }

    double horizontal_speed_case(int low_info) {
        int repeat = 80000 * 1000 / (len_width * len_height);
        uint64_t start_time_s, start_time_us, medium_time_s, medium_time_us, finish_time_s, finish_time_us;
        int32_t* buffer_temp = (int32_t*)malloc(sizeof(int32_t) * (len_width));

        get_current_time(&start_time_s, &start_time_us);

        for (int i = 0; i < repeat; i++) {
            idwt_deprecated_horizontal_c(lf_in, hf_in, data_out_c, len_width, len_height, stride_in, stride_in, stride_out);
        }

        get_current_time(&medium_time_s, &medium_time_us);

        for (int i = 0; i < repeat; i++) {
            idwt_deprecated_horizontal_avx2(lf_in, hf_in, data_out_avx2, len_width, len_height, stride_in, stride_in, stride_out);
        }

        get_current_time(&finish_time_s, &finish_time_us);

        double time_c = compute_elapsed_time_in_ms(start_time_s, start_time_us, medium_time_s, medium_time_us);
        double time_o = compute_elapsed_time_in_ms(medium_time_s, medium_time_us, finish_time_s, finish_time_us);
        if (low_info == 0) {
            printf("%6.2f  time_c: %6.2f ms  time_o: %6.2f ms", time_c / time_o, time_c, time_o);
        }
        EXPECT_EQ(0, memcmp(data_out_c, data_out_avx2, buffer_len));

        free(buffer_temp);
        return time_c / time_o;
    }

    void run_test_horizontal_speed() {
        memset(data_out_c, 0, buffer_len);
        memset(data_out_avx2, 0, buffer_len);
        SetRandData(RAND_FULL);
        int low_info = 1;

        printf("idwt53_v_avx2(%3dx%3d): ", len_width, len_height);

        double speed = horizontal_speed_case(low_info);
        if (low_info == 0) {
            printf("\n");
        }
        else {
            printf("%.1f ", speed);
        }

        if (low_info != 0) {
            printf("\n");
        }
    }

  protected:
    int len_width;
    int len_height;
    int stride_in;
    int stride_out;
    int lenght;
    int buffer_len;
    int32_t* lf_in;
    int32_t* hf_in;
    int32_t* data_out_c;
    int32_t* data_out_avx2;
    svt_jxs_test_tool::SVTRandom* rnd;
};

TEST_P(TransformIDWT53, vertical_AVX2) {
    run_test_vertical();
}

TEST_P(TransformIDWT53, DISABLED_speed_vertical_AVX2) {
    run_test_vertical_speed();
}

TEST_P(TransformIDWT53, horizontal_AVX2) {
    run_test_horizontal();
}

TEST_P(TransformIDWT53, DISABLED_speed_horizontal_AVX2) {
    run_test_horizontal_speed();
}

INSTANTIATE_TEST_SUITE_P(TransformIDWT53, TransformIDWT53, ::testing::Combine(::testing::ValuesIn(params_block_sizes)));
