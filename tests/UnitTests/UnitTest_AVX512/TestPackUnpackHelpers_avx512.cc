/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The helpers of the AVX-512 pack and unpack paths.
 *
 * They live here rather than next to the other unit tests because PDEP and PEXT
 * need BMI2, and this directory is the only one built with the same flags as the
 * production AVX-512 directories. */

#include "gtest/gtest.h"
#include "random.h"
#include "pack_group_helper_avx512.h"
#include "unpack_common.h"
#include "unpack_common_avx512.h"
#include "common_dsp_rtcd.h"
#include "EncDec.h" /* TRUNCATION_MAX */

/* The whole translation unit is compiled with AVX-512 enabled, so the compiler
 * may put those instructions anywhere in it, not only where an intrinsic asks.
 * BMI2 comes along for PDEP and PEXT - the same pair of conditions the runtime
 * dispatch checks before it hands work to this tier. */
/* The generator hands out sixteen bits at a time. */
static uint64_t rand64(svt_jxs_test_tool::SVTRandom* rnd) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        v = (v << 16) | rnd->Rand16();
    }
    return v;
}

static bool avx512_usable(void) {
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    return (get_cpu_flags() & required) == required;
}

/* Bit j of coefficient k is bit (3 - k) of nibble j: the plane of weight 2^j
 * contributes one nibble, and inside it the coefficients run from the first in
 * the top bit to the last in the bottom one. */
static uint64_t planes_to_lanes_ref(uint64_t acc, uint8_t gtli) {
    uint64_t res = 0;
    for (uint32_t k = 0; k < 4; k++) {
        uint64_t lane = 0;
        for (uint32_t j = 0; j < 16; j++) {
            lane |= ((acc >> (4 * j + (3 - k))) & 1) << j;
        }
        res |= lane << (16 * k);
    }
    return res << gtli;
}

/* Both parsers spread the nibbles of a group over four coefficients, one with
 * PEXT and one with byte shuffles and sign masks. They have to agree with each
 * other and with the definition. */
TEST(unpack_planes_to_lanes, avx512_matches_sse_and_reference) {
    if (!avx512_usable()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    /* count nibbles plus the truncation level have to stay inside a coefficient,
     * which is exactly the range the parser is called on. */
    for (uint32_t count = 1; count <= 15; count++) {
        for (uint8_t gtli = 0; gtli + count <= 16; gtli++) {
            for (uint32_t test_num = 0; test_num < 200; test_num++) {
                /* nibbles past count are zero, the way the group reader leaves them */
                uint64_t acc = rand64(rnd);
                if (4 * count < 64) {
                    acc &= ((uint64_t)1 << (4 * count)) - 1;
                }

                const uint64_t ref = planes_to_lanes_ref(acc, gtli);
                ASSERT_EQ(ref, unpack_planes_to_lanes_sse(acc, gtli));
                ASSERT_EQ(ref, unpack_planes_to_lanes(acc, gtli));
            }
        }
    }
    delete rnd;
}

/* Nibble j of the word holds bit j of the four coefficients, the first of them
 * in the top bit. */
static uint64_t group_planes_ref(const uint16_t* buf) {
    uint64_t res = 0;
    for (uint32_t j = 0; j < 16; j++) {
        for (uint32_t k = 0; k < 4; k++) {
            res |= (uint64_t)((buf[k] >> j) & 1) << (4 * j + (3 - k));
        }
    }
    return res;
}

/* The transposition of a whole group in four PDEPs. */
TEST(pack_group_planes_pdep, matches_reference) {
    if (!avx512_usable()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    for (uint32_t test_num = 0; test_num < 10000; test_num++) {
        uint16_t buf[GROUP_SIZE];
        for (uint32_t i = 0; i < GROUP_SIZE; i++) {
            buf[i] = (uint16_t)rnd->Rand16();
        }
        ASSERT_EQ(group_planes_ref(buf), pack_group_planes_pdep(buf));
    }

    /* The corners the random values practically never reach. */
    const uint16_t corners[][GROUP_SIZE] = {
        {0, 0, 0, 0},
        {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
        {0x8000, 0, 0, 1},
        {1, 0x8000, 0x0001, 0x8000},
    };
    for (uint32_t i = 0; i < sizeof(corners) / sizeof(corners[0]); i++) {
        ASSERT_EQ(group_planes_ref(corners[i]), pack_group_planes_pdep(corners[i]));
    }
    delete rnd;
}

/* The two line packers - the PDEP one and the 128-bit one shared with AVX2 -
 * write the same bits for the same line, signs on and off. */
TEST(pack_groups_masked_pdep, matches_pack_groups_masked) {
    if (!avx512_usable()) {
        GTEST_SKIP();
    }
    const uint32_t max_groups = 70; /* more than two chunks of the 32-group mask */
    const uint32_t mem_size = max_groups * 16 + 32;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint16_t* buf = (uint16_t*)malloc(max_groups * GROUP_SIZE * sizeof(uint16_t));
    uint8_t* gclis = (uint8_t*)malloc(max_groups * sizeof(uint8_t));
    uint8_t* mem_ref = (uint8_t*)malloc(mem_size);
    uint8_t* mem_cmp = (uint8_t*)malloc(mem_size);
    ASSERT_TRUE(buf && gclis && mem_ref && mem_cmp);

    for (uint32_t groups = 0; groups <= max_groups; groups += 7) {
        for (uint32_t test_num = 0; test_num < 200; test_num++) {
            const uint8_t gtli = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            for (uint32_t i = 0; i < groups; i++) {
                gclis[i] = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            }
            for (uint32_t i = 0; i < groups * GROUP_SIZE; i++) {
                buf[i] = (uint16_t)rnd->Rand16();
            }
            const int emit_signs = (int)(rnd->Rand8() % 2);
            /* the line may start on either half of a byte */
            const uint8_t start_bits_used = (uint8_t)(rnd->Rand8() % 2 ? 4 : 0);

            bitstream_writer_t wr_ref, wr_cmp;
            bitstream_writer_init(&wr_ref, mem_ref, mem_size);
            bitstream_writer_init(&wr_cmp, mem_cmp, mem_size);
            /* after the init, which fills the buffer itself in a debug build */
            memset(mem_ref, 0xA5, mem_size);
            memset(mem_cmp, 0xA5, mem_size);
            wr_ref.bits_used = wr_cmp.bits_used = start_bits_used;

            nib_writer_t nw_ref, nw_cmp;
            nibw_init(&nw_ref, &wr_ref);
            nibw_init(&nw_cmp, &wr_cmp);
            pack_groups_masked(&nw_ref, buf, gclis, groups, gtli, emit_signs);
            pack_groups_masked_pdep(&nw_cmp, buf, gclis, groups, gtli, emit_signs);
            nibw_finish(&nw_ref, &wr_ref);
            nibw_finish(&nw_cmp, &wr_cmp);

            ASSERT_EQ(wr_ref.offset, wr_cmp.offset);
            ASSERT_EQ(wr_ref.bits_used, wr_cmp.bits_used);
            ASSERT_EQ(memcmp(mem_ref, mem_cmp, mem_size), 0);
        }
    }

    free(buf);
    free(gclis);
    free(mem_ref);
    free(mem_cmp);
    delete rnd;
}
