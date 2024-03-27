/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Enc_avx512.h"
#include "NltEnc_avx2.h"
#include "SvtLog.h"
#include "GcStageProcess.h"
#include <immintrin.h>
#include "Dwt.h"
#include "Codestream.h"

static INLINE void loop_small_avx512(uint32_t len, uint32_t* id, const int32_t** tmp_in, int32_t** out_hf, int32_t** out_lf) {
    const __m128i two = _mm_set1_epi32(2);
    const __m256i reg_permutevar_mask = _mm256_setr_epi32(0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07);

    for (; *id < ((len - 1) / 2) - 3; *id += 4) {
        /* hf */
        __m256i reg = _mm256_loadu_si256((__m256i*)*tmp_in);
        reg = _mm256_permutevar8x32_epi32(reg, reg_permutevar_mask);

        __m128i id_2_p0 = _mm256_castsi256_si128(reg);
        __m128i id_2_p1 = _mm256_extracti128_si256(reg, 0x1);
        __m128i id_2_p2 = _mm_srli_si128(id_2_p0, 4);
        id_2_p2 = _mm_insert_epi32(id_2_p2, (*tmp_in)[8], 3);

        //out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        __m128i hf = _mm_add_epi32(id_2_p0, id_2_p2);
        hf = _mm_srai_epi32(hf, 1);
        hf = _mm_sub_epi32(id_2_p1, hf);
        _mm_storeu_si128((__m128i*)(*out_hf + *id), hf);

        //out_hf[id - 1]
        __m128i hf_m1 = _mm_shuffle_epi32(hf, (0 << 0) + (0 << 2) + (1 << 4) + (2 << 6));
        hf_m1 = _mm_insert_epi32(hf_m1, (*out_hf)[*id - 1], 0);

        //out_lf[id] = in[id * 2] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
        __m128i lf = _mm_add_epi32(hf, hf_m1);
        lf = _mm_add_epi32(lf, two);
        lf = _mm_srai_epi32(lf, 2);
        lf = _mm_add_epi32(id_2_p0, lf);
        _mm_storeu_si128((__m128i*)(*out_lf + *id), lf);

        *tmp_in += 8;
    }
}

void dwt_horizontal_line_small_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len) {
    if (len < 10) {
        dwt_horizontal_line_c(out_lf, out_hf, in, len);
        return;
    }

    out_hf[0] = in[1] - ((in[0] + in[2]) >> 1);
    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);

    uint32_t id = 1;

    const int32_t* tmp_in = in + 2;

    loop_small_avx512(len, &id, &tmp_in, &out_hf, &out_lf);

    //leftover
    for (; id < ((len - 1) / 2); id++) {
        out_hf[id] = tmp_in[1] - ((tmp_in[0] + tmp_in[2]) >> 1);
        out_lf[id] = tmp_in[0] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
        tmp_in += 2;
    }

    if (!(len & 1)) {
        out_hf[len / 2 - 1] = tmp_in[1] - tmp_in[0];
        out_lf[len / 2 - 1] = tmp_in[0] + ((out_hf[len / 2 - 2] + out_hf[len / 2 - 1] + 2) >> 2);
    }
    else { //if (len & 1){
        out_lf[len / 2] = tmp_in[0] + ((out_hf[len / 2 - 1] + 1) >> 1);
    }
}

