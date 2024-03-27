/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#define RTC_EXTERN

#include "CodeDeprecated-avx512.h"
#include "CodeDeprecated.h"
#include "random.h"
#include <immintrin.h>
#include <Pi.h>

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

#ifdef ARCH_X86_64
#define SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512) \
    if (((uintptr_t)NULL != (uintptr_t)mmx) && (flags & CPU_FLAGS_MMX))                           \
        ptr = mmx;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)sse) && (flags & CPU_FLAGS_SSE))                           \
        ptr = sse;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)sse2) && (flags & CPU_FLAGS_SSE2))                         \
        ptr = sse2;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)sse3) && (flags & CPU_FLAGS_SSE3))                         \
        ptr = sse3;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)ssse3) && (flags & CPU_FLAGS_SSSE3))                       \
        ptr = ssse3;                                                                              \
    if (((uintptr_t)NULL != (uintptr_t)sse4_1) && (flags & CPU_FLAGS_SSE4_1))                     \
        ptr = sse4_1;                                                                             \
    if (((uintptr_t)NULL != (uintptr_t)sse4_2) && (flags & CPU_FLAGS_SSE4_2))                     \
        ptr = sse4_2;                                                                             \
    if (((uintptr_t)NULL != (uintptr_t)avx) && (flags & CPU_FLAGS_AVX))                           \
        ptr = avx;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)avx2) && (flags & CPU_FLAGS_AVX2))                         \
        ptr = avx2;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)avx512) && (flags & CPU_FLAGS_AVX512F))                    \
        ptr = avx512;
#else /* ARCH_X86_64 */
#define SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512)
#endif /* ARCH_X86_64 */

#define SET_FUNCTIONS(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512)     \
    do {                                                                                          \
        if (check_pointer_was_set && ptr != 0) {                                                  \
            printf("Error: %s:%i: Pointer \"%s\" is set before!\n", __FILE__, __LINE__, #ptr);    \
            assert(0);                                                                            \
        }                                                                                         \
        if ((uintptr_t)NULL == (uintptr_t)c) {                                                    \
            printf("Error: %s:%i: Pointer \"%s\" on C is NULL!\n", __FILE__, __LINE__, #ptr);     \
            assert(0);                                                                            \
        }                                                                                         \
        ptr = c;                                                                                  \
        SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512) \
    } while (0)

/* Macros SET_* use local variable CPU_FLAGS flags and Bool
 * check_pointer_was_set */
#define SET_ONLY_C(ptr, c)                                  SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define SET_SSE2(ptr, c, sse2)                              SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, 0, 0)
#define SET_SSE2_AVX2(ptr, c, sse2, avx2)                   SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, avx2, 0)
#define SET_SSE2_AVX512(ptr, c, sse2, avx512)               SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, 0, avx512)
#define SET_SSSE3(ptr, c, ssse3)                            SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, ssse3, 0, 0, 0, 0, 0)
#define SET_SSSE3_AVX2(ptr, c, ssse3, avx2)                 SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, ssse3, 0, 0, 0, avx2, 0)
#define SET_SSE41(ptr, c, sse4_1)                           SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, 0, 0)
#define SET_SSE41_AVX2(ptr, c, sse4_1, avx2)                SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, avx2, 0)
#define SET_SSE41_AVX2_AVX512(ptr, c, sse4_1, avx2, avx512) SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, avx2, avx512)
#define SET_AVX2(ptr, c, avx2)                              SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, avx2, 0)
#define SET_AVX2_AVX512(ptr, c, avx2, avx512)               SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, avx2, avx512)

void setup_depricated_test_rtcd_internal(CPU_FLAGS flags) {
    static uint8_t first_call_setup = 1;
    uint8_t check_pointer_was_set = first_call_setup;
    first_call_setup = 0;

    SET_AVX2_AVX512(dwt_horizontal_depricated, dwt_horizontal_depricated_c, NULL, dwt_horizontal_depricated_avx512);
    SET_AVX2_AVX512(dwt_vertical_depricated, dwt_vertical_depricated_c, NULL, dwt_vertical_depricated_avx512);
    SET_AVX2(idwt_deprecated_vertical, idwt_deprecated_vertical_c, idwt_deprecated_vertical_avx2);
    SET_AVX2(idwt_deprecated_horizontal, idwt_deprecated_horizontal_c, idwt_deprecated_horizontal_avx2);
}

void dwt_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len, uint32_t stride_lf, uint32_t stride_hf,
                      uint32_t stride_in) {
    assert((len >= 2) && "[dwt_depricated_c()] ERROR: Length is too small!");

    for (uint32_t i = 1; i < len - 1; i += 2) {
        out_hf[(i / 2) * stride_hf] = in[i * stride_in] - ((in[(i - 1) * stride_in] + in[(i + 1) * stride_in]) >> 1);
    }
    if (!(len & 1)) {
        out_hf[((len - 1) / 2) * stride_hf] = in[(len - 1) * stride_in] - in[(len - 2) * stride_in];
    }

    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);
    for (uint32_t i = 2; i <= len - 2; i += 2) {
        out_lf[(i / 2) * stride_lf] = in[i * stride_in] +
            ((out_hf[((i - 1) / 2) * stride_hf] + out_hf[((i + 1) / 2) * stride_hf] + 2) >> 2);
    }
    if (len & 1) {
        out_lf[((len - 1) / 2) * stride_lf] = in[(len - 1) * stride_in] + ((out_hf[((len - 2) / 2) * stride_hf] + 1) >> 1);
    }
}

