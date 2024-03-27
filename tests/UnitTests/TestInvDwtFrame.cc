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
#include "Mct.h"
#include "NltDec.h"
#include "DwtDecoder.h"
#include "CodeDeprecated.h"

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

void reference_idwt_transform_components(const pi_t* const pi, int32_t* components_data[MAX_COMPONENTS_NUM],
                                         int offset[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM]);

void transform_components_test(const pi_t* pi, int16_t* buffer_in[MAX_COMPONENTS_NUM], int32_t* buffer_out[MAX_COMPONENTS_NUM],
                               int offset[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM],
                               picture_header_dynamic_t picture_header_dynamic, picture_header_const_t picture_header_const,
                               svt_jpeg_xs_image_buffer_t* out, uint8_t hdr_Cpih);
/* REFERENCE C CODE TO TESTS END */

const char* ColourFormatNames[]{"COLOUR_FORMAT_INVALID",
                                "COLOUR_FORMAT_PLANAR_YUV400",
                                "COLOUR_FORMAT_PLANAR_YUV420",
                                "COLOUR_FORMAT_PLANAR_YUV422",
                                "COLOUR_FORMAT_PLANAR_YUV444_OR_RGBP",
                                "COLOUR_FORMAT_PLANAR_4_COMPONENTS",
                                "COLOUR_FORMAT_GRAY"};

typedef ::testing::tuple<uint32_t, uint32_t> size_param_t;

// clang-format off
static size_param_t params_block_sizes[] = {
    {17, 2},
    {200, 200}, {201, 201},
    {2, 2}, {2, 11}, {3, 3},
    {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8}, {9, 9}, {10, 10},
    {11, 11}, {16, 16}, {17, 17}, {18, 18}, {19, 19}, {20, 20},
    {1920, 1080}
};
// clang-format on

static uint8_t mct_sizes[3] = {0, 1, 3}; // Multiple component transformation, 0-disable, 1-rct, 3-star-tetrix
static uint8_t nlt_sizes[3] = {0, 1, 2}; // Linearity, 0-linear, 1-quadratic, 2-extended

/*Vertical, Horizontal*/
typedef ::testing::tuple<uint32_t, uint32_t> format_param_t;

// clang-format off
static format_param_t format_params[] = {
    {0, 0},
    {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5},
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5},
    {2, 2}, {2, 3}, {2, 4}, {2, 5},
};
// clang-format on

/*                input_bit_depth, Size(WxH), (vert, hor)*/
typedef ::testing::tuple<uint8_t, size_param_t, format_param_t, ColourFormat_t> fixture_param_t;

class FRAME_IDWT : public ::testing::TestWithParam<fixture_param_t> {
  protected:
    /*Parameters*/
    uint8_t input_bit_depth;
    uint32_t width;
    uint32_t height;
    uint32_t num_comp;
    ColourFormat_t format;
    picture_header_dynamic_t picture_header_dynamic;
    picture_header_const_t picture_header_const;

    /*Variables*/
    svt_jxs_test_tool::SVTRandom* rnd;
    pi_t pi;

    /*Buffers*/
    uint32_t stride_input;
    uint32_t lenght;

    //Reference buffer out for components
    svt_jpeg_xs_image_config_t image_config;
    int32_t* buffers_in_out_ref_32bit[MAX_COMPONENTS_NUM];
    svt_jpeg_xs_image_buffer_t* ref_out = NULL;

    int16_t* buffers_in_mod_16bit[MAX_COMPONENTS_NUM];  //Buffer out for component
    int32_t* buffers_out_mod_32bit[MAX_COMPONENTS_NUM]; //Buffer out for component
    svt_jpeg_xs_image_buffer_t* mod_out = NULL;
    uint8_t skip_test;

    int offset_NW[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM];
    uint32_t sx[MAX_COMPONENTS_NUM];
    uint32_t sy[MAX_COMPONENTS_NUM];

