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
#include "encoder_dsp_rtcd.h"

static INLINE void loop_small_avx512(uint32_t len, uint32_t* id, const int32_t** tmp_in, int32_t** out_hf, int32_t** out_lf) {
    const __m128i two = _mm_set1_epi32(2);
    const __m256i reg_permutevar_mask = _mm256_setr_epi32(0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07);
    __m128i prev_hf = _mm_set1_epi32((*out_hf)[*id - 1]);

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
        const __m128i hf_m1 = _mm_alignr_epi8(hf, prev_hf, 12);
        prev_hf = hf;

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
    /* The previous high-frequency sample stays in a vector instead of being
     * re-read from out_hf. Every iteration used to load the value from the very
     * cell the vector had just been stored to: the load waited on its own store,
     * and store-to-load forwarding costs about a dozen cycles. The element is
     * already sitting in the last lane of the previous iteration's result. */
    __m512i prev_hf = _mm512_set1_epi32(out_hf[0]);

    uint32_t id = 1;

    const int32_t* tmp_in = in + 2;

    for (; id < ((len - 1) / 2) - 15; id += 16) {
        /* hf */
        __m512i reg_l = _mm512_loadu_si512(tmp_in);
        __m512i reg_h = _mm512_loadu_si512(tmp_in + 16);

        __m512i id_2_p0 = _mm512_permutex2var_epi32(reg_l, reg0_permutex2var_mask, reg_h);
        __m512i id_2_p1 = _mm512_permutex2var_epi32(reg_l, reg1_permutex2var_mask, reg_h);
        const __m512i id_2_p2 = _mm512_alignr_epi32(_mm512_set1_epi32(tmp_in[32]), id_2_p0, 1);

        //out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        __m512i hf = _mm512_add_epi32(id_2_p0, id_2_p2);
        hf = _mm512_srai_epi32(hf, 1);
        hf = _mm512_sub_epi32(id_2_p1, hf);
        _mm512_storeu_si512((__m512i*)(out_hf + id), hf);

        //out_hf[id - 1]
        const __m512i hf_m1 = _mm512_alignr_epi32(hf, prev_hf, 15);
        prev_hf = hf;

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
    const __m512i sign_bit15 = _mm512_set1_epi32(BITSTREAM_MASK_SIGN);
    const __m512i reg_permutevar_mask = _mm512_setr_epi64(0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07);
    const __m512i zero = _mm512_setzero_si512();

    uint32_t i = 0;
    for (; i + 32 <= width; i += 32) {
        __m512i data1 = _mm512_loadu_si512((const void*)(in_ptr_line + i));
        __m512i data2 = _mm512_loadu_si512((const void*)(in_ptr_line + i + 16));
        const __mmask16 sign1 = _mm512_movepi32_mask(data1);
        const __mmask16 sign2 = _mm512_movepi32_mask(data2);

        data1 = _mm512_srli_epi32(_mm512_add_epi32(_mm512_abs_epi32(data1), offset_avx512), shift);
        data2 = _mm512_srli_epi32(_mm512_add_epi32(_mm512_abs_epi32(data2), offset_avx512), shift);

        const __mmask16 mask1 = sign1 & _mm512_cmpgt_epi32_mask(data1, zero);
        const __mmask16 mask2 = sign2 & _mm512_cmpgt_epi32_mask(data2, zero);

        data1 = _mm512_mask_or_epi32(data1, mask1, data1, sign_bit15);
        data2 = _mm512_mask_or_epi32(data2, mask2, data2, sign_bit15);

        __m512i packed = _mm512_packus_epi32(data1, data2);
        packed = _mm512_permutexvar_epi64(reg_permutevar_mask, packed);
        _mm512_storeu_si512((void*)(out_ptr_line + i), packed);
    }

    for (; i < width; i += 16) {
        const uint32_t rem = width - i;
        const __mmask16 m = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i data = _mm512_maskz_loadu_epi32(m, in_ptr_line + i);
        const __mmask16 sign = _mm512_movepi32_mask(data);
        data = _mm512_srli_epi32(_mm512_add_epi32(_mm512_abs_epi32(data), offset_avx512), shift);
        const __mmask16 mask = sign & _mm512_cmpgt_epi32_mask(data, zero);
        data = _mm512_mask_or_epi32(data, mask, data, sign_bit15);
        _mm512_mask_cvtepi32_storeu_epi16(out_ptr_line + i, m, data);
    }
}

#define INPUT_PREFETCH_DISTANCE_AVX512 4096

void linear_input_scaling_line_8bit_avx512(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset) {
    const __m512i offset_avx512 = _mm512_set1_epi32(offset);
    uint32_t j = 0;

    for (; j + 64 <= w; j += 64) {
        _mm_prefetch((const char*)(src + j) + INPUT_PREFETCH_DISTANCE_AVX512, _MM_HINT_T0);
        __m512i d0 = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(src + j)));
        __m512i d1 = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(src + j + 16)));
        __m512i d2 = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(src + j + 32)));
        __m512i d3 = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(src + j + 48)));

        d0 = _mm512_sub_epi32(_mm512_slli_epi32(d0, shift), offset_avx512);
        d1 = _mm512_sub_epi32(_mm512_slli_epi32(d1, shift), offset_avx512);
        d2 = _mm512_sub_epi32(_mm512_slli_epi32(d2, shift), offset_avx512);
        d3 = _mm512_sub_epi32(_mm512_slli_epi32(d3, shift), offset_avx512);

        _mm512_storeu_si512(dst + j, d0);
        _mm512_storeu_si512(dst + j + 16, d1);
        _mm512_storeu_si512(dst + j + 32, d2);
        _mm512_storeu_si512(dst + j + 48, d3);
    }
    for (; j < w; j += 16) {
        const uint32_t rem = w - j;
        const __mmask16 m = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i data = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(m, src + j));
        data = _mm512_sub_epi32(_mm512_slli_epi32(data, shift), offset_avx512);
        _mm512_mask_storeu_epi32(dst + j, m, data);
    }
}