void dwt_horizontal_line_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len) {
    if (len < 34) {
        dwt_horizontal_line_small_avx512(out_lf, out_hf, in, len);
        return;
    }

    out_hf[0] = in[1] - ((in[0] + in[2]) >> 1);
    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);

    const __m512i two = _mm512_set1_epi32(2);
    const __m512i reg0_permutex2var_mask = _mm512_setr_epi32(
        0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e);
    const __m512i reg1_permutex2var_mask = _mm512_setr_epi32(
        0x01, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x0f, 0x11, 0x13, 0x15, 0x17, 0x19, 0x1b, 0x1d, 0x1f);
    const __m512i reg_permutevar_mask = _mm512_setr_epi32(
        0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e);

    uint32_t id = 1;

    const int32_t* tmp_in = in + 2;

    for (; id < ((len - 1) / 2) - 15; id += 16) {
        /* hf */
        __m512i reg_l = _mm512_loadu_si512(tmp_in);
        __m512i reg_h = _mm512_loadu_si512(tmp_in + 16);

        __m512i id_2_p0 = _mm512_permutex2var_epi32(reg_l, reg0_permutex2var_mask, reg_h);
        __m512i id_2_p1 = _mm512_permutex2var_epi32(reg_l, reg1_permutex2var_mask, reg_h);
        reg_l = _mm512_loadu_si512(tmp_in + 2);
        reg_h = _mm512_loadu_si512(tmp_in + 18);
        __m512i id_2_p2 = _mm512_permutex2var_epi32(reg_l, reg0_permutex2var_mask, reg_h);

        //out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        __m512i hf = _mm512_add_epi32(id_2_p0, id_2_p2);
        hf = _mm512_srai_epi32(hf, 1);
        hf = _mm512_sub_epi32(id_2_p1, hf);
        _mm512_storeu_si512((__m512i*)(out_hf + id), hf);

        //out_hf[id - 1]
        //const __m512i hf_m1 = _mm512_loadu_si512((__m512i*)(out_hf + id - 1));
        __m512i hf_m1 = _mm512_permutexvar_epi32(reg_permutevar_mask, hf);
        __m128i tmp = _mm512_castsi512_si128(hf_m1);
        tmp = _mm_insert_epi32(tmp, out_hf[id - 1], 0);
        hf_m1 = _mm512_inserti32x4(hf_m1, tmp, 0);

        //out_lf[id] = in[id * 2] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
        __m512i lf = _mm512_add_epi32(hf, hf_m1);
        lf = _mm512_add_epi32(lf, two);
        lf = _mm512_srai_epi32(lf, 2);
        lf = _mm512_add_epi32(id_2_p0, lf);
        _mm512_storeu_si512((__m512i*)(out_lf + id), lf);

        tmp_in += 32;
    }

    loop_small_avx512(len, &id, &tmp_in, &out_hf, &out_lf);

    //leftover
    for (; id < ((len - 1) / 2); id++) {
        out_hf[id] = tmp_in[1] - ((tmp_in[0] + tmp_in[2]) >> 1);
        out_lf[id] = tmp_in[0] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
        tmp_in += 2;
    }

    if (!(len & 1)) {
        out_hf[len / 2 - 1] = tmp_in[1] - tmp_in[0];
        out_lf[len / 2 - 1] = tmp_in[0] + ((out_hf[len / 2 - 2] + out_hf[len / 2 - 1] + 2) >> 2);
    }
    else { //if (len & 1){
        out_lf[len / 2] = tmp_in[0] + ((out_hf[len / 2 - 1] + 1) >> 1);
    }
}

void image_shift_avx512(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset) {
    int32_t* in_ptr_line = in_coeff_32bit;
    uint16_t* out_ptr_line = out_coeff_16bit;

    const __m512i offset_avx512 = _mm512_set1_epi32(offset);
    const __m512i sign_mask_epi32 = _mm512_set1_epi32(/*BITSTREAM_MASK_SIGN*/ ((uint32_t)1 << 31));
    const __m512i reg_permutevar_mask = _mm512_setr_epi64(0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07);
    const __m512i zero = _mm512_setzero_si512();

    const uint32_t simd_batch = width / 32;
    const uint32_t remaining = width % 32;

    for (uint32_t i = 0; i < simd_batch; i++) {
        __m512i data1 = _mm512_loadu_si512((__m512i*)(in_ptr_line));
        __m512i sign1 = _mm512_and_si512(data1, sign_mask_epi32);
        sign1 = _mm512_srli_epi32(sign1, 16);

        __m512i data2 = _mm512_loadu_si512((__m256i*)(in_ptr_line + 16));
        __m512i sign2 = _mm512_and_si512(data2, sign_mask_epi32);
        sign2 = _mm512_srli_epi32(sign2, 16);

        data1 = _mm512_abs_epi32(data1);
        data1 = _mm512_add_epi32(data1, offset_avx512);
        data1 = _mm512_srli_epi32(data1, shift);
        __mmask16 mask1 = _mm512_cmpgt_epi32_mask(data1, zero);
        data1 = _mm512_mask_or_epi32(zero, mask1, data1, sign1);

        data2 = _mm512_abs_epi32(data2);
        data2 = _mm512_add_epi32(data2, offset_avx512);
        data2 = _mm512_srli_epi32(data2, shift);
        __mmask16 mask2 = _mm512_cmpgt_epi32_mask(data2, zero);
        data2 = _mm512_mask_or_epi32(zero, mask2, data2, sign2);
        data1 = _mm512_packus_epi32(data1, data2);
        data1 = _mm512_permutexvar_epi64(reg_permutevar_mask, data1);

        _mm512_storeu_si512((__m512i*)out_ptr_line, data1);

        in_ptr_line += 32;
        out_ptr_line += 32;
    }

    if (remaining) {
        image_shift_avx2(out_ptr_line, in_ptr_line, remaining, shift, offset);
    }
}

