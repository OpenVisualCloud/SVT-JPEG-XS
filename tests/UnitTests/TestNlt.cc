/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "Pi.h"
#include "random.h"
#include "SvtJpegxsDec.h"
#include "Definitions.h"
#include "PictureControlSet.h"

#include "NltDec.h"
#include "NltEnc.h"
#include "Precinct.h"

#include "Codestream.h"

#ifdef ARCH_AARCH64
#include "NltEnc_neon.h"
#endif /* ARCH_AARCH64 */

#ifdef ARCH_X86_64
#include "NltEnc_avx2.h"
#include "NltDec_AVX2.h"
#include "NltDec_avx512.h"
#include "Enc_avx512.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Output_8bit, 8AVX2) {
    const int32_t w = 1999;
    const int32_t h = 1089;
    const int32_t bw = 20;
    const int32_t depth = 8;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    svt_jpeg_xs_image_buffer_t out_ref;
    svt_jpeg_xs_image_buffer_t out_mod;
    int32_t* comps[MAX_COMPONENTS_NUM] = {0};
    pi_t pi;

    memset(&pi, 0, sizeof(pi_t));
    memset(&out_ref, 0, sizeof(out_ref));
    memset(&out_mod, 0, sizeof(out_mod));

    pi.comps_num = MAX_COMPONENTS_NUM;

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        if (i == 0) {
            pi.components[0].width = w;
            pi.components[0].height = h;
        }
        else {
            pi.components[i].width = w / 2;
            pi.components[i].height = h / 2;
        }

        out_ref.data_yuv[i] = (uint8_t*)malloc(w * h * sizeof(uint8_t));
        out_mod.data_yuv[i] = (uint8_t*)malloc(w * h * sizeof(uint8_t));
        if (out_ref.data_yuv[i]) {
            memset(out_ref.data_yuv[i], 0xcd, w * h * sizeof(uint8_t));
        }
        if (out_mod.data_yuv[i]) {
            memset(out_mod.data_yuv[i], 0xcd, w * h * sizeof(uint8_t));
        }

        comps[i] = (int32_t*)malloc(w * h * sizeof(int32_t));

        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                comps[i][y * w + x] = rnd->Rand16();
            }
        }
    }

    linear_output_scaling_8bit_c(&pi, comps, bw, depth, &out_ref);
    linear_output_scaling_8bit_avx2(&pi, comps, bw, depth, &out_mod);

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        ASSERT_EQ(memcmp(out_ref.data_yuv[i], out_mod.data_yuv[i], sizeof(uint8_t) * w * h), 0);
    }

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        if (out_ref.data_yuv[i]) {
            free(out_ref.data_yuv[i]);
        }
        if (out_mod.data_yuv[i]) {
            free(out_mod.data_yuv[i]);
        }
        if (comps[i]) {
            free(comps[i]);
        }
    }
    delete rnd;
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Output_16bit, AVX2) {
    const int32_t w = 1999;
    const int32_t h = 1089;
    const int32_t bw = 20;
    const int32_t depth = 10;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    svt_jpeg_xs_image_buffer_t out_ref;
    svt_jpeg_xs_image_buffer_t out_mod;
    int32_t* comps[MAX_COMPONENTS_NUM] = {0};
    pi_t pi;

    memset(&pi, 0, sizeof(pi_t));
    memset(&out_ref, 0, sizeof(out_ref));
    memset(&out_mod, 0, sizeof(out_mod));

    pi.comps_num = MAX_COMPONENTS_NUM;

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        if (i == 0) {
            pi.components[0].width = w;
            pi.components[0].height = h;
        }
        else {
            pi.components[i].width = w / 2;
            pi.components[i].height = h / 2;
        }

        out_ref.data_yuv[i] = (uint8_t*)malloc(w * h * sizeof(uint16_t));
        out_mod.data_yuv[i] = (uint8_t*)malloc(w * h * sizeof(uint16_t));
        if (out_ref.data_yuv[i]) {
            memset(out_ref.data_yuv[i], 0xcd, w * h * sizeof(uint16_t));
        }
        if (out_mod.data_yuv[i]) {
            memset(out_mod.data_yuv[i], 0xcd, w * h * sizeof(uint16_t));
        }

        comps[i] = (int32_t*)malloc(w * h * sizeof(int32_t));
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                comps[i][y * w + x] = rnd->Rand16();
            }
        }
    }

    linear_output_scaling_16bit_c(&pi, comps, bw, depth, &out_ref);
    linear_output_scaling_16bit_avx2(&pi, comps, bw, depth, &out_mod);

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        ASSERT_EQ(memcmp(out_ref.data_yuv[i], out_mod.data_yuv[i], sizeof(uint16_t) * w * h), 0);
    }

    for (uint32_t i = 0; i < pi.comps_num; i++) {
        if (out_ref.data_yuv[i]) {
            free(out_ref.data_yuv[i]);
        }
        if (out_mod.data_yuv[i]) {
            free(out_mod.data_yuv[i]);
        }
        if (comps[i]) {
            free(comps[i]);
        }
    }
    delete rnd;
}
#endif /* ARCH_X86_64 */

