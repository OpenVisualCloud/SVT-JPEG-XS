/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "RateControl.h"
#include "PackPrecinct.h"
#include "SvtUtility.h"
#include "encoder_dsp_rtcd.h"
#include "EncDec.h" /* TRUNCATION_MAX */

#include "Codestream.h"

#ifdef ARCH_X86_64
#include "pack_group_helper.h"
#include "Pack_avx512.h"
#include "RateControl_avx2.h"
#include "Pack_avx2.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(pack_data_single_group, AVX512) {
    if (!(CPU_FLAGS_AVX512F & get_cpu_flags())) {
        return;
    }

    const uint32_t max_buff_size = 50;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    bitstream_writer_t bitstream_ref, bitstream_mod;
    bitstream_ref.bits_used = bitstream_mod.bits_used = 0;
    bitstream_ref.offset = bitstream_mod.offset = 0;
    bitstream_ref.size = bitstream_mod.size = max_buff_size;

    bitstream_ref.mem = (uint8_t*)malloc(max_buff_size * sizeof(uint8_t));
    bitstream_mod.mem = (uint8_t*)malloc(max_buff_size * sizeof(uint8_t));
    uint16_t* buf = (uint16_t*)malloc(max_buff_size * sizeof(uint16_t));

    if (bitstream_ref.mem == NULL || bitstream_mod.mem == NULL || buf == NULL) {
        ASSERT_EQ(0, 1);
        return;
    }

    for (uint32_t test_num = 0; test_num < 1000; test_num++) {
        memset(bitstream_ref.mem, 0xff, max_buff_size * sizeof(uint8_t));
        memset(bitstream_mod.mem, 0xff, max_buff_size * sizeof(uint8_t));
        bitstream_ref.bits_used = bitstream_mod.bits_used = rnd->Rand8() % 2 ? 0 : 4;
        bitstream_ref.offset = bitstream_mod.offset = 0;

        for (uint32_t i = 0; i < max_buff_size; i++) {
            int32_t sign = rnd->Rand16() % 2;
            buf[i] = (uint16_t)(sign ? rnd->Rand16() : -rnd->Rand16());
        }

        uint8_t gtli = rnd->Rand8() % 15;
        uint8_t gcli = gtli + rnd->Rand8() % (15 - gtli);

        pack_data_single_group_c(&bitstream_ref, buf, gcli, gtli);
        pack_data_single_group_avx512(&bitstream_mod, buf, gcli, gtli);

        ASSERT_EQ(memcmp(bitstream_ref.mem, bitstream_mod.mem, sizeof(uint8_t) * max_buff_size), 0);
    }

    free(bitstream_ref.mem);
    free(bitstream_mod.mem);
    free(buf);
    delete rnd;
}
#endif /* ARCH_X86_64 */

#define MAX_WIDTH_VLC_ENCODE_GET_BITS 3840
uint32_t vlc_encode_get_bits_sizes[] = {
    5, 7, 8, 15, 16, 17, 30, 31, 32, 33, 40, 48, 50, 64, 67, 100, 300, 1080, MAX_WIDTH_VLC_ENCODE_GET_BITS};

