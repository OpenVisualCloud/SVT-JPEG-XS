/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "GcStageProcess.h"
#include "RateControl_avx2.h"
#include "Enc_avx512.h"
#include "encoder_dsp_rtcd.h"

void test_packed_to_planar_rgb_8bit(void (*test_fn)(const void*, void*, void*, void*, uint32_t)) {
    const uint32_t width_max = 1999;
    const uint32_t packed_width_max = width_max * 3;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(16, false);

    uint8_t* src = (uint8_t*)malloc(packed_width_max * sizeof(uint8_t));

    uint8_t* dst_ref1 = (uint8_t*)malloc(width_max * sizeof(uint8_t));
    uint8_t* dst_ref2 = (uint8_t*)malloc(width_max * sizeof(uint8_t));
    uint8_t* dst_ref3 = (uint8_t*)malloc(width_max * sizeof(uint8_t));
    uint8_t* dst_mod1 = (uint8_t*)malloc(width_max * sizeof(uint8_t));
    uint8_t* dst_mod2 = (uint8_t*)malloc(width_max * sizeof(uint8_t));
    uint8_t* dst_mod3 = (uint8_t*)malloc(width_max * sizeof(uint8_t));

    for (uint32_t w = 1950; w < width_max; w++) {
        memset(dst_ref1, 0xcd, width_max * sizeof(uint8_t));
        memset(dst_ref2, 0xcd, width_max * sizeof(uint8_t));
        memset(dst_ref3, 0xcd, width_max * sizeof(uint8_t));
        memset(dst_mod1, 0xcd, width_max * sizeof(uint8_t));
        memset(dst_mod2, 0xcd, width_max * sizeof(uint8_t));
        memset(dst_mod3, 0xcd, width_max * sizeof(uint8_t));
        for (uint32_t i = 0; i < packed_width_max; i++) {
            src[i] = rnd->Rand8();
        }

        convert_packed_to_planar_rgb_8bit_c(src, dst_ref1, dst_ref2, dst_ref3, w);
        test_fn(src, dst_mod1, dst_mod2, dst_mod3, w);

        ASSERT_EQ(memcmp(dst_ref1, dst_mod1, sizeof(uint8_t) * width_max), 0);
        ASSERT_EQ(memcmp(dst_ref2, dst_mod2, sizeof(uint8_t) * width_max), 0);
        ASSERT_EQ(memcmp(dst_ref3, dst_mod3, sizeof(uint8_t) * width_max), 0);
    }

    free(src);
    free(dst_ref1);
    free(dst_ref2);
    free(dst_ref3);
    free(dst_mod1);
    free(dst_mod2);
    free(dst_mod3);

    delete rnd;
}

TEST(test_packed_to_planar_rgb_8bit, AVX2) {
    test_packed_to_planar_rgb_8bit(convert_packed_to_planar_rgb_8bit_avx2);
}

TEST(test_packed_to_planar_rgb_8bit, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_packed_to_planar_rgb_8bit(convert_packed_to_planar_rgb_8bit_avx512);
    }
}

void test_packed_to_planar_rgb_16bit(void (*test_fn)(const void*, void*, void*, void*, uint32_t)) {
    const uint32_t width_max = 1999;
    const uint32_t packed_width_max = width_max * 3;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(16, false);

    uint16_t* src = (uint16_t*)malloc(packed_width_max * sizeof(uint16_t));

    uint16_t* dst_ref1 = (uint16_t*)malloc(width_max * sizeof(uint16_t));
    uint16_t* dst_ref2 = (uint16_t*)malloc(width_max * sizeof(uint16_t));
    uint16_t* dst_ref3 = (uint16_t*)malloc(width_max * sizeof(uint16_t));
    uint16_t* dst_mod1 = (uint16_t*)malloc(width_max * sizeof(uint16_t));
    uint16_t* dst_mod2 = (uint16_t*)malloc(width_max * sizeof(uint16_t));
    uint16_t* dst_mod3 = (uint16_t*)malloc(width_max * sizeof(uint16_t));

    for (uint32_t w = 1950; w < width_max; w++) {
        memset(dst_ref1, 0xcd, width_max * sizeof(uint16_t));
        memset(dst_ref2, 0xcd, width_max * sizeof(uint16_t));
        memset(dst_ref3, 0xcd, width_max * sizeof(uint16_t));
        memset(dst_mod1, 0xcd, width_max * sizeof(uint16_t));
        memset(dst_mod2, 0xcd, width_max * sizeof(uint16_t));
        memset(dst_mod3, 0xcd, width_max * sizeof(uint16_t));
        for (uint32_t i = 0; i < packed_width_max; i++) {
            src[i] = rnd->Rand16();
        }

        convert_packed_to_planar_rgb_16bit_c(src, dst_ref1, dst_ref2, dst_ref3, w);
        test_fn(src, dst_mod1, dst_mod2, dst_mod3, w);

        ASSERT_EQ(memcmp(dst_ref1, dst_mod1, sizeof(uint16_t) * width_max), 0);
        ASSERT_EQ(memcmp(dst_ref2, dst_mod2, sizeof(uint16_t) * width_max), 0);
        ASSERT_EQ(memcmp(dst_ref3, dst_mod3, sizeof(uint16_t) * width_max), 0);
    }

    free(src);
    free(dst_ref1);
    free(dst_ref2);
    free(dst_ref3);
    free(dst_mod1);
    free(dst_mod2);
    free(dst_mod3);

    delete rnd;
}

TEST(test_packed_to_planar_rgb_16bit, AVX2) {
    test_packed_to_planar_rgb_16bit(convert_packed_to_planar_rgb_16bit_avx2);
}

TEST(test_packed_to_planar_rgb_16bit, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_packed_to_planar_rgb_16bit(convert_packed_to_planar_rgb_16bit_avx512);
    }
}
