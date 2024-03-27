/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Idwt.h"
#include "Definitions.h"
#include "decoder_dsp_rtcd.h"
#include <assert.h>

void idwt_horizontal_line_lf16_hf16_c(const int16_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    out[0] = ((int32_t)in_lf[0] << shift) - ((((int32_t)in_hf[0] << shift) + 1) >> 1);

    for (uint32_t i = 1; i < len - 2; i += 2) {
        out[2] = ((int32_t)in_lf[1] << shift) - ((((int32_t)in_hf[0] << shift) + ((int32_t)in_hf[1] << shift) + 2) >> 2);
        out[1] = ((int32_t)in_hf[0] << shift) + ((out[0] + out[2]) >> 1);
        in_lf++;
        in_hf++;
        out += 2;
    }
    if (len & 1) {
        out[2] = ((int32_t)in_lf[1] << shift) - ((((int32_t)in_hf[0] << shift) + 1) >> 1);
        out[1] = ((int32_t)in_hf[0] << shift) + ((out[0] + out[2]) >> 1);
    }
    else { //!(len & 1)
        out[1] = ((int32_t)in_hf[0] << shift) + out[0];
    }
}

void idwt_horizontal_line_lf32_hf16_c(const int32_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len, uint8_t shift) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    out[0] = in_lf[0] - ((((int32_t)in_hf[0] << shift) + 1) >> 1);

    for (uint32_t i = 1; i < len - 2; i += 2) {
        out[2] = in_lf[1] - ((((int32_t)in_hf[0] << shift) + ((int32_t)in_hf[1] << shift) + 2) >> 2);
        out[1] = ((int32_t)in_hf[0] << shift) + ((out[0] + out[2]) >> 1);
        in_lf++;
        in_hf++;
        out += 2;
    }
    if (len & 1) {
        out[2] = in_lf[1] - ((((int32_t)in_hf[0] << shift) + 1) >> 1);
        out[1] = ((int32_t)in_hf[0] << shift) + ((out[0] + out[2]) >> 1);
    }
    else { //!(len & 1)
        out[1] = ((int32_t)in_hf[0] << shift) + out[0];
    }
}

void inv_transform_V0_H1(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift) {
    UNUSED(buf_out_tmp);
    assert(component->bands_num >= 2);

    const uint32_t width_0 = component->bands[0].width;
    const uint32_t width_1 = component->bands[1].width;
    const uint32_t width = width_0 + width_1;

    int16_t* buf_in_0 = buff_in[0];
    int16_t* buf_in_1 = buff_in[1];

    int32_t* buf_out_ = buf_out;

    idwt_horizontal_line_lf16_hf16(buf_in_0, buf_in_1, buf_out_, width, shift);
}

void inv_transform_V0_H2(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift) {
    assert(component->bands_num >= 3);

    const uint32_t width_0 = component->bands[0].width;
    const uint32_t width_1 = component->bands[1].width;
    const uint32_t width_2 = component->bands[2].width;

    const uint32_t width_01 = width_0 + width_1;
    const uint32_t width_012 = width_01 + width_2;

    int16_t* buf_in_0 = buff_in[0];
    int16_t* buf_in_1 = buff_in[1];
    int16_t* buf_in_2 = buff_in[2];

    int32_t* buf_out_ = buf_out;

    idwt_horizontal_line_lf16_hf16(buf_in_0, buf_in_1, buf_out_tmp, width_01, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_2, buf_out_, width_012, shift);
}

void inv_transform_V0_H3(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift) {
    assert(component->bands_num >= 4);

    const uint32_t width_0 = component->bands[0].width;
    const uint32_t width_1 = component->bands[1].width;
    const uint32_t width_2 = component->bands[2].width;
    const uint32_t width_3 = component->bands[3].width;

    const uint32_t width_01 = width_0 + width_1;
    const uint32_t width_012 = width_01 + width_2;
    const uint32_t width_0123 = width_012 + width_3;

    int16_t* buf_in_0 = buff_in[0];
    int16_t* buf_in_1 = buff_in[1];
    int16_t* buf_in_2 = buff_in[2];
    int16_t* buf_in_3 = buff_in[3];

    int32_t* buf_out_ = buf_out;

    idwt_horizontal_line_lf16_hf16(buf_in_0, buf_in_1, buf_out_, width_01, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_, buf_in_2, buf_out_tmp, width_012, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_3, buf_out_, width_0123, shift);
}

void inv_transform_V0_H4(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift) {
    assert(component->bands_num >= 5);

    const uint32_t width_0 = component->bands[0].width;
    const uint32_t width_1 = component->bands[1].width;
    const uint32_t width_2 = component->bands[2].width;
    const uint32_t width_3 = component->bands[3].width;
    const uint32_t width_4 = component->bands[4].width;

    const uint32_t width_01 = width_0 + width_1;
    const uint32_t width_012 = width_01 + width_2;
    const uint32_t width_0123 = width_012 + width_3;
    const uint32_t width_01234 = width_0123 + width_4;

    int16_t* buf_in_0 = buff_in[0];
    int16_t* buf_in_1 = buff_in[1];
    int16_t* buf_in_2 = buff_in[2];
    int16_t* buf_in_3 = buff_in[3];
    int16_t* buf_in_4 = buff_in[4];

    int32_t* buf_out_ = buf_out;

    idwt_horizontal_line_lf16_hf16(buf_in_0, buf_in_1, buf_out_tmp, width_01, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_2, buf_out_, width_012, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_, buf_in_3, buf_out_tmp, width_0123, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_4, buf_out_, width_01234, shift);
}