void linear_input_scaling_line_16bit_avx512(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                            uint8_t bit_depth) {
    const uint16_t input_mask = (1 << bit_depth) - 1;
    const __m512i reg_offset = _mm512_set1_epi32(offset);
    const __m256i input_mask_epi16 = _mm256_set1_epi16(input_mask);
    uint32_t j = 0;

    for (; j + 64 <= w; j += 64) {
        _mm_prefetch((const char*)(src + j) + INPUT_PREFETCH_DISTANCE_AVX512, _MM_HINT_T0);
        _mm_prefetch((const char*)(src + j + 32) + INPUT_PREFETCH_DISTANCE_AVX512, _MM_HINT_T0);
        __m512i r0 = _mm512_cvtepi16_epi32(_mm256_and_si256(_mm256_loadu_si256((const __m256i*)(src + j)), input_mask_epi16));
        __m512i r1 = _mm512_cvtepi16_epi32(
            _mm256_and_si256(_mm256_loadu_si256((const __m256i*)(src + j + 16)), input_mask_epi16));
        __m512i r2 = _mm512_cvtepi16_epi32(
            _mm256_and_si256(_mm256_loadu_si256((const __m256i*)(src + j + 32)), input_mask_epi16));
        __m512i r3 = _mm512_cvtepi16_epi32(
            _mm256_and_si256(_mm256_loadu_si256((const __m256i*)(src + j + 48)), input_mask_epi16));

        r0 = _mm512_sub_epi32(_mm512_slli_epi32(r0, shift), reg_offset);
        r1 = _mm512_sub_epi32(_mm512_slli_epi32(r1, shift), reg_offset);
        r2 = _mm512_sub_epi32(_mm512_slli_epi32(r2, shift), reg_offset);
        r3 = _mm512_sub_epi32(_mm512_slli_epi32(r3, shift), reg_offset);

        _mm512_storeu_si512(dst + j, r0);
        _mm512_storeu_si512(dst + j + 16, r1);
        _mm512_storeu_si512(dst + j + 32, r2);
        _mm512_storeu_si512(dst + j + 48, r3);
    }
    for (; j < w; j += 16) {
        const uint32_t rem = w - j;
        const __mmask16 m = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i reg = _mm512_cvtepi16_epi32(_mm256_and_si256(_mm256_maskz_loadu_epi16(m, src + j), input_mask_epi16));
        __m512i result = _mm512_sub_epi32(_mm512_slli_epi32(reg, shift), reg_offset);
        _mm512_mask_storeu_epi32(dst + j, m, result);
    }
}

