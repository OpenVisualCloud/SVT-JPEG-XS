/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "BitstreamWriter.h"
#include "Codestream.h"
#include "Definitions.h"
#include "Dequant.h"
#include "EncDec.h"
#include "GcStageProcess.h"
#include "Idwt.h"
#include "Mct.h"
#include "MctEnc.h"
#include "NltDec.h"
#include "PackPrecinct.h"
#include "Packing.h"
#include "Precinct.h"
#include "RateControl.h"
#include "SvtUtility.h"
#include "encoder_dsp_rtcd.h"

#ifdef ARCH_X86_64
#include "Dequant_avx2.h"
#include "Dwt53Decoder_AVX2.h"
#include "Mct_avx2.h"
#include "NltDec_AVX2.h"
#include "Pack_avx2.h"
#include "RateControl_avx2.h"
#include "UnPack_avx2.h"

static bool has_avx2_popcnt() {
    const CPU_FLAGS req = CPU_FLAGS_AVX2 | CPU_FLAGS_POPCNT;
    return (get_cpu_flags() & req) == req;
}

static void expect_bitstream_eq_avx2(bitstream_writer_t& tst, bitstream_writer_t& ref) {
    const uint32_t used_tst = bitstream_writer_get_used_bits(&tst);
    const uint32_t used_ref = bitstream_writer_get_used_bits(&ref);
    ASSERT_EQ(used_tst, used_ref);
    ASSERT_EQ(tst.offset, ref.offset);
    ASSERT_EQ(tst.bits_used, ref.bits_used);
    const uint32_t full_bytes = used_ref >> 3;
    const uint32_t rem_bits = used_ref & 7u;
    if (full_bytes > 0) {
        ASSERT_EQ(0, memcmp(tst.mem, ref.mem, full_bytes));
    }
    if (rem_bits > 0) {
        const uint8_t mask = static_cast<uint8_t>(0xFFu << (8u - rem_bits));
        ASSERT_EQ(tst.mem[full_bytes] & mask, ref.mem[full_bytes] & mask);
    }
}

TEST(avx2_kernels, dequant_inv_sign) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, 0xFFFF);
    const uint32_t widths[] = {1, 4, 15, 16, 17, 31, 32, 33, 63, 64, 65, 131};
    const uint32_t guard = 16;

    for (QUANT_TYPE qtype : {QUANT_TYPE_DEADZONE, QUANT_TYPE_UNIFORM}) {
        for (uint8_t gtli : {uint8_t(0), uint8_t(1), uint8_t(5), uint8_t(11)}) {
            for (uint32_t w : widths) {
                const uint32_t n_groups = (w + GROUP_SIZE - 1) / GROUP_SIZE;
                std::vector<uint8_t> gclis(n_groups);
                for (uint32_t g = 0; g < n_groups; ++g) {
                    gclis[g] = static_cast<uint8_t>(rnd.random() % (TRUNCATION_MAX + 1));
                }
                std::vector<uint16_t> ref(w + guard, 0xBEEF);
                std::vector<uint16_t> tst(w + guard, 0xBEEF);
                for (uint32_t i = 0; i < w; ++i) {
                    const uint8_t g = gclis[i / GROUP_SIZE];
                    const uint16_t max_mag = (g > 0) ? static_cast<uint16_t>((1u << g) - 1u) : 0u;
                    const uint16_t clear_low = (gtli > 0) ? static_cast<uint16_t>(0xFFFFu << gtli) : 0xFFFFu;
                    const uint16_t qmag = static_cast<uint16_t>(rnd.random()) & max_mag & clear_low;
                    const uint16_t sign = (qmag && (rnd.random() & 1)) ? BITSTREAM_MASK_SIGN : 0;
                    const uint16_t v = (g > gtli) ? static_cast<uint16_t>(qmag | sign) : 0;
                    ref[i] = tst[i] = v;
                }
                dequant_c(ref.data(), w, gclis.data(), GROUP_SIZE, gtli, qtype);
                inv_sign_c(ref.data(), w);
                dequant_inv_sign_avx2(tst.data(), w, gclis.data(), GROUP_SIZE, gtli, qtype);
                ASSERT_EQ(tst, ref) << "qtype=" << qtype << " gtli=" << (int)gtli << " w=" << w;
            }
        }
    }
}