int dwt_vertical_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                              uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in) {
    for (uint32_t i = 0; i < width; i++) {
        dwt_depricated_c(out_lf, out_hf, in, height, stride_lf, stride_hf, stride_in);
        in++;
        out_lf++;
        out_hf++;
    }
    return 0;
}

int dwt_horizontal_depricated_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t width, uint32_t height,
                                uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_in) {
    for (uint32_t i = 0; i < height; i++) {
        dwt_depricated_c(out_lf, out_hf, in, width, 1, 1, 1);
        in += stride_in;
        out_lf += stride_lf;
        out_hf += stride_hf;
    }
    return 0;
}

void idwt_deprecated_vertical_avx2(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                   uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out) {
    if (width < 8) {
        idwt_deprecated_vertical_c(in_lf, in_hf, out, width, height, stride_lf, stride_hf, stride_out);
        return;
    }

    uint32_t col;
    const __m256i one_avx2 = _mm256_set1_epi32(1);
    const __m256i two_avx2 = _mm256_set1_epi32(2);
    const uint32_t loop_high_even = (height - 2) / 2;
    const uint32_t loop_high_odd = (height - 1) / 2;

    const int32_t* lf_in_col = in_lf;
    const int32_t* hf_in_col = in_hf;
    int32_t* data_out_col = out;

    // Stage 1, line idx 0
    for (col = 0; col <= width - 8; col += 8) {
        const __m256i lf_in_avx2 = _mm256_loadu_si256((__m256i const*)(lf_in_col));
        const __m256i hf_in_offset_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col));
        const __m256i tmp = _mm256_add_epi32(hf_in_offset_avx2, one_avx2);
        const __m256i tmp2 = _mm256_srai_epi32(tmp, 1);
        const __m256i sub = _mm256_sub_epi32(lf_in_avx2, tmp2);
        _mm256_storeu_si256((__m256i*)(data_out_col), sub);

        lf_in_col += 8;
        hf_in_col += 8;
        data_out_col += 8;
    }
    for (; col < width; col++) {
        out[col] = in_lf[col] - ((in_hf[col] + 1) >> 1);
    }

    // Stage 1, line idx even(2, 4, 6 ... height - 1)
    for (uint32_t i = 1; i <= loop_high_even; i++) {
        lf_in_col = in_lf + stride_lf * i;
        hf_in_col = in_hf + stride_hf * (i - 1);
        data_out_col = out + stride_out * i * 2;

        for (col = 0; col <= width - 8; col += 8) {
            const __m256i lf_in_avx2 = _mm256_loadu_si256((__m256i const*)(lf_in_col));
            const __m256i hf_in_offset_A_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col));
            const __m256i hf_in_offset_B_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col + stride_hf));
            const __m256i sum = _mm256_add_epi32(_mm256_add_epi32(hf_in_offset_A_avx2, hf_in_offset_B_avx2), two_avx2);
            const __m256i tmp2 = _mm256_srai_epi32(sum, 2);
            const __m256i sub = _mm256_sub_epi32(lf_in_avx2, tmp2);
            _mm256_storeu_si256((__m256i*)(data_out_col), sub);

            lf_in_col += 8;
            hf_in_col += 8;
            data_out_col += 8;
        }

        int32_t leftover = 0;
        for (; col < width; col++) {
            data_out_col[leftover] = lf_in_col[leftover] - ((hf_in_col[leftover] + hf_in_col[leftover + stride_hf] + 2) >> 2);
            leftover++;
        }
    }

    // Stage 1, line idx last
    if (height & 1) {
        lf_in_col = in_lf + ((height - 1) / 2) * stride_lf;
        hf_in_col = in_hf + ((height - 2) / 2) * stride_hf;
        data_out_col = out + (height - 1) * stride_out;
        for (col = 0; col <= width - 8; col += 8) {
            const __m256i lf_in_avx2 = _mm256_loadu_si256((__m256i const*)(lf_in_col));
            const __m256i hf_in_offset_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col));
            const __m256i tmp = _mm256_add_epi32(hf_in_offset_avx2, one_avx2);
            const __m256i tmp2 = _mm256_srai_epi32(tmp, 1);
            const __m256i sub = _mm256_sub_epi32(lf_in_avx2, tmp2);
            _mm256_storeu_si256((__m256i*)(data_out_col), sub);

            lf_in_col += 8;
            hf_in_col += 8;
            data_out_col += 8;
        }

        int32_t leftover = 0;
        for (; col < width; col++) {
            data_out_col[leftover] = lf_in_col[leftover] - ((hf_in_col[leftover] + 1) >> 1);
            leftover++;
        }
    }

    //Stage 2, line idx odd(1, 3, 5 ... height - 1
    for (uint32_t i = 0; i < loop_high_odd; i++) {
        hf_in_col = in_hf + stride_hf * i;
        data_out_col = out + stride_out * 2 * i;

        for (col = 0; col <= width - 8; col += 8) {
            const __m256i hf_in_offset_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col));
            const __m256i data_out_A_avx2 = _mm256_loadu_si256((__m256i const*)(data_out_col));
            const __m256i data_out_B_avx2 = _mm256_loadu_si256((__m256i const*)(data_out_col + 2 * stride_out));
            const __m256i sum = _mm256_add_epi32(data_out_A_avx2, data_out_B_avx2);
            const __m256i tmp2 = _mm256_srai_epi32(sum, 1);
            const __m256i add = _mm256_add_epi32(hf_in_offset_avx2, tmp2);
            _mm256_storeu_si256((__m256i*)(data_out_col + stride_out), add);

            hf_in_col += 8;
            data_out_col += 8;
        }

        int32_t leftover = 0;
        for (; col < width; col++) {
            data_out_col[stride_out + leftover] = hf_in_col[leftover] +
                ((data_out_col[leftover] + data_out_col[leftover + 2 * stride_out]) >> 1);
            leftover++;
        }
    }

    // Stage 2, line idx last
    if (!(height & 1)) {
        hf_in_col = in_hf + ((height - 1) / 2) * stride_hf;
        data_out_col = out + (height - 2) * stride_out;

        for (col = 0; col <= width - 8; col += 8) {
            const __m256i hf_in_offset_avx2 = _mm256_loadu_si256((__m256i const*)(hf_in_col));
            const __m256i data_out_A_avx2 = _mm256_loadu_si256((__m256i const*)(data_out_col));
            const __m256i add = _mm256_add_epi32(hf_in_offset_avx2, data_out_A_avx2);
            _mm256_storeu_si256((__m256i*)(data_out_col + stride_out), add);

            hf_in_col += 8;
            data_out_col += 8;
        }

        int32_t leftover = 0;
        for (; col < width; col++) {
            data_out_col[leftover + stride_out] = hf_in_col[leftover] + data_out_col[leftover];
            leftover++;
        }
    }
}