  protected:
    int32_t test_allocate_out_buffer(svt_jpeg_xs_image_buffer_t* out) {
        const uint32_t pixel_size = image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
        for (int32_t c = 0; c < image_config.components_num; c++) {
            //test for stride different than width
            out->stride[c] = image_config.components[c].width + 10;
            const uint32_t height = image_config.components[c].height;
            out->alloc_size[c] = out->stride[c] * height * pixel_size;
            out->data_yuv[c] = (uint8_t*)calloc(out->alloc_size[c], 1);
        }
        return 0;
    }

    uint8_t fill_mct_header_data(uint8_t Cpih, uint32_t* sx, uint32_t* sy) {
        picture_header_const.hdr_Cpih = Cpih;

        //Cpih shall be 0 if there are less than 3 components
        if (num_comp < 3) {
            return 1;
        }
        //or if any of the sampling factors sx[i] and sy[i] for i<3 are different from 1
        for (int i = 0; i < 3; i++) {
            if (sx[i] != 1 || sy[i] != 1) {
                return 1;
            }
        }
        //Furthermore, Cpih shall be smaller than 2 if there are less than 4 components, i.e. if Nc<3,
        if (picture_header_const.hdr_Cpih == 3 && num_comp < 4) {
            return 1;
        }
        if (picture_header_const.hdr_Cpih == 3) {
            //Enforce RGGB
            picture_header_dynamic.hdr_Xcrg[0] = 0;
            picture_header_dynamic.hdr_Xcrg[1] = 32768;
            picture_header_dynamic.hdr_Xcrg[2] = 0;
            picture_header_dynamic.hdr_Xcrg[3] = 32768;

            picture_header_dynamic.hdr_Ycrg[0] = 0;
            picture_header_dynamic.hdr_Ycrg[1] = 0;
            picture_header_dynamic.hdr_Ycrg[2] = 32768;
            picture_header_dynamic.hdr_Ycrg[3] = 32768;

            //Restricted in-line transformation, no access to neighbouring lines
            picture_header_dynamic.hdr_Cf = 3;
            //Full transformation, access to the line below and above required
            //picture_header_dynamic.hdr_Cf = 0;

            //Table: A.19   e1/e2 have Values in range 0..3
            picture_header_dynamic.hdr_Cf_e1 = 2;
            picture_header_dynamic.hdr_Cf_e2 = 3;
        }
        return 0;
    }

    void fill_nlt_header_data(uint8_t Tnlt) {
        picture_header_dynamic.hdr_Tnlt = Tnlt;
        picture_header_dynamic.hdr_Bw = 20;
        picture_header_dynamic.hdr_Fq = 8;

        if (picture_header_dynamic.hdr_Tnlt == 1) {
            // values 0-1
            picture_header_dynamic.hdr_Tnlt_sigma = rnd->random() % 2;
            // values 0-(2^15 - 1)
            picture_header_dynamic.hdr_Tnlt_alpha = rnd->random() % 1024;
        }

        if (picture_header_dynamic.hdr_Tnlt == 2) {
            // values 1-(2^Bw - 1)
            picture_header_dynamic.hdr_Tnlt_t1 = 1 + rnd->random() % ((1 << input_bit_depth) - 2);
            // values 1-(2^Bw - 1)
            picture_header_dynamic.hdr_Tnlt_t2 = 1 + rnd->random() % ((1 << input_bit_depth) - 2);
            // values 1-4
            picture_header_dynamic.hdr_Tnlt_e = 1 + rnd->random() % 4;
        }
    }

    void free_out_buffer(svt_jpeg_xs_image_buffer_t* out) {
        if (!out) {
            return;
        }
        for (int32_t c = 0; c < image_config.components_num; c++) {
            free(out->data_yuv[c]);
        }
    }

