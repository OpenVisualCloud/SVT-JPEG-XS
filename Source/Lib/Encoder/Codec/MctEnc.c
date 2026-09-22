/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "MctEnc.h"

/* Table F.2 - Forward reversible colour transformation.
 * Exact inverse of the decoder's inverse_rct(): comp0/1/2 are RGB-like on entry, YCbCr-like
 * (i0,i1,i2) on exit. The (i1+i2)>>2 term is added here and subtracted identically on
 * decode using the same i1/i2, so it cancels exactly regardless of rounding - this is a
 * bit-exact inverse, not an approximation. */
void forward_rct_line(int32_t* comp0, int32_t* comp1, int32_t* comp2, uint32_t width) {
    for (uint32_t x = 0; x < width; x++) {
        int32_t o0 = comp0[x];
        int32_t o1 = comp1[x];
        int32_t o2 = comp2[x];

        int32_t i1 = o2 - o1;
        int32_t i2 = o0 - o1;
        int32_t i0 = o1 + ((i1 + i2) >> 2);

        comp0[x] = i0;
        comp1[x] = i1;
        comp2[x] = i2;
    }
}