void idwt_deprecated_horizontal_avx2(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                     uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out) {
    if ((width / 2) < 2 + 8) {
        idwt_deprecated_horizontal_c(in_lf, in_hf, out, width, height, stride_lf, stride_hf, stride_out);
        return;
    }

    const __m256i two = _mm256_set1_epi32(2);

    //TODO: Move this allocation outside of this function
    int32_t* tmp_out = (int32_t*)malloc(sizeof(int32_t) * width);
    if (!tmp_out) {
        assert(0);
        return;
    }

    for (uint32_t line = 0; line < height; line++) {
        uint32_t i;
        tmp_out[0] = in_lf[0] - ((in_hf[0] + 1) >> 1);

        for (i = 0; i <= (width / 2) - 2 - 8; i += 8) {
            const __m256i lf_avx2 = _mm256_loadu_si256((__m256i*)(in_lf + i + 1));
            const __m256i hf1_avx2 = _mm256_loadu_si256((__m256i*)(in_hf + i));
            const __m256i hf2_avx2 = _mm256_loadu_si256((__m256i*)(in_hf + i + 1));

            __m256i tmp = _mm256_add_epi32(hf1_avx2, hf2_avx2);
            tmp = _mm256_add_epi32(tmp, two);
            tmp = _mm256_srai_epi32(tmp, 2);
            tmp = _mm256_sub_epi32(lf_avx2, tmp);

            _mm256_storeu_si256((__m256i*)(tmp_out + i + 1), tmp);
        }

        for (; i <= (width / 2) - 2; i++) {
            tmp_out[i + 1] = in_lf[i + 1] - ((in_hf[i] + in_hf[i + 1] + 2) >> 2);
        }

        if (width & 1) {
            tmp_out[(width / 2)] = out[width - 1] = in_lf[(width - 1) / 2] - ((in_hf[(width - 2) / 2] + 1) >> 1);
        }

        for (i = 0; i < width / 2 - 1 - 8; i += 8) {
            const __m256i tmp_out1_avx2 = _mm256_loadu_si256((__m256i*)(tmp_out + i));
            const __m256i tmp_out2_avx2 = _mm256_loadu_si256((__m256i*)(tmp_out + i + 1));
            const __m256i hf_avx2 = _mm256_loadu_si256((__m256i*)(in_hf + i));

            __m256i tmp = _mm256_add_epi32(tmp_out1_avx2, tmp_out2_avx2);
            tmp = _mm256_srai_epi32(tmp, 1);
            tmp = _mm256_add_epi32(hf_avx2, tmp);

            const __m256i unpack1_avx2 = _mm256_unpacklo_epi32(tmp_out1_avx2, tmp);
            const __m256i unpack2_avx2 = _mm256_unpackhi_epi32(tmp_out1_avx2, tmp);

            _mm_storeu_si128((__m128i*)(out + i * 2 + 0), _mm256_castsi256_si128(unpack1_avx2));
            _mm_storeu_si128((__m128i*)(out + i * 2 + 4), _mm256_castsi256_si128(unpack2_avx2));
            _mm_storeu_si128((__m128i*)(out + i * 2 + 8), _mm256_extracti128_si256(unpack1_avx2, 0x1));
            _mm_storeu_si128((__m128i*)(out + i * 2 + 12), _mm256_extracti128_si256(unpack2_avx2, 0x1));
        }

        for (; i < width / 2; i++) {
            out[i * 2] = tmp_out[i];
            out[i * 2 + 1] = in_hf[i] + ((tmp_out[i] + tmp_out[i + 1]) >> 1);
        }

        if (!(width & 1)) {
            out[width - 1] = in_hf[(width - 1) / 2] + tmp_out[width / 2 - 1];
        }

        in_lf += stride_lf;
        in_hf += stride_hf;
        out += stride_out;
    }

    //TODO: Move this free outside of this function
    if (tmp_out) {
        free(tmp_out);
    }
}