void rate_control_calc_vpred_cost_nosigf_test(uint32_t (*test_fn)(uint32_t, uint8_t*, uint8_t*, uint8_t*, uint8_t, uint8_t)) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t* gcli = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* gcli_top = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* out_ptr_ref = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* out_ptr_mod = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));

    if (!gcli || !gcli_top || !out_ptr_ref || !out_ptr_mod) {
        ASSERT_EQ(1, 0);
    }

    const uint32_t width_arr_size = sizeof(vlc_encode_get_bits_sizes) / sizeof(vlc_encode_get_bits_sizes[0]);
    for (uint32_t width_idx = 0; width_idx < width_arr_size; width_idx++) {
        const uint32_t gcli_width = vlc_encode_get_bits_sizes[width_idx];
        assert(gcli_width <= MAX_WIDTH_VLC_ENCODE_GET_BITS);

        for (uint32_t i = 0; i < gcli_width; i++) {
            gcli[i] = rnd->Rand8() % 16;
            gcli_top[i] = rnd->Rand8() % 16;
        }
        for (uint32_t gtli = 0; gtli < 16; gtli++) {
            for (uint32_t gtli_max = 0; gtli_max < 16; gtli_max++) {
                memset(out_ptr_ref, 0, gcli_width * sizeof(uint8_t));
                memset(out_ptr_mod, 0, gcli_width * sizeof(uint8_t));

                uint32_t out_ref = rate_control_calc_vpred_cost_nosigf_c(gcli_width, gcli_top, gcli, out_ptr_ref, gtli, gtli_max);
                uint32_t out_mod = test_fn(gcli_width, gcli_top, gcli, out_ptr_mod, gtli, gtli_max);
                ASSERT_EQ(out_ref, out_mod);
                ASSERT_EQ(memcmp(out_ptr_ref, out_ptr_mod, gcli_width * sizeof(uint8_t)), 0);
            }
        }
    }

    free(gcli);
    free(gcli_top);
    free(out_ptr_ref);
    free(out_ptr_mod);
    delete rnd;
}

