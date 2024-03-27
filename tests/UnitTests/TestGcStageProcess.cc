/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "PictureControlSet.h"
#include "random.h"
#include <immintrin.h>
#include "GcStageProcess.h"
#include "Enc_avx512.h"
#include "PiEnc.h"
#include "EncDec.h"
#include "encoder_dsp_rtcd.h"
#include "Codestream.h"
#include "group_coding_sse4_1.h"
#include "RateControl_avx2.h"

TEST(GcStage, gc_stage_scalar_c) {
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
        gc_precinct_stage_scalar_avx2(gc_data_avx2_ptr, coeff_data_ptr, group_size, width);
        if (memcmp(out_compare_msb, gc_data_avx2_ptr, gc_width * sizeof(int8_t))) {
            ASSERT_FALSE(1);
        }
    }

    free(coeff_data_ptr);
    free(gc_data_c_ptr);
    free(gc_data_avx2_ptr);
    free(out_compare_msb);
}

TEST(GcStage, gc_precinct_sigflags_max_sse41) {
    uint32_t gcli_width = 383;
    uint32_t significance_size = DIV_ROUND_UP(gcli_width, SIGNIFICANCE_GROUP_SIZE);
    uint8_t* gcli_data_ptr = (uint8_t*)malloc(gcli_width * sizeof(uint8_t));
    uint8_t* ref_significance_data_ptr = (uint8_t*)calloc(significance_size, sizeof(uint8_t));
    uint8_t* mod_significance_data_ptr = (uint8_t*)calloc(significance_size, sizeof(uint8_t));

    svt_jxs_test_tool::SVTRandom rand(0, TRUNCATION_MAX);

    for (uint32_t i = 0; i < gcli_width; i++) {
        gcli_data_ptr[i] = rand.random();
    }

    gc_precinct_sigflags_max_c(ref_significance_data_ptr, gcli_data_ptr, SIGNIFICANCE_GROUP_SIZE, gcli_width);
    gc_precinct_sigflags_max_sse4_1(mod_significance_data_ptr, gcli_data_ptr, SIGNIFICANCE_GROUP_SIZE, gcli_width);

    if (memcmp(ref_significance_data_ptr, mod_significance_data_ptr, significance_size * sizeof(uint8_t))) {
        ASSERT_FALSE(1);
    }

    free(gcli_data_ptr);
    free(ref_significance_data_ptr);
    free(mod_significance_data_ptr);
}
