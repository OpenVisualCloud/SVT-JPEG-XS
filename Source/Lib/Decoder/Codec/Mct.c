/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Mct.h"
#include "Definitions.h"
#include "Pi.h"

#define MAX_COMPONENTS 4
#define MAX_CFA_TYPE   2
#define MAX_SIGMA_X    2
#define MAX_SIGMA_Y    2

typedef struct comp_displacement_vector {
    uint8_t delta_x;
    uint8_t delta_y;
} comp_displacement_vector_t;

//Table F.10 — Component displacement vector by component index
comp_displacement_vector_t table_f_10[MAX_CFA_TYPE][MAX_COMPONENTS] = {{{0, 1}, {1, 1}, {0, 0}, {1, 0}},
                                                                       {{1, 1}, {0, 1}, {1, 0}, {0, 0}}};

//Table F.11 — Component index by displacement vector
uint8_t table_f_11[MAX_CFA_TYPE][MAX_SIGMA_X][MAX_SIGMA_Y] = {{{2, 0}, {3, 1}}, {{3, 1}, {2, 0}}};

// Table F.12 — Coordinate access function
static INLINE int32_t access(int32_t* comps[MAX_COMPONENTS_NUM], int32_t c, int32_t x, int32_t y, int32_t w, int32_t h,
                             int32_t rx, int32_t ry, int32_t cf, int32_t ct) {
    assert(ct < MAX_CFA_TYPE);
    assert(c < MAX_COMPONENTS);

    int8_t delta_x = table_f_10[ct][c].delta_x;
    int8_t delta_y = table_f_10[ct][c].delta_y;

    if (((2 * x + rx + delta_x) < 0) || (2 * x + rx + delta_x) >= 2 * w) {
        rx = -rx;
    }
    if ((cf == 3 && (ry + delta_y) < 0) || (cf == 3 && (ry + delta_y) > 1) || (2 * y + ry + delta_y) < 0 ||
        (2 * y + ry + delta_y) >= 2 * h) {
        ry = -ry;
    }

    x = (2 * x + rx + delta_x) / 2;
    y = (2 * y + ry + delta_y) / 2;

    int32_t comp_idx = table_f_11[ct][(MAX_SIGMA_X + rx + delta_x) % 2][(MAX_SIGMA_Y + ry + delta_y) % 2];
    return comps[comp_idx][y * w + x];
}

// Table F.8 — Inverse CbCr step
void inv_cbcr_step(int32_t* comps[MAX_COMPONENTS_NUM], int32_t cf, int32_t ct, int32_t w, int32_t h) {
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t g_l = access(comps, 1, x, y, w, h, -1, 0, cf, ct);
            int32_t g_r = access(comps, 1, x, y, w, h, 1, 0, cf, ct);
            int32_t g_t = access(comps, 1, x, y, w, h, 0, -1, cf, ct);
            int32_t g_b = access(comps, 1, x, y, w, h, 0, 1, cf, ct);

            comps[1][y * w + x] += (g_l + g_r + g_t + g_b) >> 2;

            g_l = access(comps, 2, x, y, w, h, -1, 0, cf, ct);
            g_r = access(comps, 2, x, y, w, h, 1, 0, cf, ct);
            g_t = access(comps, 2, x, y, w, h, 0, -1, cf, ct);
            g_b = access(comps, 2, x, y, w, h, 0, 1, cf, ct);

            comps[2][y * w + x] += (g_l + g_r + g_t + g_b) >> 2;
        }
    }
}

// Table F.7 — Inverse Y step
void inv_y_step(int32_t* comps[MAX_COMPONENTS_NUM], int32_t cf, int32_t ct, int32_t w, int32_t h, int32_t e1, int32_t e2) {
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t b_l = access(comps, 0, x, y, w, h, -1, 0, cf, ct);
            int32_t b_r = access(comps, 0, x, y, w, h, 1, 0, cf, ct);
            int32_t r_t = access(comps, 0, x, y, w, h, 0, -1, cf, ct);
            int32_t r_b = access(comps, 0, x, y, w, h, 0, 1, cf, ct);

            comps[0][y * w + x] -= ((1 << e2) * (b_l + b_r) + (1 << e1) * (r_t + r_b)) >> 3;

            int32_t b_t = access(comps, 3, x, y, w, h, 0, -1, cf, ct);
            int32_t b_b = access(comps, 3, x, y, w, h, 0, 1, cf, ct);
            int32_t r_l = access(comps, 3, x, y, w, h, -1, 0, cf, ct);
            int32_t r_r = access(comps, 3, x, y, w, h, 1, 0, cf, ct);

            comps[3][y * w + x] -= ((1 << e2) * (b_t + b_b) + (1 << e1) * (r_l + r_r)) >> 3;
        }
    }
}

// Table F.6 — Inverse delta step
void inv_delta_step(int32_t* comps[MAX_COMPONENTS_NUM], int32_t cf, int32_t ct, int32_t w, int32_t h) {
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t y_lt = access(comps, 3, x, y, w, h, -1, -1, cf, ct);
            int32_t y_rt = access(comps, 3, x, y, w, h, 1, -1, cf, ct);
            int32_t y_lb = access(comps, 3, x, y, w, h, -1, 1, cf, ct);
            int32_t y_rb = access(comps, 3, x, y, w, h, 1, 1, cf, ct);

            comps[3][y * w + x] += (y_lt + y_rt + y_lb + y_rb) >> 2;
        }
    }
}