    virtual void SetUp() {
        memset(buffers_in_out_ref_32bit, 0, sizeof(buffers_in_out_ref_32bit));
        memset(buffers_in_mod_16bit, 0, sizeof(buffers_in_mod_16bit));
        memset(buffers_out_mod_32bit, 0, sizeof(buffers_out_mod_32bit));
        memset(offset_NW, 0, sizeof(offset_NW));

        rnd = new svt_jxs_test_tool::SVTRandom(32, false);
        fixture_param_t params = GetParam();
        input_bit_depth = std::get<0>(params);
        size_param_t param_size = std::get<1>(params);
        width = std::get<0>(param_size);
        height = std::get<1>(param_size);
        format_param_t format_param = std::get<2>(params);
        int32_t decomp_v = std::get<0>(format_param);
        int32_t decomp_h = std::get<1>(format_param);
        format = std::get<3>(params);

        picture_header_const.hdr_bit_depth[0] = input_bit_depth;

        stride_input = width;
        printf("Bits: %i (%ix%i) V:%i H:%i %s\n", input_bit_depth, width, height, decomp_v, decomp_h, ColourFormatNames[format]);
        lenght = stride_input * height; //w*h

        format_get_sampling_factory(format, &num_comp, sx, sy, VERBOSE_INFO_FULL);

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

        if (ret || ((height & 1) && format == COLOUR_FORMAT_PLANAR_YUV420)) {
            skip_test = 1;
            return;
        }

        for (uint32_t i = 0; i < num_comp; i++) {
            buffers_in_out_ref_32bit[i] = (int32_t*)calloc(lenght, sizeof(int32_t));
            buffers_in_mod_16bit[i] = (int16_t*)calloc(lenght, sizeof(int16_t));
            buffers_out_mod_32bit[i] = (int32_t*)calloc(lenght, sizeof(int32_t));
        }

        image_config.format = svt_jpeg_xs_get_format_from_params(pi.comps_num, sx, sy);
        image_config.components_num = pi.comps_num;
        image_config.bit_depth = input_bit_depth;
        for (int32_t c = 0; c < image_config.components_num; c++) {
            image_config.components[c].width = pi.components[c].width;
            image_config.components[c].height = pi.components[c].height;
            uint32_t pixel_size = image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
            image_config.components[c].byte_size = image_config.components[c].width * image_config.components[c].height *
                pixel_size;
        }

        ref_out = (svt_jpeg_xs_image_buffer_t*)calloc(1, sizeof(svt_jpeg_xs_image_buffer_t));
        test_allocate_out_buffer(ref_out);
        mod_out = (svt_jpeg_xs_image_buffer_t*)calloc(1, sizeof(svt_jpeg_xs_image_buffer_t));
        test_allocate_out_buffer(mod_out);

        int offset_tmp[MAX_COMPONENTS_NUM] = {0};
        for (uint32_t j = 0; j < pi.bands_num_all; j++) {
            uint8_t c = pi.global_band_info[j].comp_id;
            if (c != BAND_NOT_EXIST) {
                uint8_t b = pi.global_band_info[j].band_id;
                offset_NW[c][b] = offset_tmp[c];
                offset_tmp[c] += pi.components[c].bands[b].width * pi.components[c].bands[b].height;
            }
        }
    }

    virtual void TearDown() {
        delete rnd;
        for (uint32_t i = 0; i < num_comp; i++) {
            free(buffers_in_out_ref_32bit[i]);
            free(buffers_in_mod_16bit[i]);
            free(buffers_out_mod_32bit[i]);
        }

        free_out_buffer(ref_out);
        free(ref_out);

        free_out_buffer(mod_out);
        free(mod_out);
    }

    void set_random_coeffs() {
        for (uint32_t c = 0; c < num_comp; c++) {
            for (uint32_t i = 0; i < lenght; i++) {
                int16_t coeff = rnd->Rand16();
                buffers_in_out_ref_32bit[c][i] = coeff << picture_header_dynamic.hdr_Fq;
                //Currently IDWT also shift-left by Fq internally to reduce memory bandwidth
                buffers_in_mod_16bit[c][i] = coeff;
                buffers_out_mod_32bit[c][i] = buffers_in_out_ref_32bit[c][i];
            }
        }
    }

    void reference_idwt() {
        reference_idwt_transform_components(&pi, buffers_in_out_ref_32bit, offset_NW);
        if (picture_header_const.hdr_Cpih) {
            mct_inverse_transform(buffers_in_out_ref_32bit, &pi, &picture_header_dynamic, picture_header_const.hdr_Cpih);
        }
        nlt_inverse_transform(buffers_in_out_ref_32bit, &pi, &picture_header_const, &picture_header_dynamic, ref_out);
    }