TEST(avx2_kernels, inverse_rct_output_8bit_and_16bit) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(-600000, 600000);
    const uint32_t widths[] = {1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 67};
    const uint32_t guard = 16;
    const uint32_t bw = 20;

    for (uint32_t w : widths) {
        std::vector<int32_t> c0(w), c1(w), c2(w);
        for (uint32_t i = 0; i < w; ++i) {
            c0[i] = rnd.random();
            c1[i] = rnd.random();
            c2[i] = rnd.random();
        }
        std::vector<uint8_t> r0(w + guard, 0x5A), r1(w + guard, 0x5A), r2(w + guard, 0x5A);
        std::vector<uint8_t> t0(w + guard, 0x5A), t1(w + guard, 0x5A), t2(w + guard, 0x5A);
        inverse_rct_output_8bit_line_c(c0.data(), c1.data(), c2.data(), bw, 8, r0.data(), r1.data(), r2.data(), w);
        inverse_rct_output_8bit_line_avx2(c0.data(), c1.data(), c2.data(), bw, 8, t0.data(), t1.data(), t2.data(), w);
        ASSERT_EQ(t0, r0) << "8bit w=" << w;
        ASSERT_EQ(t1, r1) << "8bit w=" << w;
        ASSERT_EQ(t2, r2) << "8bit w=" << w;

        for (uint32_t depth : {10u, 12u}) {
            for (uint8_t msb : {uint8_t(0), uint8_t(1)}) {
                std::vector<uint16_t> r16_0(w + guard, 0xA5A5), r16_1(w + guard, 0xA5A5), r16_2(w + guard, 0xA5A5);
                std::vector<uint16_t> t16_0(w + guard, 0xA5A5), t16_1(w + guard, 0xA5A5), t16_2(w + guard, 0xA5A5);
                inverse_rct_output_16bit_line_c(
                    c0.data(), c1.data(), c2.data(), bw, depth, r16_0.data(), r16_1.data(), r16_2.data(), w, msb);
                inverse_rct_output_16bit_line_avx2(
                    c0.data(), c1.data(), c2.data(), bw, depth, t16_0.data(), t16_1.data(), t16_2.data(), w, msb);
                ASSERT_EQ(t16_0, r16_0) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
                ASSERT_EQ(t16_1, r16_1) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
                ASSERT_EQ(t16_2, r16_2) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
            }
        }
    }
}

TEST(avx2_kernels, idwt_vertical_line_output_8bit_and_16bit) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(-700000, 700000);
    const uint32_t widths[] = {2, 3, 15, 16, 17, 31, 32, 33, 64, 67};
    const uint32_t guard = 16;
    const uint32_t bw = 20;

    for (uint32_t w : widths) {
        std::vector<int32_t> lf(w), hf0(w), hf1(w), out0(w);
        for (uint32_t i = 0; i < w; ++i) {
            lf[i] = rnd.random();
            hf0[i] = rnd.random();
            hf1[i] = rnd.random();
            out0[i] = rnd.random();
        }
        std::vector<int32_t> r_out2(w + guard, 0x13579), t_out2(w + guard, 0x13579);
        std::vector<uint8_t> r_odd8(w + guard, 0x5A), r_even8(w + guard, 0x5A);
        std::vector<uint8_t> t_odd8(w + guard, 0x5A), t_even8(w + guard, 0x5A);

        idwt_vertical_line_output_8bit_c(
            lf.data(), hf0.data(), hf1.data(), out0.data(), r_out2.data(), r_odd8.data(), r_even8.data(), w, bw, 8);
        idwt_vertical_line_output_8bit_avx2(
            lf.data(), hf0.data(), hf1.data(), out0.data(), t_out2.data(), t_odd8.data(), t_even8.data(), w, bw, 8);
        ASSERT_EQ(t_out2, r_out2) << "8bit w=" << w;
        ASSERT_EQ(t_odd8, r_odd8) << "8bit w=" << w;
        ASSERT_EQ(t_even8, r_even8) << "8bit w=" << w;

        for (uint32_t depth : {10u, 12u}) {
            for (uint8_t msb : {uint8_t(0), uint8_t(1)}) {
                std::fill(r_out2.begin(), r_out2.end(), 0x13579);
                std::fill(t_out2.begin(), t_out2.end(), 0x13579);
                std::vector<uint16_t> r_odd16(w + guard, 0xA5A5), r_even16(w + guard, 0xA5A5);
                std::vector<uint16_t> t_odd16(w + guard, 0xA5A5), t_even16(w + guard, 0xA5A5);

                idwt_vertical_line_output_16bit_c(lf.data(),
                                                  hf0.data(),
                                                  hf1.data(),
                                                  out0.data(),
                                                  r_out2.data(),
                                                  r_odd16.data(),
                                                  r_even16.data(),
                                                  w,
                                                  bw,
                                                  depth,
                                                  msb);
                idwt_vertical_line_output_16bit_avx2(lf.data(),
                                                     hf0.data(),
                                                     hf1.data(),
                                                     out0.data(),
                                                     t_out2.data(),
                                                     t_odd16.data(),
                                                     t_even16.data(),
                                                     w,
                                                     bw,
                                                     depth,
                                                     msb);
                ASSERT_EQ(t_out2, r_out2) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
                ASSERT_EQ(t_odd16, r_odd16) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
                ASSERT_EQ(t_even16, r_even16) << "16bit w=" << w << " depth=" << depth << " msb=" << (int)msb;
            }
        }
    }
}