// Table F.5 — Inverse average step
void inv_avg_step(int32_t* comps[MAX_COMPONENTS_NUM], int32_t cf, int32_t ct, int32_t w, int32_t h) {
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t d_lt = access(comps, 0, x, y, w, h, -1, -1, cf, ct);
            int32_t d_rt = access(comps, 0, x, y, w, h, 1, -1, cf, ct);
            int32_t d_lb = access(comps, 0, x, y, w, h, -1, 1, cf, ct);
            int32_t d_rb = access(comps, 0, x, y, w, h, 1, 1, cf, ct);

            comps[0][y * w + x] -= (d_lt + d_rt + d_lb + d_rb) >> 3;
        }
    }
}

// Table F.4 — Inverse Star - Tetrix transform
void inverse_star_tetrix(int32_t* comps[MAX_COMPONENTS_NUM], int32_t cf, int32_t ct, int32_t e1, int32_t e2, int32_t w,
                         int32_t h) {
    inv_avg_step(comps, cf, ct, w, h);
    inv_delta_step(comps, cf, ct, w, h);
    inv_y_step(comps, cf, ct, w, h, e1, e2);
    inv_cbcr_step(comps, cf, ct, w, h);

    int32_t* tmp = comps[0];
    comps[0] = comps[2];
    comps[2] = tmp;
    tmp = comps[1];
    comps[1] = comps[3];
    comps[3] = tmp;
}

// Table F.2 — Inverse reversible multiple component transformation
void inverse_rct(int32_t* comps[MAX_COMPONENTS_NUM], int32_t w, int32_t h) {
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t i0 = comps[0][y * w + x];
            int32_t i1 = comps[1][y * w + x];
            int32_t i2 = comps[2][y * w + x];

            int32_t o1 = i0 - ((i1 + i2) >> 2);
            int32_t o0 = o1 + i2;
            int32_t o2 = o1 + i1;

            comps[0][y * w + x] = o0;
            comps[1][y * w + x] = o1;
            comps[2][y * w + x] = o2;
        }
    }
}

/* Please refer to documentation: Table F.9 CFA Pattern type derived from the component registration*/
int32_t get_cfa_pattern(const picture_header_dynamic_t* picture_header_dynamic) {
    if (picture_header_dynamic->hdr_Xcrg[1] == 32768 && picture_header_dynamic->hdr_Ycrg[1] == 0 &&
        picture_header_dynamic->hdr_Xcrg[2] == 0 && picture_header_dynamic->hdr_Ycrg[2] == 32768) {
        assert((picture_header_dynamic->hdr_Xcrg[0] == 0 && picture_header_dynamic->hdr_Ycrg[0] == 0) ||
               (picture_header_dynamic->hdr_Xcrg[0] == 32768 && picture_header_dynamic->hdr_Ycrg[0] == 32768));
        assert((picture_header_dynamic->hdr_Xcrg[3] == 0 && picture_header_dynamic->hdr_Ycrg[3] == 0) ||
               (picture_header_dynamic->hdr_Xcrg[3] == 32768 && picture_header_dynamic->hdr_Ycrg[3] == 32768));
        // RGGB or BGGR
        return 0;
    }
    else {
        assert(picture_header_dynamic->hdr_Xcrg[1] == 0 && picture_header_dynamic->hdr_Ycrg[1] == 0);
        assert(picture_header_dynamic->hdr_Xcrg[2] == 32768 && picture_header_dynamic->hdr_Ycrg[2] == 32768);
        assert((picture_header_dynamic->hdr_Xcrg[0] == 32768 && picture_header_dynamic->hdr_Ycrg[0] == 0) ||
               (picture_header_dynamic->hdr_Xcrg[0] == 0 && picture_header_dynamic->hdr_Ycrg[0] == 32768));
        assert((picture_header_dynamic->hdr_Xcrg[3] == 32768 && picture_header_dynamic->hdr_Ycrg[3] == 0) ||
               (picture_header_dynamic->hdr_Xcrg[3] == 0 && picture_header_dynamic->hdr_Ycrg[3] == 32768));
        // GRBG or GBRG
        return 1;
    }
}

void mct_inverse_transform(int32_t* out_comps[MAX_COMPONENTS_NUM], const pi_t* pi,
                           const picture_header_dynamic_t* picture_header_dynamic, uint8_t hdr_Cpih) {
    int32_t w = pi->width;
    int32_t h = pi->height;
    if (hdr_Cpih == 1) {
        // Reversible RGB to YCbCr colour transformation
        inverse_rct(out_comps, w, h);
    }
    else if (hdr_Cpih == 3) {
        int32_t ct = get_cfa_pattern(picture_header_dynamic);
        inverse_star_tetrix(out_comps,
                            picture_header_dynamic->hdr_Cf,
                            ct,
                            picture_header_dynamic->hdr_Cf_e1,
                            picture_header_dynamic->hdr_Cf_e2,
                            w,
                            h);
    }
    else {
        fprintf(stderr, "Unknown color transform!!\n");
        assert(0);
    }
}

void mct_inverse_transform_precinct(int32_t* out_comps[MAX_COMPONENTS_NUM],
                                    const picture_header_dynamic_t* picture_header_dynamic, int32_t w, int32_t h,
                                    uint8_t hdr_Cpih) {
    if (hdr_Cpih == 1) {
        // Reversible RGB to YCbCr colour transformation
        inverse_rct(out_comps, w, h);
    }
    else if (hdr_Cpih == 3) {
        int32_t ct = get_cfa_pattern(picture_header_dynamic);
        inverse_star_tetrix(out_comps,
                            picture_header_dynamic->hdr_Cf,
                            ct,
                            picture_header_dynamic->hdr_Cf_e1,
                            picture_header_dynamic->hdr_Cf_e2,
                            w,
                            h);
    }
    else {
        fprintf(stderr, "Unknown color transform!!\n");
        assert(0);
    }
}