    void per_slice_idwt_compare() {
        transform_components_test(&pi,
                                  buffers_in_mod_16bit,
                                  buffers_out_mod_32bit,
                                  offset_NW,
                                  picture_header_dynamic,
                                  picture_header_const,
                                  mod_out,
                                  picture_header_const.hdr_Cpih);

        for (int32_t c = 0; c < image_config.components_num; c++) {
            uint32_t pixels = ref_out->alloc_size[c];

            if (memcmp(ref_out->data_yuv[c], mod_out->data_yuv[c], ref_out->alloc_size[c])) {
                if (image_config.bit_depth == 8) {
                    uint8_t* ref = (uint8_t*)ref_out->data_yuv[c];
                    uint8_t* mod = (uint8_t*)mod_out->data_yuv[c];
                    for (uint32_t pix = 0; pix < pixels; pix++) {
                        if (ref[pix] != mod[pix]) {
                            int y = pix / mod_out->stride[c];
                            int x = pix % mod_out->stride[c];
                            printf("error for y=%d x=%d", y, x);
                            UNUSED(y);
                            UNUSED(x);
                            ASSERT_TRUE(0);
                        }
                    }
                }
                //16bit
                else {
                    uint16_t* ref = (uint16_t*)ref_out->data_yuv[c];
                    uint16_t* mod = (uint16_t*)mod_out->data_yuv[c];
                    for (uint32_t pix = 0; pix < pixels; pix++) {
                        if (ref[pix] != mod[pix]) {
                            int y = pix / mod_out->stride[c];
                            int x = pix % mod_out->stride[c];
                            printf("error for y=%d x=%d", y, x);
                            UNUSED(y);
                            UNUSED(x);
                            ASSERT_TRUE(0);
                        }
                    }
                }
            }
        }
    }

    void test_transform_result() {
        if (skip_test) {
            printf("Skipping tests for non support configuration.\n");
            return;
        }

        for (uint32_t j = 0; j < sizeof(mct_sizes) / sizeof(mct_sizes[0]); j++) {
            if (fill_mct_header_data(mct_sizes[j], sx, sy)) {
                printf("Skipping tests for mct=%d.\n", j);
                continue;
            }
            for (uint32_t i = 0; i < sizeof(nlt_sizes) / sizeof(nlt_sizes[0]); i++) {
                fill_nlt_header_data(nlt_sizes[i]);
                set_random_coeffs();
                reference_idwt();
                per_slice_idwt_compare();
            }
        }
    }

    void run_test_transform_c() {
        setup_decoder_rtcd_internal(0);
        setup_depricated_test_rtcd_internal(0);
        test_transform_result();
    }

    void run_test_transform_avx2() {
        setup_decoder_rtcd_internal(CPU_FLAGS_AVX2);
        setup_depricated_test_rtcd_internal(CPU_FLAGS_AVX2);
        test_transform_result();
    }

    void run_test_transform_avx512() {
        setup_decoder_rtcd_internal(CPU_FLAGS_ALL);
        setup_depricated_test_rtcd_internal(CPU_FLAGS_ALL);
        test_transform_result();
    }
};

TEST_P(FRAME_IDWT, TRANSFORM_FRAME_C) {
    run_test_transform_c();
}

TEST_P(FRAME_IDWT, TRANSFORM_FRAME_AVX2) {
    run_test_transform_avx2();
}

TEST_P(FRAME_IDWT, TRANSFORM_FRAME_AVX512) {
    if (!(CPU_FLAGS_AVX512F & get_cpu_flags())) {
        return;
    }
    run_test_transform_avx512();
}

INSTANTIATE_TEST_SUITE_P(FRAME_IDWT, FRAME_IDWT,
                         ::testing::Combine(::testing::Values(8, 10), ::testing::ValuesIn(params_block_sizes),
                                            ::testing::ValuesIn(format_params),
                                            ::testing::Values(COLOUR_FORMAT_PLANAR_YUV420, COLOUR_FORMAT_PLANAR_YUV422,
                                                              COLOUR_FORMAT_PLANAR_YUV444_OR_RGB,
                                                              COLOUR_FORMAT_PLANAR_4_COMPONENTS, COLOUR_FORMAT_GRAY)));