TEST(avx2_kernels, unpack_gclis_sig_scatter) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t group_nums[] = {1, 2, 7, 8, 9, 15, 16, 17, 33};
    const uint32_t guard = 16;

    for (uint32_t gnum : group_nums) {
        const uint32_t total_bytes = gnum * SIGNIFICANCE_GROUP_SIZE;
        for (int pat = 0; pat < 3; ++pat) {
            std::vector<uint8_t> sigs(gnum);
            uint32_t active_groups = 0;
            for (uint32_t g = 0; g < gnum; ++g) {
                sigs[g] = (pat == 0) ? 0 : (pat == 1 ? 1 : static_cast<uint8_t>(rnd.random() & 1));
                if (sigs[g] == 0) {
                    ++active_groups;
                }
            }
            const uint32_t src_bytes = active_groups * SIGNIFICANCE_GROUP_SIZE;
            for (uint8_t gtli : {uint8_t(0), uint8_t(5), uint8_t(15)}) {
                /* The GCLIs of a line start at any byte: also at an offset that is not a multiple of 8 */
                for (uint32_t offset : {0u, 1u, 3u}) {
                    std::vector<uint8_t> ref(offset + total_bytes + guard, 0xCC);
                    std::vector<uint8_t> tst(offset + total_bytes + guard, 0xCC);
                    for (uint32_t i = 0; i < src_bytes; ++i) {
                        ref[offset + i] = tst[offset + i] = static_cast<uint8_t>(rnd.random());
                    }
                    unpack_gclis_sig_scatter_c(ref.data() + offset, sigs.data(), gnum, src_bytes, gtli);
                    unpack_gclis_sig_scatter_avx2(tst.data() + offset, sigs.data(), gnum, src_bytes, gtli);
                    ASSERT_EQ(tst, ref) << "gnum=" << gnum << " pat=" << pat << " gtli=" << (int)gtli << " offset=" << offset;
                }
            }
        }
    }
}

TEST(avx2_kernels, gc_histogram_16) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {0, 1, 7, 8, 9, 31, 32, 33, 64, 131};

    for (uint32_t w : widths) {
        for (int max_val : {7, TRUNCATION_MAX}) {
            std::vector<uint8_t> data(w);
            for (uint32_t i = 0; i < w; ++i) {
                data[i] = static_cast<uint8_t>(rnd.random() % (max_val + 1));
            }
            uint16_t ref[TRUNCATION_MAX + 1];
            uint16_t tst[TRUNCATION_MAX + 1];
            memset(ref, 0xAA, sizeof(ref));
            memset(tst, 0x55, sizeof(tst));
            gc_histogram_16_c(data.data(), w, ref);
            gc_histogram_16_avx2(data.data(), w, tst);
            ASSERT_EQ(0, memcmp(tst, ref, sizeof(ref))) << "w=" << w << " max=" << max_val;
        }
    }
}

