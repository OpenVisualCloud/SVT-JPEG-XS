/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "Codestream.h"
#include "Dwt.h"
#include "Dwt_AVX2.h"

void dwt_horizontal_line_avx2(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len) {
    if (len == 2) {
        out_hf[0] = in[1] - in[0];
        out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);
        return;
    }

    out_hf[0] = in[1] - ((in[0] + in[2]) >> 1);
    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);

    const uint32_t count = ((len - 1) / 2);
    const __m256i two = _mm256_set1_epi32(2);
    const uint32_t count_all = (count - 1) / 8;
    const __m256i reg_permutevar_mask_move_right = _mm256_setr_epi32(0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
    const __m256i reg_permutevar_mask_move_left = _mm256_setr_epi32(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07);
    const __m256i reg_permutevar_mask_A = _mm256_setr_epi32(0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07);
    const __m256i reg_permutevar_mask_B = _mm256_setr_epi32(0x01, 0x03, 0x05, 0x07, 0x00, 0x02, 0x04, 0x06);

    for (uint32_t id = 1; id < count_all * 8; id += 8) {
        const __m256i in_A = _mm256_loadu_si256((__m256i*)(&in[id * 2])); //0  1  2  3  4  5  6  7
        __m256i in_A_step2 = _mm256_permutevar8x32_epi32(in_A, reg_permutevar_mask_A);

        __m256i in_B = _mm256_loadu_si256((__m256i*)(&in[id * 2 + 8])); //8  9 10 11 12 13 14 15
        __m256i in_B_step2 = _mm256_permutevar8x32_epi32(in_B, reg_permutevar_mask_B);

        __m256i in_0 = _mm256_blend_epi32(in_A_step2, in_B_step2, 16 + 32 + 64 + 128);     //0  2  4  6  8  10 12 14
        __m256i in_1x = _mm256_blend_epi32(in_A_step2, in_B_step2, 1 + 2 + 4 + 8);         //9 11 13 15  1  3  5  7
        __m256i in_1 = _mm256_permute4x64_epi64(in_1x, (2 * 1 + 3 * 4 + 0 * 16 + 1 * 64)); //1  3  5  7  9 11 13 15

        __m256i in_2 = _mm256_permutevar8x32_epi32(in_0, reg_permutevar_mask_move_left);
        in_2 = _mm256_insert_epi32(in_2, in[id * 2 + 16], 7);

        __m256i hf_out = _mm256_add_epi32(in_0, in_2);
        hf_out = _mm256_srai_epi32(hf_out, 1);
        hf_out = _mm256_sub_epi32(in_1, hf_out);
        _mm256_storeu_si256((__m256i*)(out_hf + id), hf_out); //out_hf[id]

        __m256i hf_m1 = _mm256_permutevar8x32_epi32(hf_out, reg_permutevar_mask_move_right);
        hf_m1 = _mm256_insert_epi32(hf_m1, out_hf[id - 1], 0);

        __m256i lf = _mm256_add_epi32(hf_out, hf_m1);
        lf = _mm256_add_epi32(lf, two);
        lf = _mm256_srai_epi32(lf, 2);
        lf = _mm256_add_epi32(in_0, lf);
        _mm256_storeu_si256((__m256i*)(out_lf + id), lf);
    }

    for (uint32_t id = count_all * 8 + 1; id < count; id++) {
        out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        out_lf[id] = in[id * 2] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
    }

    if (!(len & 1)) {
        out_hf[len / 2 - 1] = in[len - 1] - in[len - 2];
        out_lf[len / 2 - 1] = in[len - 2] + ((out_hf[len / 2 - 2] + out_hf[len / 2 - 1] + 2) >> 2);
    }
    else { //if (len & 1){
        out_lf[len / 2] = in[len - 1] + ((out_hf[len / 2 - 1] + 1) >> 1);
    }
}

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_avx2(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1) {
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i l1 = _mm256_loadu_si256((__m256i*)(line_1 + i));
        _mm256_storeu_si256((__m256i*)(out_hf + i), _mm256_sub_epi32(l1, l0));
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
    }
}

