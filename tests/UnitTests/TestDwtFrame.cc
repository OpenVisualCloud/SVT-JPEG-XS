/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "Enc_avx512.h"
#include "Dwt53Decoder_AVX2.h"
#include "SvtUtility.h"
#include "NltEnc.h"
#include "Pi.h"
#include "encoder_dsp_rtcd.h"
#include "Dwt.h"
#include "CodeDeprecated.h"
#include "WeightTable.h"

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

//Default values:
#define WAVELET_IN_DEPTH_BW_DEFAULT      20
#define WAVELET_FRACTION_BITS_FQ_DEFAULT 8

/* REFERENCE C CODE TO TESTS BEGIN */
void reference_transform_component(
    const pi_t* pi, uint32_t component_id, const int32_t* plane_buffer_in, uint8_t input_bit_depth, uint32_t plane_width,
    uint32_t plane_height, uint16_t plane_stride, uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit,
    uint32_t reference_coeff_buff_tmp_pos_offset_32bit_frame[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM]);
/* REFERENCE C CODE TO TESTS END */

typedef ::testing::tuple<uint32_t, uint32_t> size_param_t;

// clang-format off
static size_param_t params_block_sizes[] = {
    {200, 200},
    {201, 201},
    {2, 2}, {2, 11}, {3, 3}, {4, 4}, {5, 5},
    {6, 6}, {7, 7}, {8, 8}, {9, 9}, {10, 10},
    {11, 11}, {12, 12}, {13, 13}, {14, 14}, {15, 15},
    {16, 16}, {17, 17}, {18, 18}, {19, 19}, {20, 20},
    {1920, 1080},
    /* Test to not squares */
    {30, 60}, {240, 120},
    //{270,480}, {960, 540},
    //{3840, 2160},
};
// clang-format on

/*Vertical, Horizontal*/
typedef ::testing::tuple<uint32_t, uint32_t> format_param_t;

// clang-format off
static format_param_t format_params[] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5},
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5},
    {2, 2}, {2, 3}, {2, 4}, {2, 5},
};
// clang-format on

/*                input_bit_depth, Size(WxH), (vert, hor)*/
typedef ::testing::tuple<uint8_t, size_param_t, format_param_t> fixture_param_t;

void transform_component_test_V0(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit, uint8_t convert_input_per_slice);

void transform_component_test_V1(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit, uint8_t convert_input_per_slice,
                                 uint8_t calculate_separate_precinct);

void transform_component_test_V2(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit, uint8_t convert_input_per_slice,
                                 uint8_t calculate_separate_precinct);

class FRAME_DWT : public ::testing::TestWithParam<fixture_param_t> {
  protected:
    /*Parameters*/
    uint8_t param_Bw;
    uint8_t param_Fq;
    uint8_t input_bit_depth;
    uint32_t width;
    uint32_t height;

    /*Variables*/
    svt_jxs_test_tool::SVTRandom* rnd;
    pi_t pi;
    pi_enc_t pi_enc;

    /*Buffers*/
    uint8_t bits_per_pixel;
    uint32_t stride_input;
    uint32_t lenght;
    uint32_t buffer_out_len_16bit;
    int32_t* buffer_in_yuv;          //Buffer in for component
    uint16_t* buffer_out_ref_16bit;  //Buffer out for component
    uint16_t* buffer_out_test_16bit; //Buffer out for component
    uint8_t skip_test;

    uint32_t reference_coeff_buff_tmp_pos_offset_32bit_frame[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM];
    uint32_t reference_coeff_buff_tmp_size_32bit_frame[MAX_COMPONENTS_NUM];

