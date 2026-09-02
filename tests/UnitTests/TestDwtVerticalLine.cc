/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The vertical half of the forward 5/3 transform: seven elementwise kernels that
 * differ only in which neighbouring lines a coefficient is made of. They were
 * covered only through a full-frame encode (TestDwtFrame.cc), which says that
 * the seven agree with each other but not that any one of them agrees with its
 * own reference.
 *
 * What is checked: every width from zero up, so the tail past the last full
 * vector is walked at each of its lengths rather than at one convenient one;
 * both output rows are checked past the width for not having been written; and
 * the samples are signed, because the lifting shifts a signed value right and
 * that rounds towards minus infinity - a coefficient made of negative samples is
 * where an implementation that divides instead of shifting parts from the
 * reference. */

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

/* Bounded rather than fully random: the lifting adds two samples together, and a
 * pair near INT32_MAX would overflow in the C reference itself. */
const int kSampleBits = 24;
const uint32_t kMaxWidth = 71;
const int32_t kMarker = 0x5A5A5A5A;

typedef void (*fn_hf_line_0)(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1);
typedef void (*fn_lf_line_0)(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0);
typedef void (*fn_lf_hf_line_0)(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1,
                                const int32_t* line_2);
typedef void (*fn_lf_hf_line_x_prev)(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                     const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                     const int32_t* line_p2);
typedef void (*fn_lf_hf_hf_line_x)(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                   const int32_t* line_0, const int32_t* line_1, const int32_t* line_2);
typedef void (*fn_lf_hf_hf_last_even)(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                      const int32_t* line_0, const int32_t* line_1);
typedef void (*fn_recalc_hf_prev)(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0, const int32_t* line_1,
                                  const int32_t* line_2);

class DwtVerticalLine : public ::testing::Test {
  protected:
    void SetUp() override {
        rnd_ = new svt_jxs_test_tool::SVTRandom(kSampleBits, true);
    }
    void TearDown() override {
        delete rnd_;
    }

    /* Fresh samples, and both output rows filled with the marker so that a write
     * past the width is visible whatever the implementation would have put there. */
    void arrange() {
        for (uint32_t line = 0; line < kLines; line++) {
            for (uint32_t i = 0; i < kMaxWidth; i++) {
                in_[line][i] = rnd_->random();
            }
        }
        for (uint32_t i = 0; i < kMaxWidth; i++) {
            lf_ref_[i] = lf_mod_[i] = kMarker;
            hf_ref_[i] = hf_mod_[i] = kMarker;
        }
    }

    void assert_row(const int32_t* ref, const int32_t* mod, uint32_t width, const char* what, const char* row) {
        for (uint32_t i = 0; i < kMaxWidth; i++) {
            ASSERT_EQ(ref[i], mod[i]) << what << " " << row << " index " << i << " width " << width;
            if (i >= width) {
                ASSERT_EQ(kMarker, mod[i]) << what << " wrote past the " << row << " row, width " << width;
            }
        }
    }

    void run_hf_line_0(fn_hf_line_0 fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_hf_line_0_c(width, hf_ref_, in_[0], in_[1]);
            fn(width, hf_mod_, in_[0], in_[1]);
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    void run_lf_line_0(fn_lf_line_0 fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_lf_line_0_c(width, lf_ref_, in_[0], in_[1]);
            fn(width, lf_mod_, in_[0], in_[1]);
            assert_row(lf_ref_, lf_mod_, width, what, "lf");
        }
    }