/* MSB-aligned: no masking needed, values use the full 16-bit range so zero-extend (not sign-extend). */
void linear_input_scaling_line_16bit_msb_avx512(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                                uint8_t bit_depth) {
    (void)bit_depth;
    const __m512i reg_offset = _mm512_set1_epi32(offset);
    uint32_t j = 0;

    for (; j + 64 <= w; j += 64) {
        _mm_prefetch((const char*)(src + j) + INPUT_PREFETCH_DISTANCE_AVX512, _MM_HINT_T0);
        _mm_prefetch((const char*)(src + j + 32) + INPUT_PREFETCH_DISTANCE_AVX512, _MM_HINT_T0);
        __m512i r0 = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(src + j)));
        __m512i r1 = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(src + j + 16)));
        __m512i r2 = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(src + j + 32)));
        __m512i r3 = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(src + j + 48)));

        r0 = _mm512_sub_epi32(_mm512_slli_epi32(r0, shift), reg_offset);
        r1 = _mm512_sub_epi32(_mm512_slli_epi32(r1, shift), reg_offset);
        r2 = _mm512_sub_epi32(_mm512_slli_epi32(r2, shift), reg_offset);
        r3 = _mm512_sub_epi32(_mm512_slli_epi32(r3, shift), reg_offset);

        _mm512_storeu_si512(dst + j, r0);
        _mm512_storeu_si512(dst + j + 16, r1);
        _mm512_storeu_si512(dst + j + 32, r2);
        _mm512_storeu_si512(dst + j + 48, r3);
    }
    for (; j < w; j += 16) {
        const uint32_t rem = w - j;
        const __mmask16 m = (rem >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << rem) - 1);
        __m512i reg = _mm512_cvtepu16_epi32(_mm256_maskz_loadu_epi16(m, src + j));
        __m512i result = _mm512_sub_epi32(_mm512_slli_epi32(reg, shift), reg_offset);
        _mm512_mask_storeu_epi32(dst + j, m, result);
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
    for (; i < width; i++) {
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

void gc_precinct_stage_scalar_avx512(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size,
                                     uint32_t width) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);

    const __m512i dup32 = _mm512_set1_epi32(32);
    const __m512i mask_15bit = _mm512_set1_epi32(0x7FFF);
    const __m512i even_dwords = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);

    uint32_t i = 0;
    for (; i + 64 <= width; i += 64) {
        __m512i v0 = _mm512_loadu_si512((const void*)(coeff_data_ptr_16bit + i));
        __m512i v1 = _mm512_loadu_si512((const void*)(coeff_data_ptr_16bit + i + 32));

        v0 = _mm512_or_si512(v0, _mm512_srli_epi64(v0, 32));
        v0 = _mm512_or_si512(v0, _mm512_srli_epi64(v0, 16));
        v1 = _mm512_or_si512(v1, _mm512_srli_epi64(v1, 32));
        v1 = _mm512_or_si512(v1, _mm512_srli_epi64(v1, 16));

        __m512i merged = _mm512_and_si512(_mm512_permutex2var_epi32(v0, even_dwords, v1), mask_15bit);
        __m512i gclis = _mm512_sub_epi32(dup32, _mm512_lzcnt_epi32(merged));
        _mm_storeu_si128((__m128i*)(gcli_data_ptr + (i >> 2)), _mm512_cvtepi32_epi8(gclis));
    }

    const uint32_t leftover = width - i;
    if (leftover) {
        const __mmask32 m0 = (leftover >= 32) ? ~(__mmask32)0 : (((__mmask32)1 << leftover) - 1);
        const __mmask32 m1 = (leftover > 32) ? (((__mmask32)1 << (leftover - 32)) - 1) : 0;
        __m512i v0 = _mm512_maskz_loadu_epi16(m0, coeff_data_ptr_16bit + i);
        __m512i v1 = _mm512_maskz_loadu_epi16(m1, coeff_data_ptr_16bit + i + 32);

        v0 = _mm512_or_si512(v0, _mm512_srli_epi64(v0, 32));
        v0 = _mm512_or_si512(v0, _mm512_srli_epi64(v0, 16));
        v1 = _mm512_or_si512(v1, _mm512_srli_epi64(v1, 32));
        v1 = _mm512_or_si512(v1, _mm512_srli_epi64(v1, 16));

        __m512i merged = _mm512_and_si512(_mm512_permutex2var_epi32(v0, even_dwords, v1), mask_15bit);
        __m512i gclis = _mm512_sub_epi32(dup32, _mm512_lzcnt_epi32(merged));
        const uint32_t out_groups = (leftover + GROUP_SIZE - 1) >> 2;
        const __mmask16 out_mask = (out_groups >= 16) ? (__mmask16)0xFFFF : (__mmask16)((1u << out_groups) - 1);
        _mm_mask_storeu_epi8(gcli_data_ptr + (i >> 2), out_mask, _mm512_cvtepi32_epi8(gclis));
    }
}