void linear_input_scaling_line_8bit_avx512(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset) {
    const __m512i offset_avx512 = _mm512_set1_epi32(offset);
    const uint32_t simd_batch = w / 16;
    const uint32_t remaining = w % 16;

    for (uint32_t j = 0; j < simd_batch; j++) {
        __m128i temp_data = _mm_loadu_si128((__m128i*)src);
        __m512i data = _mm512_cvtepu8_epi32(temp_data);

        data = _mm512_slli_epi32(data, shift);
        data = _mm512_sub_epi32(data, offset_avx512);

        _mm512_storeu_si512(dst, data);

        src += 16;
        dst += 16;
    }
    for (uint32_t j = 0; j < remaining; j++) {
        dst[j] = ((uint32_t)src[j] << shift) - offset;
    }
}

void linear_input_scaling_line_16bit_avx512(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                            uint8_t bit_depth) {
    /* each __m512i has 16 int32_t(int32_t) */
    int simd_batch = w / 16;
    int remaining = w % 16;
    uint16_t input_mask = (1 << bit_depth) - 1;

    __m512i reg_offset = _mm512_set1_epi32(offset);
    __m256i input_mask_epi16 = _mm256_set1_epi16(input_mask);
    for (int i = 0; i < simd_batch; i++) {
        __m512i reg = _mm512_cvtepi16_epi32(_mm256_and_si256(_mm256_loadu_si256((__m256i*)src), input_mask_epi16));
        __m512i result = _mm512_sub_epi32(_mm512_slli_epi32(reg, shift), reg_offset);
        _mm512_storeu_si512(dst, result);
        src += 16;
        dst += 16;
    }
    for (int i = 0; i < remaining; i++) {
        dst[i] = ((src[i] & input_mask) << shift) - offset;
    }
}

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_avx512(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1) {
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i l1 = _mm512_loadu_si512((__m512i*)(line_1 + i));
        _mm512_storeu_si512((__m512i*)(out_hf + i), _mm512_sub_epi32(l1, l0));
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
    }
}

