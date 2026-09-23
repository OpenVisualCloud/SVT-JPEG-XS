/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Mct_avx512.h"

/* Table F.2 - Inverse reversible multiple component transformation (Cpih=1). Every pixel is
 * independent (no neighbour access, unlike the CFA/star-tetrix path), and comps[0..2] are flat
 * w*h buffers with no row padding, so this can vectorize as one linear pass instead of per-row. */
void inverse_rct_avx512(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h) {
    int32_t* c0 = comps[0];
    int32_t* c1 = comps[1];
    int32_t* c2 = comps[2];
    const int64_t total = (int64_t)w * h;
    int64_t i = 0;

    for (; i <= total - 16; i += 16) {
        __m512i i0 = _mm512_loadu_si512((__m512i const*)(c0 + i));
        __m512i i1 = _mm512_loadu_si512((__m512i const*)(c1 + i));
        __m512i i2 = _mm512_loadu_si512((__m512i const*)(c2 + i));

        __m512i o1 = _mm512_sub_epi32(i0, _mm512_srai_epi32(_mm512_add_epi32(i1, i2), 2));
        __m512i o0 = _mm512_add_epi32(o1, i2);
        __m512i o2 = _mm512_add_epi32(o1, i1);

        _mm512_storeu_si512((__m512i*)(c0 + i), o0);
        _mm512_storeu_si512((__m512i*)(c1 + i), o1);
        _mm512_storeu_si512((__m512i*)(c2 + i), o2);
    }
    for (; i < total; i++) {
        int32_t i0 = c0[i];
        int32_t i1 = c1[i];
        int32_t i2 = c2[i];

        int32_t o1 = i0 - ((i1 + i2) >> 2);
        int32_t o0 = o1 + i2;
        int32_t o2 = o1 + i1;

        c0[i] = o0;
        c1[i] = o1;
        c2[i] = o2;
    }
}
