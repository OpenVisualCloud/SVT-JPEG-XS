/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The forward 5/3 transform of one line. Like its inverse, it had no test of its
 * own on any architecture and was only exercised through a full encode.
 *
 * What has to hold across a length is the placement as much as the arithmetic:
 * the shortest line, the first pair and the last one all follow rules of their
 * own, and which of them applies depends on whether the length is odd or even.
 * Lengths are therefore walked one by one from the shortest the function
 * accepts, and both output rows are checked past their end for not having been
 * written. */

#include "gtest/gtest.h"
#include "random.h"
#include "Dwt.h"
#include "SvtUtility.h"

#ifdef ARCH_X86_64
#include "Dwt_AVX2.h"
#include "Enc_avx512.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
#include "Dwt_neon.h"
#endif /* ARCH_AARCH64 */

namespace {

typedef void (*dwt_horizontal_line_fn)(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len);

/* Bounded rather than fully random: the lifting adds two samples together, and a
 * pair near INT32_MAX would overflow in the C reference itself. */
const int kSampleBits = 24;
const uint32_t kMaxLen = 71;
const int32_t kMarker = 0x5A5A5A5A;

class DwtHorizontalLine : public ::testing::Test {
  protected:
    void SetUp() override {
        rnd_ = new svt_jxs_test_tool::SVTRandom(kSampleBits, true);
    }
    void TearDown() override {
        delete rnd_;
    }

    void run(dwt_horizontal_line_fn fn, const char* what) {
        for (uint32_t len = 2; len <= kMaxLen; len++) {
            for (uint32_t i = 0; i < kMaxLen; i++) {
                in_[i] = rnd_->random();
            }
            for (uint32_t i = 0; i < kMaxLen; i++) {
                lf_ref_[i] = lf_mod_[i] = kMarker;
                hf_ref_[i] = hf_mod_[i] = kMarker;
            }

            dwt_horizontal_line_c(lf_ref_, hf_ref_, in_, len);
            fn(lf_mod_, hf_mod_, in_, len);

            /* how many coefficients of each kind a line of this length produces */
            const uint32_t lf_count = (len + 1) / 2;
            const uint32_t hf_count = len / 2;
            for (uint32_t i = 0; i < kMaxLen; i++) {
                ASSERT_EQ(lf_ref_[i], lf_mod_[i]) << what << " lf index " << i << " len " << len;
                ASSERT_EQ(hf_ref_[i], hf_mod_[i]) << what << " hf index " << i << " len " << len;
                if (i >= lf_count) {
                    ASSERT_EQ(kMarker, lf_mod_[i]) << what << " wrote past the low-pass row, len " << len;
                }
                if (i >= hf_count) {
                    ASSERT_EQ(kMarker, hf_mod_[i]) << what << " wrote past the high-pass row, len " << len;
                }
            }
        }
    }

    svt_jxs_test_tool::SVTRandom* rnd_;
    int32_t in_[kMaxLen];
    int32_t lf_ref_[kMaxLen];
    int32_t hf_ref_[kMaxLen];
    int32_t lf_mod_[kMaxLen];
    int32_t hf_mod_[kMaxLen];
};

/* The C implementation against itself establishes that the harness reaches every
 * branch and that neither row is written past the count its length implies. */
TEST_F(DwtHorizontalLine, C) {
    run(dwt_horizontal_line_c, "dwt_horizontal_line_c");
}

#ifdef ARCH_X86_64
TEST_F(DwtHorizontalLine, AVX2) {
    run(dwt_horizontal_line_avx2, "dwt_horizontal_line_avx2");
}

TEST_F(DwtHorizontalLine, AVX512) {
    /* Same condition the encoder dispatch uses: the AVX-512 directory is built
     * with -mbmi2 and -mpopcnt, so the tier is only usable when the processor
     * has those too. */
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    run(dwt_horizontal_line_avx512, "dwt_horizontal_line_avx512");
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST_F(DwtHorizontalLine, NEON) {
    run(dwt_horizontal_line_neon, "dwt_horizontal_line_neon");
}
#endif /* ARCH_AARCH64 */

} // namespace