void gc_precinct_sigflags_max_avx512(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                     uint32_t gcli_width) {
    UNUSED(group_sign_size);
    assert(group_sign_size == SIGNIFICANCE_GROUP_SIZE);

    uint32_t i = 0;
    for (; i + 64 <= gcli_width; i += 64) {
        __m512i v = _mm512_loadu_si512((const void*)(gcli_data_ptr + i));
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 32));
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 16));
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 8));
        _mm_storel_epi64((__m128i*)(significance_data_max_ptr + (i >> 3)), _mm512_cvtepi64_epi8(v));
    }

    const uint32_t rem = gcli_width - i;
    if (rem) {
        const __mmask64 m = ((__mmask64)1 << rem) - 1;
        __m512i v = _mm512_maskz_loadu_epi8(m, gcli_data_ptr + i);
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 32));
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 16));
        v = _mm512_max_epu8(v, _mm512_srli_epi64(v, 8));
        const uint32_t out_n = (rem + SIGNIFICANCE_GROUP_SIZE - 1) >> 3;
        _mm_mask_storeu_epi8(significance_data_max_ptr + (i >> 3), (__mmask16)((1u << out_n) - 1), _mm512_cvtepi64_epi8(v));
    }
}

/* Histogram of sixteen bins over bytes from [0, TRUNCATION_MAX].
 *
 * Built the same way as the AVX2 version (population of a compare mask, no
 * prologue and no epilogue), except that here the compare mask is a ready k
 * register rather than the result of VPMOVMSKB, and sixty-four bytes are
 * handled per pass instead of thirty-two. The tail is read by a masked load
 * into a vector pre-filled with 0xFF, so no copying into a buffer is needed at
 * all. */
#define GC_HIST_BIN_AVX512(v, K) \
    total[K] += (uint32_t)_mm_popcnt_u64((uint64_t)_mm512_cmpeq_epi8_mask((v), _mm512_set1_epi8((char)(K))))

#define GC_HIST_LO_AVX512(v)      \
    do {                          \
        GC_HIST_BIN_AVX512(v, 1); \
        GC_HIST_BIN_AVX512(v, 2); \
        GC_HIST_BIN_AVX512(v, 3); \
        GC_HIST_BIN_AVX512(v, 4); \
        GC_HIST_BIN_AVX512(v, 5); \
        GC_HIST_BIN_AVX512(v, 6); \
        GC_HIST_BIN_AVX512(v, 7); \
    } while (0)

