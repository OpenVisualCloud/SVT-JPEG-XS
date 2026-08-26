/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The vertical half of the inverse 5/3 transform. It had no test of its own:
 * the vector implementations were only ever exercised through a full decode,
 * which does not reach the corner cases - a component two lines high, the first
 * and the last precinct, an odd and an even height - on every input.
 *
 * Each case drives one branch of idwt_vertical_line and compares the rows it is
 * responsible for against the C implementation. The rows it must not touch are
 * filled with a marker and checked as well: the branches differ in how many of
 * the four output rows they write, and writing one row too many is the kind of
 * mistake a whole-decode test hides. */

#include "gtest/gtest.h"
#include "random.h"
#include "Idwt.h"
#include "SvtUtility.h"

#ifdef ARCH_X86_64
#include "Dwt53Decoder_AVX2.h"
#include "idwt-avx512.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
#include "Idwt_neon.h"
#endif /* ARCH_AARCH64 */

typedef void (*idwt_vertical_line_fn)(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                      uint32_t len, int32_t first_precinct, int32_t last_precinct, int32_t height);
typedef void (*idwt_vertical_line_recalc_fn)(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                             uint32_t len, uint32_t precinct_line_idx);
typedef void (*idwt_horizontal_lf16_fn)(const int16_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);
typedef void (*idwt_horizontal_lf32_fn)(const int32_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift);

namespace {

/* Coefficients are bounded rather than fully random: the lifting step adds two
 * of them together, and a pair near INT32_MAX would overflow in the C reference
 * itself, which is not what is being compared here. */
const int kCoeffBits = 24;
const uint32_t kMaxLen = 67;
const int32_t kMarker = 0x5A5A5A5A;

struct VerticalCase {
    int32_t first_precinct;
    int32_t last_precinct;
    int32_t height;
    /* which of the four output rows the branch is allowed to write */
    bool writes[4];
};

const VerticalCase kCases[] = {
    /* height == 2 */
    {0, 0, 2, {false, false, true, true}},
    {1, 1, 2, {false, false, true, true}},
    /* first precinct of the component */
    {1, 0, 9, {false, false, true, false}},
    /* last precinct, odd height */
    {0, 1, 9, {false, true, true, false}},
    /* last precinct, even height */
    {0, 1, 10, {false, true, true, true}},
    /* the ordinary precinct in the middle */
    {0, 0, 9, {false, true, true, false}},
    {0, 0, 10, {false, true, true, false}},
};

class IdwtVerticalLine : public ::testing::Test {
  protected:
    void SetUp() override {
        rnd_ = new svt_jxs_test_tool::SVTRandom(kCoeffBits, true);
    }
    void TearDown() override {
        delete rnd_;
    }

    void fill_input(uint32_t len) {
        for (uint32_t i = 0; i < len; i++) {
            in_lf_[i] = rnd_->random();
            in_hf0_[i] = rnd_->random();
            in_hf1_[i] = rnd_->random();
        }
    }

    /* The four output rows of both sides start from the same marker, so a row a
     * branch must not touch is compared against the marker and not against
     * whatever the other side happened to leave there. */
    void reset_output(uint32_t len) {
        for (uint32_t row = 0; row < 4; row++) {
            for (uint32_t i = 0; i < len; i++) {
                out_ref_[row][i] = kMarker;
                out_mod_[row][i] = kMarker;
            }
            ref_rows_[row] = out_ref_[row];
            mod_rows_[row] = out_mod_[row];
        }
    }

    void compare(const VerticalCase& c, uint32_t len, const char* what) {
        for (uint32_t row = 0; row < 4; row++) {
            for (uint32_t i = 0; i < len; i++) {
                ASSERT_EQ(out_ref_[row][i], out_mod_[row][i])
                    << what << " row " << row << " index " << i << " len " << len << " height " << c.height;
                if (!c.writes[row]) {
                    ASSERT_EQ(kMarker, out_mod_[row][i])
                        << what << " wrote row " << row << " which this branch must leave alone";
                }
            }
        }
    }