#ifdef ARCH_X86_64
TEST(rate_control_calc_vpred_cost_nosigf_, AVX2) {
    rate_control_calc_vpred_cost_nosigf_test(rate_control_calc_vpred_cost_nosigf_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(rate_control_calc_vpred_cost_nosigf_, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        rate_control_calc_vpred_cost_nosigf_test(rate_control_calc_vpred_cost_nosigf_avx512);
    }
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(rate_control_calc_vpred_cost_sigf_nosigf_, AVX512) {
    if (!(CPU_FLAGS_AVX512F & get_cpu_flags())) {
        return;
    }
    setup_encoder_rtcd_internal(0);
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t* gcli = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* gcli_top = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* out_bits_ref = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* out_bits_mod = (uint8_t*)calloc(MAX_WIDTH_VLC_ENCODE_GET_BITS, sizeof(uint8_t));
    uint8_t* out_sigf_ref = (uint8_t*)calloc(DIV_ROUND_UP(MAX_WIDTH_VLC_ENCODE_GET_BITS, SIGNIFICANCE_GROUP_SIZE),
                                             sizeof(uint8_t));
    uint8_t* out_sigf_mod = (uint8_t*)calloc(DIV_ROUND_UP(MAX_WIDTH_VLC_ENCODE_GET_BITS, SIGNIFICANCE_GROUP_SIZE),
                                             sizeof(uint8_t));

    if (!gcli || !gcli_top || !out_bits_ref || !out_bits_mod || !out_sigf_ref || !out_sigf_mod) {
        ASSERT_EQ(1, 0);
    }

    const uint32_t width_arr_size = sizeof(vlc_encode_get_bits_sizes) / sizeof(vlc_encode_get_bits_sizes[0]);
    for (uint32_t width_idx = 0; width_idx < width_arr_size; width_idx++) {
        const uint32_t gcli_width = vlc_encode_get_bits_sizes[width_idx];
        assert(gcli_width <= MAX_WIDTH_VLC_ENCODE_GET_BITS);

        for (uint32_t i = 0; i < gcli_width; i++) {
            gcli[i] = rnd->Rand8() % 16;
            gcli_top[i] = rnd->Rand8() % 16;
        }
        for (uint8_t hdr_Rm = 0; hdr_Rm < 2; hdr_Rm++) {
            for (uint32_t gtli = 0; gtli < 16; gtli++) {
                for (uint32_t gtli_max = 0; gtli_max < 16; gtli_max++) {
                    memset(out_bits_ref, 0, gcli_width * sizeof(uint8_t));
                    memset(out_bits_mod, 0, gcli_width * sizeof(uint8_t));

                    uint32_t sigf_width = DIV_ROUND_UP(gcli_width, SIGNIFICANCE_GROUP_SIZE);
                    memset(out_sigf_ref, 0xcd, sigf_width * sizeof(uint8_t));
                    memset(out_sigf_mod, 0xcd, sigf_width * sizeof(uint8_t));

                    uint32_t out_ref1 = 0xcdcd;
                    uint32_t out_ref2 = 0xcdcd;
                    rate_control_calc_vpred_cost_sigf_nosigf_c(sigf_width,
                                                               gcli_width,
                                                               hdr_Rm,
                                                               SIGNIFICANCE_GROUP_SIZE,
                                                               gcli_top,
                                                               gcli,
                                                               out_bits_ref,
                                                               out_sigf_ref,
                                                               gtli,
                                                               gtli_max,
                                                               &out_ref1,
                                                               &out_ref2);
                    uint32_t out_mod1 = 0xcdcd;
                    uint32_t out_mod2 = 0xcdcd;
                    rate_control_calc_vpred_cost_sigf_nosigf_avx512(sigf_width,
                                                                    gcli_width,
                                                                    hdr_Rm,
                                                                    SIGNIFICANCE_GROUP_SIZE,
                                                                    gcli_top,
                                                                    gcli,
                                                                    out_bits_mod,
                                                                    out_sigf_mod,
                                                                    gtli,
                                                                    gtli_max,
                                                                    &out_mod1,
                                                                    &out_mod2);
                    ASSERT_EQ(out_ref1, out_mod1);
                    ASSERT_EQ(out_ref2, out_mod2);
                    ASSERT_EQ(memcmp(out_bits_ref, out_bits_mod, gcli_width * sizeof(uint8_t)), 0);
                    ASSERT_EQ(memcmp(out_sigf_ref, out_sigf_mod, sigf_width * sizeof(uint8_t)), 0);
                }
            }
        }
    }

    free(gcli);
    free(gcli_top);
    free(out_bits_ref);
    free(out_bits_mod);
    free(out_sigf_ref);
    free(out_sigf_mod);
    delete rnd;
}
#endif /* ARCH_X86_64 */

/* Packing one group with the 128-bit implementation shared by AVX2 and
 * AVX-512. The AVX-512 wrapper has had a test since the beginning; the AVX2 one
 * is the same code behind a different name, and it had none. */
#ifdef ARCH_X86_64
TEST(pack_data_single_group, AVX2) {
    const uint32_t max_buff_size = 50;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* mem_ref = (uint8_t*)malloc(max_buff_size);
    uint8_t* mem_cmp = (uint8_t*)malloc(max_buff_size);
    uint16_t* buf = (uint16_t*)malloc(max_buff_size * sizeof(uint16_t));
    ASSERT_TRUE(mem_ref != NULL && mem_cmp != NULL && buf != NULL);

    for (uint32_t test_num = 0; test_num < 1000; test_num++) {
        memset(mem_ref, 0xff, max_buff_size);
        memset(mem_cmp, 0xff, max_buff_size);
        bitstream_writer_t ref, cmp;
        bitstream_writer_init(&ref, mem_ref, max_buff_size);
        bitstream_writer_init(&cmp, mem_cmp, max_buff_size);
        ref.bits_used = cmp.bits_used = rnd->Rand8() % 2 ? 0 : 4;

        for (uint32_t i = 0; i < max_buff_size; i++) {
            int32_t sign = rnd->Rand16() % 2;
            buf[i] = (uint16_t)(sign ? rnd->Rand16() : -rnd->Rand16());
        }

        uint8_t gtli = rnd->Rand8() % 15;
        uint8_t gcli = gtli + rnd->Rand8() % (15 - gtli);

        pack_data_single_group_c(&ref, buf, gcli, gtli);
        pack_data_single_group_avx2(&cmp, buf, gcli, gtli);

        ASSERT_EQ(ref.offset, cmp.offset);
        ASSERT_EQ(ref.bits_used, cmp.bits_used);
        ASSERT_EQ(memcmp(mem_ref, mem_cmp, max_buff_size), 0);
    }

    free(mem_ref);
    free(mem_cmp);
    free(buf);
    delete rnd;
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
/* The shared 128-bit implementation itself, reached by its own name rather
 * than through either wrapper. */
TEST(pack_group_helper, data_single_group_sse_matches_c) {
    const uint32_t max_buff_size = 50;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* mem_ref = (uint8_t*)malloc(max_buff_size);
    uint8_t* mem_cmp = (uint8_t*)malloc(max_buff_size);
    uint16_t* buf = (uint16_t*)malloc(max_buff_size * sizeof(uint16_t));
    ASSERT_TRUE(mem_ref != NULL && mem_cmp != NULL && buf != NULL);

    for (uint32_t test_num = 0; test_num < 1000; test_num++) {
        memset(mem_ref, 0xff, max_buff_size);
        memset(mem_cmp, 0xff, max_buff_size);
        bitstream_writer_t ref, cmp;
        bitstream_writer_init(&ref, mem_ref, max_buff_size);
        bitstream_writer_init(&cmp, mem_cmp, max_buff_size);
        ref.bits_used = cmp.bits_used = rnd->Rand8() % 2 ? 0 : 4;

        for (uint32_t i = 0; i < max_buff_size; i++) {
            buf[i] = (uint16_t)rnd->Rand16();
        }

        const uint8_t gtli = (uint8_t)(rnd->Rand8() % TRUNCATION_MAX);
        const uint8_t gcli = (uint8_t)(gtli + rnd->Rand8() % (TRUNCATION_MAX - gtli));

        pack_data_single_group_c(&ref, buf, gcli, gtli);
        pack_data_single_group_sse(&cmp, buf, gcli, gtli);

        ASSERT_EQ(ref.offset, cmp.offset);
        ASSERT_EQ(ref.bits_used, cmp.bits_used);
        ASSERT_EQ(memcmp(mem_ref, mem_cmp, max_buff_size), 0);
    }

    free(mem_ref);
    free(mem_cmp);
    free(buf);
    delete rnd;
}
#endif /* ARCH_X86_64 */

/* The nibble of a bit plane: bit (3 - k) belongs to coefficient k. */
static uint32_t group_plane_nibble_ref(const uint16_t* buf, uint32_t plane) {
    uint32_t nibble = 0;
    for (uint32_t k = 0; k < GROUP_SIZE; k++) {
        nibble |= ((buf[k] >> plane) & 1) << (3 - k);
    }
    return nibble;
}

#ifdef ARCH_X86_64
/* The lane reversal and the sign mask that stand in for a horizontal fold. The
 * sign nibble is the plane of weight 2^15, so one reference serves both. */
TEST(pack_group_helper, load_reversed_and_nibble) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    for (uint32_t test_num = 0; test_num < 10000; test_num++) {
        uint16_t buf[GROUP_SIZE];
        for (uint32_t i = 0; i < GROUP_SIZE; i++) {
            buf[i] = (uint16_t)rnd->Rand16();
        }
        const __m128i reversed = pack_group_load_reversed(buf);
        ASSERT_EQ(group_plane_nibble_ref(buf, 15), pack_group_nibble(reversed));

        /* the reversal itself: lane k has to hold coefficient 3 - k */
        uint16_t lanes[8];
        _mm_storeu_si128((__m128i*)lanes, reversed);
        for (uint32_t k = 0; k < GROUP_SIZE; k++) {
            ASSERT_EQ(buf[GROUP_SIZE - 1 - k], lanes[k]);
        }
    }
    delete rnd;
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
/* All the planes of a group in one word, the top plane in the top nibble. */
TEST(pack_group_helper, planes_word_matches_reference) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    for (uint32_t gcli = 1; gcli <= TRUNCATION_MAX; gcli++) {
        for (uint32_t gtli = 0; gtli < gcli; gtli++) {
            for (uint32_t test_num = 0; test_num < 100; test_num++) {
                uint16_t buf[GROUP_SIZE];
                for (uint32_t i = 0; i < GROUP_SIZE; i++) {
                    buf[i] = (uint16_t)rnd->Rand16();
                }
                uint64_t ref = 0;
                for (int32_t plane = (int32_t)gcli - 1; plane >= (int32_t)gtli; plane--) {
                    ref = (ref << 4) | group_plane_nibble_ref(buf, (uint32_t)plane);
                }
                ASSERT_EQ(ref, pack_group_planes_word(pack_group_load_reversed(buf), (uint8_t)gcli, (uint8_t)gtli));
            }
        }
    }
    delete rnd;
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
/* The mask of non-empty groups, including a partial chunk at the end of a line:
 * the padding must not add groups that do not exist. */
TEST(pack_group_helper, nonempty_mask_matches_reference) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t gclis[32];
    for (uint32_t chunk = 1; chunk <= 32; chunk++) {
        for (uint32_t gtli = 0; gtli <= TRUNCATION_MAX; gtli++) {
            for (uint32_t test_num = 0; test_num < 200; test_num++) {
                uint64_t ref = 0;
                for (uint32_t i = 0; i < chunk; i++) {
                    gclis[i] = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
                    if (gclis[i] > gtli) {
                        ref |= (uint64_t)1 << i;
                    }
                }
                ASSERT_EQ(ref, pack_nonempty_mask(gclis, chunk, (uint8_t)gtli));
            }
        }
    }
    delete rnd;
}
#endif /* ARCH_X86_64 */

#define PACK_GROUPS_MAX_GROUPS 70
#define PACK_GROUPS_BUF_SIZE   1024

typedef void (*pack_data_groups_fn)(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups,
                                    uint8_t gtli, uint8_t sign_flag);

/* An independent reference: the definition of a packed line, nibble by nibble
 * through the generic writer. Every implementation - the scalar one included -
 * is checked against this rather than against another implementation, so the
 * whole family cannot agree on a wrong answer.
 *
 * sign_flag zero means the signs travel with the data, as one more nibble in
 * front of the planes of the group. */
static void pack_data_groups_ref(bitstream_writer_t* bitstream, const uint16_t* buf_16bit, const uint8_t* gclis, uint32_t groups,
                                 uint8_t gtli, uint8_t sign_flag) {
    for (uint32_t group = 0; group < groups; group++) {
        const uint8_t gcli = gclis[group];
        const uint16_t* g = buf_16bit + (size_t)group * GROUP_SIZE;
        if (gcli > gtli) {
            if (sign_flag == 0) {
                write_4_bits_align4(bitstream, (uint8_t)group_plane_nibble_ref(g, BITSTREAM_BIT_POSITION_SIGN));
            }
            for (int32_t plane = (int32_t)gcli - 1; plane >= (int32_t)gtli; plane--) {
                write_4_bits_align4(bitstream, (uint8_t)group_plane_nibble_ref(g, (uint32_t)plane));
            }
        }
    }
}

/* A line of full groups packed by the reference and by the implementation under
 * test have to be the same bits.
 *
 * The line is walked in chunks of thirty-two groups, so seventy groups cover a
 * full chunk, a second one and a partial third; the start is put on both halves
 * of a byte because the nibble writer picks the stream up mid-byte. */
static void pack_data_groups_test(pack_data_groups_fn pack_cmp) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint16_t* buf = (uint16_t*)malloc(PACK_GROUPS_MAX_GROUPS * GROUP_SIZE * sizeof(uint16_t));
    uint8_t* gclis = (uint8_t*)malloc(PACK_GROUPS_MAX_GROUPS);
    uint8_t* mem_ref = (uint8_t*)malloc(PACK_GROUPS_BUF_SIZE);
    uint8_t* mem_cmp = (uint8_t*)malloc(PACK_GROUPS_BUF_SIZE);
    ASSERT_TRUE(buf != NULL && gclis != NULL && mem_ref != NULL && mem_cmp != NULL);

    for (uint32_t groups = 0; groups <= PACK_GROUPS_MAX_GROUPS; groups++) {
        for (uint32_t test_num = 0; test_num < 20; test_num++) {
            const uint8_t gtli = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            for (uint32_t i = 0; i < groups; i++) {
                /* a mix of empty and non-empty groups, which is what the mask walk
                 * is there for */
                gclis[i] = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            }
            for (uint32_t i = 0; i < groups * GROUP_SIZE; i++) {
                buf[i] = (uint16_t)rnd->Rand16();
            }
            const uint8_t sign_flag = (uint8_t)(rnd->Rand8() % 2);

            bitstream_writer_t ref, cmp;
            bitstream_writer_init(&ref, mem_ref, PACK_GROUPS_BUF_SIZE);
            bitstream_writer_init(&cmp, mem_cmp, PACK_GROUPS_BUF_SIZE);
            /* after the init: a debug build fills the buffer with 0xFF in there,
             * and the nibble written into the second half of a byte is ORed into
             * whatever the first half holds */
            memset(mem_ref, 0, PACK_GROUPS_BUF_SIZE);
            memset(mem_cmp, 0, PACK_GROUPS_BUF_SIZE);
            const uint8_t start_bits_used = (uint8_t)(rnd->Rand8() % 2 ? 4 : 0);
            ref.bits_used = cmp.bits_used = start_bits_used;

            pack_data_groups_ref(&ref, buf, gclis, groups, gtli, sign_flag);
            pack_cmp(&cmp, buf, gclis, groups, gtli, sign_flag);

            ASSERT_EQ(ref.offset, cmp.offset);
            ASSERT_EQ(ref.bits_used, cmp.bits_used);
            ASSERT_EQ(memcmp(mem_ref, mem_cmp, PACK_GROUPS_BUF_SIZE), 0);
        }
    }

    free(buf);
    free(gclis);
    free(mem_ref);
    free(mem_cmp);
    delete rnd;
}

TEST(pack_data_groups, C) {
    pack_data_groups_test(pack_data_groups_c);
}

#ifdef ARCH_X86_64
TEST(pack_data_groups, AVX2) {
    pack_data_groups_test(pack_data_groups_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(pack_data_groups, AVX512) {
    /* the same pair of conditions the encoder dispatch checks: the directory is
     * built with -mbmi2 and -mpopcnt */
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    pack_data_groups_test(pack_data_groups_avx512);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
/* The line packer that walks the mask, called directly rather than through the
 * dispatched wrapper: the wrapper only owns the nibble writer around it. */
TEST(pack_group_helper, groups_masked_matches_reference) {
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint16_t* buf = (uint16_t*)malloc(PACK_GROUPS_MAX_GROUPS * GROUP_SIZE * sizeof(uint16_t));
    uint8_t* gclis = (uint8_t*)malloc(PACK_GROUPS_MAX_GROUPS);
    uint8_t* mem_ref = (uint8_t*)malloc(PACK_GROUPS_BUF_SIZE);
    uint8_t* mem_cmp = (uint8_t*)malloc(PACK_GROUPS_BUF_SIZE);
    ASSERT_TRUE(buf != NULL && gclis != NULL && mem_ref != NULL && mem_cmp != NULL);

    for (uint32_t groups = 0; groups <= PACK_GROUPS_MAX_GROUPS; groups += 3) {
        for (uint32_t emit_signs = 0; emit_signs < 2; emit_signs++) {
            for (uint32_t test_num = 0; test_num < 50; test_num++) {
                const uint8_t gtli = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
                for (uint32_t i = 0; i < groups; i++) {
                    gclis[i] = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
                }
                for (uint32_t i = 0; i < groups * GROUP_SIZE; i++) {
                    buf[i] = (uint16_t)rnd->Rand16();
                }

                bitstream_writer_t ref, cmp;
                bitstream_writer_init(&ref, mem_ref, PACK_GROUPS_BUF_SIZE);
                bitstream_writer_init(&cmp, mem_cmp, PACK_GROUPS_BUF_SIZE);
                memset(mem_ref, 0, PACK_GROUPS_BUF_SIZE);
                memset(mem_cmp, 0, PACK_GROUPS_BUF_SIZE);

                /* sign_flag is inverted with respect to emit_signs: zero means the
                 * signs are written here rather than by unpack_sign later */
                pack_data_groups_ref(&ref, buf, gclis, groups, gtli, (uint8_t)(emit_signs ? 0 : 1));

                nib_writer_t w;
                nibw_init(&w, &cmp);
                pack_groups_masked(&w, buf, gclis, groups, gtli, (int)emit_signs);
                nibw_finish(&w, &cmp);

                ASSERT_EQ(ref.offset, cmp.offset);
                ASSERT_EQ(ref.bits_used, cmp.bits_used);
                ASSERT_EQ(memcmp(mem_ref, mem_cmp, PACK_GROUPS_BUF_SIZE), 0);
            }
        }
    }

    free(buf);
    free(gclis);
    free(mem_ref);
    free(mem_cmp);
    delete rnd;
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
/* pack_data_groups_sse is what both wrappers call; it is reached here by its
 * own name rather than through them. */
TEST(pack_group_helper, data_groups_sse_matches_reference) {
    pack_data_groups_test(pack_data_groups_sse);
}
#endif /* ARCH_X86_64 */

/* A unary code through the accumulating bit writer must be the code the generic
 * writer produces, at every starting alignment and at every length - including
 * the two short cases the generic writer handles separately. */
TEST(vlc_put_unary, matches_vlc_encode_pack_bits) {
    const uint32_t buf_size = 512;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t* mem_ref = (uint8_t*)malloc(buf_size);
    uint8_t* mem_cmp = (uint8_t*)malloc(buf_size);
    ASSERT_TRUE(mem_ref != NULL && mem_cmp != NULL);

    for (uint32_t prefix_bits = 0; prefix_bits < 8; prefix_bits++) {
        for (uint32_t test_num = 0; test_num < 500; test_num++) {
            const uint32_t codes = 1 + rnd->Rand8() % 30;
            uint8_t nbits[30];
            for (uint32_t i = 0; i < codes; i++) {
                nbits[i] = (uint8_t)(rnd->Rand8() % 32);
            }

            bitstream_writer_t ref, cmp;
            bitstream_writer_init(&ref, mem_ref, buf_size);
            bitstream_writer_init(&cmp, mem_cmp, buf_size);
            memset(mem_ref, 0, buf_size);
            memset(mem_cmp, 0, buf_size);
            if (prefix_bits) {
                const uint8_t prefix_value = (uint8_t)(rnd->Rand8() & ((1u << prefix_bits) - 1));
                write_N_bits(&ref, prefix_value, (uint8_t)prefix_bits);
                write_N_bits(&cmp, prefix_value, (uint8_t)prefix_bits);
            }

            for (uint32_t i = 0; i < codes; i++) {
                vlc_encode_pack_bits(&ref, nbits[i]);
            }

            bit_writer_t w;
            bitw_init(&w, &cmp);
            for (uint32_t i = 0; i < codes; i++) {
                vlc_put_unary(&w, nbits[i]);
            }
            bitw_finish(&w, &cmp);

            ASSERT_EQ(ref.offset, cmp.offset);
            ASSERT_EQ(ref.bits_used, cmp.bits_used);
            ASSERT_EQ(memcmp(mem_ref, mem_cmp, buf_size), 0);
        }
    }
    free(mem_ref);
    free(mem_cmp);
    delete rnd;
}
