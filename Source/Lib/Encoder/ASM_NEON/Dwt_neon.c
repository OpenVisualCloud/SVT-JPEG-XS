/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dwt_neon.h"
#include "Definitions.h"
#include <arm_neon.h>

/* The forward 5/3 transform of one line. The input is interleaved - the samples
 * a high-pass coefficient is made of sit next to the ones a low-pass coefficient
 * is made of - and Advanced SIMD loads it split in one instruction, which is
 * what makes this worth vectorizing here: the two-register load hands over the
 * even and the odd samples already separated, with no shuffle to undo the
 * interleaving first.
 *
 * A block of four coefficients needs the even sample just past its end, and the
 * low-pass step needs the high-pass coefficient just before its start; both come
 * from the neighbouring block through a lane shift rather than from a second
 * pass over the line. Both ends of the line keep the rules of the C reference
 * and stay scalar. */
void dwt_horizontal_line_neon(int32_t *out_lf, int32_t *out_hf, const int32_t *in, uint32_t len) {
    assert((len >= 2) && "[dwt_horizontal_line_c()] ERROR: Length is too small!");

    if (len == 2) {
        out_hf[0] = in[1] - in[0];
        out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);
        return;
    }

    out_hf[0] = in[1] - ((in[0] + in[2]) >> 1);
    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);

    const uint32_t count = ((len - 1) / 2);
    uint32_t id = 1;

    if (count >= 9) {
        /* lane 3 is what the first lane shift reaches for, and there it has to
         * be the high-pass coefficient of the pair before the block */
        int32x4_t prev_hf = vdupq_n_s32(out_hf[0]);
        int32x4x2_t cur = vld2q_s32(in + 2 * id);
        while (id + 8 <= count) {
            const int32x4x2_t nxt = vld2q_s32(in + 2 * (id + 4));
            const int32x4_t even_next = vextq_s32(cur.val[0], nxt.val[0], 1);

            const int32x4_t hf = vsubq_s32(cur.val[1], vshrq_n_s32(vaddq_s32(cur.val[0], even_next), 1));
            const int32x4_t hf_prev = vextq_s32(prev_hf, hf, 3);
            const int32x4_t lf =
                vaddq_s32(cur.val[0], vshrq_n_s32(vaddq_s32(vaddq_s32(hf_prev, hf), vdupq_n_s32(2)), 2));

            vst1q_s32(out_hf + id, hf);
            vst1q_s32(out_lf + id, lf);

            prev_hf = hf;
            cur = nxt;
            id += 4;
        }
    }

    for (; id < count; id++) {
        out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        out_lf[id] = in[id * 2] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
    }

    if (!(len & 1)) {
        out_hf[len / 2 - 1] = in[len - 1] - in[len - 2];
        out_lf[len / 2 - 1] = in[len - 2] + ((out_hf[len / 2 - 2] + out_hf[len / 2 - 1] + 2) >> 2);
    }
    else { //if (len & 1){
        out_lf[len / 2] = in[len - 1] + ((out_hf[len / 2 - 1] + 1) >> 1);
    }
}

/* The vertical half of the transform. Unlike the horizontal one there is no
 * interleaving to undo here: every coefficient is made of samples that sit at
 * the same offset in neighbouring lines, so the loops are elementwise and the
 * whole family is the same shape - load the operands of one iteration from each
 * line, do the lifting, store. Eight coefficients are done per iteration, which
 * is what keeps the pointer arithmetic off the critical path on the narrow
 * bands, where a line is only a few vectors long.
 *
 * The shifts are the reason these cannot be left to the compiler's own
 * vectorizer with confidence: the reference shifts a signed value right, which
 * rounds towards minus infinity, and vshrq_n_s32 is the instruction that does
 * exactly that. A rewrite into a division would round the other way for negative
 * coefficients, and a high-pass coefficient is negative about half the time. */

void transform_vertical_loop_hf_line_0_neon(uint32_t width, int32_t *out_hf, const int32_t *line_0, const int32_t *line_1) {
    uint32_t i = 0;
    for (; i + 8 <= width; i += 8) {
        const int32x4_t l0_a = vld1q_s32(line_0 + i);
        const int32x4_t l0_b = vld1q_s32(line_0 + i + 4);
        const int32x4_t l1_a = vld1q_s32(line_1 + i);
        const int32x4_t l1_b = vld1q_s32(line_1 + i + 4);
        vst1q_s32(out_hf + i, vsubq_s32(l1_a, l0_a));
        vst1q_s32(out_hf + i + 4, vsubq_s32(l1_b, l0_b));
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
    }
}