#define GC_HIST_HI_AVX512(v)       \
    do {                           \
        GC_HIST_BIN_AVX512(v, 8);  \
        GC_HIST_BIN_AVX512(v, 9);  \
        GC_HIST_BIN_AVX512(v, 10); \
        GC_HIST_BIN_AVX512(v, 11); \
        GC_HIST_BIN_AVX512(v, 12); \
        GC_HIST_BIN_AVX512(v, 13); \
        GC_HIST_BIN_AVX512(v, 14); \
        GC_HIST_BIN_AVX512(v, 15); \
    } while (0)

void gc_histogram_16_avx512(const uint8_t* data, uint32_t width, uint16_t* hist) {
    if (width <= 8) {
        _mm256_storeu_si256((__m256i*)hist, _mm256_setzero_si256());
        for (uint32_t i = 0; i < width; ++i) {
            hist[data[i]]++;
        }
        return;
    }

    uint32_t total[TRUNCATION_MAX + 1] = {0};
    const __m512i high_bit3 = _mm512_set1_epi8(0x08);
    uint32_t i = 0;

    for (; i + 64 <= width; i += 64) {
        const __m512i v = _mm512_loadu_si512((const void*)(data + i));
        GC_HIST_LO_AVX512(v);
        if (_mm512_test_epi8_mask(v, high_bit3) != 0) {
            GC_HIST_HI_AVX512(v);
        }
    }
    if (i < width) {
        /* Unused lanes stay at 0x10: outside [0, 15] and bit 3 is clear. */
        const __mmask64 m = ((__mmask64)1 << (width - i)) - 1;
        const __m512i v = _mm512_mask_loadu_epi8(_mm512_set1_epi8(0x10), m, data + i);
        GC_HIST_LO_AVX512(v);
        if (_mm512_test_epi8_mask(v, high_bit3) != 0) {
            GC_HIST_HI_AVX512(v);
        }
    }
    uint32_t nonzero = 0;
    for (uint32_t k = 1; k <= TRUNCATION_MAX; ++k) {
        hist[k] = (uint16_t)total[k];
        nonzero += total[k];
    }
    hist[0] = (uint16_t)(width - nonzero);
}

