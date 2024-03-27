/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif
// int abc(int a);
#ifdef __cplusplus
}
#endif

TEST(SampleUT, Sample1) {
    ASSERT_EQ(1, 1);
    ASSERT_NE(1, 0);
}

TEST(SampleUT, Sample2) {
    ASSERT_EQ(1, 1);
    ASSERT_NE(1, 0);
}

TEST(SampleUT, sample_test) {
    //Test with test sample function from encoder
    pi_t pi;
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 2, 2};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1};
    int ret = pi_compute(&pi, 1 /*Init encoder*/, 3, 4, 8, 200, 100, 5, 2, 0, sx, sy, 4, 16);
    EXPECT_EQ(ret, 0);

    ASSERT_EQ(pi.comps_num, 3);
}