TEST(avx2_kernels, gc_precinct_sigflags_max) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {1, 7, 8, 9, 15, 16, 31, 32, 33, 63, 64, 65, 131};
    const uint32_t guard = 16;

    for (uint32_t w : widths) {
        const uint32_t out_w = (w + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
        std::vector<uint8_t> gclis(w);
        for (uint32_t i = 0; i < w; ++i) {
            gclis[i] = static_cast<uint8_t>(rnd.random());
        }
        std::vector<uint8_t> ref(out_w + guard, 0xCC);
        std::vector<uint8_t> tst(out_w + guard, 0xCC);
        gc_precinct_sigflags_max_c(ref.data(), gclis.data(), SIGNIFICANCE_GROUP_SIZE, w);
        gc_precinct_sigflags_max_avx2(tst.data(), gclis.data(), SIGNIFICANCE_GROUP_SIZE, w);
        ASSERT_EQ(tst, ref) << "w=" << w;
    }
}

TEST(avx2_kernels, forward_rct_line) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(-500000, 500000);
    const uint32_t widths[] = {1, 7, 8, 9, 15, 16, 17, 33, 67};
    const uint32_t guard = 8;

    for (uint32_t w : widths) {
        std::vector<int32_t> r0(w + guard, 0x12345), r1(w + guard, 0x23456), r2(w + guard, 0x34567);
        for (uint32_t i = 0; i < w; ++i) {
            r0[i] = rnd.random();
            r1[i] = rnd.random();
            r2[i] = rnd.random();
        }
        std::vector<int32_t> t0 = r0, t1 = r1, t2 = r2;
        forward_rct_line(r0.data(), r1.data(), r2.data(), w);
        forward_rct_line_avx2(t0.data(), t1.data(), t2.data(), w);
        ASSERT_EQ(t0, r0) << "w=" << w;
        ASSERT_EQ(t1, r1) << "w=" << w;
        ASSERT_EQ(t2, r2) << "w=" << w;
    }
}

TEST(avx2_kernels, gc_lossless_stats_gtli0) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {1, 7, 15, 31, 32, 33, 63, 64, 65, 131};

    for (uint32_t w : widths) {
        std::vector<uint8_t> data(w);
        for (uint32_t i = 0; i < w; ++i) {
            data[i] = static_cast<uint8_t>(rnd.random());
        }
        uint32_t r_sum = 0, t_sum = 0;
        uint16_t r_zero = 0, t_zero = 0;
        gc_lossless_stats_gtli0_c(data.data(), w, &r_sum, &r_zero);
        gc_lossless_stats_gtli0_avx2(data.data(), w, &t_sum, &t_zero);
        ASSERT_EQ(t_sum, r_sum) << "w=" << w;
        ASSERT_EQ(t_zero, r_zero) << "w=" << w;
    }
}

TEST(avx2_kernels, rc_lut_derive) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, 600);
    for (int iter = 0; iter < 50; ++iter) {
        uint16_t gc_r[TRUNCATION_MAX + 1], gc_t[TRUNCATION_MAX + 1];
        uint16_t sig_r[TRUNCATION_MAX + 1], sig_t[TRUNCATION_MAX + 1];
        uint32_t ns_r[TRUNCATION_MAX + 1], ns_t[TRUNCATION_MAX + 1];
        uint32_t s_r[TRUNCATION_MAX + 1], s_t[TRUNCATION_MAX + 1];

        for (uint32_t i = 0; i <= TRUNCATION_MAX; ++i) {
            gc_r[i] = gc_t[i] = static_cast<uint16_t>(rnd.random());
            sig_r[i] = sig_t[i] = static_cast<uint16_t>(rnd.random() & 0xFF);
        }
        rc_lut_derive_c(gc_r, ns_r, s_r, sig_r);
        rc_lut_derive_avx2(gc_t, ns_t, s_t, sig_t);
        ASSERT_EQ(0, memcmp(gc_t, gc_r, sizeof(gc_r)));
        ASSERT_EQ(0, memcmp(sig_t, sig_r, sizeof(sig_r)));
        ASSERT_EQ(0, memcmp(ns_t, ns_r, sizeof(ns_r)));
        ASSERT_EQ(0, memcmp(s_t, s_r, sizeof(s_r)));

        /* Also test null sign_tbl and sig_hist. */
        for (uint32_t i = 0; i <= TRUNCATION_MAX; ++i) {
            gc_r[i] = gc_t[i] = static_cast<uint16_t>(rnd.random());
        }
        rc_lut_derive_c(gc_r, ns_r, nullptr, nullptr);
        rc_lut_derive_avx2(gc_t, ns_t, nullptr, nullptr);
        ASSERT_EQ(0, memcmp(gc_t, gc_r, sizeof(gc_r)));
        ASSERT_EQ(0, memcmp(ns_t, ns_r, sizeof(ns_r)));
    }
}