void forward_rct_line_avx512(int32_t* comp0, int32_t* comp1, int32_t* comp2, uint32_t width) {
    uint32_t x = 0;
    for (; x + 16 <= width; x += 16) {
        const __m512i o0 = _mm512_loadu_si512((const void*)(comp0 + x));
        const __m512i o1 = _mm512_loadu_si512((const void*)(comp1 + x));
        const __m512i o2 = _mm512_loadu_si512((const void*)(comp2 + x));
        const __m512i i1 = _mm512_sub_epi32(o2, o1);
        const __m512i i2 = _mm512_sub_epi32(o0, o1);
        const __m512i i0 = _mm512_add_epi32(o1, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        _mm512_storeu_si512((void*)(comp0 + x), i0);
        _mm512_storeu_si512((void*)(comp1 + x), i1);
        _mm512_storeu_si512((void*)(comp2 + x), i2);
    }
    if (x < width) {
        const __mmask16 m = (__mmask16)((1u << (width - x)) - 1);
        const __m512i o0 = _mm512_maskz_loadu_epi32(m, comp0 + x);
        const __m512i o1 = _mm512_maskz_loadu_epi32(m, comp1 + x);
        const __m512i o2 = _mm512_maskz_loadu_epi32(m, comp2 + x);
        const __m512i i1 = _mm512_sub_epi32(o2, o1);
        const __m512i i2 = _mm512_sub_epi32(o0, o1);
        const __m512i i0 = _mm512_add_epi32(o1, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        _mm512_mask_storeu_epi32(comp0 + x, m, i0);
        _mm512_mask_storeu_epi32(comp1 + x, m, i1);
        _mm512_mask_storeu_epi32(comp2 + x, m, i2);
    }
}

void gc_lossless_stats_gtli0_avx512(const uint8_t* data, uint32_t width, uint32_t* out_sum, uint16_t* out_zero) {
    const __m512i zero = _mm512_setzero_si512();
    __m512i sum_v = zero;
    uint32_t zcnt = 0;
    uint32_t i = 0;

    for (; i + 64 <= width; i += 64) {
        const __m512i v = _mm512_loadu_si512((const void*)(data + i));
        sum_v = _mm512_add_epi64(sum_v, _mm512_sad_epu8(v, zero));
        zcnt += (uint32_t)_mm_popcnt_u64((uint64_t)_mm512_cmpeq_epi8_mask(v, zero));
    }
    if (i < width) {
        const __mmask64 m = ((__mmask64)1 << (width - i)) - 1;
        const __m512i v = _mm512_maskz_loadu_epi8(m, data + i);
        sum_v = _mm512_add_epi64(sum_v, _mm512_sad_epu8(v, zero));
        zcnt += (uint32_t)_mm_popcnt_u64((uint64_t)_mm512_mask_cmpeq_epi8_mask(m, v, zero));
    }
    *out_sum = (uint32_t)_mm512_reduce_add_epi64(sum_v);
    *out_zero = (uint16_t)zcnt;
}

void rc_lut_derive_avx512(uint16_t* gc_hist, uint32_t* no_sign_tbl, uint32_t* sign_tbl, uint16_t* sig_hist) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i h = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)gc_hist));

    /* Suffix sum of h[i+1..15]: s[i] = h[i+1] for i<15, s[15] = 0. */
    __m512i sum_back = _mm512_alignr_epi32(zero, h, 1);
    sum_back = _mm512_add_epi32(sum_back, _mm512_alignr_epi32(zero, sum_back, 1));
    sum_back = _mm512_add_epi32(sum_back, _mm512_alignr_epi32(zero, sum_back, 2));
    sum_back = _mm512_add_epi32(sum_back, _mm512_alignr_epi32(zero, sum_back, 4));
    sum_back = _mm512_add_epi32(sum_back, _mm512_alignr_epi32(zero, sum_back, 8));

    /* Second suffix sum: size_sign[i] = sum_{j=i+1..15} (j - i) * h[j]. */
    __m512i size_sign = _mm512_add_epi32(sum_back, _mm512_alignr_epi32(zero, sum_back, 1));
    size_sign = _mm512_add_epi32(size_sign, _mm512_alignr_epi32(zero, size_sign, 2));
    size_sign = _mm512_add_epi32(size_sign, _mm512_alignr_epi32(zero, size_sign, 4));
    size_sign = _mm512_add_epi32(size_sign, _mm512_alignr_epi32(zero, size_sign, 8));

    /* size_no_sign[i] = sum_{j=i+1..15} (j - i + 1) * h[j] = size_sign[i] + sum_back[i]. */
    _mm512_storeu_si512((void*)no_sign_tbl, _mm512_add_epi32(size_sign, sum_back));
    if (sign_tbl) {
        _mm512_storeu_si512((void*)sign_tbl, size_sign);
    }

    /* Prefix sum of gc_hist[0..15]. */
    __m512i p = _mm512_add_epi32(h, _mm512_alignr_epi32(h, zero, 15));
    p = _mm512_add_epi32(p, _mm512_alignr_epi32(p, zero, 14));
    p = _mm512_add_epi32(p, _mm512_alignr_epi32(p, zero, 12));
    p = _mm512_add_epi32(p, _mm512_alignr_epi32(p, zero, 8));
    _mm256_storeu_si256((__m256i*)gc_hist, _mm512_cvtepi32_epi16(p));

    if (sig_hist) {
        __m512i sh = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)sig_hist));
        sh = _mm512_add_epi32(sh, _mm512_alignr_epi32(sh, zero, 15));
        sh = _mm512_add_epi32(sh, _mm512_alignr_epi32(sh, zero, 14));
        sh = _mm512_add_epi32(sh, _mm512_alignr_epi32(sh, zero, 12));
        sh = _mm512_add_epi32(sh, _mm512_alignr_epi32(sh, zero, 8));
        _mm256_storeu_si256((__m256i*)sig_hist, _mm512_cvtepi32_epi16(sh));
    }
}
