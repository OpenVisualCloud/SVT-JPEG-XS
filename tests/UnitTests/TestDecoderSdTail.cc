/*
* Copyright(c) 2026 Imazen
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "Decoder.h"
#include "decoder_dsp_rtcd.h"

namespace {
struct SdTailCase {
    uint32_t vertical;
    uint32_t height;
    uint8_t depth;
};

std::vector<SdTailCase> sd_tail_cases() {
    std::vector<SdTailCase> cases;
    for (uint32_t vertical = 0; vertical <= 2; ++vertical) {
        for (uint32_t height : {4u, 5u, 6u, 7u, 8u, 9u}) {
            for (uint8_t depth : {8, 12}) {
                cases.push_back({vertical, height, depth});
            }
        }
    }
    return cases;
}

class DecoderSdTail : public ::testing::TestWithParam<SdTailCase> {};

TEST_P(DecoderSdTail, ReconstructsEveryRowAndPreservesStridePadding) {
    const SdTailCase param = GetParam();
    SCOPED_TRACE(::testing::Message() << "vertical=" << param.vertical << " height=" << param.height
                                      << " depth=" << (unsigned)param.depth);
    const uint32_t width = 8;
    const uint32_t stride = width + 3;
    setup_common_rtcd_internal(CPU_FLAGS_C);
    setup_decoder_rtcd_internal(CPU_FLAGS_C);

    svt_jpeg_xs_decoder_common_t common = {};
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 1, 1};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1};
    ASSERT_EQ(SvtJxsErrorNone,
              pi_compute(&common.pi,
                         0,
                         3,
                         4,
                         8,
                         width,
                         param.height,
                         std::max(1u, param.vertical),
                         param.vertical,
                         1,
                         sx,
                         sy,
                         0,
                         param.height));
    common.picture_header_const.hdr_Cpih = 1;
    for (uint32_t c = 0; c < 3; ++c) {
        common.picture_header_const.hdr_bit_depth[c] = param.depth;
    }
    // Sd=1 leaves only two decomposed components, so RCT runs on the whole frame.
    ASSERT_FALSE(common.rct_per_precinct);
    std::array<std::vector<int32_t>, 3> frame;
    for (uint32_t c = 0; c < 3; ++c) {
        // Deterministic poison makes omitted rows fail even when malloc returns zeroed pages.
        frame[c].assign(width * param.height, 512);
        common.buffer_tmp_cpih[c] = frame[c].data();
    }
    std::unique_ptr<svt_jpeg_xs_decoder_instance_t, decltype(&svt_jpeg_xs_dec_instance_free)> instance(
        svt_jpeg_xs_dec_instance_alloc(&common), svt_jpeg_xs_dec_instance_free);
    ASSERT_NE(nullptr, instance.get());
    instance->picture_header_dynamic.hdr_Bw = param.depth;
    // All transformed coefficients are zero. The non-decomposed Cr plane is 64 everywhere.
    const pi_component_t& sd = common.pi.components[2];
    for (uint32_t p = 0; p < common.pi.precincts_line_num; ++p) {
        int16_t* coefficients = instance->coeff_buff_ptr_16bit + p * instance->precincts_line_coeff_size +
            instance->precincts_line_coeff_comp_offset[2];
        std::fill_n(coefficients, sd.bands[0].width * sd.bands[0].height_lines_num, (int16_t)64);
    }

    std::array<std::vector<uint8_t>, 3> bytes;
    std::array<std::vector<uint16_t>, 3> words;
    svt_jpeg_xs_image_buffer_t output = {};
    for (uint32_t c = 0; c < 3; ++c) {
        bytes[c].assign(stride * param.height, 0xa5);
        words[c].assign(stride * param.height, 0xa5a5);
        output.data_yuv[c] = param.depth == 8 ? (void*)bytes[c].data() : (void*)words[c].data();
        output.stride[c] = stride;
        output.alloc_size[c] = stride * param.height * (param.depth == 8 ? 1 : 2);
    }
    ASSERT_EQ(SvtJxsErrorNone, svt_jpeg_xs_decode_final(instance.get(), &output));
    // Inverse RCT maps (Y,Cb,Cr)=(0,0,64) to (R,G,B)=(48,-16,-16), before the output bias.
    const int32_t bias = 1 << (param.depth - 1);
    const int32_t expected[3] = {bias + 48, bias - 16, bias - 16};
    for (uint32_t c = 0; c < 3; ++c) {
        for (uint32_t y = 0; y < param.height; ++y) {
            for (uint32_t x = 0; x < stride; ++x) {
                const uint32_t actual = param.depth == 8 ? bytes[c][y * stride + x] : words[c][y * stride + x];
                const uint32_t want = x < width ? (uint32_t)expected[c] : (param.depth == 8 ? 0xa5u : 0xa5a5u);
                ASSERT_EQ(want, actual) << "component=" << c << " x=" << x << " y=" << y;
            }
        }
    }
}

TEST_P(DecoderSdTail, WithoutColourTransformWritesEveryRow) {
    const SdTailCase param = GetParam();
    SCOPED_TRACE(::testing::Message() << "vertical=" << param.vertical << " height=" << param.height
                                      << " depth=" << (unsigned)param.depth);
    const uint32_t width = 8;
    const uint32_t stride = width + 3;
    setup_common_rtcd_internal(CPU_FLAGS_C);
    setup_decoder_rtcd_internal(CPU_FLAGS_C);

    svt_jpeg_xs_decoder_common_t common = {};
    uint32_t sx[MAX_COMPONENTS_NUM] = {1, 1, 1};
    uint32_t sy[MAX_COMPONENTS_NUM] = {1, 1, 1};
    ASSERT_EQ(SvtJxsErrorNone,
              pi_compute(&common.pi,
                         0,
                         3,
                         4,
                         8,
                         width,
                         param.height,
                         std::max(1u, param.vertical),
                         param.vertical,
                         1,
                         sx,
                         sy,
                         0,
                         param.height));
    common.picture_header_const.hdr_bit_depth[0] = param.depth;
    std::unique_ptr<svt_jpeg_xs_decoder_instance_t, decltype(&svt_jpeg_xs_dec_instance_free)> instance(
        svt_jpeg_xs_dec_instance_alloc(&common), svt_jpeg_xs_dec_instance_free);
    ASSERT_NE(nullptr, instance.get());
    instance->picture_header_dynamic.hdr_Bw = param.depth;
    const pi_component_t& sd = common.pi.components[2];
    for (uint32_t p = 0; p < common.pi.precincts_line_num; ++p) {
        int16_t* coefficients = instance->coeff_buff_ptr_16bit + p * instance->precincts_line_coeff_size +
            instance->precincts_line_coeff_comp_offset[2];
        for (uint32_t row = 0; row < sd.precinct_height && p * sd.precinct_height + row < sd.height; ++row) {
            for (uint32_t x = 0; x < width; ++x) {
                coefficients[row * width + x] = (int16_t)((int32_t)(p * sd.precinct_height + row) * 7 + (int32_t)x - 16);
            }
        }
    }
    std::array<std::vector<uint8_t>, 3> bytes;
    std::array<std::vector<uint16_t>, 3> words;
    svt_jpeg_xs_image_buffer_t output = {};
    for (uint32_t c = 0; c < 3; ++c) {
        bytes[c].assign(stride * param.height, 0xa5);
        words[c].assign(stride * param.height, 0xa5a5);
        output.data_yuv[c] = param.depth == 8 ? (void*)bytes[c].data() : (void*)words[c].data();
        output.stride[c] = stride;
        output.alloc_size[c] = stride * param.height * (param.depth == 8 ? 1 : 2);
    }
    ASSERT_EQ(SvtJxsErrorNone, svt_jpeg_xs_decode_final(instance.get(), &output));
    const uint32_t bias = 1u << (param.depth - 1);
    for (uint32_t y = 0; y < param.height; ++y) {
        for (uint32_t x = 0; x < stride; ++x) {
            const uint32_t actual = param.depth == 8 ? bytes[2][y * stride + x] : words[2][y * stride + x];
            const uint32_t want = x < width ? bias + y * 7 + x - 16 : (param.depth == 8 ? 0xa5u : 0xa5a5u);
            ASSERT_EQ(want, actual) << "x=" << x << " y=" << y;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(FullAndPartialPrecincts, DecoderSdTail, ::testing::ValuesIn(sd_tail_cases()));
} // namespace