  protected:
    virtual void SetUp() {
        param_Bw = WAVELET_IN_DEPTH_BW_DEFAULT;
        param_Fq = WAVELET_FRACTION_BITS_FQ_DEFAULT;

        rnd = new svt_jxs_test_tool::SVTRandom(32, false);
        fixture_param_t params = GetParam();
        input_bit_depth = std::get<0>(params);
        size_param_t param_size = std::get<1>(params);
        width = std::get<0>(param_size);
        height = std::get<1>(param_size);
        format_param_t format_param = std::get<2>(params);
        int32_t decomp_v = std::get<0>(format_param);
        int32_t decomp_h = std::get<1>(format_param);

        stride_input = width + (rand() % 1024);
        printf("Bits: %i (%ix%i) V:%i H:%i\n", input_bit_depth, width, height, decomp_v, decomp_h);
        bits_per_pixel = (input_bit_depth + 7) / 8;
        lenght = stride_input * height; //w*h
        buffer_out_len_16bit = lenght * sizeof(int16_t);
        buffer_in_yuv = (int32_t*)malloc(lenght * bits_per_pixel);
        buffer_out_ref_16bit = (uint16_t*)malloc(buffer_out_len_16bit);
        buffer_out_test_16bit = NULL; // (uint16_t*)malloc(buffer_out_len_16bit);

        uint32_t sx[MAX_COMPONENTS_NUM];
        uint32_t sy[MAX_COMPONENTS_NUM];
        uint32_t num_comp = 0;

        format_get_sampling_factory(COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, &num_comp, sx, sy, VERBOSE_INFO_FULL);

        skip_test = 0;

        int ret = pi_compute(&pi,
                             0 /*ignore Init encoder*/,
                             num_comp,
                             GROUP_SIZE,
                             SIGNIFICANCE_GROUP_SIZE,
                             width,
                             height,
                             decomp_h,
                             decomp_v,
                             0,
                             sx,
                             sy,
                             0 /*Cw*/,
                             16 /*slice_height*/);
        if (ret) {
            //GTEST_SKIP() << "Skipping all tests for non support configuration";
            skip_test = 1;
            return;
        }

        ret = weight_table_calculate(&pi, 0, COLOUR_FORMAT_PACKED_YUV444_OR_RGB);
        if (ret) {
            //GTEST_SKIP() << "Skipping all tests for non support configuration";
            skip_test = 1;
            return;
        }

        ret = pi_compute_encoder(&pi, &pi_enc, 1, 1, 0);
        if (ret) {
            //GTEST_SKIP() << "Skipping all tests for non support configuration";
            skip_test = 1;
            return;
        }

        /*RECALCUATE REFERENCE BUFFERS FOR 32bit COEFFICIENTS*/
        uint32_t offset;
        for (uint32_t c = 0; c < pi.comps_num; ++c) {
            offset = 0;
            for (uint32_t b = 0; b < pi.components[c].bands_num; ++b) {
                uint32_t w = pi.components[c].bands[b].width;
                uint32_t h = pi.components[c].bands[b].height;
                reference_coeff_buff_tmp_pos_offset_32bit_frame[c][b] = offset;
                offset += w * h;
            }
            reference_coeff_buff_tmp_size_32bit_frame[c] = offset;
        }

        //buffer_out_test_16bit = (uint16_t*)malloc(buffer_out_len_16bit);
        //ASSERT_TRUE((pi.precincts_line_num * pi_enc.coeff_buff_tmp_size_precinct[0] * sizeof(int16_t)) >= buffer_out_len_16bit);
        buffer_out_test_16bit = (uint16_t*)malloc(pi.precincts_line_num * pi_enc.coeff_buff_tmp_size_precinct[0] *
                                                  sizeof(int16_t));
    }

    virtual void TearDown() {
        delete rnd;
        free(buffer_in_yuv);
        free(buffer_out_ref_16bit);
        free(buffer_out_test_16bit);
    }

    void set_random_image() {
        ASSERT_TRUE((bits_per_pixel == 1) || (bits_per_pixel == 2));
        if (input_bit_depth <= 8) {
            uint8_t* buff = (uint8_t*)buffer_in_yuv;
            for (uint32_t i = 0; i < lenght; i++) {
                buff[i] = rnd->random() & 0xFF;
            }
        }
        else {
            uint16_t* buff = (uint16_t*)buffer_in_yuv;
            uint32_t mask = (((uint32_t)1) << input_bit_depth) - 1;
            for (uint32_t i = 0; i < lenght; i++) {
                buff[i] = rnd->random() & mask;
            }
        }
    }

    void reference_shift_and_dwt() {
        uint32_t component_id = 0;
        for (int i = 0; i < 1; ++i) {
            memset(buffer_out_ref_16bit, 0, sizeof(*buffer_out_ref_16bit) * width * height);
            reference_transform_component(&pi,
                                          component_id,
                                          buffer_in_yuv,
                                          input_bit_depth,
                                          width,
                                          height,
                                          stride_input,
                                          param_Bw,
                                          param_Fq,
                                          buffer_out_ref_16bit,
                                          reference_coeff_buff_tmp_pos_offset_32bit_frame);
        }
    }

    void compare_dwt_results() {
        /*
        * Compare result of bands between memory organizes PER FRAME vs PER PRECINCT
        */
        int new_compare_frame_precicnt_fail = 0;
        for (uint32_t band = 0; band < pi.components[0].bands_num; ++band) {
            uint16_t* buffer_out_ref_16bit_local = buffer_out_ref_16bit +
                reference_coeff_buff_tmp_pos_offset_32bit_frame[0][band];
            uint16_t* buffer_out_test_16bit_local = buffer_out_test_16bit +
                pi_enc.components[0].bands[band].coeff_buff_tmp_pos_offset_16bit;
            for (uint32_t line = 0; line < pi.components[0].bands[band].height; ++line) {
                for (uint32_t x = 0; x < pi.components[0].bands[band].width; ++x) {
                    if (buffer_out_ref_16bit_local[x] != buffer_out_test_16bit_local[x]) {
                        new_compare_frame_precicnt_fail = 1;
                        printf("Diff (band=%u line=%u, x=%u) val: %u %u\n",
                               band,
                               line,
                               x,
                               buffer_out_ref_16bit_local[x],
                               buffer_out_test_16bit_local[x]);
                        ASSERT_TRUE(0);
                        break;
                    }
                }
                buffer_out_ref_16bit_local += pi.components[0].bands[band].width;

                if (pi.components[0].bands[band].height_lines_num == 1) {
                    buffer_out_test_16bit_local += pi_enc.coeff_buff_tmp_size_precinct[0];
                }
                else {
                    if ((line % pi.components[0].bands[band].height_lines_num) == 0) {
                        buffer_out_test_16bit_local += pi.components[0].bands[band].width;
                    }
                    else {
                        buffer_out_test_16bit_local += pi_enc.coeff_buff_tmp_size_precinct[0] -
                            pi.components[0].bands[band].width;
                        ;
                    }
                }
            }
        }

        /*
        * Compare OLD (when previous find error) result of bands memory organizes PER FRAME
        */
        ASSERT_TRUE(new_compare_frame_precicnt_fail == 0);
        //if (new_compare_frame_precicnt_fail)
        //{
        //    ASSERT_EQ(0, memcmp(buffer_out_ref_16bit, buffer_out_test_16bit, sizeof(*buffer_out_ref_16bit) * width * height));
        //}
    }