void reference_idwt_c(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t len, uint32_t stride_lf,
                      uint32_t stride_hf, uint32_t stride_out) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    out[0] = in_lf[0] - ((in_hf[0] + 1) >> 1);
    for (uint32_t i = 2; i <= len - 2; i += 2) {
        out[i * stride_out] = in_lf[(i / 2) * stride_lf] -
            ((in_hf[((i - 1) / 2) * stride_hf] + in_hf[((i + 1) / 2) * stride_hf] + 2) >> 2);
    }
    if (len & 1) {
        out[(len - 1) * stride_out] = in_lf[((len - 1) / 2) * stride_lf] - ((in_hf[((len - 2) / 2) * stride_hf] + 1) >> 1);
    }

    for (uint32_t i = 1; i < len - 1; i += 2) {
        out[i * stride_out] = in_hf[(i / 2) * stride_hf] + ((out[(i - 1) * stride_out] + out[(i + 1) * stride_out]) >> 1);
    }
    if (!(len & 1)) {
        out[(len - 1) * stride_out] = in_hf[((len - 1) / 2) * stride_hf] + out[(len - 2) * stride_out];
    }
}

void idwt_deprecated_vertical_c(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out) {
    for (uint32_t col = 0; col < width; col++) {
        reference_idwt_c(in_lf + col, in_hf + col, out + col, height, stride_lf, stride_hf, stride_out);
    }
}

void idwt_deprecated_horizontal_c(const int32_t* in_lf, const int32_t* in_hf, int32_t* out, uint32_t width, uint32_t height,
                                  uint32_t stride_lf, uint32_t stride_hf, uint32_t stride_out) {
    for (uint32_t row = 0; row < height; row++) {
        reference_idwt_c(in_lf + (stride_lf * row), in_hf + (stride_hf * row), out + (stride_out * row), width, 1, 1, 1);
    }
}

void linear_input_scaling_8bit_depricated(uint8_t Bw, uint8_t* src, int32_t* dst, uint32_t w, uint32_t h, uint32_t src_stride,
                                          uint32_t dst_stride, uint8_t input_bit_depth) {
    assert(input_bit_depth <= 8);
    int32_t offset = 1 << (Bw - 1);
    int left = Bw - input_bit_depth;

    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            dst[j] = ((uint32_t)src[j] << left) - offset;
        }
        src += src_stride;
        dst += dst_stride;
    }
}

void linear_input_scaling_16bit_depricated(uint8_t Bw, uint16_t* src, int32_t* dst, uint32_t w, uint32_t h, uint32_t src_stride,
                                           uint32_t dst_stride, uint8_t input_bit_depth) {
    assert(input_bit_depth > 8 && input_bit_depth <= 16);
    int32_t offset = 1 << (Bw - 1);
    int left = Bw - input_bit_depth;

    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            dst[j] = (src[j] << left) - offset;
        }
        src += src_stride;
        dst += dst_stride;
    }
}