void transform_vertical_loop_lf_line_0_avx512(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0) {
    const __m512i one = _mm512_set1_epi32(1);
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i next = _mm512_loadu_si512((__m512i*)(in_hf + i));

        next = _mm512_add_epi32(next, one);
        next = _mm512_srai_epi32(next, 1);
        next = _mm512_add_epi32(next, l0);
        _mm512_storeu_si512((__m512i*)(out_lf + i), next);
    }
    for (; i < width; i++) {
        out_lf[i] = line_0[i] + ((in_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_0_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                                 const int32_t* line_1, const int32_t* line_2) {
    const __m512i one = _mm512_set1_epi32(1);
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i l1 = _mm512_loadu_si512((__m512i*)(line_1 + i));
        __m512i l2 = _mm512_loadu_si512((__m512i*)(line_2 + i));

        __m512i out_tmp = _mm512_add_epi32(l0, l2);
        out_tmp = _mm512_srai_epi32(out_tmp, 1);
        out_tmp = _mm512_sub_epi32(l1, out_tmp);
        _mm512_storeu_si512((__m512i*)(out_hf + i), out_tmp);

        out_tmp = _mm512_add_epi32(out_tmp, one);
        out_tmp = _mm512_srai_epi32(out_tmp, 1);
        out_tmp = _mm512_add_epi32(out_tmp, l0);
        _mm512_storeu_si512((__m512i*)(out_lf + i), out_tmp);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((out_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_x_prev_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                      const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                      const int32_t* line_p2) {
    const __m512i two = _mm512_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i lp6 = _mm512_loadu_si512((__m512i*)(line_p6 + i));
        __m512i lp5 = _mm512_loadu_si512((__m512i*)(line_p5 + i));
        __m512i lp4 = _mm512_loadu_si512((__m512i*)(line_p4 + i));
        __m512i lp3 = _mm512_loadu_si512((__m512i*)(line_p3 + i));
        __m512i lp2 = _mm512_loadu_si512((__m512i*)(line_p2 + i));

        __m512i out_hf_vec = _mm512_add_epi32(lp4, lp2);
        out_hf_vec = _mm512_srai_epi32(out_hf_vec, 1);
        out_hf_vec = _mm512_sub_epi32(lp3, out_hf_vec);
        _mm512_storeu_si512((__m512i*)(out_hf + i), out_hf_vec);

        __m512i tmp_hf = _mm512_add_epi32(lp6, lp4);
        tmp_hf = _mm512_srai_epi32(tmp_hf, 1);
        tmp_hf = _mm512_sub_epi32(lp5, tmp_hf);
        tmp_hf = _mm512_add_epi32(tmp_hf, out_hf_vec);
        tmp_hf = _mm512_add_epi32(tmp_hf, two);
        tmp_hf = _mm512_srai_epi32(tmp_hf, 2);
        tmp_hf = _mm512_add_epi32(tmp_hf, lp4);
        _mm512_storeu_si512((__m512i*)(out_lf + i), tmp_hf);
    }
    for (; i < width; i++) {
        out_hf[i] = line_p3[i] - ((line_p4[i] + line_p2[i]) >> 1);
        int32_t out_tmp_line_56_HF_next_local = line_p5[i] - ((line_p6[i] + line_p4[i]) >> 1);
        out_lf[i] = line_p4[i] + ((out_tmp_line_56_HF_next_local + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_x_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                                    const int32_t* line_0, const int32_t* line_1, const int32_t* line_2) {
    const __m512i two = _mm512_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i l1 = _mm512_loadu_si512((__m512i*)(line_1 + i));
        __m512i l2 = _mm512_loadu_si512((__m512i*)(line_2 + i));
        __m512i prev = _mm512_loadu_si512((__m512i*)(in_hf_prev + i));

        __m512i out_tmp = _mm512_add_epi32(l0, l2);
        out_tmp = _mm512_srai_epi32(out_tmp, 1);
        out_tmp = _mm512_sub_epi32(l1, out_tmp);
        _mm512_storeu_si512((__m512i*)(out_hf + i), out_tmp);

        out_tmp = _mm512_add_epi32(out_tmp, prev);
        out_tmp = _mm512_add_epi32(out_tmp, two);
        out_tmp = _mm512_srai_epi32(out_tmp, 2);
        out_tmp = _mm512_add_epi32(out_tmp, l0);
        _mm512_storeu_si512((__m512i*)(out_lf + i), out_tmp);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_last_even_avx512(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                            const int32_t* in_hf_prev, const int32_t* line_0,
                                                            const int32_t* line_1) {
    const __m512i two = _mm512_set1_epi32(2);
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i l1 = _mm512_loadu_si512((__m512i*)(line_1 + i));
        __m512i prev = _mm512_loadu_si512((__m512i*)(in_hf_prev + i));
        __m512i out_tmp = _mm512_sub_epi32(l1, l0);
        _mm512_storeu_si512((__m512i*)(out_hf + i), out_tmp);

        out_tmp = _mm512_add_epi32(out_tmp, prev);
        out_tmp = _mm512_add_epi32(out_tmp, two);
        out_tmp = _mm512_srai_epi32(out_tmp, 2);
        out_tmp = _mm512_add_epi32(out_tmp, l0);
        _mm512_storeu_si512((__m512i*)(out_lf + i), out_tmp);
    }
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_V1_Hx_precinct_recalc_HF_prev_avx512(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                                    const int32_t* line_1, const int32_t* line_2) {
    uint32_t i = 0;
    for (; (i + 15) < width; i += 16) {
        __m512i l0 = _mm512_loadu_si512((__m512i*)(line_0 + i));
        __m512i l1 = _mm512_loadu_si512((__m512i*)(line_1 + i));
        __m512i l2 = _mm512_loadu_si512((__m512i*)(line_2 + i));

        __m512i out_tmp = _mm512_add_epi32(l0, l2);
        out_tmp = _mm512_srai_epi32(out_tmp, 1);
        out_tmp = _mm512_sub_epi32(l1, out_tmp);
        _mm512_storeu_si512((__m512i*)(out_tmp_line_HF_next + i), out_tmp);
    }
    for (; i < width; i++) {
        out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
    }
}

void convert_packed_to_planar_rgb_8bit_avx512(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                              uint32_t line_width) {
    const uint8_t* in = (const uint8_t*)in_rgb;
    uint8_t* out_c1 = (uint8_t*)out_comp1;
    uint8_t* out_c2 = (uint8_t*)out_comp2;
    uint8_t* out_c3 = (uint8_t*)out_comp3;

    const uint8_t mask[] = {0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11, 12, 13, 14, 15};
    __m128i shuffle_mask_sse = _mm_loadu_si128((__m128i*)mask);
    __m512i shuffle_mask = _mm512_broadcast_i64x2(shuffle_mask_sse);

    uint32_t pix = 0;
    for (; (pix + 64) < line_width; pix += 64) {
#if 0
        // 0, 1, 2, 3
        __m512i a = _mm512_loadu_si512(in);
        // 4, 5, 6, 7
        __m512i b = _mm512_loadu_si512(in + 64);
        // 8, 9, 10, 11
        __m512i c = _mm512_loadu_si512(in + 128);

        // 0, 3, 6, XX
        __m512i rgb1 = _mm512_shuffle_i64x2(a, b, (0 << 0) + (3 << 2) + (2 << 4) + (0 << 6));
        // 0, 3, 6, 9
        rgb1 = _mm512_inserti64x2(rgb1, _mm512_extracti64x2_epi64(c, 0x1), 0x3);
        // XX, 5, 8, 11
        __m512i rgb3 = _mm512_shuffle_i64x2(b, c, (0 << 0) + (1 << 2) + (0 << 4) + (3 << 6));
        // 2, 5, 8, 11
        rgb3 = _mm512_inserti64x2(rgb3, _mm512_extracti64x2_epi64(a, 0x2), 0x0);

        // 1, 4
        __m256i rgb2_t1 = _mm256_shuffle_i64x2(_mm512_castsi512_si256(a), _mm512_castsi512_si256(b), (1 << 0) + (0 << 1));
        // 7, 10
        __m256i rgb2_t2 = _mm256_inserti128_si256(
            _mm256_castsi128_si256(_mm512_extracti64x2_epi64(b, 0x3)), _mm512_extracti64x2_epi64(c, 0x2), 0x1);
        // 1, 4, 7, 10
        __m512i rgb2 = _mm512_inserti64x4(_mm512_castsi256_si512(rgb2_t1), rgb2_t2, 0x1);
        in += 192;
#else
        __m128i a = _mm_loadu_si128((__m128i*)(in));
        __m128i b = _mm_loadu_si128((__m128i*)(in + 48));
        __m256i ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        __m128i c = _mm_loadu_si128((__m128i*)(in + 96));
        __m128i d = _mm_loadu_si128((__m128i*)(in + 144));
        __m256i cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb1 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);

        in += 16;
        a = _mm_loadu_si128((__m128i*)(in));
        b = _mm_loadu_si128((__m128i*)(in + 48));
        ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        c = _mm_loadu_si128((__m128i*)(in + 96));
        d = _mm_loadu_si128((__m128i*)(in + 144));
        cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb2 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);

        in += 16;
        a = _mm_loadu_si128((__m128i*)(in));
        b = _mm_loadu_si128((__m128i*)(in + 48));
        ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        c = _mm_loadu_si128((__m128i*)(in + 96));
        d = _mm_loadu_si128((__m128i*)(in + 144));
        cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb3 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);
        in += 160;
#endif

        __m512i pack_lo = _mm512_shuffle_epi8(rgb1, shuffle_mask);
        rgb1 = _mm512_alignr_epi8(rgb2, rgb1, 12);
        __m512i pack_hi = _mm512_shuffle_epi8(rgb1, shuffle_mask);
        rgb2 = _mm512_alignr_epi8(rgb3, rgb2, 8);
        __m512i pack_lo2 = _mm512_shuffle_epi8(rgb2, shuffle_mask);
        rgb3 = _mm512_bsrli_epi128(rgb3, 4);
        __m512i pack_hi2 = _mm512_shuffle_epi8(rgb3, shuffle_mask);

        __m512i tmp1 = _mm512_unpacklo_epi32(pack_lo, pack_hi);
        __m512i tmp2 = _mm512_unpacklo_epi32(pack_lo2, pack_hi2);

        _mm512_storeu_si512((__m512i*)out_c1, _mm512_unpacklo_epi64(tmp1, tmp2));
        _mm512_storeu_si512((__m512i*)out_c2, _mm512_unpackhi_epi64(tmp1, tmp2));

        tmp1 = _mm512_unpackhi_epi32(pack_lo, pack_hi);
        tmp2 = _mm512_unpackhi_epi32(pack_lo2, pack_hi2);
        _mm512_storeu_si512((__m512i*)out_c3, _mm512_unpacklo_epi64(tmp1, tmp2));

        out_c1 += 64;
        out_c2 += 64;
        out_c3 += 64;
    }

    for (; (pix + 4) < line_width; pix += 4) {
        __m128i input = _mm_loadu_si128((__m128i*)in);
        input = _mm_shuffle_epi8(input, shuffle_mask_sse);

        (void)(*(int*)(out_c1) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int*)(out_c2) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int*)(out_c3) = _mm_cvtsi128_si32(input));

        out_c1 += 4;
        out_c2 += 4;
        out_c3 += 4;
        in += 12;
    }

    for (; pix < line_width; pix++) {
        out_c1[0] = in[0];
        out_c2[0] = in[1];
        out_c3[0] = in[2];
        out_c1++;
        out_c2++;
        out_c3++;
        in += 3;
    }
}

void convert_packed_to_planar_rgb_16bit_avx512(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                               uint32_t line_width) {
    const uint16_t* in = (const uint16_t*)in_rgb;
    uint16_t* out_c1 = (uint16_t*)out_comp1;
    uint16_t* out_c2 = (uint16_t*)out_comp2;
    uint16_t* out_c3 = (uint16_t*)out_comp3;

    const uint8_t mask[] = {0, 1, 6, 7, 2, 3, 8, 9, 4, 5, 10, 11, 12, 13, 14, 15};
    __m128i shuffle_mask_sse = _mm_loadu_si128((__m128i*)mask);
    __m512i shuffle_mask = _mm512_broadcast_i64x2(shuffle_mask_sse);

    uint32_t pix = 0;
    for (; (pix + 32) < line_width; pix += 32) {
#if 0
        // 0, 1, 2, 3
        __m512i a = _mm512_loadu_si512(in);
        // 4, 5, 6, 7
        __m512i b = _mm512_loadu_si512(in + 32);
        // 8, 9, 10, 11
        __m512i c = _mm512_loadu_si512(in + 64);

        // 0, 3, 6, XX
        __m512i rgb1 = _mm512_shuffle_i64x2(a, b, (0 << 0) + (3 << 2) + (2 << 4) + (0 << 6));
        // 0, 3, 6, 9
        rgb1 = _mm512_inserti64x2(rgb1, _mm512_extracti64x2_epi64(c, 0x1), 0x3);
        // XX, 5, 8, 11
        __m512i rgb3 = _mm512_shuffle_i64x2(b, c, (0 << 0) + (1 << 2) + (0 << 4) + (3 << 6));
        // 2, 5, 8, 11
        rgb3 = _mm512_inserti64x2(rgb3, _mm512_extracti64x2_epi64(a, 0x2), 0x0);

        // 1, 4
        __m256i rgb2_t1 = _mm256_shuffle_i64x2(_mm512_castsi512_si256(a), _mm512_castsi512_si256(b), (1 << 0) + (0 << 1));
        // 7, 10
        __m256i rgb2_t2 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm512_extracti64x2_epi64(b, 0x3)), _mm512_extracti64x2_epi64(c,0x2), 0x1);
        // 1, 4, 7, 10
        __m512i rgb2 = _mm512_inserti64x4(_mm512_castsi256_si512(rgb2_t1), rgb2_t2, 0x1);
        in += 96;
#else
        __m128i a = _mm_loadu_si128((__m128i*)(in));
        __m128i b = _mm_loadu_si128((__m128i*)(in + 24));
        __m256i ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        __m128i c = _mm_loadu_si128((__m128i*)(in + 48));
        __m128i d = _mm_loadu_si128((__m128i*)(in + 72));
        __m256i cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb1 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);

        in += 8;
        a = _mm_loadu_si128((__m128i*)(in));
        b = _mm_loadu_si128((__m128i*)(in + 24));
        ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        c = _mm_loadu_si128((__m128i*)(in + 48));
        d = _mm_loadu_si128((__m128i*)(in + 72));
        cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb2 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);

        in += 8;
        a = _mm_loadu_si128((__m128i*)(in));
        b = _mm_loadu_si128((__m128i*)(in + 24));
        ab = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 0x1);
        c = _mm_loadu_si128((__m128i*)(in + 48));
        d = _mm_loadu_si128((__m128i*)(in + 72));
        cd = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 0x1);
        __m512i rgb3 = _mm512_inserti64x4(_mm512_castsi256_si512(ab), cd, 0x1);
        in += 80;