TEST(avx2_kernels, rate_control_calc_vpred_cost_sigf_nosigf) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    setup_encoder_rtcd_internal(0);
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {1, 7, 8, 9, 15, 16, 17, 24, 25, 31, 32, 33, 71};

    for (uint32_t w : widths) {
        const uint32_t sig_w = (w + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
        std::vector<uint8_t> top(w), cur(w);
        for (uint32_t i = 0; i < w; ++i) {
            top[i] = static_cast<uint8_t>(rnd.random());
            cur[i] = static_cast<uint8_t>(rnd.random());
        }
        for (uint8_t hdr_Rm : {uint8_t(0), uint8_t(1)}) {
            for (uint8_t gtli : {uint8_t(0), uint8_t(4), uint8_t(12)}) {
                for (uint8_t gtli_max : {gtli, static_cast<uint8_t>(gtli + 2 <= 15 ? gtli + 2 : 15)}) {
                    std::vector<uint8_t> bits_r(w, 0xAA), bits_t(w, 0x55);
                    std::vector<uint8_t> sig_r(sig_w, 0xAA), sig_t(sig_w, 0x55);
                    uint32_t red_r = 0, red_t = 0, nosig_r = 0, nosig_t = 0;

                    rate_control_calc_vpred_cost_sigf_nosigf_c(sig_w,
                                                               w,
                                                               hdr_Rm,
                                                               SIGNIFICANCE_GROUP_SIZE,
                                                               top.data(),
                                                               cur.data(),
                                                               bits_r.data(),
                                                               sig_r.data(),
                                                               gtli,
                                                               gtli_max,
                                                               &red_r,
                                                               &nosig_r);
                    rate_control_calc_vpred_cost_sigf_nosigf_avx2(sig_w,
                                                                  w,
                                                                  hdr_Rm,
                                                                  SIGNIFICANCE_GROUP_SIZE,
                                                                  top.data(),
                                                                  cur.data(),
                                                                  bits_t.data(),
                                                                  sig_t.data(),
                                                                  gtli,
                                                                  gtli_max,
                                                                  &red_t,
                                                                  &nosig_t);
                    ASSERT_EQ(red_t, red_r) << "w=" << w << " Rm=" << (int)hdr_Rm << " gtli=" << (int)gtli;
                    ASSERT_EQ(nosig_t, nosig_r) << "w=" << w << " Rm=" << (int)hdr_Rm << " gtli=" << (int)gtli;
                    ASSERT_EQ(bits_t, bits_r) << "w=" << w << " Rm=" << (int)hdr_Rm << " gtli=" << (int)gtli;
                    ASSERT_EQ(sig_t, sig_r) << "w=" << w << " Rm=" << (int)hdr_Rm << " gtli=" << (int)gtli;
                }
            }
        }
    }
}

TEST(avx2_kernels, pack_significance) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {1, 7, 8, 9, 31, 32, 33, 63, 64, 65, 131};

    for (uint32_t w : widths) {
        std::vector<uint8_t> sig_max(w);
        for (uint32_t i = 0; i < w; ++i) {
            sig_max[i] = static_cast<uint8_t>(rnd.random());
        }
        for (uint8_t gtli : {uint8_t(0), uint8_t(4), uint8_t(15)}) {
            for (uint32_t pre_bits : {0u, 3u, 7u}) {
                for (bool tight_buf : {false, true}) {
                    const size_t buf_sz = tight_buf ? ((w + 7) / 8 + 2) : 256;
                    std::vector<uint8_t> ref_buf(buf_sz, 0), tst_buf(buf_sz, 0);
                    bitstream_writer_t bw_ref, bw_tst;
                    bitstream_writer_init(&bw_ref, ref_buf.data(), buf_sz);
                    bitstream_writer_init(&bw_tst, tst_buf.data(), buf_sz);
                    if (pre_bits) {
                        write_N_bits(&bw_ref, 0x5, pre_bits);
                        write_N_bits(&bw_tst, 0x5, pre_bits);
                    }
                    pack_significance_c(&bw_ref, gtli, sig_max.data(), w);
                    pack_significance_avx2(&bw_tst, gtli, sig_max.data(), w);
                    SCOPED_TRACE("w=" + std::to_string(w) + " gtli=" + std::to_string(gtli) +
                                 " tight=" + std::to_string(tight_buf));
                    expect_bitstream_eq_avx2(bw_tst, bw_ref);
                }
            }
        }
    }
}

