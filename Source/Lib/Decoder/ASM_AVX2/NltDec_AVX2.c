/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltDec_AVX2.h"

void linear_output_scaling_8bit_avx2(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                     svt_jpeg_xs_image_buffer_t* out) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m256i round_avx2 = _mm256_set1_epi32(round);
    const __m256i zero_avx2 = _mm256_setzero_si256();
    const __m256i max_avx2 = _mm256_set1_epi32(m);
    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        uint32_t h = pi->components[i].height;
        uint8_t* out_buf = (uint8_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        int32_t* in_buf = comps[i];
        for (uint32_t y = 0; y < h; y++) {
            for (x = 0; x <= w - 16; x += 16) {
                __m256i v1_avx2 = _mm256_loadu_si256((__m256i*)(in_buf + y * w + x));
                v1_avx2 = _mm256_add_epi32(v1_avx2, round_avx2);
                v1_avx2 = _mm256_srai_epi32(v1_avx2, dzeta);
                v1_avx2 = _mm256_max_epi32(_mm256_min_epi32(v1_avx2, max_avx2), zero_avx2);

                __m256i v2_avx2 = _mm256_loadu_si256((__m256i*)(in_buf + y * w + x + 8));
                v2_avx2 = _mm256_add_epi32(v2_avx2, round_avx2);
                v2_avx2 = _mm256_srai_epi32(v2_avx2, dzeta);
                v2_avx2 = _mm256_max_epi32(_mm256_min_epi32(v2_avx2, max_avx2), zero_avx2);

                __m256i packed = _mm256_packus_epi32(v1_avx2, v2_avx2);
                packed = _mm256_permute4x64_epi64(packed, 0xd8);
                packed = _mm256_packus_epi16(packed, packed);
                packed = _mm256_permute4x64_epi64(packed, 0xd8);
                _mm_storeu_si128((__m128i*)(out_buf + y * out_stride + x), _mm256_castsi256_si128(packed));
            }
            for (; x < w; x++) {
                int32_t v = in_buf[y * w + x];
                v += round;
                v >>= dzeta;
                out_buf[y * out_stride + x] = (uint8_t)(v > m ? m : v < 0 ? 0 : v);
            }
        }
    }
}

void linear_output_scaling_16bit_avx2(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                      svt_jpeg_xs_image_buffer_t* out) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m256i round_avx2 = _mm256_set1_epi32(round);
    const __m256i zero_avx2 = _mm256_setzero_si256();
    const __m256i max_avx2 = _mm256_set1_epi32(m);

    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        uint32_t h = pi->components[i].height;
        uint16_t* out_buf = (uint16_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        int32_t* in_buf = comps[i];
        for (uint32_t y = 0; y < h; y++) {
            for (x = 0; x <= w - 16; x += 16) {
                __m256i v1_avx2 = _mm256_loadu_si256((__m256i*)(in_buf + y * w + x));
                v1_avx2 = _mm256_add_epi32(v1_avx2, round_avx2);
                v1_avx2 = _mm256_srai_epi32(v1_avx2, dzeta);
                v1_avx2 = _mm256_max_epi32(_mm256_min_epi32(v1_avx2, max_avx2), zero_avx2);

                __m256i v2_avx2 = _mm256_loadu_si256((__m256i*)(in_buf + y * w + x + 8));
                v2_avx2 = _mm256_add_epi32(v2_avx2, round_avx2);
                v2_avx2 = _mm256_srai_epi32(v2_avx2, dzeta);
                v2_avx2 = _mm256_max_epi32(_mm256_min_epi32(v2_avx2, max_avx2), zero_avx2);

                __m256i packed = _mm256_packus_epi32(v1_avx2, v2_avx2);
                packed = _mm256_permute4x64_epi64(packed, 0xd8);
                _mm256_storeu_si256((__m256i*)(out_buf + y * out_stride + x), packed);
            }
            for (; x < w; x++) {
                int32_t v = in_buf[y * w + x];
                v += round;
                v >>= dzeta;
                out_buf[y * out_stride + x] = (uint16_t)(v > m ? m : v < 0 ? 0 : v);
            }
        }
    }
}