    void test_transform_result() {
        if (skip_test) {
            printf("Skipping tests for non support configuration.\n");
            return;
        }

        uint32_t component_id = 0;
        for (int i = 0; i < 1 /*5*/; ++i) {
            set_random_image();
            reference_shift_and_dwt();
            for (uint8_t convert_input_per_slice = 0; convert_input_per_slice < 2; ++convert_input_per_slice) {
                for (uint8_t calculate_separate_precinct = 0; calculate_separate_precinct < (!!pi.decom_v) + 1;
                     ++calculate_separate_precinct) {
                    /*decomp_v == 0 ignore: calculate_separate_precinct*/
                    memset(buffer_out_test_16bit, 0, sizeof(*buffer_out_test_16bit) * width * height);

                    switch (pi.components[component_id].decom_v) {
                    case 0: {
                        transform_component_test_V0(&pi,
                                                    &pi_enc,
                                                    component_id,
                                                    buffer_in_yuv,
                                                    input_bit_depth,
                                                    width,
                                                    height,
                                                    stride_input,
                                                    param_Bw,
                                                    param_Fq,
                                                    buffer_out_test_16bit,
                                                    convert_input_per_slice);
                        break;
                    }
                    case 1: {
                        transform_component_test_V1(&pi,
                                                    &pi_enc,
                                                    component_id,
                                                    buffer_in_yuv,
                                                    input_bit_depth,
                                                    width,
                                                    height,
                                                    stride_input,
                                                    param_Bw,
                                                    param_Fq,
                                                    buffer_out_test_16bit,
                                                    convert_input_per_slice,
                                                    calculate_separate_precinct);
                        break;
                    }
                    case 2: {
                        transform_component_test_V2(&pi,
                                                    &pi_enc,
                                                    component_id,
                                                    buffer_in_yuv,
                                                    input_bit_depth,
                                                    width,
                                                    height,
                                                    stride_input,
                                                    param_Bw,
                                                    param_Fq,
                                                    buffer_out_test_16bit,
                                                    convert_input_per_slice,
                                                    calculate_separate_precinct);
                        break;
                    }
                    default:
                        ASSERT_TRUE(0);
                    }
                    compare_dwt_results();
                }
            }
        }
    }

    void run_test_transform_c() {
        setup_encoder_rtcd_internal(0);
        setup_depricated_test_rtcd_internal(0);
        test_transform_result();
    }

    void run_test_transform_avx2() {
        setup_encoder_rtcd_internal(CPU_FLAGS_AVX2);
        setup_depricated_test_rtcd_internal(CPU_FLAGS_AVX2);
        test_transform_result();
    }

    void run_test_transform_avx512() {
        setup_encoder_rtcd_internal(CPU_FLAGS_ALL);
        setup_depricated_test_rtcd_internal(CPU_FLAGS_ALL);
        test_transform_result();
    }
};

TEST_P(FRAME_DWT, TRANSFORM_FRAME_C) {
    run_test_transform_c();
}

TEST_P(FRAME_DWT, TRANSFORM_FRAME_AVX2) {
    run_test_transform_avx2();
}

TEST_P(FRAME_DWT, TRANSFORM_FRAME_AVX512) {
    if (!(CPU_FLAGS_AVX512F & get_cpu_flags())) {
        //GTEST_SKIP() << "Skipping all tests for AVX512";
        return;
    }
    run_test_transform_avx512();
}

INSTANTIATE_TEST_SUITE_P(FRAME_DWT, FRAME_DWT,
                         ::testing::Combine(::testing::Values(8, 10), ::testing::ValuesIn(params_block_sizes),
                                            ::testing::ValuesIn(format_params)));

static int32_t* convert_input_buffer_alloc(const int32_t* plane_buffer_in, uint8_t param_Bw, uint32_t width, uint32_t height,
                                           uint32_t stride, uint32_t input_bit_depth) {
    int32_t* buffers_input_unpack = (int32_t*)malloc(width * height * sizeof(int32_t));
    if (input_bit_depth <= 8) {
        linear_input_scaling_8bit_depricated(
            param_Bw, (uint8_t*)plane_buffer_in, buffers_input_unpack, width, height, stride, width, input_bit_depth);
    }
    else {
        linear_input_scaling_16bit_depricated(
            param_Bw, (uint16_t*)plane_buffer_in, buffers_input_unpack, width, height, stride, width, input_bit_depth);
    }
    return buffers_input_unpack;
}