/*
* Recalculate input pointers from image space to band-space
*/
void recalculate_input_pointers(const pi_t* pi, int16_t* reference_in, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM],
                                int offset[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM], uint32_t c /*component*/,
                                uint32_t precinct_line_idx) {
    for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
        const uint32_t band_width = pi->components[c].bands[b].width;
        const uint32_t band_height = pi->p_info[PRECINCT_NORMAL].b_info[c][b].height;

        buff_in[b] = reference_in + offset[c][b] + precinct_line_idx * band_height * band_width;
    }
}

void transform_components_test(const pi_t* pi, int16_t* buffer_in[MAX_COMPONENTS_NUM], int32_t* buffer_out[MAX_COMPONENTS_NUM],
                               int offset[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM],
                               picture_header_dynamic_t picture_header_dynamic, picture_header_const_t picture_header_const,
                               svt_jpeg_xs_image_buffer_t* out, uint8_t hdr_Cpih) {
    int32_t* buffer_tmp[MAX_COMPONENTS_NUM] = {0};
    int32_t* buff_outt[MAX_COMPONENTS_NUM] = {0};
    uint32_t bit_depth = picture_header_const.hdr_bit_depth[0];

    if (pi->decom_v == 0) {
        buffer_tmp[0] = buffer_tmp[1] = buffer_tmp[2] = buffer_tmp[3] = (int32_t*)malloc(pi->width * sizeof(int32_t));
        buff_outt[0] = buff_outt[1] = buff_outt[2] = buff_outt[3] = (int32_t*)malloc(pi->width * sizeof(int32_t));
    }
    else {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            if (pi->components[c].decom_v == 0) {
                buffer_tmp[c] = (int32_t*)malloc(pi->components[c].width * sizeof(int32_t));
                buff_outt[c] = (int32_t*)malloc(pi->components[c].width * sizeof(int32_t));
            }
            else if (pi->components[c].decom_v == 1) {
                buffer_tmp[c] = (int32_t*)malloc(3 * pi->components[c].width * sizeof(int32_t));
                buff_outt[c] = (int32_t*)malloc(4 * pi->components[c].width * sizeof(int32_t));
            }
            else { // pi->components[c].decom_v == 2
                uint32_t V1_len = (pi->components[c].width / 2) + (pi->components[c].width & 1);
                buffer_tmp[c] = (int32_t*)malloc((7 * V1_len + 3 * pi->components[c].width) * sizeof(int32_t));
                buff_outt[c] = (int32_t*)malloc(8 * pi->components[c].width * sizeof(int32_t));
            }
        }
    }

    if (hdr_Cpih == 0) {
        // Number of "precincts" lines in one slice
        uint32_t lines_per_slice_last = pi->precincts_line_num - (pi->slice_num - 1) * pi->precincts_per_slice;

        for (uint32_t slice = 0; slice < pi->slice_num; slice++) {
            const uint32_t is_last_slice = (slice == (pi->slice_num - 1));

            const uint32_t lines_per_slice = is_last_slice ? lines_per_slice_last : pi->precincts_per_slice;

            //TODO: wipe all data in buffer_tmp[c] every slice
            for (uint32_t c = 0; c < pi->comps_num; c++) {
                int16_t* buffer_in_precinct_prev_2[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                int16_t* buffer_in_precinct_prev_1[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                int32_t precinct_idx_to_recalc = slice * pi->precincts_per_slice;
                uint32_t width = pi->components[c].width;
                transform_lines_t out_lines;
                memset(&out_lines, 0, sizeof(transform_lines_t));

                if (pi->components[c].decom_v == 0) {
                    memset(buffer_tmp[c], 0x0, 1 * pi->components[c].width * sizeof(int32_t));
                    memset(buff_outt[c], 0x0, 1 * pi->components[c].width * sizeof(int32_t));

                    out_lines.buffer_out[0] = buff_outt[c];
                }
                else if (pi->components[c].decom_v == 1) {
                    memset(buffer_tmp[c], 0x0, 3 * pi->components[c].width * sizeof(int32_t));
                    memset(buff_outt[c], 0x0, 4 * pi->components[c].width * sizeof(int32_t));
                    out_lines.buffer_out[0] = buff_outt[c] + ((2 * precinct_idx_to_recalc + 0) % 4) * width;
                }
                else { // pi->components[c].decom_v == 2
                    uint32_t V1_len = (pi->components[c].width / 2) + (pi->components[c].width & 1);
                    memset(buffer_tmp[c],
                           0x0,
                           (7 * V1_len + 3 * pi->components[c].width) * sizeof(int32_t)); // ~6.5 * component->width
                    memset(buff_outt[c], 0x0, 8 * pi->components[c].width * sizeof(int32_t));

                    out_lines.buffer_out[0] = buff_outt[c] + ((4 * precinct_idx_to_recalc + 0) % 8) * width;
                }

                if (precinct_idx_to_recalc > 1) {
                    recalculate_input_pointers(
                        pi, buffer_in[c], buffer_in_precinct_prev_2, offset, c, precinct_idx_to_recalc - 2);
                }
                if (precinct_idx_to_recalc > 0) {
                    recalculate_input_pointers(
                        pi, buffer_in[c], buffer_in_precinct_prev_1, offset, c, precinct_idx_to_recalc - 1);
                }
                new_transform_component_line_recalc(&pi->components[c],
                                                    buffer_in_precinct_prev_2,
                                                    buffer_in_precinct_prev_1,
                                                    out_lines.buffer_out,
                                                    precinct_idx_to_recalc,
                                                    buffer_tmp[c],
                                                    picture_header_dynamic.hdr_Fq);
            }

            for (uint32_t precinct_line = 0; precinct_line < lines_per_slice; precinct_line++) {
                uint32_t precinct_line_idx = slice * pi->precincts_per_slice + precinct_line;

                for (uint32_t comp_id = 0; comp_id < pi->comps_num - pi->Sd; ++comp_id) {
                    int16_t* buffer_in_precinct[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                    int16_t* buffer_in_precinct_prev[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                    uint32_t width = pi->components[comp_id].width;
                    uint32_t component_precinct_height = 1 << pi->components[comp_id].decom_v;
                    int32_t component_line_idx = precinct_line_idx * component_precinct_height;

                    transform_lines_t out_lines;
                    memset(&out_lines, 0, sizeof(transform_lines_t));

                    //int32_t* buff_out_arr[8] = {0};
                    if (pi->components[comp_id].decom_v == 0) {
                        out_lines.buffer_out[0] = buff_outt[comp_id];
                    }
                    else if (pi->components[comp_id].decom_v == 1) {
                        out_lines.buffer_out[0] = buff_outt[comp_id] + ((2 * precinct_line_idx + 0) % 4) * width;
                        out_lines.buffer_out[1] = buff_outt[comp_id] + ((2 * precinct_line_idx + 1) % 4) * width;
                        out_lines.buffer_out[2] = buff_outt[comp_id] + ((2 * precinct_line_idx + 2) % 4) * width;
                        out_lines.buffer_out[3] = buff_outt[comp_id] + ((2 * precinct_line_idx + 3) % 4) * width;
                    }
                    else { //(pi->components[comp_id].decom_v == 2)
                        out_lines.buffer_out[0] = buff_outt[comp_id] + ((4 * precinct_line_idx + 0) % 8) * width;
                        out_lines.buffer_out[1] = buff_outt[comp_id] + ((4 * precinct_line_idx + 1) % 8) * width;
                        out_lines.buffer_out[2] = buff_outt[comp_id] + ((4 * precinct_line_idx + 2) % 8) * width;
                        out_lines.buffer_out[3] = buff_outt[comp_id] + ((4 * precinct_line_idx + 3) % 8) * width;
                        out_lines.buffer_out[4] = buff_outt[comp_id] + ((4 * precinct_line_idx + 4) % 8) * width;
                        out_lines.buffer_out[5] = buff_outt[comp_id] + ((4 * precinct_line_idx + 5) % 8) * width;
                        out_lines.buffer_out[6] = buff_outt[comp_id] + ((4 * precinct_line_idx + 6) % 8) * width;
                        out_lines.buffer_out[7] = buff_outt[comp_id] + ((4 * precinct_line_idx + 7) % 8) * width;
                    }

                    recalculate_input_pointers(pi, buffer_in[comp_id], buffer_in_precinct, offset, comp_id, precinct_line_idx);
                    if (precinct_line_idx > 0) {
                        recalculate_input_pointers(
                            pi, buffer_in[comp_id], buffer_in_precinct_prev, offset, comp_id, precinct_line_idx - 1);
                    }

                    new_transform_component_line(&pi->components[comp_id],
                                                 buffer_in_precinct,
                                                 buffer_in_precinct_prev,
                                                 &out_lines,
                                                 precinct_line_idx,
                                                 buffer_tmp[comp_id],
                                                 pi->precincts_line_num,
                                                 picture_header_dynamic.hdr_Fq);

                    component_line_idx += out_lines.offset;

                    for (uint32_t line = out_lines.line_start; line <= out_lines.line_stop; line++) {
                        int32_t* in = out_lines.buffer_out[line];
                        void* out_buf = out->data_yuv[comp_id];
                        uint32_t out_stride = out->stride[comp_id];
                        if (bit_depth <= 8) {
                            uint8_t* out_buf_8 = ((uint8_t*)out_buf) + component_line_idx * out_stride;
                            nlt_inverse_transform_line_8bit(in, bit_depth, &picture_header_dynamic, out_buf_8, width);
                        }
                        else {
                            uint16_t* out_buf_16 = ((uint16_t*)out_buf) + component_line_idx * out_stride;
                            nlt_inverse_transform_line_16bit(in, bit_depth, &picture_header_dynamic, out_buf_16, width);
                        }
                        component_line_idx++;
                    }
                }
            }
        }
    }
    else {
        //for each component
        for (uint32_t comp_id = 0; comp_id < pi->comps_num; ++comp_id) {
            //for each precinct in component
            for (uint32_t precinct_idx = 0; precinct_idx < pi->precincts_line_num; precinct_idx++) {
                int16_t* buffer_in_precinct[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                int16_t* buffer_in_precinct_prev[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                recalculate_input_pointers(pi, buffer_in[comp_id], buffer_in_precinct, offset, comp_id, precinct_idx);
                if (precinct_idx > 0) {
                    recalculate_input_pointers(
                        pi, buffer_in[comp_id], buffer_in_precinct_prev, offset, comp_id, precinct_idx - 1);
                }

                uint32_t width = pi->components[comp_id].width;
                uint32_t component_precinct_height = 1 << pi->components[comp_id].decom_v;
                int32_t* buff_out = buffer_out[comp_id] + precinct_idx * component_precinct_height * width;
                transform_lines_t out_lines;
                memset(&out_lines, 0, sizeof(transform_lines_t));

                if (pi->components[comp_id].decom_v == 0) {
                    out_lines.buffer_out[0] = buff_out;
                }
                else if (pi->components[comp_id].decom_v == 1) {
                    out_lines.buffer_out[0] = buff_out - 2 * width;
                    out_lines.buffer_out[1] = buff_out - 1 * width;
                    out_lines.buffer_out[2] = buff_out + 0 * width;
                    out_lines.buffer_out[3] = buff_out + 1 * width;
                }
                else { //(pi->components[comp_id].decom_v == 2)
                    out_lines.buffer_out[0] = buff_out - 4 * width;
                    out_lines.buffer_out[1] = buff_out - 3 * width;
                    out_lines.buffer_out[2] = buff_out - 2 * width;
                    out_lines.buffer_out[3] = buff_out - 1 * width;
                    out_lines.buffer_out[4] = buff_out + 0 * width;
                    out_lines.buffer_out[5] = buff_out + 1 * width;
                    out_lines.buffer_out[6] = buff_out + 2 * width;
                    out_lines.buffer_out[7] = buff_out + 3 * width;
                }
                new_transform_component_line(&pi->components[comp_id],
                                             buffer_in_precinct,
                                             buffer_in_precinct_prev,
                                             &out_lines,
                                             precinct_idx,
                                             buffer_tmp[comp_id],
                                             pi->precincts_line_num,
                                             picture_header_dynamic.hdr_Fq);
            }
        }

        if (hdr_Cpih) {
            mct_inverse_transform(buffer_out, pi, &picture_header_dynamic, hdr_Cpih);
        }
        nlt_inverse_transform(buffer_out, pi, &picture_header_const, &picture_header_dynamic, out);
    }

    if (pi->decom_v == 0) {
        free(buffer_tmp[0]);
        free(buff_outt[0]);
    }
    else {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            free(buffer_tmp[c]);
            free(buff_outt[c]);
        }
    }
}

static void reference_idwt_transform_component(const pi_component_t* const pi_component, int32_t* buffer,
                                               int offset_band[MAX_BANDS_PER_COMPONENT_NUM], int32_t* data_tmp[2]) {
    uint32_t copy_id = 0;
    uint32_t band_id = 0;
    uint32_t width = pi_component->bands[band_id].width;
    uint32_t height = pi_component->bands[band_id].height;

    int32_t* buff_int = buffer;
    band_id++;

    assert(pi_component->decom_v <= pi_component->decom_h);
    for (uint32_t d = 0; d < pi_component->decom_h - pi_component->decom_v; ++d) {
        uint32_t width2 = pi_component->bands[band_id].width;
        assert(height == pi_component->bands[band_id].height);
        int32_t* buff_int2 = buffer + offset_band[band_id];
        int32_t* buff_out = data_tmp[(copy_id + 1) % 2];

        idwt_deprecated_horizontal(buff_int, buff_int2, buff_out, width + width2, height, width, width2, width + width2);
        buff_int = buff_out;
        width = width + width2;
        band_id++;
        copy_id = (copy_id + 1) % 2;
    }

    if (pi_component->decom_v <= 0) {
        memcpy(buffer, buff_int, sizeof(buffer[0]) * height * width);
    }
    else {
        int32_t* buff_outA = data_tmp[(copy_id + 1) % 2];
        int32_t* buff_outB = data_tmp[copy_id];
        for (uint32_t d = 0; d < pi_component->decom_v; ++d) {
            uint32_t width2 = pi_component->bands[band_id].width;
            assert(height == pi_component->bands[band_id].height);
            int32_t* buff_int2 = buffer + offset_band[band_id];

            idwt_deprecated_horizontal(buff_int, buff_int2, buff_outA, width + width2, height, width, width2, width + width2);
            band_id++;

            int32_t* buff_int1 = buffer + offset_band[band_id];
            uint32_t height2 = pi_component->bands[band_id].height;
            assert(width == pi_component->bands[band_id].width);
            band_id++;
            buff_int2 = buffer + offset_band[band_id];
            assert(width2 == pi_component->bands[band_id].width);
            assert(height2 == pi_component->bands[band_id].height);

            idwt_deprecated_horizontal(buff_int1, buff_int2, buff_outB, width + width2, height2, width, width2, width + width2);
            width = width + width2;
            band_id++;

            idwt_deprecated_vertical(buff_outA, buff_outB, buffer, width, height + height2, width, width, width);
            height = height + height2;
            buff_int = buffer;
        }
    }
}

void reference_idwt_transform_components(const pi_t* const pi, int32_t* components_data[MAX_COMPONENTS_NUM],
                                         int offset[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM]) {
    int32_t* data_tmp[2];
    data_tmp[0] = (int32_t*)malloc((pi->width * pi->height) * sizeof(int32_t));
    data_tmp[1] = (int32_t*)malloc((pi->width * pi->height) * sizeof(int32_t));

    for (uint32_t comp_id = 0; comp_id < pi->comps_num - pi->Sd; ++comp_id) {
        const pi_component_t* const pi_component = &pi->components[comp_id];
        int32_t* buff_comp = components_data[comp_id];
        int* offset_band = offset[comp_id];
        assert(pi_component->decom_v <= pi_component->decom_h);

        reference_idwt_transform_component(pi_component, buff_comp, offset_band, data_tmp);
    }
    free(data_tmp[0]);
    free(data_tmp[1]);
}