void inv_transform_V0_H5(const pi_component_t* const component, int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buf_out,
                         int32_t* buf_out_tmp, uint8_t shift) {
    assert(component->bands_num >= 6);

    const uint32_t width_0 = component->bands[0].width;
    const uint32_t width_1 = component->bands[1].width;
    const uint32_t width_2 = component->bands[2].width;
    const uint32_t width_3 = component->bands[3].width;
    const uint32_t width_4 = component->bands[4].width;
    const uint32_t width_5 = component->bands[5].width;

    const uint32_t width_01 = width_0 + width_1;
    const uint32_t width_012 = width_01 + width_2;
    const uint32_t width_0123 = width_012 + width_3;
    const uint32_t width_01234 = width_0123 + width_4;
    const uint32_t width_012345 = width_01234 + width_5;

    int16_t* buf_in_0 = buff_in[0];
    int16_t* buf_in_1 = buff_in[1];
    int16_t* buf_in_2 = buff_in[2];
    int16_t* buf_in_3 = buff_in[3];
    int16_t* buf_in_4 = buff_in[4];
    int16_t* buf_in_5 = buff_in[5];

    int32_t* buf_out_ = buf_out;

    idwt_horizontal_line_lf16_hf16(buf_in_0, buf_in_1, buf_out_, width_01, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_, buf_in_2, buf_out_tmp, width_012, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_3, buf_out_, width_0123, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_, buf_in_4, buf_out_tmp, width_01234, shift);
    idwt_horizontal_line_lf32_hf16(buf_out_tmp, buf_in_5, buf_out_, width_012345, shift);
}

/*
out[0] -> line with idx -2,
out[1] -> line with idx -1,
out[2] -> line with idx  0,
out[3] -> line with idx  1,
*/
void idwt_vertical_line_c(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4], uint32_t len,
                          int32_t first_precinct, int32_t last_precinct, int32_t height) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");
    //Corner case: height is equal to 2
    if (height == 2) {
        int32_t* out_2 = out[2];
        int32_t* out_3 = out[3];
        for (uint32_t i = 0; i < len; i++) {
            out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_3[0] = in_hf1[0] + (out_2[0]);
            out_2++;
            out_3++;
            in_lf++;
            in_hf1++;
        }
        return;
    }
    //Corner case: first precinct in component
    if (first_precinct) {
        int32_t* out_2 = out[2];
        for (uint32_t i = 0; i < len; i++) {
            out_2[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_2++;
            in_lf++;
            in_hf1++;
        }
        return;
    }
    //Corner case: last precinct in component, height odd
    if (last_precinct && (height & 1)) {
        int32_t* out_0 = out[0];
        int32_t* out_1 = out[1];
        int32_t* out_2 = out[2];
        for (uint32_t i = 0; i < len; i++) {
            out_2[0] = in_lf[0] - ((in_hf0[0] + 1) >> 1);
            out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            out_0++;
            out_1++;
            out_2++;
            in_lf++;
            in_hf0++;
        }
        return;
    }
    //Corner case: last precinct in component, height even
    if (last_precinct && (!(height & 1))) {
        int32_t* out_0 = out[0];
        int32_t* out_1 = out[1];
        int32_t* out_2 = out[2];
        int32_t* out_3 = out[3];
        for (uint32_t i = 0; i < len; i++) {
            out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
            out_3[0] = in_hf1[0] + (out_2[0]);
            out_0++;
            out_1++;
            out_2++;
            out_3++;
            in_lf++;
            in_hf0++;
            in_hf1++;
        }
        return;
    }
    int32_t* out_0 = out[0];
    int32_t* out_1 = out[1];
    int32_t* out_2 = out[2];
    for (uint32_t i = 0; i < len; i++) {
        out_2[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
        out_1[0] = in_hf0[0] + ((out_0[0] + out_2[0]) >> 1);
        out_0++;
        out_1++;
        out_2++;
        in_lf++;
        in_hf0++;
        in_hf1++;
    }
}

void idwt_vertical_line_recalc_c(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                 uint32_t len, uint32_t precinct_line_idx) {
    assert((len >= 2) && "[idwt_c()] ERROR: Length is too small!");

    int32_t* out_0 = out[0];
    if (precinct_line_idx > 1) {
        for (uint32_t i = 0; i < len; i++) {
            out_0[0] = in_lf[0] - ((in_hf0[0] + in_hf1[0] + 2) >> 2);
            out_0++;
            in_lf++;
            in_hf0++;
            in_hf1++;
        }
    }
    else {
        for (uint32_t i = 0; i < len; i++) {
            out_0[0] = in_lf[0] - ((in_hf1[0] + 1) >> 1);
            out_0++;
            in_lf++;
            in_hf1++;
        }
    }
}