void transform_component_test_V0(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit,
                                 uint8_t convert_input_per_slice) {
    const pi_component_t* const component = &(pi->components[component_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[component_id]);
    int decom_h = component->decom_h;
    int decom_v = component->decom_v;

    picture_header_dynamic_t hdr_dynamic;
    hdr_dynamic.hdr_Tnlt = 0; //linear
    hdr_dynamic.hdr_Bw = param_Bw;
    hdr_dynamic.hdr_Fq = param_Fq;
    //Transformation precision from input buffer to temporary buffers_tmp buffer
    uint8_t bit_depth = input_bit_depth;
    int32_t* buffers_input_unpack = NULL;
    if (convert_input_per_slice == 0) {
        buffers_input_unpack = convert_input_buffer_alloc(
            plane_buffer_in, param_Bw, plane_width, plane_height, plane_stride, input_bit_depth);
        bit_depth = 0; // Inform to not convert input
    }
    ASSERT_TRUE(decom_v <= decom_h);
    ASSERT_TRUE(decom_v <= 2);
    ASSERT_TRUE(decom_v == 0);

    int32_t* buffers_tmp_horizontal = (int32_t*)malloc((pi->width * 3 / 2) * sizeof(int32_t));

    transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(decom_h);
    ASSERT_TRUE(transform_V0_Hn != NULL);

    for (uint32_t line = 0; line < plane_height; line += 1) {
        /*Add offset to buffers*/
        uint16_t* buffer_out_16bit_offset = buffer_out_16bit + line * pi_enc->coeff_buff_tmp_size_precinct[component_id];
        const int32_t* plane_buffer_in_offset;
        if (bit_depth == 0) {
            //Not convert input
            plane_buffer_in_offset = buffers_input_unpack + line * plane_width;
        }
        else if (bit_depth <= 8) {
            plane_buffer_in_offset = (int32_t*)(((uint8_t*)plane_buffer_in) + line * plane_stride);
        }
        else {
            plane_buffer_in_offset = (int32_t*)(((uint16_t*)plane_buffer_in) + line * plane_stride);
        }
        //transform_V0_H1, transform_V0_H2, transform_V0_H3, transform_V0_H4, transform_V0_H5
        transform_V0_Hn(component,
                        component_enc,
                        plane_buffer_in_offset,
                        bit_depth,
                        &hdr_dynamic,
                        buffer_out_16bit_offset,
                        param_Fq,
                        plane_width,
                        buffers_tmp_horizontal);
    }
    free(buffers_tmp_horizontal);
    free(buffers_input_unpack);
}

void transform_component_test_V1(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit, uint8_t convert_input_per_slice,
                                 uint8_t calculate_separate_precinct) {
    const pi_component_t* const component = &(pi->components[component_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[component_id]);
    int decom_h = component->decom_h;
    int decom_v = component->decom_v;
    ASSERT_TRUE(decom_v <= decom_h);
    ASSERT_TRUE(decom_v <= 2);
    ASSERT_TRUE(decom_v == 1);

    int32_t* buffers_tmp = (int32_t*)malloc((pi->width * 9 / 2) * sizeof(int32_t)); //Buffer TEMP size: (4.5 * width)

    if (convert_input_per_slice == 0) {
        //Transformation precision from input buffer to temporary buffers_tmp buffer
        int32_t* buffers_input_unpack = convert_input_buffer_alloc(
            plane_buffer_in, param_Bw, plane_width, plane_height, plane_stride, input_bit_depth);

        uint32_t precinct_offset = pi_enc->coeff_buff_tmp_size_precinct[component_id];
        transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(decom_h);

        int32_t* in_tmp_line_HF_prev = buffers_tmp;
        int32_t* out_tmp_line_HF_next = buffers_tmp + plane_width;
        int32_t* buffer_tmp = buffers_tmp + 2 * plane_width;

        const int32_t* line_0; //Line previous precinct
        const int32_t* line_1; //Line 0
        const int32_t* line_2; //Line 1

        for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 2) {
            if (calculate_separate_precinct) {
                //Recalculate overhead between precincts
                memset(in_tmp_line_HF_prev, 0, plane_width * sizeof(*in_tmp_line_HF_prev));
                if (line_idx > 0) {
                    //Precalculate previous line
                    line_0 = buffers_input_unpack + (line_idx - 2) * plane_width; //Line 0
                    line_1 = line_0 + plane_width;
                    line_2 = line_1 + plane_width;
                    transform_V1_Hx_precinct_recalc_HF_prev(plane_width, in_tmp_line_HF_prev, line_0, line_1, line_2);
                }
            }

            line_0 = buffers_input_unpack + line_idx * plane_width; //Line 0
            line_1 = line_0 + plane_width;
            line_2 = line_1 + plane_width;

            transform_V1_Hx_precinct(component,
                                     component_enc,
                                     transform_V0_Hn,
                                     decom_h,
                                     line_idx,
                                     plane_width,
                                     plane_height,
                                     line_0,
                                     line_1,
                                     line_2,
                                     buffer_out_16bit + (line_idx / 2) * precinct_offset,
                                     param_Fq,
                                     in_tmp_line_HF_prev,
                                     out_tmp_line_HF_next,
                                     buffer_tmp);

            /*Swap lines*/
            int32_t* tmp = in_tmp_line_HF_prev;
            in_tmp_line_HF_prev = out_tmp_line_HF_next;
            out_tmp_line_HF_next = tmp;
        }
        free(buffers_input_unpack);
    }
    else {
        uint8_t line_begin = 0;
        int32_t* line_x[3];
        line_x[0] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[1] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[2] = (int32_t*)malloc(pi->width * sizeof(int32_t));

        picture_header_dynamic_t hdr_dynamic;
        hdr_dynamic.hdr_Tnlt = 0; //linear
        hdr_dynamic.hdr_Bw = param_Bw;
        hdr_dynamic.hdr_Fq = param_Fq;

        uint32_t precinct_offset = pi_enc->coeff_buff_tmp_size_precinct[component_id];
        transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(decom_h);

        int32_t* in_tmp_line_HF_prev = buffers_tmp;
        int32_t* out_tmp_line_HF_next = buffers_tmp + plane_width;
        int32_t* buffer_tmp = buffers_tmp + 2 * plane_width;
        const int8_t* plane_buffer_in_8 = (int8_t*)plane_buffer_in;
        const uint32_t pixel_size = input_bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);

        //Read first line on last position:
        line_begin = 2;

        nlt_input_scaling_line(plane_buffer_in_8, line_x[(line_begin + 0) % 3], plane_width, &hdr_dynamic, input_bit_depth);
        line_begin = 0; //First line is on last position to reuse

        for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 2) {
            if (calculate_separate_precinct) {
                //Recalculate overhead between precincts
                memset(in_tmp_line_HF_prev, 0, plane_width * sizeof(*in_tmp_line_HF_prev));
                if (line_idx > 0) {
                    //Precalculate previous line
                    transform_V1_Hx_precinct_recalc_HF_prev(plane_width,
                                                            in_tmp_line_HF_prev,
                                                            line_x[(line_begin + 0) % 3],
                                                            line_x[(line_begin + 1) % 3],
                                                            line_x[(line_begin + 2) % 3]);
                }
            }

            //Read next 2 lines and reuse last one as first
            line_begin = (line_begin + 2) % 3;

            if ((line_idx + 1) < plane_height) {
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 1) * plane_stride,
                                       line_x[(line_begin + 1) % 3],
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
            }
            if ((line_idx + 2) < plane_height) {
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 2) * plane_stride,
                                       line_x[(line_begin + 2) % 3],
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
            }
            transform_V1_Hx_precinct(component,
                                     component_enc,
                                     transform_V0_Hn,
                                     decom_h,
                                     line_idx,
                                     plane_width,
                                     plane_height,
                                     line_x[(line_begin + 0) % 3],
                                     line_x[(line_begin + 1) % 3],
                                     line_x[(line_begin + 2) % 3],
                                     buffer_out_16bit + (line_idx / 2) * precinct_offset,
                                     param_Fq,
                                     in_tmp_line_HF_prev,
                                     out_tmp_line_HF_next,
                                     buffer_tmp);

            /*Swap lines*/
            int32_t* tmp = in_tmp_line_HF_prev;
            in_tmp_line_HF_prev = out_tmp_line_HF_next;
            out_tmp_line_HF_next = tmp;
        }
        free(line_x[0]);
        free(line_x[1]);
        free(line_x[2]);
    }
    free(buffers_tmp);
}

