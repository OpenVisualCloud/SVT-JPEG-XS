/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Idwt_neon.h"
#include "Definitions.h"
#include <arm_neon.h>

/* The inverse 5/3 lifting steps, one vector of four coefficients at a time.
 * Every line below is the C expression it is named after, and the order inside
 * a step is kept: the even sample is produced before the odd one that reads it,
 * so an output row aliasing another gives the same result it gives in C. */

/* out_2[i] = in_lf[i] - ((in_hf1[i] + 1) >> 1) */
static INLINE int32x4_t idwt_even_one_hf(int32x4_t lf, int32x4_t hf) {
    return vsubq_s32(lf, vshrq_n_s32(vaddq_s32(hf, vdupq_n_s32(1)), 1));
}

/* out_2[i] = in_lf[i] - ((in_hf0[i] + in_hf1[i] + 2) >> 2) */
static INLINE int32x4_t idwt_even_two_hf(int32x4_t lf, int32x4_t hf0, int32x4_t hf1) {
    return vsubq_s32(lf, vshrq_n_s32(vaddq_s32(vaddq_s32(hf0, hf1), vdupq_n_s32(2)), 2));
}

/* out_1[i] = in_hf0[i] + ((out_0[i] + out_2[i]) >> 1) */
static INLINE int32x4_t idwt_odd(int32x4_t hf0, int32x4_t out_0, int32x4_t out_2) {
    return vaddq_s32(hf0, vshrq_n_s32(vaddq_s32(out_0, out_2), 1));
}

void idwt_vertical_line_neon(const int32_t *in_lf, const int32_t *in_hf0, const int32_t *in_hf1, int32_t *out[4], uint32_t len,
                             int32_t first_precinct, int32_t last_precinct, int32_t height) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");
    const uint32_t simd_len = len & ~3u;
    uint32_t i;

    //Corner case: height is equal to 2
    if (height == 2) {
        int32_t *out_2 = out[2];
        int32_t *out_3 = out[3];
        for (i = 0; i < simd_len; i += 4) {
            const int32x4_t hf1 = vld1q_s32(in_hf1 + i);
            const int32x4_t out2 = idwt_even_one_hf(vld1q_s32(in_lf + i), hf1);
            vst1q_s32(out_2 + i, out2);
            vst1q_s32(out_3 + i, vaddq_s32(hf1, out2));
        }
        for (; i < len; i++) {
            out_2[i] = in_lf[i] - ((in_hf1[i] + 1) >> 1);
            out_3[i] = in_hf1[i] + out_2[i];
        }
        return;
    }
    //Corner case: first precinct in component
    if (first_precinct) {
        int32_t *out_2 = out[2];
        for (i = 0; i < simd_len; i += 4) {
            vst1q_s32(out_2 + i, idwt_even_one_hf(vld1q_s32(in_lf + i), vld1q_s32(in_hf1 + i)));
        }
        for (; i < len; i++) {
            out_2[i] = in_lf[i] - ((in_hf1[i] + 1) >> 1);
        }
        return;
    }
    //Corner case: last precinct in component, height odd
    if (last_precinct && (height & 1)) {
        int32_t *out_0 = out[0];
        int32_t *out_1 = out[1];
        int32_t *out_2 = out[2];
        for (i = 0; i < simd_len; i += 4) {
            const int32x4_t hf0 = vld1q_s32(in_hf0 + i);
            const int32x4_t out2 = idwt_even_one_hf(vld1q_s32(in_lf + i), hf0);
            vst1q_s32(out_2 + i, out2);
            vst1q_s32(out_1 + i, idwt_odd(hf0, vld1q_s32(out_0 + i), out2));
        }
        for (; i < len; i++) {
            out_2[i] = in_lf[i] - ((in_hf0[i] + 1) >> 1);
            out_1[i] = in_hf0[i] + ((out_0[i] + out_2[i]) >> 1);
        }
        return;
    }
    //Corner case: last precinct in component, height even
    if (last_precinct && (!(height & 1))) {
        int32_t *out_0 = out[0];
        int32_t *out_1 = out[1];
        int32_t *out_2 = out[2];
        int32_t *out_3 = out[3];
        for (i = 0; i < simd_len; i += 4) {
            const int32x4_t hf0 = vld1q_s32(in_hf0 + i);
            const int32x4_t hf1 = vld1q_s32(in_hf1 + i);
            const int32x4_t out2 = idwt_even_two_hf(vld1q_s32(in_lf + i), hf0, hf1);
            vst1q_s32(out_2 + i, out2);
            vst1q_s32(out_1 + i, idwt_odd(hf0, vld1q_s32(out_0 + i), out2));
            vst1q_s32(out_3 + i, vaddq_s32(hf1, out2));
        }
        for (; i < len; i++) {
            out_2[i] = in_lf[i] - ((in_hf0[i] + in_hf1[i] + 2) >> 2);
            out_1[i] = in_hf0[i] + ((out_0[i] + out_2[i]) >> 1);
            out_3[i] = in_hf1[i] + out_2[i];
        }
        return;
    }

    int32_t *out_0 = out[0];
    int32_t *out_1 = out[1];
    int32_t *out_2 = out[2];
    for (i = 0; i < simd_len; i += 4) {
        const int32x4_t hf0 = vld1q_s32(in_hf0 + i);
        const int32x4_t out2 = idwt_even_two_hf(vld1q_s32(in_lf + i), hf0, vld1q_s32(in_hf1 + i));
        vst1q_s32(out_2 + i, out2);
        vst1q_s32(out_1 + i, idwt_odd(hf0, vld1q_s32(out_0 + i), out2));
    }
    for (; i < len; i++) {
        out_2[i] = in_lf[i] - ((in_hf0[i] + in_hf1[i] + 2) >> 2);
        out_1[i] = in_hf0[i] + ((out_0[i] + out_2[i]) >> 1);
    }
}

void idwt_vertical_line_recalc_neon(const int32_t *in_lf, const int32_t *in_hf0, const int32_t *in_hf1, int32_t *out[4],
                                    uint32_t len, uint32_t precinct_line_idx) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");
    const uint32_t simd_len = len & ~3u;
    int32_t *out_0 = out[0];
    uint32_t i;

    if (precinct_line_idx > 1) {
        for (i = 0; i < simd_len; i += 4) {
            vst1q_s32(out_0 + i,
                      idwt_even_two_hf(vld1q_s32(in_lf + i), vld1q_s32(in_hf0 + i), vld1q_s32(in_hf1 + i)));
        }
        for (; i < len; i++) {
            out_0[i] = in_lf[i] - ((in_hf0[i] + in_hf1[i] + 2) >> 2);
        }
    }
    else {
        for (i = 0; i < simd_len; i += 4) {
            vst1q_s32(out_0 + i, idwt_even_one_hf(vld1q_s32(in_lf + i), vld1q_s32(in_hf1 + i)));
        }
        for (; i < len; i++) {
            out_0[i] = in_lf[i] - ((in_hf1[i] + 1) >> 1);
        }
    }
}