void transform_vertical_loop_lf_line_0_avx2(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0) {
    const __m256i one = _mm256_set1_epi32(1);
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i next = _mm256_loadu_si256((__m256i*)(in_hf + i));

        next = _mm256_add_epi32(next, one);
        next = _mm256_srai_epi32(next, 1);
        next = _mm256_add_epi32(next, l0);
        _mm256_storeu_si256((__m256i*)(out_lf + i), next);
    }
    for (; i < width; i++) {
        out_lf[i] = line_0[i] + ((in_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_0_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                               const int32_t* line_1, const int32_t* line_2) {
    const __m256i one = _mm256_set1_epi32(1);
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i l1 = _mm256_loadu_si256((__m256i*)(line_1 + i));
        __m256i l2 = _mm256_loadu_si256((__m256i*)(line_2 + i));

        __m256i out_tmp = _mm256_add_epi32(l0, l2);
        out_tmp = _mm256_srai_epi32(out_tmp, 1);
        out_tmp = _mm256_sub_epi32(l1, out_tmp);
        _mm256_storeu_si256((__m256i*)(out_hf + i), out_tmp);

        out_tmp = _mm256_add_epi32(out_tmp, one);
        out_tmp = _mm256_srai_epi32(out_tmp, 1);
        out_tmp = _mm256_add_epi32(out_tmp, l0);
        _mm256_storeu_si256((__m256i*)(out_lf + i), out_tmp);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((out_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_x_prev_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                    const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                    const int32_t* line_p2) {
    const __m256i two = _mm256_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i lp6 = _mm256_loadu_si256((__m256i*)(line_p6 + i));
        __m256i lp5 = _mm256_loadu_si256((__m256i*)(line_p5 + i));
        __m256i lp4 = _mm256_loadu_si256((__m256i*)(line_p4 + i));
        __m256i lp3 = _mm256_loadu_si256((__m256i*)(line_p3 + i));
        __m256i lp2 = _mm256_loadu_si256((__m256i*)(line_p2 + i));

        __m256i out_hf_vec = _mm256_add_epi32(lp4, lp2);
        out_hf_vec = _mm256_srai_epi32(out_hf_vec, 1);
        out_hf_vec = _mm256_sub_epi32(lp3, out_hf_vec);
        _mm256_storeu_si256((__m256i*)(out_hf + i), out_hf_vec);

        __m256i tmp_hf = _mm256_add_epi32(lp6, lp4);
        tmp_hf = _mm256_srai_epi32(tmp_hf, 1);
        tmp_hf = _mm256_sub_epi32(lp5, tmp_hf);
        tmp_hf = _mm256_add_epi32(tmp_hf, out_hf_vec);
        tmp_hf = _mm256_add_epi32(tmp_hf, two);
        tmp_hf = _mm256_srai_epi32(tmp_hf, 2);
        tmp_hf = _mm256_add_epi32(tmp_hf, lp4);
        _mm256_storeu_si256((__m256i*)(out_lf + i), tmp_hf);
    }
    for (; i < width; i++) {
        out_hf[i] = line_p3[i] - ((line_p4[i] + line_p2[i]) >> 1);
        int32_t out_tmp_line_56_HF_next_local = line_p5[i] - ((line_p6[i] + line_p4[i]) >> 1);
        out_lf[i] = line_p4[i] + ((out_tmp_line_56_HF_next_local + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_x_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                                  const int32_t* line_0, const int32_t* line_1, const int32_t* line_2) {
    const __m256i two = _mm256_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i l1 = _mm256_loadu_si256((__m256i*)(line_1 + i));
        __m256i l2 = _mm256_loadu_si256((__m256i*)(line_2 + i));
        __m256i prev = _mm256_loadu_si256((__m256i*)(in_hf_prev + i));

        __m256i out_tmp = _mm256_add_epi32(l0, l2);
        out_tmp = _mm256_srai_epi32(out_tmp, 1);
        out_tmp = _mm256_sub_epi32(l1, out_tmp);
        _mm256_storeu_si256((__m256i*)(out_hf + i), out_tmp);

        out_tmp = _mm256_add_epi32(out_tmp, prev);
        out_tmp = _mm256_add_epi32(out_tmp, two);
        out_tmp = _mm256_srai_epi32(out_tmp, 2);
        out_tmp = _mm256_add_epi32(out_tmp, l0);
        _mm256_storeu_si256((__m256i*)(out_lf + i), out_tmp);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_last_even_avx2(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                          const int32_t* in_hf_prev, const int32_t* line_0,
                                                          const int32_t* line_1) {
    const __m256i two = _mm256_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i l1 = _mm256_loadu_si256((__m256i*)(line_1 + i));
        __m256i prev = _mm256_loadu_si256((__m256i*)(in_hf_prev + i));
        __m256i out_tmp = _mm256_sub_epi32(l1, l0);
        _mm256_storeu_si256((__m256i*)(out_hf + i), out_tmp);

        out_tmp = _mm256_add_epi32(out_tmp, prev);
        out_tmp = _mm256_add_epi32(out_tmp, two);
        out_tmp = _mm256_srai_epi32(out_tmp, 2);
        out_tmp = _mm256_add_epi32(out_tmp, l0);
        _mm256_storeu_si256((__m256i*)(out_lf + i), out_tmp);
    }
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_V1_Hx_precinct_recalc_HF_prev_avx2(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                                  const int32_t* line_1, const int32_t* line_2) {
    uint32_t i = 0;
    for (; (i + 7) < width; i += 8) {
        __m256i l0 = _mm256_loadu_si256((__m256i*)(line_0 + i));
        __m256i l1 = _mm256_loadu_si256((__m256i*)(line_1 + i));
        __m256i l2 = _mm256_loadu_si256((__m256i*)(line_2 + i));

        __m256i out_tmp = _mm256_add_epi32(l0, l2);
        out_tmp = _mm256_srai_epi32(out_tmp, 1);
        out_tmp = _mm256_sub_epi32(l1, out_tmp);
        _mm256_storeu_si256((__m256i*)(out_tmp_line_HF_next + i), out_tmp);
    }
    for (; i < width; i++) {
        out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
    }
}
