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

/* The horizontal half. One low-pass and one high-pass row go in, a single
 * interleaved row comes out, which is what makes this worth vectorizing on
 * Advanced SIMD in particular: the even and the odd samples are computed as two
 * separate vectors and handed to a two-register interleaving store, so the
 * result lands in place without a shuffle. x86 has to build the interleaving by
 * hand.
 *
 * The lifting is the same as in the C reference. The even sample of a pair is
 * produced from the two high-pass neighbours around it, the odd one from the two
 * even samples around it - so a block of four pairs needs five even samples, and
 * the fifth comes from the next block through a lane-shift rather than from a
 * second pass over the data.
 *
 * The first pair and the last one follow rules of their own (there is no
 * high-pass sample before the first, and none after the last when the length is
 * even), so both ends are done scalar. */

/* LSHIFT32 on a vector: the shift is on the unsigned reinterpretation for the
 * same reason it is in the scalar macro - shifting a negative value left is
 * undefined, and the wavelet coefficients are signed. */
static INLINE int32x4_t idwt_lshift32(int32x4_t v, int32x4_t shift) {
    return vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(v), shift));
}

static INLINE int32x4_t idwt_load_hf(const int16_t *in_hf, int32x4_t shift) {
    return idwt_lshift32(vmovl_s16(vld1_s16(in_hf)), shift);
}

/* E[k] = L[k] - ((H[k - 1] + H[k] + 2) >> 2), four at a time.
 *
 * The two windows of the high-pass row a block needs overlap the windows the
 * next block needs, so the loop carries the current one and reaches the one
 * before the next block by a lane shift rather than by loading, widening and
 * shifting it again. */
static INLINE int32x4_t idwt_even_block(int32x4_t lf, int32x4_t hf_prev, int32x4_t hf_cur) {
    return vsubq_s32(lf, vshrq_n_s32(vaddq_s32(vaddq_s32(hf_prev, hf_cur), vdupq_n_s32(2)), 2));
}

/* The two rows go out interleaved. A pair of zips and one plain store of two
 * registers does that; the interleaving store does it in one instruction and
 * costs more than the three on this core. */
static INLINE void idwt_store_pair(int32_t *out, int32x4_t odd, int32x4_t even) {
    int32x4x2_t zipped;
    zipped.val[0] = vzip1q_s32(odd, even);
    zipped.val[1] = vzip2q_s32(odd, even);
    vst1q_s32_x2(out, zipped);
}

void idwt_horizontal_line_lf16_hf16_neon(const int16_t *in_lf, const int16_t *in_hf, int32_t *out, uint32_t len, uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");
    const uint32_t pairs = len / 2;
    const int32x4_t shift_vec = vdupq_n_s32(shift);
    uint32_t k = 1;

    out[0] = LSHIFT32(in_lf[0], shift) - (((LSHIFT32(in_hf[0], shift)) + 1) >> 1);

    /* The block writes the odd sample of pair k together with the even sample of
     * pair k + 1, so the even sample of the first pair and the odd sample of the
     * one before it are produced before the loop starts. */
    if (pairs >= 9) {
        out[2] = LSHIFT32(in_lf[1], shift) - (((LSHIFT32(in_hf[0], shift)) + LSHIFT32(in_hf[1], shift) + 2) >> 2);
        out[1] = LSHIFT32(in_hf[0], shift) + ((out[0] + out[2]) >> 1);

        int32x4_t hf_cur = idwt_load_hf(in_hf + k, shift_vec);
        int32x4_t even = idwt_even_block(
            idwt_lshift32(vmovl_s16(vld1_s16(in_lf + k)), shift_vec), idwt_load_hf(in_hf + k - 1, shift_vec), hf_cur);
        while (k + 8 <= pairs) {
            const uint32_t next = k + 4;
            const int32x4_t hf_next = idwt_load_hf(in_hf + next, shift_vec);
            const int32x4_t even_next = idwt_even_block(idwt_lshift32(vmovl_s16(vld1_s16(in_lf + next)), shift_vec),
                                                        vextq_s32(hf_cur, hf_next, 3),
                                                        hf_next);
            const int32x4_t even_shifted = vextq_s32(even, even_next, 1);
            const int32x4_t odd = vaddq_s32(hf_cur, vshrq_n_s32(vaddq_s32(even, even_shifted), 1));
            idwt_store_pair(out + 2 * k + 1, odd, even_shifted);
            even = even_next;
            hf_cur = hf_next;
            k = next;
        }
    }

    for (; k < pairs; k++) {
        out[2 * k] = LSHIFT32(in_lf[k], shift) -
            (((LSHIFT32(in_hf[k - 1], shift)) + LSHIFT32(in_hf[k], shift) + 2) >> 2);
        out[2 * k - 1] = LSHIFT32(in_hf[k - 1], shift) + ((out[2 * k - 2] + out[2 * k]) >> 1);
    }
    if (len & 1) {
        out[len - 1] = LSHIFT32(in_lf[pairs], shift) - (((LSHIFT32(in_hf[pairs - 1], shift)) + 1) >> 1);
        out[len - 2] = LSHIFT32(in_hf[pairs - 1], shift) + ((out[len - 3] + out[len - 1]) >> 1);
    }
    else {
        out[len - 1] = LSHIFT32(in_hf[pairs - 1], shift) + out[len - 2];
    }
}