    void run_lf_hf_line_0(fn_lf_hf_line_0 fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_lf_hf_line_0_c(width, lf_ref_, hf_ref_, in_[0], in_[1], in_[2]);
            fn(width, lf_mod_, hf_mod_, in_[0], in_[1], in_[2]);
            assert_row(lf_ref_, lf_mod_, width, what, "lf");
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    void run_lf_hf_line_x_prev(fn_lf_hf_line_x_prev fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_lf_hf_line_x_prev_c(width, lf_ref_, hf_ref_, in_[0], in_[1], in_[2], in_[3], in_[4]);
            fn(width, lf_mod_, hf_mod_, in_[0], in_[1], in_[2], in_[3], in_[4]);
            assert_row(lf_ref_, lf_mod_, width, what, "lf");
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    void run_lf_hf_hf_line_x(fn_lf_hf_hf_line_x fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_lf_hf_hf_line_x_c(width, lf_ref_, hf_ref_, in_[0], in_[1], in_[2], in_[3]);
            fn(width, lf_mod_, hf_mod_, in_[0], in_[1], in_[2], in_[3]);
            assert_row(lf_ref_, lf_mod_, width, what, "lf");
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    void run_lf_hf_hf_last_even(fn_lf_hf_hf_last_even fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_vertical_loop_lf_hf_hf_line_last_even_c(width, lf_ref_, hf_ref_, in_[0], in_[1], in_[2]);
            fn(width, lf_mod_, hf_mod_, in_[0], in_[1], in_[2]);
            assert_row(lf_ref_, lf_mod_, width, what, "lf");
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    void run_recalc_hf_prev(fn_recalc_hf_prev fn, const char* what) {
        for (uint32_t width = 0; width <= kMaxWidth; width++) {
            arrange();
            transform_V1_Hx_precinct_recalc_HF_prev_c(width, hf_ref_, in_[0], in_[1], in_[2]);
            fn(width, hf_mod_, in_[0], in_[1], in_[2]);
            assert_row(hf_ref_, hf_mod_, width, what, "hf");
        }
    }

    static const uint32_t kLines = 5;
    svt_jxs_test_tool::SVTRandom* rnd_;
    int32_t in_[kLines][kMaxWidth];
    int32_t lf_ref_[kMaxWidth];
    int32_t hf_ref_[kMaxWidth];
    int32_t lf_mod_[kMaxWidth];
    int32_t hf_mod_[kMaxWidth];
};

#define VERTICAL_SUITE(name, suffix)                                                                        \
    TEST_F(DwtVerticalLine, name) {                                                                         \
        run_hf_line_0(transform_vertical_loop_hf_line_0_##suffix, "hf_line_0_" #suffix);                     \
        run_lf_line_0(transform_vertical_loop_lf_line_0_##suffix, "lf_line_0_" #suffix);                     \
        run_lf_hf_line_0(transform_vertical_loop_lf_hf_line_0_##suffix, "lf_hf_line_0_" #suffix);            \
        run_lf_hf_line_x_prev(transform_vertical_loop_lf_hf_line_x_prev_##suffix, "lf_hf_line_x_prev_" #suffix); \
        run_lf_hf_hf_line_x(transform_vertical_loop_lf_hf_hf_line_x_##suffix, "lf_hf_hf_line_x_" #suffix);   \
        run_lf_hf_hf_last_even(transform_vertical_loop_lf_hf_hf_line_last_even_##suffix,                     \
                               "lf_hf_hf_line_last_even_" #suffix);                                          \
        run_recalc_hf_prev(transform_V1_Hx_precinct_recalc_HF_prev_##suffix, "recalc_HF_prev_" #suffix);     \
    }

/* The C implementations against themselves: establishes that the harness reaches
 * every width and that none of the seven writes past the width it was given. */
VERTICAL_SUITE(C, c)

#ifdef ARCH_X86_64
VERTICAL_SUITE(AVX2, avx2)

TEST_F(DwtVerticalLine, AVX512) {
    /* Same condition the encoder dispatch uses: the AVX-512 directory is built
     * with -mbmi2 and -mpopcnt, so the tier is only usable when the processor
     * has those too. */
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    run_hf_line_0(transform_vertical_loop_hf_line_0_avx512, "hf_line_0_avx512");
    run_lf_line_0(transform_vertical_loop_lf_line_0_avx512, "lf_line_0_avx512");
    run_lf_hf_line_0(transform_vertical_loop_lf_hf_line_0_avx512, "lf_hf_line_0_avx512");
    run_lf_hf_line_x_prev(transform_vertical_loop_lf_hf_line_x_prev_avx512, "lf_hf_line_x_prev_avx512");
    run_lf_hf_hf_line_x(transform_vertical_loop_lf_hf_hf_line_x_avx512, "lf_hf_hf_line_x_avx512");
    run_lf_hf_hf_last_even(transform_vertical_loop_lf_hf_hf_line_last_even_avx512, "lf_hf_hf_line_last_even_avx512");
    run_recalc_hf_prev(transform_V1_Hx_precinct_recalc_HF_prev_avx512, "recalc_HF_prev_avx512");
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
VERTICAL_SUITE(NEON, neon)
#endif /* ARCH_AARCH64 */

} // namespace