void inv_sign_avx2(uint16_t* in_out, uint32_t width) {
    const __m256i coeff_mask = _mm256_set1_epi16((uint16_t)(BITSTREAM_MASK_SIGN)-1);

    const uint32_t simd_batch = width / 16;
    const uint32_t remaining = width % 16;

    for (uint32_t i = 0; i < simd_batch; i++) {
        __m256i data = _mm256_loadu_si256((__m256i*)(in_out));

        __m256i data_abs = _mm256_and_si256(data, coeff_mask);
        data = _mm256_sign_epi16(data_abs, data);

        _mm256_storeu_si256((__m256i*)(in_out), data);
        in_out += 16;
    }

    for (uint32_t i = 0; i < remaining; i++) {
        const uint16_t val = in_out[i];
        in_out[i] = (val & BITSTREAM_MASK_SIGN) ? -((int32_t)(val & ~BITSTREAM_MASK_SIGN)) : (int32_t)val;
    }
}

void linear_output_scaling_8bit_line_avx2(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m256i round_avx2 = _mm256_set1_epi32(round);
    const __m256i zero_avx2 = _mm256_setzero_si256();
    const __m256i max_avx2 = _mm256_set1_epi32(m);

    for (x = 0; x <= (int32_t)w - 16; x += 16) {
        __m256i v1_avx2 = _mm256_loadu_si256((__m256i*)(in + x));
        v1_avx2 = _mm256_add_epi32(v1_avx2, round_avx2);
        v1_avx2 = _mm256_srai_epi32(v1_avx2, dzeta);
        v1_avx2 = _mm256_max_epi32(_mm256_min_epi32(v1_avx2, max_avx2), zero_avx2);

        __m256i v2_avx2 = _mm256_loadu_si256((__m256i*)(in + x + 8));
        v2_avx2 = _mm256_add_epi32(v2_avx2, round_avx2);
        v2_avx2 = _mm256_srai_epi32(v2_avx2, dzeta);
        v2_avx2 = _mm256_max_epi32(_mm256_min_epi32(v2_avx2, max_avx2), zero_avx2);

        __m256i packed = _mm256_packus_epi32(v1_avx2, v2_avx2);
        packed = _mm256_permute4x64_epi64(packed, 0xd8);
        packed = _mm256_packus_epi16(packed, packed);
        packed = _mm256_permute4x64_epi64(packed, 0xd8);
        _mm_storeu_si128((__m128i*)(out + x), _mm256_castsi256_si128(packed));
    }
    for (; x < (int32_t)w; x++) {
        int32_t v = in[x];
        v += round;
        v >>= dzeta;
        out[x] = (uint8_t)(v > m ? m : v < 0 ? 0 : v);
    }
}

void linear_output_scaling_16bit_line_avx2(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w) {
    int32_t x;
    const int32_t dzeta = bw - depth;
    const uint32_t round = ((1 << bw) >> 1) + ((1 << dzeta) >> 1);
    const int32_t m = (1 << depth) - 1;
    const __m256i round_avx2 = _mm256_set1_epi32(round);
    const __m256i zero_avx2 = _mm256_setzero_si256();
    const __m256i max_avx2 = _mm256_set1_epi32(m);

    for (x = 0; x <= (int32_t)w - 16; x += 16) {
        __m256i v1_avx2 = _mm256_loadu_si256((__m256i*)(in + x));
        v1_avx2 = _mm256_add_epi32(v1_avx2, round_avx2);
        v1_avx2 = _mm256_srai_epi32(v1_avx2, dzeta);
        v1_avx2 = _mm256_max_epi32(_mm256_min_epi32(v1_avx2, max_avx2), zero_avx2);

        __m256i v2_avx2 = _mm256_loadu_si256((__m256i*)(in + x + 8));
        v2_avx2 = _mm256_add_epi32(v2_avx2, round_avx2);
        v2_avx2 = _mm256_srai_epi32(v2_avx2, dzeta);
        v2_avx2 = _mm256_max_epi32(_mm256_min_epi32(v2_avx2, max_avx2), zero_avx2);

        __m256i packed = _mm256_packus_epi32(v1_avx2, v2_avx2);
        packed = _mm256_permute4x64_epi64(packed, 0xd8);
        _mm256_storeu_si256((__m256i*)(out + x), packed);
    }
    for (; x < (int32_t)w; x++) {
        int32_t v = in[x];
        v += round;
        v >>= dzeta;
        out[x] = (uint16_t)(v > m ? m : v < 0 ? 0 : v);
    }
}