void test_image_shift(void (*test_fn)(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift,
                                      int32_t offset)) {
    const int32_t w = 1999;
    const int32_t h = 1089;
    const int shift = 8;
    const int offset = 1 << (shift - 1);

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    int32_t* area_ref_ptr_32 = (int32_t*)malloc(w * h * sizeof(int32_t));
    int32_t* area_mod_ptr_32 = (int32_t*)malloc(w * h * sizeof(int32_t));
    for (uint32_t test_id = 0; test_id < 2; ++test_id) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                int32_t value = 0;
                if (test_id == 0) {
                    //Test -0
                    value = -1;
                }
                else {
                    value = rnd->random() & ((1 << (16 + shift - 1)) - 1); //Left max 15 bits after shift
                    /*Set zero to avoid too big value after roundup*/
                    value &= ~(1 << shift);
                    if (rnd->Rand16() % 2) {
                        //Sign
                        value = -value;
                    }
                }
                area_ref_ptr_32[y * w + x] = value;
                area_mod_ptr_32[y * w + x] = area_ref_ptr_32[y * w + x];
            }
            uint16_t* area_ref_ptr_16 = (uint16_t*)(area_ref_ptr_32 + y * w);
            uint16_t* area_mod_ptr_16 = (uint16_t*)(area_mod_ptr_32 + y * w);

            image_shift_c(area_ref_ptr_16, area_ref_ptr_32 + y * w, w, shift, offset);
            test_fn(area_mod_ptr_16, area_mod_ptr_32 + y * w, w, shift, offset);
        }
        ASSERT_EQ(memcmp(area_ref_ptr_32, area_mod_ptr_32, sizeof(int32_t) * w * h), 0);
    }

    free(area_ref_ptr_32);
    free(area_mod_ptr_32);

    delete rnd;
}

#ifdef ARCH_X86_64
TEST(image_shift, AVX2) {
    test_image_shift(image_shift_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(image_shift, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_image_shift(image_shift_avx512);
    }
}
#endif /* ARCH_X86_64 */

void test_linear_input_scaling_line_8bit(void (*test_fn)(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift,
                                                         int32_t offset)) {
    const uint32_t w = 1999;
    const int input_bit_depth = 8;
    uint8_t param_Bw = 20;
    const uint8_t shift = param_Bw - input_bit_depth;
    const int32_t offset = 1 << (param_Bw - 1);

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* src = (uint8_t*)malloc(w * sizeof(uint8_t));
    int32_t* dst_ref = (int32_t*)malloc(w * sizeof(int32_t));
    int32_t* dst_mod = (int32_t*)malloc(w * sizeof(int32_t));

    memset(src, 0, w * sizeof(uint8_t));
    memset(dst_ref, 0, w * sizeof(int32_t));
    memset(dst_mod, 0, w * sizeof(int32_t));

    for (uint32_t j = 0; j < w; j++) {
        src[j] = rnd->Rand8();
    }

    linear_input_scaling_line_8bit_c(src, dst_ref, w, shift, offset);
    test_fn(src, dst_mod, w, shift, offset);

    ASSERT_EQ(memcmp(dst_ref, dst_mod, sizeof(int32_t) * w), 0);

    free(src);
    free(dst_ref);
    free(dst_mod);
    delete rnd;
}

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Input_8bit, AVX2) {
    test_linear_input_scaling_line_8bit(linear_input_scaling_line_8bit_avx2);
}

