/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include <immintrin.h>
#include "SvtUtility.h"
#include "DecHandle.h"
#include "SvtType.h"
#include "Dequant.h"
#include "Dequant_SSE4.h"
#include "Dequant_avx512.h"

enum RAND_TYPE { RAND_CUSTOM = 0, RAND_ONE, RAND_ZERO, RAND_FULL, RAND_SIZE };

class DequantFixture : public ::testing::TestWithParam<int> {
  protected:
    static svt_jxs_test_tool::SVTRandom* rnd;
    RAND_TYPE rand_type;
    size_t buffer_size;
    uint16_t* buf_c;
    uint16_t* buf_avx;
    uint8_t* gclis;

  protected:
    static void SetUpTestCase() {
        rnd = new svt_jxs_test_tool::SVTRandom(16, false);
    } //Prepare data for all tests​
    static void TearDownTestCase() {
        delete rnd;
    } //Release data for all tests
    virtual void SetUp() {
        buffer_size = 4098;

        rand_type = (enum RAND_TYPE)GetParam();

        buf_c = (uint16_t*)malloc(buffer_size * sizeof(uint16_t));
        buf_avx = (uint16_t*)malloc(buffer_size * sizeof(uint16_t));
        gclis = (uint8_t*)malloc(buffer_size * sizeof(uint8_t));
    }

    virtual void TearDown() {
        free(gclis);
        free(buf_avx);
        free(buf_c);
    }

    uint8_t SetRandData(RAND_TYPE type, int buf_len, int group_size) {
        uint8_t gtli = rnd->random() % 15;
        for (int j = 0; j < ((buf_len + group_size - 1) / group_size); ++j) {
            gclis[j] = rnd->random() % 31;
        }

        switch (type) {
        case RAND_ZERO:
            for (int j = 0; j < buf_len; ++j) {
                buf_c[j] = 0;
            }
            break;
        case RAND_ONE:
            for (int j = 0; j < buf_len; ++j) {
                buf_c[j] = 1;
            }
            break;
        case RAND_CUSTOM:
            for (int j = 0; j < buf_len; ++j) {
                buf_c[j] = 12345;
            }
            break;
        default: //RAND_FULL:
            for (int j = 0; j < buf_len; ++j) {
                buf_c[j] = rnd->random();
            }
            break;
        };

        memcpy(buf_avx, buf_c, buf_len * sizeof(buf_c[0]));
        return gtli;
    }

    void run_test(QUANT_TYPE dq_type, void (*fn_ptr)(uint16_t*, uint32_t, uint8_t*, uint32_t, uint8_t, QUANT_TYPE)) {
        int group_size = 4; //Now only support one group size 4
        for (int size_id = 0; size_id < 2; size_id++) {
            int buf_len = size_id ? 3840 : 4096;
            while (buf_len) {
                for (int i = 1; i < 10; ++i) {
                    uint8_t gtli = SetRandData(rand_type, buf_len, group_size);
                    dequant_c(buf_c, buf_len, gclis, group_size, gtli, dq_type);
                    fn_ptr(buf_avx, buf_len, gclis, group_size, gtli, dq_type);

                    if (0 != memcmp(buf_avx, buf_c, buf_len * sizeof(buf_c[0]))) {
                        printf("Error: %i %i %i\n", buf_len, i, i);
                    }

                    ASSERT_EQ(0, memcmp(buf_avx, buf_c, buf_len * sizeof(buf_c[0])));
                }
                buf_len = buf_len / 2;
            }
        }
    }

    void run_test_uniform(void (*fn_ptr)(uint16_t*, uint32_t, uint8_t*, uint32_t, uint8_t, QUANT_TYPE)) {
        run_test(QUANT_TYPE_UNIFORM, fn_ptr);
    }

    void run_test_deadzone(void (*fn_ptr)(uint16_t*, uint32_t, uint8_t*, uint32_t, uint8_t, QUANT_TYPE)) {
        run_test(QUANT_TYPE_DEADZONE, fn_ptr);
    }

    void run_test_speed(QUANT_TYPE dq_type, void (*fn_ptr)(uint16_t*, uint32_t, uint8_t*, uint32_t, uint8_t, QUANT_TYPE)) {
        int group_size = 4; //Now only support one group size 4
        int buf_len = 1920;
        uint8_t gtli = SetRandData(rand_type, buf_len, group_size);
        uint64_t start_time_s, start_time_us, medium_time_s, medium_time_us, finish_time_s, finish_time_us;
        int repeat = 100000;
        get_current_time(&start_time_s, &start_time_us);

        for (int i = 0; i < repeat; i++) {
            dequant_c(buf_c, buf_len, gclis, group_size, gtli, dq_type);
        }

        get_current_time(&medium_time_s, &medium_time_us);

        for (int i = 0; i < repeat; i++) {
            fn_ptr(buf_avx, buf_len, gclis, group_size, gtli, dq_type);
        }

        get_current_time(&finish_time_s, &finish_time_us);

        double time_c = compute_elapsed_time_in_ms(start_time_s, start_time_us, medium_time_s, medium_time_us);
        double time_o = compute_elapsed_time_in_ms(medium_time_s, medium_time_us, finish_time_s, finish_time_us);
        printf("dq_type %i, %6.2f  time_c: %6.2f ms  time_o: %6.2f ms\n", dq_type, time_c / time_o, time_c, time_o);
    }
};

svt_jxs_test_tool::SVTRandom* DequantFixture::rnd = NULL;

TEST_P(DequantFixture, Uniform_SSE4_1) {
    run_test_uniform(dequant_sse4_1);
}

TEST_P(DequantFixture, Deadzone_SSE4_1) {
    run_test_deadzone(dequant_sse4_1);
}

TEST_P(DequantFixture, Uniform_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        run_test_uniform(dequant_avx512);
    }
}

TEST_P(DequantFixture, Deadzone_AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        run_test_deadzone(dequant_avx512);
    }
}

TEST_P(DequantFixture, DISABLED_speed_SSE4_1) {
    run_test_speed(QUANT_TYPE_UNIFORM, dequant_sse4_1);
    run_test_speed(QUANT_TYPE_DEADZONE, dequant_sse4_1);
}

INSTANTIATE_TEST_SUITE_P(Dequant, DequantFixture, ::testing::Range(0, (int)RAND_SIZE));