void idwt_horizontal_line_lf32_hf16_neon(const int32_t *in_lf, const int16_t *in_hf, int32_t *out, uint32_t len, uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");
    const uint32_t pairs = len / 2;
    const int32x4_t shift_vec = vdupq_n_s32(shift);
    uint32_t k = 1;

    out[0] = in_lf[0] - (((LSHIFT32(in_hf[0], shift)) + 1) >> 1);

    if (pairs >= 9) {
        out[2] = in_lf[1] - (((LSHIFT32(in_hf[0], shift)) + LSHIFT32(in_hf[1], shift) + 2) >> 2);
        out[1] = LSHIFT32(in_hf[0], shift) + ((out[0] + out[2]) >> 1);

        int32x4_t hf_cur = idwt_load_hf(in_hf + k, shift_vec);
        int32x4_t even = idwt_even_block(vld1q_s32(in_lf + k), idwt_load_hf(in_hf + k - 1, shift_vec), hf_cur);
        while (k + 8 <= pairs) {
            const uint32_t next = k + 4;
            const int32x4_t hf_next = idwt_load_hf(in_hf + next, shift_vec);
            const int32x4_t even_next =
                idwt_even_block(vld1q_s32(in_lf + next), vextq_s32(hf_cur, hf_next, 3), hf_next);
            const int32x4_t even_shifted = vextq_s32(even, even_next, 1);
            const int32x4_t odd = vaddq_s32(hf_cur, vshrq_n_s32(vaddq_s32(even, even_shifted), 1));
            idwt_store_pair(out + 2 * k + 1, odd, even_shifted);
            even = even_next;
            hf_cur = hf_next;
            k = next;
        }
    }

    for (; k < pairs; k++) {
        out[2 * k] = in_lf[k] - (((LSHIFT32(in_hf[k - 1], shift)) + LSHIFT32(in_hf[k], shift) + 2) >> 2);
        out[2 * k - 1] = LSHIFT32(in_hf[k - 1], shift) + ((out[2 * k - 2] + out[2 * k]) >> 1);
    }
    if (len & 1) {
        out[len - 1] = in_lf[pairs] - (((LSHIFT32(in_hf[pairs - 1], shift)) + 1) >> 1);
        out[len - 2] = LSHIFT32(in_hf[pairs - 1], shift) + ((out[len - 3] + out[len - 1]) >> 1);
    }
    else {
        out[len - 1] = LSHIFT32(in_hf[pairs - 1], shift) + out[len - 2];
    }
}