    void run(idwt_vertical_line_fn fn, const char* what) {
        for (size_t case_id = 0; case_id < sizeof(kCases) / sizeof(kCases[0]); case_id++) {
            const VerticalCase& c = kCases[case_id];
            for (uint32_t len = 2; len <= kMaxLen; len++) {
                fill_input(len);
                reset_output(len);
                idwt_vertical_line_c(in_lf_, in_hf0_, in_hf1_, ref_rows_, len, c.first_precinct, c.last_precinct, c.height);
                fn(in_lf_, in_hf0_, in_hf1_, mod_rows_, len, c.first_precinct, c.last_precinct, c.height);
                ASSERT_NO_FATAL_FAILURE(compare(c, len, what));
            }
        }
    }

    void run_recalc(idwt_vertical_line_recalc_fn fn, const char* what) {
        const VerticalCase writes_row_zero = {0, 0, 0, {true, false, false, false}};
        for (uint32_t precinct_line_idx = 0; precinct_line_idx < 4; precinct_line_idx++) {
            for (uint32_t len = 2; len <= kMaxLen; len++) {
                fill_input(len);
                reset_output(len);
                idwt_vertical_line_recalc_c(in_lf_, in_hf0_, in_hf1_, ref_rows_, len, precinct_line_idx);
                fn(in_lf_, in_hf0_, in_hf1_, mod_rows_, len, precinct_line_idx);
                ASSERT_NO_FATAL_FAILURE(compare(writes_row_zero, len, what));
            }
        }
    }

    svt_jxs_test_tool::SVTRandom* rnd_;
    int32_t in_lf_[kMaxLen];
    int32_t in_hf0_[kMaxLen];
    int32_t in_hf1_[kMaxLen];
    int32_t out_ref_[4][kMaxLen];
    int32_t out_mod_[4][kMaxLen];
    int32_t* ref_rows_[4];
    int32_t* mod_rows_[4];
};

/* The horizontal half. Both entry points read one low-pass and one high-pass row
 * and write a single interleaved row, so what has to hold across a length is the
 * placement as much as the arithmetic: the last output is produced by a rule of
 * its own that depends on whether the length is odd or even, and an
 * implementation that gets the general step right can still misplace that one.
 * Lengths are therefore walked one by one from the shortest the function accepts,
 * and the tail past the line is checked for not having been written. */
class IdwtHorizontalLine : public ::testing::Test {
  protected:
    void SetUp() override {
        rnd_ = new svt_jxs_test_tool::SVTRandom(kCoeffBits, true);
    }
    void TearDown() override {
        delete rnd_;
    }

    void fill_input(uint32_t len, uint8_t shift) {
        /* The 16-bit inputs are shifted left by shift before they are used, so
         * they are drawn narrow enough that the shift stays inside int32. */
        svt_jxs_test_tool::SVTRandom narrow(16 - shift > 1 ? 16 - shift : 1, true);
        for (uint32_t i = 0; i < kMaxLen; i++) {
            in_lf16_[i] = (int16_t)narrow.random();
            in_hf16_[i] = (int16_t)narrow.random();
            in_lf32_[i] = rnd_->random();
        }
        (void)len;
    }

    void reset_output() {
        for (uint32_t i = 0; i < kMaxLen * 2; i++) {
            out_ref_[i] = kMarker;
            out_mod_[i] = kMarker;
        }
    }

    void compare(uint32_t len, const char* what) {
        for (uint32_t i = 0; i < kMaxLen * 2; i++) {
            ASSERT_EQ(out_ref_[i], out_mod_[i]) << what << " index " << i << " len " << len;
            if (i >= len) {
                ASSERT_EQ(kMarker, out_mod_[i]) << what << " wrote past the line, index " << i << " len " << len;
            }
        }
    }