TEST(Nlt_Linear_Input_8bit, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_linear_input_scaling_line_8bit(linear_input_scaling_line_8bit_avx512);
    }
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST(Nlt_Linear_Input_8bit, NEON) {
    test_linear_input_scaling_line_8bit(linear_input_scaling_line_8bit_neon);
}
#endif /* ARCH_AARCH64 */


void test_inv_sign(void (*test_fn)(uint16_t* in_out, uint32_t width)) {
    const uint32_t w = 1999;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(16, false);

    uint16_t* src_ref = (uint16_t*)malloc(w * sizeof(uint16_t));
    uint16_t* src_mod = (uint16_t*)malloc(w * sizeof(uint16_t));

    for (uint32_t i = 0; i < w; i++) {
        int32_t sign = rnd->Rand16() % 2;
        uint32_t val = rnd->Rand16();
        src_ref[i] = src_mod[i] = sign ? (val | BITSTREAM_MASK_SIGN) : val;
    }

    inv_sign_c(src_ref, w);
    test_fn(src_mod, w);

    ASSERT_EQ(memcmp(src_ref, src_mod, sizeof(int16_t) * w), 0);

    free(src_ref);
    free(src_mod);

    delete rnd;
}

#ifdef ARCH_X86_64
TEST(test_inverse_sign, AVX2) {
    test_inv_sign(inv_sign_avx2);
}
#endif /* ARCH_X86_64 */