void transform_component_test_V2(const pi_t* pi, const pi_enc_t* pi_enc, uint32_t component_id, const int32_t* plane_buffer_in,
                                 uint8_t input_bit_depth, uint32_t plane_width, uint32_t plane_height, uint16_t plane_stride,
                                 uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit, uint8_t convert_input_per_slice,
                                 uint8_t calculate_separate_precinct) {
    const pi_component_t* const component = &(pi->components[component_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[component_id]);
    int decom_h = component->decom_h;
    int decom_v = component->decom_v;

    ASSERT_TRUE(decom_v <= decom_h);
    ASSERT_TRUE(decom_v == 2);

    transform_V0_ptr_t transform_V0_Hn_sub_1 = transform_V0_get_function_ptr(decom_h - 1);
    uint32_t precinct_offset = pi_enc->coeff_buff_tmp_size_precinct[component_id];

    int32_t* buffer_tmp = (int32_t*)malloc((5 * pi->width + 1) * sizeof(int32_t));      //Temporary Size:  (5 * width + 1)
    int32_t* buffer_on_place = (int32_t*)malloc((3 * pi->width / 2) * sizeof(int32_t)); //Temporary Size:  3*width/2
    int32_t* buffer_prev = (int32_t*)malloc((pi->width + 1) * sizeof(int32_t));         //Temporary Size:  3*width/2
    int32_t* buffer_next = (int32_t*)malloc((pi->width + 1) * sizeof(int32_t));         //Temporary Size:  3*width/2

    if (convert_input_per_slice == 0) {
        //Transformation precision from input buffer to temporary buffers_tmp buffer
        int32_t* buffers_input_unpack = convert_input_buffer_alloc(
            plane_buffer_in, param_Bw, plane_width, plane_height, plane_stride, input_bit_depth);

        if (!calculate_separate_precinct) {
            transform_V2_Hx_precinct_recalc_prec_0(plane_width,
                                                   buffers_input_unpack + (0 + 0) * plane_width,
                                                   buffers_input_unpack + (1 + 0) * plane_width,
                                                   buffers_input_unpack + (2 + 0) * plane_width,
                                                   buffer_prev,
                                                   buffer_on_place,
                                                   buffer_tmp);
        }

        for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 4) {
            memset(buffer_tmp, 0, (5 * pi->width + 1) * sizeof(int32_t)); //Invalidate buffers to test
            memset(buffer_next, 0, (plane_width + 1) * sizeof(int32_t));  //Invalidate buffers to test

            if (calculate_separate_precinct) {
                //Recalculate overhead between precincts
                memset(buffer_prev, 0, (plane_width + 1) * sizeof(int32_t));
                memset(buffer_on_place, 0, (3 * plane_width / 2) * sizeof(int32_t));

                transform_V2_Hx_precinct_recalc(component,
                                                decom_h,
                                                line_idx,
                                                plane_width,
                                                plane_height,
                                                buffers_input_unpack + (line_idx - 6) * plane_width, //-4
                                                buffers_input_unpack + (line_idx - 5) * plane_width, //-4
                                                buffers_input_unpack + (line_idx - 4) * plane_width, //-4
                                                buffers_input_unpack + (line_idx - 3) * plane_width, //-3
                                                buffers_input_unpack + (line_idx - 2) * plane_width, //-2
                                                buffers_input_unpack + (line_idx - 1) * plane_width, //-1
                                                buffers_input_unpack + (line_idx + 0) * plane_width, //0
                                                buffers_input_unpack + (line_idx + 1) * plane_width, //1
                                                buffers_input_unpack + (line_idx + 2) * plane_width, //2
                                                buffer_prev,
                                                buffer_on_place,
                                                buffer_tmp);

                //Recalculate overhead between precincts
                memset(buffer_next, 0, (plane_width + 1) * sizeof(int32_t));  //Invalidate buffers to test
                memset(buffer_tmp, 0, (5 * pi->width + 1) * sizeof(int32_t)); //Invalidate buffers to test
            }

            transform_V2_Hx_precinct(component,
                                     component_enc,
                                     transform_V0_Hn_sub_1,
                                     decom_h,
                                     line_idx,
                                     plane_width,
                                     plane_height,
                                     buffers_input_unpack + (2 + line_idx) * plane_width,
                                     buffers_input_unpack + (3 + line_idx) * plane_width,
                                     buffers_input_unpack + (4 + line_idx) * plane_width,
                                     buffers_input_unpack + (5 + line_idx) * plane_width,
                                     buffers_input_unpack + (6 + line_idx) * plane_width,
                                     buffer_out_16bit + (line_idx / 4) * precinct_offset,
                                     param_Fq,
                                     buffer_prev,
                                     buffer_next,
                                     buffer_on_place,
                                     buffer_tmp);

            /*Swap buffers*/
            int32_t* tmp = buffer_prev;
            buffer_prev = buffer_next;
            buffer_next = tmp;
        }
        free(buffers_input_unpack);
    }
    else /*convert_input_per_slice == 1*/ {
        int32_t* line_x[9];
        line_x[0] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[1] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[2] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[3] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[4] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        line_x[5] = NULL;
        line_x[6] = NULL;
        line_x[7] = NULL;
        line_x[8] = NULL;
        if (calculate_separate_precinct) {
            line_x[5] = (int32_t*)malloc(pi->width * sizeof(int32_t));
            line_x[6] = (int32_t*)malloc(pi->width * sizeof(int32_t));
            line_x[7] = (int32_t*)malloc(pi->width * sizeof(int32_t));
            line_x[8] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        }

        int32_t* line_2 = line_x[0]; //buffers_input_unpack + (0 + 0) * plane_width;
        int32_t* line_3 = line_x[1]; //buffers_input_unpack + (0 + 0) * plane_width;
        int32_t* line_4 = line_x[2]; //buffers_input_unpack + (0 + 0) * plane_width;
        int32_t* line_5 = line_x[3]; //buffers_input_unpack + (0 + 0) * plane_width;
        int32_t* line_6 = line_x[4]; //buffers_input_unpack + (0 + 0) * plane_width;

        picture_header_dynamic_t hdr_dynamic;
        hdr_dynamic.hdr_Tnlt = 0; //linear
        hdr_dynamic.hdr_Bw = param_Bw;
        hdr_dynamic.hdr_Fq = param_Fq;

        const int8_t* plane_buffer_in_8 = (int8_t*)plane_buffer_in;
        const uint32_t pixel_size = input_bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);

        if (2 < plane_height)
            nlt_input_scaling_line(
                plane_buffer_in_8 + pixel_size * 2 * plane_stride, line_6, plane_width, &hdr_dynamic, input_bit_depth);

        if (!calculate_separate_precinct) {
            int32_t* line_0 = line_x[2]; //buffers_input_unpack + (0 + 0) * plane_width;
            int32_t* line_1 = line_x[3]; //buffers_input_unpack + (0 + 0) * plane_width;
            nlt_input_scaling_line(
                plane_buffer_in_8 + pixel_size * 0 * plane_stride, line_0, plane_width, &hdr_dynamic, input_bit_depth);
            nlt_input_scaling_line(
                plane_buffer_in_8 + pixel_size * 1 * plane_stride, line_1, plane_width, &hdr_dynamic, input_bit_depth);

            transform_V2_Hx_precinct_recalc_prec_0(plane_width,
                                                   line_0,
                                                   line_1,
                                                   /*line_2*/ line_6,
                                                   buffer_prev,
                                                   buffer_on_place,
                                                   buffer_tmp);
        }

        for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 4) {
            memset(buffer_tmp, 0, (5 * pi->width + 1) * sizeof(int32_t)); //Invalidate buffers to test
            memset(buffer_next, 0, (plane_width + 1) * sizeof(int32_t));  //Invalidate buffers to test

            int32_t* tmp = line_2;
            line_2 = line_6;
            line_6 = tmp;

            if (calculate_separate_precinct) {
                //Recalculate overhead between precincts
                memset(buffer_prev, 0, (plane_width + 1) * sizeof(int32_t));
                memset(buffer_on_place, 0, (3 * plane_width / 2) * sizeof(int32_t));
                int32_t* line_p6 = line_x[5]; //Line previous precinct
                int32_t* line_p5 = line_x[6]; //Line previous precinct
                int32_t* line_p4 = line_x[7]; //Line previous precinct
                int32_t* line_p3 = line_x[8]; //Line previous precinct
                int32_t* line_p2 = line_6;    //Line previous precinct
                int32_t* line_p1 = line_3;    //Line previous precinct
                int32_t* line_0 = line_4;     //Line previous precinct
                int32_t* line_1 = line_5;     //Line previous precinct

                if (line_idx >= 6)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 6) * plane_stride,
                                           line_p6,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                if (line_idx >= 5)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 5) * plane_stride,
                                           line_p5,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                if (line_idx >= 4)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 4) * plane_stride,
                                           line_p4,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                if (line_idx >= 3)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 3) * plane_stride,
                                           line_p3,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                if (line_idx >= 2)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 2) * plane_stride,
                                           line_p2,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                if (line_idx >= 1)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 1) * plane_stride,
                                           line_p1,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx - 0) * plane_stride,
                                       line_0,
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
                if (line_idx + 1 < plane_height)
                    nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 1) * plane_stride,
                                           line_1,
                                           plane_width,
                                           &hdr_dynamic,
                                           input_bit_depth);

                transform_V2_Hx_precinct_recalc(component,
                                                decom_h,
                                                line_idx,
                                                plane_width,
                                                plane_height,
                                                line_p6, //buffers_input_unpack + (line_idx - 6) * plane_width,//-4
                                                line_p5, //buffers_input_unpack + (line_idx - 5) * plane_width,//-4
                                                line_p4, //buffers_input_unpack + (line_idx - 4) * plane_width,//-4
                                                line_p3, //buffers_input_unpack + (line_idx - 3) * plane_width,//-3
                                                line_p2, //buffers_input_unpack + (line_idx - 2) * plane_width,//-2
                                                line_p1, //buffers_input_unpack + (line_idx - 1) * plane_width,//-1
                                                line_0,  //buffers_input_unpack + (line_idx + 0) * plane_width,//0
                                                line_1,  //buffers_input_unpack + (line_idx + 1) * plane_width,//1
                                                line_2,  //2
                                                buffer_prev,
                                                buffer_on_place,
                                                buffer_tmp);

                //Recalculate overhead between precincts
                memset(buffer_next, 0, (plane_width + 1) * sizeof(int32_t));  //Invalidate buffers to test
                memset(buffer_tmp, 0, (5 * pi->width + 1) * sizeof(int32_t)); //Invalidate buffers to test
            }

            if (3 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 3) * plane_stride,
                                       line_3,
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
            if (4 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 4) * plane_stride,
                                       line_4,
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
            if (5 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 5) * plane_stride,
                                       line_5,
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);
            if (6 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in_8 + pixel_size * (line_idx + 6) * plane_stride,
                                       line_6,
                                       plane_width,
                                       &hdr_dynamic,
                                       input_bit_depth);

            transform_V2_Hx_precinct(component,
                                     component_enc,
                                     transform_V0_Hn_sub_1,
                                     decom_h,
                                     line_idx,
                                     plane_width,
                                     plane_height,

                                     line_2,
                                     line_3,
                                     line_4,
                                     line_5,
                                     line_6,

                                     buffer_out_16bit + (line_idx / 4) * precinct_offset,

                                     param_Fq,
                                     buffer_prev,
                                     buffer_next,
                                     buffer_on_place,
                                     buffer_tmp);

            /*Swap buffers*/
            /* int32_t* */ tmp = buffer_prev;
            buffer_prev = buffer_next;
            buffer_next = tmp;
        }

        free(line_x[0]);
        free(line_x[1]);
        free(line_x[2]);
        free(line_x[3]);
        free(line_x[4]);
        if (calculate_separate_precinct) {
            free(line_x[5]);
            free(line_x[6]);
            free(line_x[7]);
            free(line_x[8]);
        }
    }

    free(buffer_tmp);
    free(buffer_on_place);
    free(buffer_prev);
    free(buffer_next);
}

