/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "CodeDeprecated-avx512.h"
#include "CodeDeprecated.h"
#include <immintrin.h>

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

int dwt_vertical_depricated_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                   uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in) {
    const int32_t *col0, *col1, *col2;
    int32_t *ptr_lf, *ptr_hf, *pre_hf;
    /* each __m512i has 16 int32_t(int32_t) */
    if ((width % 16) || (height < 2)) {
        printf("%s, unsupport w %d\n", __func__, width);
        return dwt_vertical_depricated_c(out_lf, out_hf, in, width, height, stride_lf, stride_hf, stride_in);
    }
    uint32_t col;
    __m512i one = _mm512_set1_epi32(1);
    __m512i two = _mm512_set1_epi32(2);

    /* the first col */
    col = 0;
    col0 = in + col * stride_in;
    col1 = in + (col + 1) * stride_in;
    col2 = in + (col + 2) * stride_in;
    ptr_lf = out_lf + (col / 2) * stride_lf;
    ptr_hf = out_hf + (col / 2) * stride_hf;
    for (uint32_t row = 0; row < width; row += 16) {
        __m512i reg_col0 = _mm512_loadu_si512(col0);
        __m512i reg_col1 = _mm512_loadu_si512(col1);
        __m512i reg_col2 = _mm512_loadu_si512(col2);

        __m512i reg_add = _mm512_add_epi32(reg_col0, reg_col2);
        __m512i reg_div2 = _mm512_srai_epi32(reg_add, 1);
        __m512i hf = _mm512_sub_epi32(reg_col1, reg_div2);
        _mm512_storeu_si512(ptr_hf, hf);

        __m512i reg_col1_plus = _mm512_add_epi32(hf, one);
        reg_div2 = _mm512_srai_epi32(reg_col1_plus, 1);
        __m512i lf = _mm512_add_epi32(reg_col0, reg_div2);
        _mm512_storeu_si512(ptr_lf, lf);

        col0 += 16;
        col1 += 16;
        col2 += 16;
        ptr_hf += 16;
        ptr_lf += 16;
    }

    /* the middle col */
    for (col = 2; col < (height - 2); col += 2) {
        col0 = in + col * stride_in;
        col1 = in + (col + 1) * stride_in;
        col2 = in + (col + 2) * stride_in;
        pre_hf = out_hf + ((col - 1) / 2) * stride_hf;
        ptr_lf = out_lf + (col / 2) * stride_lf;
        ptr_hf = out_hf + (col / 2) * stride_hf;
        for (uint32_t row = 0; row < width; row += 16) {
            __m512i reg_col0 = _mm512_loadu_si512(col0);
            __m512i reg_col1 = _mm512_loadu_si512(col1);
            __m512i reg_col2 = _mm512_loadu_si512(col2);

            __m512i reg_add = _mm512_add_epi32(reg_col0, reg_col2);
            __m512i reg_div2 = _mm512_srai_epi32(reg_add, 1);
            __m512i hf = _mm512_sub_epi32(reg_col1, reg_div2);
            _mm512_storeu_si512(ptr_hf, hf);

            __m512i reg_pre_hf = _mm512_loadu_si512(pre_hf);
            reg_add = _mm512_add_epi32(_mm512_add_epi32(reg_pre_hf, hf), two);
            __m512i reg_div4 = _mm512_srai_epi32(reg_add, 2);
            __m512i lf = _mm512_add_epi32(reg_col0, reg_div4);
            _mm512_storeu_si512(ptr_lf, lf);

            col0 += 16;
            col1 += 16;
            col2 += 16;
            pre_hf += 16;
            ptr_hf += 16;
            ptr_lf += 16;
        }
    }

    // the last col
    col0 = in + col * stride_in;
    col1 = in + (col + 1) * stride_in;
    pre_hf = out_hf + ((col - 1) / 2) * stride_hf;
    ptr_lf = out_lf + (col / 2) * stride_lf;
    ptr_hf = out_hf + (col / 2) * stride_hf;

    if (height % 2) {
        for (uint32_t row = 0; row < width; row += 16) {
            __m512i reg_col0 = _mm512_loadu_si512(col0);
            __m512i reg_pre_hf = _mm512_loadu_si512(pre_hf);

            __m512i reg_add = _mm512_add_epi32(reg_pre_hf, one);
            __m512i reg_div2 = _mm512_srai_epi32(reg_add, 1);
            __m512i lf = _mm512_add_epi32(reg_col0, reg_div2);
            _mm512_storeu_si512(ptr_lf, lf);

            col0 += 16;
            pre_hf += 16;
            ptr_lf += 16;
        }
    }
    else {
        for (uint32_t row = 0; row < width; row += 16) {
            __m512i reg_col0 = _mm512_loadu_si512(col0);
            __m512i reg_col1 = _mm512_loadu_si512(col1);
            __m512i hf = _mm512_sub_epi32(reg_col1, reg_col0);
            _mm512_storeu_si512(ptr_hf, hf);

            __m512i reg_pre_hf = _mm512_loadu_si512(pre_hf);
            __m512i reg_add = _mm512_add_epi32(_mm512_add_epi32(reg_pre_hf, hf), two);
            __m512i reg_div4 = _mm512_srai_epi32(reg_add, 2);
            __m512i lf = _mm512_add_epi32(reg_col0, reg_div4);
            _mm512_storeu_si512(ptr_lf, lf);

            col0 += 16;
            col1 += 16;
            pre_hf += 16;
            ptr_hf += 16;
            ptr_lf += 16;
        }
    }

    return 0;
}

