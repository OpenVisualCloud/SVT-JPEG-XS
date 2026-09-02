/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "RateControl_neon.h"
#include "Definitions.h"
#include "SvtUtility.h"
#include "EncDec.h"
#include <arm_neon.h>
#include <string.h>

/* The histogram of GCLI values over one band line - sixteen bins, one per
 * possible truncation level.
 *
 * The scalar variant does a read-modify-write to memory per element, and GCLI
 * is spatially correlated, so neighbouring values keep falling into the same
 * bin and the processor stalls forwarding the store to the load that follows
 * it. Comparing against all sixteen bins instead has no such dependency: a
 * comparison yields all-ones on a match, and subtracting that from a byte
 * counter is an increment, so a bin is counted with two instructions per vector
 * and nothing leaves the vector registers until the line is done.
 *
 * Counters are bytes, and a pass adds at most two to any one of them, so the
 * line is walked in blocks short enough that a byte cannot overflow. Everything
 * a real band line can be - the widest is a quarter of the picture width - fits
 * in a single block. */

#define GC_HIST_BLOCK_MAX 4064u /* at most 254 counts into a byte */

#define GC_HIST_BIN_2(K) \
    acc##K = vsubq_u8(vsubq_u8(acc##K, vceqq_u8(v0, vdupq_n_u8(K))), vceqq_u8(v1, vdupq_n_u8(K)))
#define GC_HIST_BIN_1(K) acc##K = vsubq_u8(acc##K, vceqq_u8(v0, vdupq_n_u8(K)))

#define GC_HIST_ALL(BIN)  \
    do {               \
        BIN(0);  \
        BIN(1);  \
        BIN(2);  \
        BIN(3);  \
        BIN(4);  \
        BIN(5);  \
        BIN(6);  \
        BIN(7);  \
        BIN(8);  \
        BIN(9);  \
        BIN(10); \
        BIN(11); \
        BIN(12); \
        BIN(13); \
        BIN(14); \
        BIN(15); \
    } while (0)

#define GC_HIST_DECL()                                                                                              \
    uint8x16_t acc0 = vdupq_n_u8(0), acc1 = vdupq_n_u8(0), acc2 = vdupq_n_u8(0), acc3 = vdupq_n_u8(0),               \
               acc4 = vdupq_n_u8(0), acc5 = vdupq_n_u8(0), acc6 = vdupq_n_u8(0), acc7 = vdupq_n_u8(0),               \
               acc8 = vdupq_n_u8(0), acc9 = vdupq_n_u8(0), acc10 = vdupq_n_u8(0), acc11 = vdupq_n_u8(0),             \
               acc12 = vdupq_n_u8(0), acc13 = vdupq_n_u8(0), acc14 = vdupq_n_u8(0), acc15 = vdupq_n_u8(0)

#define GC_HIST_DRAIN()                        \
    do {                                       \
        total[0] += vaddlvq_u8(acc0);          \
        total[1] += vaddlvq_u8(acc1);          \
        total[2] += vaddlvq_u8(acc2);          \
        total[3] += vaddlvq_u8(acc3);          \
        total[4] += vaddlvq_u8(acc4);          \
        total[5] += vaddlvq_u8(acc5);          \
        total[6] += vaddlvq_u8(acc6);          \
        total[7] += vaddlvq_u8(acc7);          \
        total[8] += vaddlvq_u8(acc8);          \
        total[9] += vaddlvq_u8(acc9);          \
        total[10] += vaddlvq_u8(acc10);        \
        total[11] += vaddlvq_u8(acc11);        \
        total[12] += vaddlvq_u8(acc12);        \
        total[13] += vaddlvq_u8(acc13);        \
        total[14] += vaddlvq_u8(acc14);        \
        total[15] += vaddlvq_u8(acc15);        \
    } while (0)

/* Below this the vector form loses. Sixteen counters have to be set up and,
 * more to the point, drained one by one at the end, and that cost does not
 * depend on the length of the line: measured against the scalar loop, the two
 * come out even at sixteen elements and the vector form is behind at eight.
 * Bands do get that narrow - the smallest of them are a few elements wide - so
 * the short lines are handed to the scalar loop rather than paid for. */
#define GC_HIST_VECTOR_MIN 32u

void gc_histogram_16_neon(const uint8_t *data, uint32_t width, uint16_t *hist) {
    if (width < GC_HIST_VECTOR_MIN) {
        memset(hist, 0, (TRUNCATION_MAX + 1) * sizeof(hist[0]));
        for (uint32_t i = 0; i < width; i++) {
            hist[data[i]]++;
        }
        return;
    }

    uint32_t total[TRUNCATION_MAX + 1] = {0};
    for (uint32_t pos = 0; pos < width;) {
        const uint32_t block = MIN(width - pos, GC_HIST_BLOCK_MAX);
        const uint8_t *const in = data + pos;
        uint32_t i = 0;
        GC_HIST_DECL();

        for (; i + 32 <= block; i += 32) {
            const uint8x16_t v0 = vld1q_u8(in + i);
            const uint8x16_t v1 = vld1q_u8(in + i + 16);
            GC_HIST_ALL(GC_HIST_BIN_2);
        }
        if (i + 16 <= block) {
            const uint8x16_t v0 = vld1q_u8(in + i);
            GC_HIST_ALL(GC_HIST_BIN_1);
            i += 16;
        }
        GC_HIST_DRAIN();

        /* Fewer than sixteen left, and on the narrow bands that is the whole
         * line. Counting them one by one beats padding a vector: the padding
         * would have to be written and the line copied into it, and that is
         * more work than the elements themselves. */
        for (; i < block; i++) {
            total[in[i]]++;
        }
        pos += block;
    }

    for (uint32_t k = 0; k <= TRUNCATION_MAX; ++k) {
        hist[k] = (uint16_t)total[k];
    }
}
