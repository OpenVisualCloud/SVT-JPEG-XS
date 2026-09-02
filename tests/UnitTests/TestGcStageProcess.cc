/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "PictureControlSet.h"
#include "random.h"
#include "GcStageProcess.h"
#include "PiEnc.h"
#include "EncDec.h"
#include "encoder_dsp_rtcd.h"
#include "Codestream.h"

#ifdef ARCH_X86_64
#include <immintrin.h>
#include "Enc_avx512.h"
#include "group_coding_sse4_1.h"
#include "RateControl_avx2.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
#include "GcStage_neon.h"
#endif /* ARCH_AARCH64 */

void test_gc_stage_scalar(void (*test_fn)(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size,
                                          uint32_t width)) {
    /*Set pointers directly before calling the RTC function may cause an assert for Debug in other tests.*/
    setup_common_rtcd_internal(CPU_FLAGS_ALL);
    setup_encoder_rtcd_internal(CPU_FLAGS_ALL);

    svt_log2_32 = log2_32_c; //Set ASM pointer
    gc_precinct_stage_scalar_loop = gc_precinct_stage_scalar_loop_ASM;
    uint32_t group_size = 4;

    //Input
    uint32_t width = 343;
    uint32_t height = 10;
    uint16_t* coeff_data_ptr = (uint16_t*)malloc(width * height * sizeof(int16_t));

    //Output
    uint32_t gc_width = (width + group_size - 1) / group_size;
    uint32_t gc_height = height;
    uint8_t* gc_data_c_ptr = (uint8_t*)malloc(gc_width * gc_height * sizeof(int8_t));
    uint8_t* gc_data_avx2_ptr = (uint8_t*)malloc(gc_width * gc_height * sizeof(int8_t));

    svt_jxs_test_tool::SVTRandom rand4(4, false);
    svt_jxs_test_tool::SVTRandom rand32(16, false);
    int8_t* out_compare_msb = (int8_t*)malloc(gc_width * gc_height * sizeof(int8_t));
    for (uint32_t i = 0; i < gc_width * gc_height; ++i) {
        out_compare_msb[i] = rand4.random();
    }
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int8_t msb = out_compare_msb[y * gc_width + x / group_size];
            if (msb) {
                coeff_data_ptr[y * width + x] = 1 << (msb - 1);
                //Add noise data
                coeff_data_ptr[y * width + x] += (coeff_data_ptr[y * width + x] - 1) & (rand32.random());
                if (rand32.random() & 1) {
                    //Add sign
                    coeff_data_ptr[y * width + x] += BITSTREAM_MASK_SIGN;
                }
            }
            else {
                coeff_data_ptr[y * width + x] = 0;
            }
        }

        memset(gc_data_c_ptr, 0, gc_width * gc_height * sizeof(int8_t));
        gc_precinct_stage_scalar_c(gc_data_c_ptr, coeff_data_ptr, group_size, width);
        if (memcmp(out_compare_msb, gc_data_c_ptr, gc_width * sizeof(int8_t))) {
            ASSERT_FALSE(1);
        }

        memset(gc_data_avx2_ptr, 0, gc_width * gc_height * sizeof(int8_t));
        test_fn(gc_data_avx2_ptr, coeff_data_ptr, group_size, width);
        if (memcmp(out_compare_msb, gc_data_avx2_ptr, gc_width * sizeof(int8_t))) {
            ASSERT_FALSE(1);
        }
    }

    free(coeff_data_ptr);
    free(gc_data_c_ptr);
    free(gc_data_avx2_ptr);
    free(out_compare_msb);
}

#ifdef ARCH_AARCH64
/* The group loop on its own, against the C one. The group count is walked from
 * zero upwards because the vector body takes eight groups at a time and the
 * remainder is what the scalar tail has to pick up; the coefficients include
 * values with the sign bit set, which must not raise the reported index. */
TEST(GcStage, gc_stage_scalar_loop_neon) {
    const uint32_t max_groups = 40;
    svt_jxs_test_tool::SVTRandom rnd(16, false);
    uint16_t coeff[max_groups * GROUP_SIZE];
    uint8_t gcli_ref[max_groups];
    uint8_t gcli_mod[max_groups];

    svt_log2_32 = log2_32_c;
    for (uint32_t groups = 0; groups <= max_groups; groups++) {
        for (uint32_t i = 0; i < max_groups * GROUP_SIZE; i++) {
            /* a mix of zeroes, small values and values carrying a sign */
            const uint32_t r = rnd.random();
            coeff[i] = (r % 5 == 0) ? 0 : (uint16_t)(r & 0x7FFF);
            if (r & 0x10000) {
                coeff[i] |= BITSTREAM_MASK_SIGN;
            }
        }
        memset(gcli_ref, 0xAA, sizeof(gcli_ref));
        memset(gcli_mod, 0xAA, sizeof(gcli_mod));

        gc_precinct_stage_scalar_loop_c(groups, coeff, gcli_ref);
        gc_precinct_stage_scalar_loop_neon(groups, coeff, gcli_mod);

        for (uint32_t g = 0; g < max_groups; g++) {
            ASSERT_EQ(gcli_ref[g], gcli_mod[g]) << "group " << g << " of " << groups;
            if (g >= groups) {
                ASSERT_EQ(0xAA, gcli_mod[g]) << "wrote past the group count " << groups;
            }
        }
    }
}
#endif /* ARCH_AARCH64 */