#endif

        __m512i pack_lo = _mm512_shuffle_epi8(rgb1, shuffle_mask);
        rgb1 = _mm512_alignr_epi8(rgb2, rgb1, 12);
        __m512i pack_hi = _mm512_shuffle_epi8(rgb1, shuffle_mask);
        rgb2 = _mm512_alignr_epi8(rgb3, rgb2, 8);
        __m512i pack_lo2 = _mm512_shuffle_epi8(rgb2, shuffle_mask);
        rgb3 = _mm512_bsrli_epi128(rgb3, 4);
        __m512i pack_hi2 = _mm512_shuffle_epi8(rgb3, shuffle_mask);

        __m512i tmp1 = _mm512_unpacklo_epi32(pack_lo, pack_hi);
        __m512i tmp2 = _mm512_unpacklo_epi32(pack_lo2, pack_hi2);

        _mm512_storeu_si512((__m512i*)out_c1, _mm512_unpacklo_epi64(tmp1, tmp2));
        _mm512_storeu_si512((__m512i*)out_c2, _mm512_unpackhi_epi64(tmp1, tmp2));

        tmp1 = _mm512_unpackhi_epi32(pack_lo, pack_hi);
        tmp2 = _mm512_unpackhi_epi32(pack_lo2, pack_hi2);
        _mm512_storeu_si512((__m512i*)out_c3, _mm512_unpacklo_epi64(tmp1, tmp2));

        out_c1 += 32;
        out_c2 += 32;
        out_c3 += 32;
    }

    for (; (pix + 2) < line_width; pix += 2) {
        __m128i input = _mm_loadu_si128((__m128i*)in);
        input = _mm_shuffle_epi8(input, shuffle_mask_sse);

        (void)(*(int*)(out_c1) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int*)(out_c2) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int*)(out_c3) = _mm_cvtsi128_si32(input));

        out_c1 += 2;
        out_c2 += 2;
        out_c3 += 2;
        in += 6;
    }

    for (; pix < line_width; pix++) {
        out_c1[0] = in[0];
        out_c2[0] = in[1];
        out_c3[0] = in[2];
        out_c1++;
        out_c2++;
        out_c3++;
        in += 3;
    }
}
