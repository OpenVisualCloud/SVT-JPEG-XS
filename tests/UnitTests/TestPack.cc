/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "Pack_avx512.h"
#include "RateControl_avx2.h"
#include "RateControl.h"
#include "PackPrecinct.h"
#include "SvtUtility.h"
#include "encoder_dsp_rtcd.h"

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
            buf[i] = sign ? rnd->Rand16() : -rnd->Rand16();
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

TEST(rate_control_calc_vpred_cost_nosigf_, AVX2) {
    rate_control_calc_vpred_cost_nosigf_test(rate_control_calc_vpred_cost_nosigf_avx2);
}

TEST(rate_control_calc_vpred_cost_nosigf_, AVX512) {
    if (CPU_FLAGS_AVX512F & get_cpu_flags()) {
        rate_control_calc_vpred_cost_nosigf_test(rate_control_calc_vpred_cost_nosigf_avx512);
    }
}

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