    svt_jxs_test_tool::SVTRandom* rnd_;
    int16_t in_lf16_[kMaxLen];
    int16_t in_hf16_[kMaxLen];
    int32_t in_lf32_[kMaxLen];
    int32_t out_ref_[kMaxLen * 2];
    int32_t out_mod_[kMaxLen * 2];
};

TEST_F(IdwtHorizontalLine, lf16_hf16_C) {
    for (uint8_t shift = 0; shift <= 4; shift++) {
        for (uint32_t len = 2; len <= kMaxLen; len++) {
            fill_input(len, shift);
            reset_output();
            idwt_horizontal_line_lf16_hf16_c(in_lf16_, in_hf16_, out_ref_, len, shift);
            idwt_horizontal_line_lf16_hf16_c(in_lf16_, in_hf16_, out_mod_, len, shift);
            ASSERT_NO_FATAL_FAILURE(compare(len, "idwt_horizontal_line_lf16_hf16_c"));
        }
    }
}

TEST_F(IdwtHorizontalLine, lf32_hf16_C) {
    for (uint8_t shift = 0; shift <= 4; shift++) {
        for (uint32_t len = 2; len <= kMaxLen; len++) {
            fill_input(len, shift);
            reset_output();
            idwt_horizontal_line_lf32_hf16_c(in_lf32_, in_hf16_, out_ref_, len, shift);
            idwt_horizontal_line_lf32_hf16_c(in_lf32_, in_hf16_, out_mod_, len, shift);
            ASSERT_NO_FATAL_FAILURE(compare(len, "idwt_horizontal_line_lf32_hf16_c"));
        }
    }
}

/* The C implementation against itself: not a tautology here, because what is
 * being established is that the harness drives every branch and that the rows
 * outside a branch's own are really left untouched. */
TEST_F(IdwtVerticalLine, C) {
    run(idwt_vertical_line_c, "idwt_vertical_line_c");
}

TEST_F(IdwtVerticalLine, recalc_C) {
    run_recalc(idwt_vertical_line_recalc_c, "idwt_vertical_line_recalc_c");
}

#ifdef ARCH_X86_64
TEST_F(IdwtVerticalLine, AVX2) {
    run(idwt_vertical_line_avx2, "idwt_vertical_line_avx2");
}

TEST_F(IdwtVerticalLine, recalc_AVX2) {
    run_recalc(idwt_vertical_line_recalc_avx2, "idwt_vertical_line_recalc_avx2");
}

TEST_F(IdwtVerticalLine, AVX512) {
    /* Same condition the decoder dispatch uses: the AVX-512 directory is built
     * with -mbmi2, so the tier is only usable when the processor has BMI2 too. */
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    run(idwt_vertical_line_avx512, "idwt_vertical_line_avx512");
}

TEST_F(IdwtVerticalLine, recalc_AVX512) {
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    run_recalc(idwt_vertical_line_recalc_avx512, "idwt_vertical_line_recalc_avx512");
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST_F(IdwtHorizontalLine, lf16_hf16_NEON) {
    for (uint8_t shift = 0; shift <= 4; shift++) {
        for (uint32_t len = 2; len <= kMaxLen; len++) {
            fill_input(len, shift);
            reset_output();
            idwt_horizontal_line_lf16_hf16_c(in_lf16_, in_hf16_, out_ref_, len, shift);
            idwt_horizontal_line_lf16_hf16_neon(in_lf16_, in_hf16_, out_mod_, len, shift);
            ASSERT_NO_FATAL_FAILURE(compare(len, "idwt_horizontal_line_lf16_hf16_neon"));
        }
    }
}

TEST_F(IdwtHorizontalLine, lf32_hf16_NEON) {
    for (uint8_t shift = 0; shift <= 4; shift++) {
        for (uint32_t len = 2; len <= kMaxLen; len++) {
            fill_input(len, shift);
            reset_output();
            idwt_horizontal_line_lf32_hf16_c(in_lf32_, in_hf16_, out_ref_, len, shift);
            idwt_horizontal_line_lf32_hf16_neon(in_lf32_, in_hf16_, out_mod_, len, shift);
            ASSERT_NO_FATAL_FAILURE(compare(len, "idwt_horizontal_line_lf32_hf16_neon"));
        }
    }
}

TEST_F(IdwtVerticalLine, NEON) {
    run(idwt_vertical_line_neon, "idwt_vertical_line_neon");
}

TEST_F(IdwtVerticalLine, recalc_NEON) {
    run_recalc(idwt_vertical_line_recalc_neon, "idwt_vertical_line_recalc_neon");
}
#endif /* ARCH_AARCH64 */

} // namespace