TEST(avx2_kernels, pack_bitplane_count_significance_and_nosig) {
    if (!has_avx2_popcnt()) {
        GTEST_SKIP();
    }
    svt_jxs_test_tool::SVTRandom rnd(0, TRUNCATION_MAX);
    const uint32_t widths[] = {1, 7, 8, 9, 31, 32, 33, 64, 71, 131, 256, 263, 512, 523};

    for (uint32_t w : widths) {
        const uint32_t sig_w = (w + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
        std::vector<uint8_t> bp(w);
        std::vector<uint8_t> sig_max(sig_w, 0);

        for (int pattern = 0; pattern < 3; ++pattern) {
            for (uint32_t i = 0; i < w; ++i) {
                if (pattern == 0) {
                    bp[i] = 0;
                }
                else if (pattern == 1) {
                    bp[i] = TRUNCATION_MAX;
                }
                else {
                    bp[i] = static_cast<uint8_t>(rnd.random());
                }
            }
            gc_precinct_sigflags_max_c(sig_max.data(), bp.data(), SIGNIFICANCE_GROUP_SIZE, w);

            for (int8_t gtli : {int8_t(0), int8_t(3), int8_t(12)}) {
                for (bool tight_buf : {false, true}) {
                    const size_t buf_sz = tight_buf ? (w * 2 + 4) : (w * 4 + 64);
                    std::vector<uint8_t> ref_buf(buf_sz, 0), tst_buf(buf_sz, 0);
                    bitstream_writer_t bw_ref, bw_tst;

                    bitstream_writer_init(&bw_ref, ref_buf.data(), buf_sz);
                    bitstream_writer_init(&bw_tst, tst_buf.data(), buf_sz);
                    write_N_bits(&bw_ref, 0x3, 3);
                    write_N_bits(&bw_tst, 0x3, 3);
                    pack_bitplane_count_no_significance_c(&bw_ref, bp.data(), w, gtli);
                    pack_bitplane_count_no_significance_avx2(&bw_tst, bp.data(), w, gtli);
                    {
                        SCOPED_TRACE("nosig w=" + std::to_string(w) + " pat=" + std::to_string(pattern) +
                                     " gtli=" + std::to_string(gtli));
                        expect_bitstream_eq_avx2(bw_tst, bw_ref);
                    }

                    std::fill(ref_buf.begin(), ref_buf.end(), 0);
                    std::fill(tst_buf.begin(), tst_buf.end(), 0);
                    bitstream_writer_init(&bw_ref, ref_buf.data(), buf_sz);
                    bitstream_writer_init(&bw_tst, tst_buf.data(), buf_sz);
                    write_N_bits(&bw_ref, 0x5, 5);
                    write_N_bits(&bw_tst, 0x5, 5);
                    pack_bitplane_count_significance_c(&bw_ref, bp.data(), w, gtli, sig_max.data(), SIGNIFICANCE_GROUP_SIZE);
                    pack_bitplane_count_significance_avx2(&bw_tst, bp.data(), w, gtli, sig_max.data(), SIGNIFICANCE_GROUP_SIZE);
                    {
                        SCOPED_TRACE("sig w=" + std::to_string(w) + " pat=" + std::to_string(pattern) +
                                     " gtli=" + std::to_string(gtli));
                        expect_bitstream_eq_avx2(bw_tst, bw_ref);
                    }
                }
            }
        }
    }
}

#endif /* ARCH_X86_64 */