void transform_vertical_loop_lf_line_0_neon(uint32_t width, int32_t *out_lf, const int32_t *in_hf, const int32_t *line_0) {
    const int32x4_t one = vdupq_n_s32(1);
    uint32_t i = 0;
    for (; i + 8 <= width; i += 8) {
        const int32x4_t hf_a = vld1q_s32(in_hf + i);
        const int32x4_t hf_b = vld1q_s32(in_hf + i + 4);
        const int32x4_t l0_a = vld1q_s32(line_0 + i);
        const int32x4_t l0_b = vld1q_s32(line_0 + i + 4);
        vst1q_s32(out_lf + i, vaddq_s32(l0_a, vshrq_n_s32(vaddq_s32(hf_a, one), 1)));
        vst1q_s32(out_lf + i + 4, vaddq_s32(l0_b, vshrq_n_s32(vaddq_s32(hf_b, one), 1)));
    }
    for (; i < width; i++) {
        out_lf[i] = line_0[i] + ((in_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_0_neon(uint32_t width, int32_t *out_lf, int32_t *out_hf, const int32_t *line_0,
                                               const int32_t *line_1, const int32_t *line_2) {
    const int32x4_t one = vdupq_n_s32(1);
    uint32_t i = 0;
    for (; i + 4 <= width; i += 4) {
        const int32x4_t l0 = vld1q_s32(line_0 + i);
        const int32x4_t l1 = vld1q_s32(line_1 + i);
        const int32x4_t l2 = vld1q_s32(line_2 + i);
        const int32x4_t hf = vsubq_s32(l1, vshrq_n_s32(vaddq_s32(l0, l2), 1));
        const int32x4_t lf = vaddq_s32(l0, vshrq_n_s32(vaddq_s32(hf, one), 1));
        vst1q_s32(out_hf + i, hf);
        vst1q_s32(out_lf + i, lf);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((out_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_x_prev_neon(uint32_t width, int32_t *out_lf, int32_t *out_hf, const int32_t *line_p6,
                                                    const int32_t *line_p5, const int32_t *line_p4, const int32_t *line_p3,
                                                    const int32_t *line_p2) {
    const int32x4_t two = vdupq_n_s32(2);
    uint32_t i = 0;
    for (; i + 4 <= width; i += 4) {
        const int32x4_t p6 = vld1q_s32(line_p6 + i);
        const int32x4_t p5 = vld1q_s32(line_p5 + i);
        const int32x4_t p4 = vld1q_s32(line_p4 + i);
        const int32x4_t p3 = vld1q_s32(line_p3 + i);
        const int32x4_t p2 = vld1q_s32(line_p2 + i);
        /* the high-pass coefficient of the pair before this one, recomputed
         * rather than carried in, exactly as the reference does */
        const int32x4_t hf_prev = vsubq_s32(p5, vshrq_n_s32(vaddq_s32(p6, p4), 1));
        const int32x4_t hf = vsubq_s32(p3, vshrq_n_s32(vaddq_s32(p4, p2), 1));
        const int32x4_t lf = vaddq_s32(p4, vshrq_n_s32(vaddq_s32(vaddq_s32(hf_prev, hf), two), 2));
        vst1q_s32(out_hf + i, hf);
        vst1q_s32(out_lf + i, lf);
    }
    for (; i < width; i++) {
        const int32_t hf_prev = line_p5[i] - ((line_p6[i] + line_p4[i]) >> 1);
        out_hf[i] = line_p3[i] - ((line_p4[i] + line_p2[i]) >> 1);
        out_lf[i] = line_p4[i] + ((hf_prev + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_x_neon(uint32_t width, int32_t *out_lf, int32_t *out_hf, const int32_t *in_hf_prev,
                                                  const int32_t *line_0, const int32_t *line_1, const int32_t *line_2) {
    const int32x4_t two = vdupq_n_s32(2);
    uint32_t i = 0;
    for (; i + 4 <= width; i += 4) {
        const int32x4_t l0 = vld1q_s32(line_0 + i);
        const int32x4_t l1 = vld1q_s32(line_1 + i);
        const int32x4_t l2 = vld1q_s32(line_2 + i);
        const int32x4_t hf_prev = vld1q_s32(in_hf_prev + i);
        const int32x4_t hf = vsubq_s32(l1, vshrq_n_s32(vaddq_s32(l0, l2), 1));
        const int32x4_t lf = vaddq_s32(l0, vshrq_n_s32(vaddq_s32(vaddq_s32(hf_prev, hf), two), 2));
        vst1q_s32(out_hf + i, hf);
        vst1q_s32(out_lf + i, lf);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_last_even_neon(uint32_t width, int32_t *out_lf, int32_t *out_hf,
                                                          const int32_t *in_hf_prev, const int32_t *line_0,
                                                          const int32_t *line_1) {
    const int32x4_t two = vdupq_n_s32(2);
    uint32_t i = 0;
    for (; i + 4 <= width; i += 4) {
        const int32x4_t l0 = vld1q_s32(line_0 + i);
        const int32x4_t l1 = vld1q_s32(line_1 + i);
        const int32x4_t hf_prev = vld1q_s32(in_hf_prev + i);
        const int32x4_t hf = vsubq_s32(l1, l0);
        const int32x4_t lf = vaddq_s32(l0, vshrq_n_s32(vaddq_s32(vaddq_s32(hf_prev, hf), two), 2));
        vst1q_s32(out_hf + i, hf);
        vst1q_s32(out_lf + i, lf);
    }
    for (; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_V1_Hx_precinct_recalc_HF_prev_neon(uint32_t width, int32_t *out_tmp_line_HF_next, const int32_t *line_0,
                                                  const int32_t *line_1, const int32_t *line_2) {
    uint32_t i = 0;
    for (; i + 8 <= width; i += 8) {
        const int32x4_t l0_a = vld1q_s32(line_0 + i);
        const int32x4_t l0_b = vld1q_s32(line_0 + i + 4);
        const int32x4_t l1_a = vld1q_s32(line_1 + i);
        const int32x4_t l1_b = vld1q_s32(line_1 + i + 4);
        const int32x4_t l2_a = vld1q_s32(line_2 + i);
        const int32x4_t l2_b = vld1q_s32(line_2 + i + 4);
        vst1q_s32(out_tmp_line_HF_next + i, vsubq_s32(l1_a, vshrq_n_s32(vaddq_s32(l0_a, l2_a), 1)));
        vst1q_s32(out_tmp_line_HF_next + i + 4, vsubq_s32(l1_b, vshrq_n_s32(vaddq_s32(l0_b, l2_b), 1)));
    }
    for (; i < width; i++) {
        out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
    }
}
