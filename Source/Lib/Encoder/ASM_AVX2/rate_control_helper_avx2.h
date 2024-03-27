/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Definitions.h"
#include <immintrin.h>

static INLINE __m256i vlc_encode_get_bits_avx2(__m256i x, __m256i r, __m256i t) {
    //    assert(x < 32);
    //    int32_t max = r - t;
    //    if (max < 0) {
    //        max = 0;
    //    }
    __m256i max_avx2 = _mm256_subs_epu16(r, t);
    //    int n = 0;
    //    if (x > max) {
    const __m256i cmp = _mm256_cmpgt_epi16(x, max_avx2);
    //        n = x + max;
    __m256i n1 = _mm256_add_epi16(x, max_avx2);
    //    }
    //    else {
    //        x = x * 2;
    x = _mm256_slli_epi16(x, 1);
    //        if (x < 0) {
    const __m256i cmp2 = _mm256_cmpgt_epi16(_mm256_setzero_si256(), x);
    //            n = -x - 1;
    __m256i n2 = _mm256_sub_epi16(_mm256_abs_epi16(x), _mm256_set1_epi16(1));
    //        }
    //        else {
    //            n = x;
    //        }
    __m256i n3 = _mm256_blendv_epi8(x, n2, cmp2);
    //    }
    __m256i n = _mm256_blendv_epi8(n3, n1, cmp);
    return n;
}

static INLINE __m128i vlc_encode_get_bits_sse(__m128i x, __m128i r, __m128i t) {
    //    assert(x < 32);
    //    int32_t max = r - t;
    //    if (max < 0) {
    //        max = 0;
    //    }
    __m128i max_avx2 = _mm_subs_epu16(r, t);
    //    int n = 0;
    //    if (x > max) {
    const __m128i cmp = _mm_cmpgt_epi16(x, max_avx2);
    //        n = x + max;
    __m128i n1 = _mm_add_epi16(x, max_avx2);
    //    }
    //    else {
    //        x = x * 2;
    x = _mm_slli_epi16(x, 1);
    //        if (x < 0) {
    const __m128i cmp2 = _mm_cmpgt_epi16(_mm_setzero_si128(), x);
    //            n = -x - 1;
    __m128i n2 = _mm_sub_epi16(_mm_abs_epi16(x), _mm_set1_epi16(1));
    //        }
    //        else {
    //            n = x;
    //        }
    __m128i n3 = _mm_blendv_epi8(x, n2, cmp2);
    //    }
    __m128i n = _mm_blendv_epi8(n3, n1, cmp);
    return n;
}