#ifdef ARCH_X86_64
TEST(GcStage, gc_stage_scalar_avx2) {
    test_gc_stage_scalar(gc_precinct_stage_scalar_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(GcStage, gc_stage_scalar_avx512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_gc_stage_scalar(gc_precinct_stage_scalar_avx512);
    }
}
#endif /* ARCH_X86_64 */

/* The largest coded line index within each significance group.
 *
 * Widths are walked one by one rather than tried at a single convenient one:
 * the group is eight indices and the vector step is eight groups, so a line
 * ends inside a group as often as not, and both ends have rules of their own -
 * a whole group short of the step, and a partial group at the very end. The
 * output is marker-filled, so a write past the number of groups the width
 * implies shows up as a mismatch and is asserted on directly. */
static void test_gc_precinct_sigflags_max(void (*test_fn)(uint8_t*, uint8_t*, uint32_t, uint32_t)) {
    const uint32_t widths[] = {0,  1,  2,   7,   8,   9,   15,  16,  17,  31,  32,  33,  63,  64,
                               65, 71, 127, 128, 129, 255, 256, 383, 511, 512, 513, 1024};
    const uint32_t widths_num = sizeof(widths) / sizeof(widths[0]);
    const uint32_t max_width = 1024;
    const uint32_t sig_max = DIV_ROUND_UP(max_width, SIGNIFICANCE_GROUP_SIZE) + 8;
    const uint8_t marker = 0xAA;

    uint8_t* gcli_data_ptr = (uint8_t*)malloc(max_width);
    uint8_t* ref = (uint8_t*)malloc(sig_max);
    uint8_t* mod = (uint8_t*)malloc(sig_max);
    ASSERT_TRUE(gcli_data_ptr != NULL && ref != NULL && mod != NULL);

    svt_jxs_test_tool::SVTRandom rand(0, TRUNCATION_MAX);

    for (uint32_t width_idx = 0; width_idx < widths_num; width_idx++) {
        const uint32_t gcli_width = widths[width_idx];
        for (uint32_t round = 0; round < 4; round++) {
            for (uint32_t i = 0; i < max_width; i++) {
                /* the first round is a whole line of the same index: GCLI is
                 * spatially correlated and such lines do occur */
                gcli_data_ptr[i] = (uint8_t)(round == 0 ? TRUNCATION_MAX : rand.random());
            }
            memset(ref, marker, sig_max);
            memset(mod, marker, sig_max);

            gc_precinct_sigflags_max_c(ref, gcli_data_ptr, SIGNIFICANCE_GROUP_SIZE, gcli_width);
            test_fn(mod, gcli_data_ptr, SIGNIFICANCE_GROUP_SIZE, gcli_width);

            const uint32_t significance_size = DIV_ROUND_UP(gcli_width, SIGNIFICANCE_GROUP_SIZE);
            ASSERT_EQ(memcmp(ref, mod, sig_max), 0) << "width " << gcli_width << " round " << round;
            for (uint32_t i = significance_size; i < sig_max; i++) {
                ASSERT_EQ(marker, mod[i]) << "wrote past the group count, width " << gcli_width;
            }
        }
    }

    free(gcli_data_ptr);
    free(ref);
    free(mod);
}

/* The C implementation against itself: establishes that the harness reaches
 * every width and that nothing is written past the group count. */
TEST(GcStage, gc_precinct_sigflags_max_c) {
    test_gc_precinct_sigflags_max(gc_precinct_sigflags_max_c);
}

#ifdef ARCH_X86_64
TEST(GcStage, gc_precinct_sigflags_max_sse41) {
    test_gc_precinct_sigflags_max(gc_precinct_sigflags_max_sse4_1);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST(GcStage, gc_precinct_sigflags_max_neon) {
    test_gc_precinct_sigflags_max(gc_precinct_sigflags_max_neon);
}
#endif /* ARCH_AARCH64 */
