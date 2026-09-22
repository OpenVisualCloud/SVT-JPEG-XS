/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <vector>
#include "gtest/gtest.h"
#include "random.h"
#include "MctEnc.h"
#include "Mct.h"
#include "Definitions.h"

/* forward_rct_line() (encoder) must be the exact bit-for-bit inverse of inverse_rct()
 * (decoder). Verified algebraically (the (i1+i2)>>2 term is added on encode and subtracted
 * on decode using the same i1/i2, so it cancels regardless of rounding) - this test pins
 * that property against the real decoder implementation, isolated from lossy quantization
 * (this codec is lossy-only, so an end-to-end pixel-exact encode/decode check isn't
 * meaningful; see TestColorTransform.cc for the encode/decode smoke test instead). */
TEST(MctEnc, ForwardRctIsExactInverseOfDecoderInverseRct) {
    const int32_t w = 1999; //Odd width, matches TestNlt.cc's convention of avoiding accidental power-of-2 alignment bugs.
    const int32_t h = 7;
    const int32_t bw = 20; //Matches WAVELET_IN_DEPTH_BW_DEFAULT: the int32 domain this transform operates in.

    svt_jxs_test_tool::SVTRandom rnd(bw, /*is_signed=*/true);

    std::vector<int32_t> original[3];
    std::vector<int32_t> transformed[3];
    int32_t* comps[MAX_COMPONENTS_NUM] = {nullptr, nullptr, nullptr, nullptr};

    for (int c = 0; c < 3; c++) {
        original[c].resize((size_t)w * h);
        for (auto& v : original[c]) {
            v = rnd.random();
        }
        transformed[c] = original[c];
        comps[c] = transformed[c].data();
    }

    for (int32_t y = 0; y < h; y++) {
        forward_rct_line(comps[0] + (size_t)y * w, comps[1] + (size_t)y * w, comps[2] + (size_t)y * w, w);
    }

    pi_t pi;
    memset(&pi, 0, sizeof(pi));
    pi.width = w;
    pi.height = h;
    picture_header_dynamic_t picture_header_dynamic;
    memset(&picture_header_dynamic, 0, sizeof(picture_header_dynamic)); //Unused by the Cpih==1 (RCT) path.
    mct_inverse_transform(comps, &pi, &picture_header_dynamic, /*hdr_Cpih=*/1);

    for (int c = 0; c < 3; c++) {
        for (size_t i = 0; i < original[c].size(); i++) {
            ASSERT_EQ(original[c][i], transformed[c][i]) << "component " << c << " index " << i;
        }
    }
}
