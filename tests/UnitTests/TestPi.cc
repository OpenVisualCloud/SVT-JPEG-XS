/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include <immintrin.h>
#include <Pi.h>

struct TestTopology {
    ColourFormat_t format;
    int components;
    int group_size;
    int sig_group_size;
    int w;
    int h;
    int dcomp_h;
    int dcomp_v;
    int sd;
    int32_t bands_num_all;
    struct {
        int w;
        int h;
        int nbands;
        struct {
            int w;
            int h;
        } bands[10];
    } comp[4];

    uint8_t topology_band_id_to_comp_id[40];
    uint8_t topology_band_id_to_band_in_comp[40];

    int npackets;
    struct {
        int start;
        int stop;
        int line;
    } packets[14];
};

// clang-format off
static const  struct TestTopology TestSizes[] = {
    {COLOUR_FORMAT_PLANAR_YUV420, 3, 1, 2, 1520, 1200, 5, 2, 0, 30,
        {{1520, 1200, 10, {{48,   300}, {  47, 300}, {  95, 300}, { 190, 300}, { 380, 300}, { 380, 300}, { 380, 300}, {760, 600}, {760, 600}, {760, 600}}},
         { 760,  600, 8,  {{24,   300}, {  24, 300}, {  47, 300}, {  95, 300}, { 190, 300}, { 380, 300}, { 380, 300}, {380, 300}, {  0,   0}, {  0,   0}}},
         { 760,  600, 8,  {{24,   300}, {  24, 300}, {  47, 300}, {  95, 300}, { 190, 300}, { 380, 300}, { 380, 300}, {380, 300}, {  0,   0}, {  0,   0}}},
         {   0,    0, 0,  {{ 0,     0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}}},
        {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 255, 255, 0, 255, 255, 0, 1, 2, 0, 1, 2, 0, 1, 2, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 255, 255, 6, 255, 255, 7, 5, 5, 8, 6, 6, 9, 7, 7, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        10, {{0,12,0}, {12,15,0}, {15,16,0}, {18,19,0}, {21,24,0}, {24,27,0}, {27,30,0}, {21,22,1}, {24,25,1}, {27,28,1}, {0,0,0}, {0,0,0}, {0,0,0},{0,0,0}}
    },
    {COLOUR_FORMAT_PLANAR_4_COMPONENTS, 4, 2, 3, 4096, 1743, 2, 2, 0, 28,
        {{4096, 1743, 7,  {{1024, 436}, {1024, 436}, {1024, 436}, {1024, 436}, {2048, 872}, {2048, 871}, {2048, 871}, {  0,   0}, {  0,   0}, {  0,   0}}},
         {4096, 1743, 7,  {{1024, 436}, {1024, 436}, {1024, 436}, {1024, 436}, {2048, 872}, {2048, 871}, {2048, 871}, {  0,   0}, {  0,   0}, {  0,   0}}},
         {4096, 1743, 7,  {{1024, 436}, {1024, 436}, {1024, 436}, {1024, 436}, {2048, 872}, {2048, 871}, {2048, 871}, {  0,   0}, {  0,   0}, {  0,   0}}},
         {4096, 1743, 7,  {{1024, 436}, {1024, 436}, {1024, 436}, {1024, 436}, {2048, 872}, {2048, 871}, {2048, 871}, {  0,   0}, {  0,   0}, {  0,   0}}}},
        {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        10, {{0,4,0}, {4,8,0}, {8,12,0}, {12,16,0}, {16,20,0}, {20,24,0}, {24,28,0}, {16,20,1}, {20,24,1}, {24,28,1}, {0,0,0}, {0,0,0}, {0,0,0},{0,0,0}}
    },

    {COLOUR_FORMAT_PLANAR_4_COMPONENTS, 4, 4, 8, 488, 325, 2, 0, 1/*Sd*/, 10,
         {{488,  325, 3,  {{ 122, 325}, { 122, 325}, { 244, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 3,  {{ 122, 325}, { 122, 325}, { 244, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 3,  {{ 122, 325}, { 122, 325}, { 244, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 1,  {{ 488, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}}},
        {0, 1, 2, 0, 1, 2, 0, 1, 2, 3, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 0, 1, 1, 1, 2, 2, 2, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
         2, {{0,9,0}, {9,10,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},{0,0,0}}
    },
    {COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, 3, 7, 8, 1025, 257, 2, 2, 1/*Sd*/, 15,
         {{1025, 257, 7,  {{ 257,  65}, { 256,  65}, { 257,  64}, { 256,  64}, { 512, 129}, { 513, 128}, { 512, 128}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {1025, 257, 7,  {{ 257,  65}, { 256,  65}, { 257,  64}, { 256,  64}, { 512, 129}, { 513, 128}, { 512, 128}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {1025, 257, 1,  {{1025, 257}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {  0,    0, 0,  {{ 0,     0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}}},
        {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
         14, {{0,2,0}, {2,4,0}, {4,6,0}, {6,8,0}, {8,10,0}, {10,12,0}, {12,14,0}, {8,10,1}, {10,12,1}, {12,14,1}, {14,15,0}, {14,15,1}, {14,15,2},{14,15,3}}
    },
    {COLOUR_FORMAT_PLANAR_4_COMPONENTS, 4, 1, 1, 488, 325, 2, 0, 2/*Sd*/, 8,
         {{488,  325, 3,  {{ 122, 325}, { 122, 325}, { 244, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 3,  {{ 122, 325}, { 122, 325}, { 244, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 1,  {{ 488, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}},
          {488,  325, 1,  {{ 488, 325}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {  0,   0}, {  0,   0}, {  0,   0}}}},
        {0, 1, 0, 1, 0, 1, 2, 3, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 1, 1, 2, 2, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255},
         3, {{0,6,0}, {6,7,0}, {7,8,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},{0,0,0}}
    },
};
// clang-format on

static const size_t TestSizesNum = sizeof(TestSizes) / sizeof(TestSizes[0]);

TEST(TestPi, Topology_Tests_resolutions) {
    ASSERT_EQ(1, 2);
    for (size_t test_id = 0; test_id < TestSizesNum; ++test_id) {
        const struct TestTopology* const test = &TestSizes[test_id];
        uint32_t sx[MAX_COMPONENTS_NUM];
        uint32_t sy[MAX_COMPONENTS_NUM];
        uint32_t num_comp;
        SvtJxsErrorType_t ret = format_get_sampling_factory(test->format, &num_comp, sx, sy, VERBOSE_INFO_FULL);
        ASSERT_EQ(ret, SvtJxsErrorNone);
        pi_t pi;
        ret = pi_compute(&pi,
                         1 /*Init encoder*/,
                         test->components,
                         test->group_size,
                         test->sig_group_size,
                         test->w,
                         test->h,
                         test->dcomp_h,
                         test->dcomp_v,
                         test->sd,
                         sx,
                         sy,
                         0 /*Cw*/,
                         4 /*slice_height*/);
        ASSERT_EQ(ret, SvtJxsErrorNone);
        ASSERT_EQ(test->components, pi.comps_num);
        ASSERT_EQ(test->group_size, pi.coeff_group_size);
        ASSERT_EQ(test->sig_group_size, pi.significance_group_size);
        ASSERT_EQ(test->dcomp_h, pi.decom_h);
        ASSERT_EQ(test->dcomp_v, pi.decom_v);
        ASSERT_EQ(test->h, pi.height);
        ASSERT_EQ(test->w, pi.width);
        ASSERT_EQ(test->sd, pi.Sd);
        ASSERT_EQ(test->bands_num_all, pi.bands_num_all);
        for (int c = 0; c < test->components; ++c) {
            ASSERT_EQ(test->comp[c].w, pi.components[c].width);
            ASSERT_EQ(test->comp[c].h, pi.components[c].height);
            ASSERT_EQ(test->comp[c].nbands, pi.components[c].bands_num);
            for (int b = 0; b < test->comp[c].nbands; ++b) {
                ASSERT_EQ(test->comp[c].bands[b].w, pi.components[c].bands[b].width);
                ASSERT_EQ(test->comp[c].bands[b].h, pi.components[c].bands[b].height);
            }
        }
        //Check Bands
        for (uint32_t b = 0; b < pi.bands_num_all; ++b) {
            ASSERT_EQ(test->topology_band_id_to_comp_id[b], pi.global_band_info[b].comp_id);
            ASSERT_EQ(test->topology_band_id_to_band_in_comp[b], pi.global_band_info[b].band_id);
        }

        ASSERT_EQ(test->npackets, pi.packets_num);
        for (uint32_t p = 0; p < pi.packets_num; ++p) {
            ASSERT_EQ(test->packets[p].line, pi.packets[p].line_idx);
            ASSERT_EQ(test->packets[p].start, pi.packets[p].band_start);
            ASSERT_EQ(test->packets[p].stop, pi.packets[p].band_stop);
        }
    }
}

TEST(TestPi, Topology_Tests420_5_2) {
    pi_t pi;
    SvtJxsErrorType_t ret;
    uint32_t sx[MAX_COMPONENTS_NUM];
    uint32_t sy[MAX_COMPONENTS_NUM];
    uint32_t num_comp;

    ret = format_get_sampling_factory(COLOUR_FORMAT_PLANAR_YUV420, &num_comp, sx, sy, VERBOSE_INFO_FULL);
    ASSERT_EQ(ret, SvtJxsErrorNone);
    ret = pi_compute(&pi, 1 /*Init encoder*/, 3, 4, 8, 1920, 1080, 5, 2, 0, sx, sy, 0 /*Cw*/, 4 /*slice_height*/);
    ASSERT_EQ(ret, SvtJxsErrorNone);
}

TEST(TestPi, Invalid_SliceHeight) {
    pi_t pi;
    SvtJxsErrorType_t ret;
    uint32_t sx[MAX_COMPONENTS_NUM];
    uint32_t sy[MAX_COMPONENTS_NUM];
    uint32_t num_comp;

    ret = format_get_sampling_factory(COLOUR_FORMAT_PLANAR_YUV422, &num_comp, sx, sy, VERBOSE_INFO_FULL);
    ASSERT_EQ(ret, SvtJxsErrorNone);

    /*If one slice then slice height can be not divisible by 4*/
    ret = pi_compute(&pi, 1 /*Init encoder*/, 3, 4, 8, 1920, 1078, 5, 2, 0, sx, sy, 0 /*Cw*/, 1078 /*slice_height*/);
    ASSERT_EQ(ret, SvtJxsErrorNone);

    ret = pi_compute(&pi, 1 /*Init encoder*/, 3, 4, 8, 1920, 1078, 5, 2, 0, sx, sy, 0 /*Cw*/, 22 /*slice_height*/);
    ASSERT_NE(ret, SvtJxsErrorNone);
}
