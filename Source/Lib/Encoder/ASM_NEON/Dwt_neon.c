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