/*REFERENCE C CODE TO TESTS BEGIN*/
void reference_transform_component(
    const pi_t* pi, uint32_t component_id, const int32_t* plane_buffer_in, uint8_t input_bit_depth, uint32_t plane_width,
    uint32_t plane_height, uint16_t plane_stride, uint8_t param_Bw, uint8_t param_Fq, uint16_t* buffer_out_16bit,
    uint32_t reference_coeff_buff_tmp_pos_offset_32bit_frame[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM]) {
    /*Image shift->DWT->Image shift from (plane_buffer_in) to (buffer_out)*/

    const pi_component_t* const component = &(pi->components[component_id]);

    int32_t* buffers_tmp[2];
    buffers_tmp[0] = (int32_t*)malloc(pi->width * pi->height * sizeof(int32_t));
    buffers_tmp[1] = (int32_t*)malloc(pi->width * pi->height * sizeof(int32_t));
    int32_t* buffer_out32bit = (int32_t*)malloc(pi->width * pi->height * sizeof(int32_t)); //Temporary out 32bit

    int decom_h = component->decom_h;
    int decom_v = component->decom_v;

    //Transformation precision from input buffer to temporary buffers_tmp buffer
    int32_t* tmp_dst_ptr;
    if (decom_h || decom_v) {
        tmp_dst_ptr = buffers_tmp[0];
    }
    else {
        tmp_dst_ptr = buffer_out32bit;
    }
    if (input_bit_depth <= 8) {
        uint8_t* tmp_src_ptr = (uint8_t*)plane_buffer_in;
        linear_input_scaling_8bit_depricated(
            param_Bw, tmp_src_ptr, tmp_dst_ptr, plane_width, plane_height, plane_stride, plane_width, input_bit_depth);
    }
    else {
        uint16_t* tmp_src_ptr = (uint16_t*)plane_buffer_in;
        linear_input_scaling_16bit_depricated(
            param_Bw, tmp_src_ptr, tmp_dst_ptr, plane_width, plane_height, plane_stride, plane_width, input_bit_depth);
    }

    plane_buffer_in = tmp_dst_ptr;

    ASSERT_TRUE(decom_v <= decom_h);
    int32_t band_id = component->bands_num - 1;
    int32_t width_vertical = plane_width;

    for (int32_t d = 0; d < decom_v; ++d) {
        ASSERT_TRUE(band_id - 2 > 0);
        uint32_t heigh_1 = component->bands[band_id - 2].height;
        uint32_t heigh_2 = component->bands[band_id - 0].height;
        uint32_t width_1 = component->bands[band_id - 1].width;
        uint32_t width_2 = component->bands[band_id - 0].width;
        ASSERT_TRUE((d != 0) || (plane_width == (width_1 + width_2)));

        int32_t* out_ptr_lf = buffers_tmp[1];
        int32_t* out_ptr_hf = buffers_tmp[1] + reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id - 1];

        dwt_vertical_depricated(out_ptr_lf,
                                out_ptr_hf,
                                plane_buffer_in,
                                width_1 + width_2,
                                heigh_1 + heigh_2,
                                width_1 + width_2,
                                width_1 + width_2,
                                width_1 + width_2);

        int32_t* out_ptr_lf_lf_hor;
        if (d + 1 == decom_h) {
            //For last band dwt to out buffer
            out_ptr_lf_lf_hor = buffer_out32bit;
        }
        else {
            out_ptr_lf_lf_hor = buffers_tmp[0];
        }
        int32_t* out_ptr_lf_hf_hor = buffer_out32bit + reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id - 2];
        int32_t* out_ptr_hf_lf_hor = buffer_out32bit + reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id - 1];
        int32_t* out_ptr_hf_hf_hor = buffer_out32bit + reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id];

        dwt_horizontal_depricated(
            out_ptr_lf_lf_hor, out_ptr_lf_hf_hor, out_ptr_lf, width_1 + width_2, heigh_1, width_1, width_2, width_1 + width_2);
        dwt_horizontal_depricated(
            out_ptr_hf_lf_hor, out_ptr_hf_hf_hor, out_ptr_hf, width_1 + width_2, heigh_2, width_1, width_2, width_1 + width_2);
        band_id -= 3;

        plane_buffer_in = out_ptr_lf_lf_hor;
        width_vertical = width_1;
    }

    uint32_t buff_id = 1;
    uint32_t height = component->bands[band_id].height;
    uint32_t width = width_vertical;

    for (int d = decom_v; d < decom_h; ++d) {
        ASSERT_TRUE(band_id - 1 >= 0);
        uint32_t width_2 = component->bands[band_id].width;
        uint32_t width_1 = width - width_2;
        width -= width_2;

        int32_t* out_ptr_lf;
        if (d + 1 == decom_h) {
            //For last band dwt to out buffer
            out_ptr_lf = buffer_out32bit;
        }
        else {
            out_ptr_lf = buffers_tmp[buff_id];
        }
        int32_t* out_ptr_hf = buffer_out32bit + reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id];
        dwt_horizontal_depricated(
            out_ptr_lf, out_ptr_hf, plane_buffer_in, width_1 + width_2, height, width_1, width_2, width_1 + width_2);
        --band_id;

        plane_buffer_in = out_ptr_lf;
        buff_id = (buff_id + 1) % 2;
    }
    ASSERT_TRUE(width == component->bands[0].width);
    ASSERT_TRUE(reference_coeff_buff_tmp_pos_offset_32bit_frame[component_id][band_id] == 0);

    int32_t shift = param_Fq;
    int32_t offset = 1 << (param_Fq - 1);
    for (uint32_t i = 0; i < plane_height; ++i) {
        image_shift(&buffer_out_16bit[i * plane_width], &buffer_out32bit[i * plane_width], plane_width, shift, offset);
    }

    free(buffers_tmp[0]);
    free(buffers_tmp[1]);
    free(buffer_out32bit);
}
/*REFERENCE C CODE TO TESTS END*/