static uint32_t reg0_permutex2var_mask_table[16] = {
    0x00,
    0x02,
    0x04,
    0x06,
    0x08,
    0x0a,
    0x0c,
    0x0e,
    0x10,
    0x12,
    0x14,
    0x16,
    0x18,
    0x1a,
    0x1c,
    0x1e,
};

static uint32_t reg1_permutex2var_mask_table[16] = {
    0x01,
    0x03,
    0x05,
    0x07,
    0x09,
    0x0b,
    0x0d,
    0x0f,
    0x11,
    0x13,
    0x15,
    0x17,
    0x19,
    0x1b,
    0x1d,
    0x1f,
};

int dwt_horizontal_depricated_avx512(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                     uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in) {
    /* each __m512i batch handle 32 int32_t(int32_t) */
    __m512i reg0_permutex2var_mask = _mm512_loadu_si512(reg0_permutex2var_mask_table);
    __m512i reg1_permutex2var_mask = _mm512_loadu_si512(reg1_permutex2var_mask_table);
    __m512i two = _mm512_set1_epi32(2);

    uint32_t simd_batch = width / 32;
    uint32_t remaining = width % 32;

    /* simd for hf and lf */
    for (uint32_t row = 0; row < height; row++) {
        const int32_t* src_row = in + row * stride_in;
        int32_t* dst_hf_row = out_hf + row * stride_hf;
        int32_t* dst_lf_row = out_lf + row * stride_lf;
        const int32_t *src_l, *src_h;

        for (uint32_t b = 0; b < simd_batch; b++) {
            src_l = src_row;      /* d0 - d15 */
            src_h = src_row + 16; /* d16 - d31 */

            /* hf */
            __m512i reg_l = _mm512_loadu_si512(src_l);
            __m512i reg_h = _mm512_loadu_si512(src_h);
            /* d0, d2, d4, d6, d8, d10, d12, d14 */
            __m512i reg_col0 = _mm512_permutex2var_epi32(reg_l, reg0_permutex2var_mask, reg_h);
            __m512i reg_col1 = _mm512_permutex2var_epi32(reg_l, reg1_permutex2var_mask, reg_h);
            reg_l = _mm512_loadu_si512(src_l + 2);
            reg_h = _mm512_loadu_si512(src_h + 2);
            __m512i reg_col2 = _mm512_permutex2var_epi32(reg_l, reg0_permutex2var_mask, reg_h);
            __m512i reg_add = _mm512_add_epi32(reg_col0, reg_col2);
            __m512i reg_div2 = _mm512_srai_epi32(reg_add, 1);
            __m512i hf = _mm512_sub_epi32(reg_col1, reg_div2);
            _mm512_storeu_si512(dst_hf_row, hf);

            /* lf */
            reg_col1 = _mm512_loadu_si512(dst_hf_row - 1);
            reg_add = _mm512_add_epi32(_mm512_add_epi32(reg_col1, hf), two);
            __m512i reg_div4 = _mm512_srai_epi32(reg_add, 2);
            __m512i lf = _mm512_add_epi32(reg_col0, reg_div4);
            _mm512_storeu_si512(dst_lf_row, lf);

            dst_lf_row += 16;
            dst_hf_row += 16;
            src_row += 32;
        }

        if (remaining) {
            for (uint32_t i = 0; i < (remaining - 1); i += 2) {
                dst_hf_row[i / 2] = src_row[1 + i] - ((src_row[0 + i] + src_row[2 + i]) >> 1);
            }

            if (remaining > 1) {
                dst_lf_row[0] = src_row[0] + ((dst_hf_row[-1] + dst_hf_row[0] + 2) >> 2);
            }

            for (uint32_t i = 1; i < (remaining / 2); i++) {
                dst_lf_row[i] = src_row[2 * i] + ((dst_hf_row[i - 1] + dst_hf_row[i] + 2) >> 2);
            }
            dst_lf_row += remaining / 2;
            dst_hf_row += remaining / 2;
            src_row += remaining;
        }

        // correct the last for hf and lf
        if (width % 2) {
            dst_lf_row[0] = src_row[-1] + ((dst_hf_row[-1] + 1) >> 1);
        }
        else {
            dst_hf_row[-1] = src_row[-1] - src_row[-2];
            dst_lf_row[-1] = src_row[-2] + ((dst_hf_row[-2] + dst_hf_row[-1] + 2) >> 2);
        }

        // correct the first for lf
        out_lf[row * stride_lf] = in[row * stride_in] + ((out_hf[row * stride_hf] + 1) >> 1);
    }

    return 0;
}