void test_linear_input_scaling_line_16bit(void (*test_fn)(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift,
                                                          int32_t offset, uint8_t bit_depth)) {
    const uint32_t w = 1999;
    const uint8_t param_Bw = 20;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint16_t* src = (uint16_t*)malloc(w * sizeof(uint16_t));
    int32_t* dst_ref = (int32_t*)malloc(w * sizeof(int32_t));
    int32_t* dst_mod = (int32_t*)malloc(w * sizeof(int32_t));

    memset(src, 0, w * sizeof(uint16_t));
    memset(dst_ref, 0, w * sizeof(int32_t));
    memset(dst_mod, 0, w * sizeof(int32_t));

    for (uint8_t input_bit_depth = 10; input_bit_depth < 16; ++input_bit_depth) {
        const uint8_t shift = param_Bw - input_bit_depth;
        const int32_t offset = 1 << (param_Bw - 1);

        for (uint32_t j = 0; j < w; j++) {
            src[j] = rnd->Rand16() % ((1 << input_bit_depth) - 1);
        }

        //Test Valid input
        linear_input_scaling_line_16bit_c(src, dst_ref, w, shift, offset, input_bit_depth);
        test_fn(src, dst_mod, w, shift, offset, input_bit_depth);
        ASSERT_EQ(memcmp(dst_ref, dst_mod, sizeof(int32_t) * w), 0);

        //Test invalid input, when input values out of a range
        if (input_bit_depth < 16) {
            uint16_t invalid_bits = (uint16_t)((unsigned)0xFFFF << input_bit_depth);
            for (uint32_t j = 0; j < w; j++) {
                src[j] |= invalid_bits;
            }
            memset(dst_ref, 0, w * sizeof(int32_t));
            linear_input_scaling_line_16bit_c(src, dst_ref, w, shift, offset, input_bit_depth);
            ASSERT_EQ(memcmp(dst_ref, dst_mod, sizeof(int32_t) * w), 0);
            memset(dst_mod, 0, w * sizeof(int32_t));
            test_fn(src, dst_mod, w, shift, offset, input_bit_depth);
            ASSERT_EQ(memcmp(dst_ref, dst_mod, sizeof(int32_t) * w), 0);
        }
    }

    free(src);
    free(dst_ref);
    free(dst_mod);
    delete rnd;
}

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Input_16bit, AVX2) {
    test_linear_input_scaling_line_16bit(linear_input_scaling_line_16bit_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST(Nlt_Linear_Input_16bit, NEON) {
    test_linear_input_scaling_line_16bit(linear_input_scaling_line_16bit_neon);
}
#endif /* ARCH_AARCH64 */


#ifdef ARCH_X86_64
TEST(Nlt_Linear_Input_16bit, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_linear_input_scaling_line_16bit(linear_input_scaling_line_16bit_avx512);
    }
}
#endif /* ARCH_X86_64 */

void test_linear_input_scaling_line_16bit_msb(void (*test_fn)(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift,
                                                              int32_t offset, uint8_t bit_depth)) {
    const uint32_t w = 1999;
    const uint8_t param_Bw = 20;
    const uint8_t shift = param_Bw - 16; /* MSB-aligned: constant, depth-independent shift = Bw-16 = 4 */
    const int32_t offset = 1 << (param_Bw - 1);

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint16_t* src = (uint16_t*)malloc(w * sizeof(uint16_t));
    int32_t* dst_ref = (int32_t*)malloc(w * sizeof(int32_t));
    int32_t* dst_mod = (int32_t*)malloc(w * sizeof(int32_t));

    for (uint8_t input_bit_depth = 10; input_bit_depth <= 12; input_bit_depth += 2) {
        for (uint32_t j = 0; j < w; j++) {
            uint16_t pixel = rnd->Rand16() % (1 << input_bit_depth);
            src[j] = (uint16_t)(pixel << (16 - input_bit_depth)); /* MSB-align */
        }

        memset(dst_ref, 0, w * sizeof(int32_t));
        memset(dst_mod, 0, w * sizeof(int32_t));
        linear_input_scaling_line_16bit_msb_c(src, dst_ref, w, shift, offset, input_bit_depth);
        test_fn(src, dst_mod, w, shift, offset, input_bit_depth);
        ASSERT_EQ(memcmp(dst_ref, dst_mod, sizeof(int32_t) * w), 0);

        /* Cross-check against the LSB kernel operating on the equivalent LSB-packed value:
         * result must match exactly, proving the MSB path is a pure re-alignment, not a different scale. */
        uint16_t* src_lsb = (uint16_t*)malloc(w * sizeof(uint16_t));
        for (uint32_t j = 0; j < w; j++) {
            src_lsb[j] = src[j] >> (16 - input_bit_depth);
        }
        int32_t* dst_lsb = (int32_t*)malloc(w * sizeof(int32_t));
        const uint8_t shift_lsb = param_Bw - input_bit_depth;
        linear_input_scaling_line_16bit_c(src_lsb, dst_lsb, w, shift_lsb, offset, input_bit_depth);
        ASSERT_EQ(memcmp(dst_ref, dst_lsb, sizeof(int32_t) * w), 0);
        free(src_lsb);
        free(dst_lsb);
    }

    free(src);
    free(dst_ref);
    free(dst_mod);
    delete rnd;
}

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Input_16bit_MSB, AVX2) {
    test_linear_input_scaling_line_16bit_msb(linear_input_scaling_line_16bit_msb_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
TEST(Nlt_Linear_Input_16bit_MSB, NEON) {
    test_linear_input_scaling_line_16bit_msb(linear_input_scaling_line_16bit_msb_neon);
}
#endif /* ARCH_AARCH64 */


#ifdef ARCH_X86_64
TEST(Nlt_Linear_Input_16bit_MSB, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_linear_input_scaling_line_16bit_msb(linear_input_scaling_line_16bit_msb_avx512);
    }
}
#endif /* ARCH_X86_64 */

void test_linear_output_scaling_16bit_line_msb(void (*test_fn)(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out,
                                                               uint32_t w)) {
    const uint32_t w = 1999;
    const uint32_t bw = 20;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    int32_t* in = (int32_t*)malloc(w * sizeof(int32_t));
    uint16_t* out_ref = (uint16_t*)malloc(w * sizeof(uint16_t));
    uint16_t* out_mod = (uint16_t*)malloc(w * sizeof(uint16_t));

    for (uint32_t j = 0; j < w; j++) {
        in[j] = rnd->Rand16();
    }

    for (uint32_t depth = 10; depth <= 12; depth += 2) {
        memset(out_ref, 0xcd, w * sizeof(uint16_t));
        memset(out_mod, 0xcd, w * sizeof(uint16_t));

        linear_output_scaling_16bit_line_msb_c(in, bw, depth, out_ref, w);
        test_fn(in, bw, depth, out_mod, w);
        ASSERT_EQ(memcmp(out_ref, out_mod, sizeof(uint16_t) * w), 0);

        /* MSB-alignment must hold: low (16-depth) bits are always zero. */
        for (uint32_t j = 0; j < w; j++) {
            uint16_t low_bits_mask = (uint16_t)((1 << (16 - depth)) - 1);
            ASSERT_EQ(out_ref[j] & low_bits_mask, 0) << "pixel " << j << " depth=" << depth << " not MSB-aligned";
        }

        /* Cross-check against the LSB kernel: MSB result right-shifted must equal the LSB result exactly. */
        uint16_t* out_lsb = (uint16_t*)malloc(w * sizeof(uint16_t));
        linear_output_scaling_16bit_line_c(in, bw, depth, out_lsb, w);
        for (uint32_t j = 0; j < w; j++) {
            ASSERT_EQ((uint16_t)(out_ref[j] >> (16 - depth)), out_lsb[j]);
        }
        free(out_lsb);
    }

    free(in);
    free(out_ref);
    free(out_mod);
    delete rnd;
}

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Output_16bit_MSB, AVX2) {
    test_linear_output_scaling_16bit_line_msb(linear_output_scaling_16bit_line_msb_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(Nlt_Linear_Output_16bit_MSB, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        test_linear_output_scaling_16bit_line_msb(linear_output_scaling_16bit_line_msb_avx512);
    }
}
#endif /* ARCH_X86_64 */

/* Covers the Tnlt=1/2 gap: the msb_aligned post-shift must apply regardless of transfer curve. */
TEST(Nlt_Inverse_Transform_Line_16bit_MSB, QuadraticAndExtended) {
    const int32_t w = 257;
    const uint32_t depth = 10;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    int32_t* in = (int32_t*)malloc(w * sizeof(int32_t));
    uint16_t* out_lsb = (uint16_t*)malloc(w * sizeof(uint16_t));
    uint16_t* out_msb = (uint16_t*)malloc(w * sizeof(uint16_t));
    for (int32_t j = 0; j < w; j++) {
        in[j] = rnd->Rand16();
    }

    picture_header_dynamic_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.hdr_Bw = 20;

    for (uint8_t tnlt = 1; tnlt <= 2; tnlt++) {
        hdr.hdr_Tnlt = tnlt;
        hdr.hdr_Tnlt_alpha = 1 << 14;
        hdr.hdr_Tnlt_sigma = 0;
        hdr.hdr_Tnlt_t1 = 1 << 10;
        hdr.hdr_Tnlt_t2 = 1 << 18;
        hdr.hdr_Tnlt_e = 4;

        nlt_inverse_transform_line_16bit(in, depth, &hdr, out_lsb, w, 0);
        nlt_inverse_transform_line_16bit(in, depth, &hdr, out_msb, w, 1);

        for (int32_t j = 0; j < w; j++) {
            ASSERT_EQ((uint16_t)(out_msb[j] >> (16 - depth)), out_lsb[j]) << "tnlt=" << (int)tnlt << " pixel " << j;
            uint16_t low_bits_mask = (uint16_t)((1 << (16 - depth)) - 1);
            ASSERT_EQ(out_msb[j] & low_bits_mask, 0) << "tnlt=" << (int)tnlt << " pixel " << j << " not MSB-aligned";
        }
    }

    free(in);
    free(out_lsb);
    free(out_msb);
    delete rnd;
}
