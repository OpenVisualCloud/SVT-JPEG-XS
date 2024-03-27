/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "DwtDecoder.h"
#include <assert.h>
#include "SvtUtility.h"
#include "decoder_dsp_rtcd.h"
#include "Idwt.h"

inv_transform_V0_ptr_t inv_transform_V0_get_function_ptr(uint8_t decom_h) {
    inv_transform_V0_ptr_t ptrs[] = {
        NULL, inv_transform_V0_H1, inv_transform_V0_H2, inv_transform_V0_H3, inv_transform_V0_H4, inv_transform_V0_H5};
    if (decom_h < sizeof(ptrs) / sizeof(ptrs[0])) {
        return ptrs[decom_h];
    }
    return NULL;
}

/*
  * n- max band number
                                                STAGE 1               STAGE 2
    |-|-|--|----|---------|----------------------|  |------------------|  |------------------|
 l0 |0| |  |    |         |        decom_h       |  |        lf        |  |      line_0      |
    |-|-|--|----|---------|----------------------|=>|------------------|=>|                  |
 l1 |    decom_h + 1      |      decom_h + 2     |  |        hf        |  |      line_1      |
    |---------------------|----------------------|  |------------------|  |------------------|


xxx-filled after previous call
yyy-filled after this call
               START             After 1st FUNCTION CALL   After 2nd FUNCTION CALL   After 3rd FUNCTION CALL   After last FUNCTION CALL
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_0      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
prec-0  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_1      |      |      line_1      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_2      |      |      line_2      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
prec-1  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_3      |      |      line_3      |      |      line_3      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_4      |  =>  |      line_4      |  =>  |      line_4      |  =>  |yyyyyyyyyyyyyyyyyy|  =>  |xxxxxxxxxxxxxxxxxx|
prec-2  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_5      |      |      line_5      |      |      line_5      |      |      line_5      |      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_6      |      |      line_6      |      |      line_6      |      |      line_6      |      |xxxxxxxxxxxxxxxxxx|
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
        |    line 2*n-2    |      |    line 2*n-2    |      |    line 2*n-2    |      |    line 2*n-2    |      |yyyyyyyyyyyyyyyyyy|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |    line 2*n-1    |      |    line 2*n-1    |      |    line 2*n-1    |      |    line 2*n-1    |      |yyyyyyyyyyyyyyyyyy|
prec-n  |                  |      |                  |      |                  |      |                  |      |                  |
last    |    line 2*n      |      |    line 2*n      |      |    line 2*n      |      |    line 2*n      |      |yyyyyyyyyyyyyyyyyy|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|

buffer_tmp Required size: 3 * component->width
*/
void transform_component_line_V1_Hx(const pi_component_t* const component, int16_t* buffer_in[MAX_BANDS_PER_COMPONENT_NUM],
                                    int32_t* buffer_out[4], uint32_t precinct_line_idx, int32_t* buffer_tmp,
                                    uint32_t precinct_num, uint32_t idwt_idx, uint32_t len, uint8_t shift) {
    inv_transform_V0_ptr_t inv_transform_V0_Hn = inv_transform_V0_get_function_ptr(idwt_idx);
    int32_t height = component->bands[0].height + component->bands[idwt_idx + 1].height;
    uint32_t if_first_precinct = (precinct_line_idx == 0);
    uint32_t if_last_precinct = (precinct_line_idx == (precinct_num - 1));
    int16_t* buf_hf_1 = buffer_in[idwt_idx + 1];
    int16_t* buf_hf_2 = buffer_in[idwt_idx + 2];

    int32_t* hf_0 = buffer_tmp + (precinct_line_idx % 2) * len;
    int32_t* hf_1 = buffer_tmp + ((precinct_line_idx + 1) % 2) * len;
    int32_t* lf = buffer_tmp + 2 * len;
    int32_t* lf_buff_tmp = hf_1;

    //STAGE 1 lf
    inv_transform_V0_Hn(component, buffer_in, lf, lf_buff_tmp, shift);
    //STAGE 1 hf
    if (!((height & 1) && if_last_precinct)) {
        idwt_horizontal_line_lf16_hf16(buf_hf_1, buf_hf_2, hf_1, len, shift);
    }
    //STAGE 2
    idwt_vertical_line(lf, hf_0, hf_1, buffer_out, len, if_first_precinct, if_last_precinct, height);
}

/*
xxx-filled after previous call
yyy-filled after this call
               START             After 1st FUNCTION CALL   After 2nd FUNCTION CALL   After 3rd FUNCTION CALL   After last FUNCTION CALL
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_0      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_1      |      |      line_1      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
prec-0  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_2      |      |      line_2      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_3      |      |      line_3      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_4      |  =>  |      line_4      |  =>  |yyyyyyyyyyyyyyyyyy|  =>  |xxxxxxxxxxxxxxxxxx|  =>  |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_5      |      |      line_5      |      |      line_5      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|
prec-1  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_6      |      |      line_6      |      |      line_6      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_6      |      |      line_6      |      |      line_6      |      |yyyyyyyyyyyyyyyyyy|      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_7      |  =>  |      line_7      |  =>  |      line_7      |  =>  |yyyyyyyyyyyyyyyyyy|  =>  |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_8      |      |      line_8      |      |      line_8      |      |      line_8      |      |xxxxxxxxxxxxxxxxxx|
prec-2  |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_9      |      |      line_9      |      |      line_9      |      |      line_9      |      |xxxxxxxxxxxxxxxxxx|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |      line_10     |      |      line_10     |      |      line_10     |      |      line_10     |      |xxxxxxxxxxxxxxxxxx|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |      line_11     |  =>  |     line_11      |  =>  |      line_11     |  =>  |      line_11     |  =>  |xxxxxxxxxxxxxxxxxx|
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
        |    line 2*n-6    |      |    line 2*n-6    |      |    line 2*n-6    |      |    line 2*n-6    |      |yyyyyyyyyyyyyyyyyy|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |    line 2*n-5    |      |    line 2*n-5    |      |    line 2*n-5    |      |    line 2*n-5    |      |yyyyyyyyyyyyyyyyyy|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |    line 2*n-4    |      |    line 2*n-4    |      |    line 2*n-4    |      |    line 2*n-4    |      |yyyyyyyyyyyyyyyyyy|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|
        |    line 4*n-3    |      |    line 4*n-3    |      |    line 4*n-3    |      |    line 4*n-3    |      |yyyyyyyyyyyyyyyyyy|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |    line 4*n-2    |      |    line 4*n-2    |      |    line 4*n-2    |      |    line 4*n-2    |      |yyyyyyyyyyyyyyyyyy|
prec-n  |                  |      |                  |      |                  |      |                  |      |                  |
last    |    line 4*n-1    |      |    line 4*n-1    |      |    line 4*n-1    |      |    line 4*n-1    |      |yyyyyyyyyyyyyyyyyy|
        |                  |      |                  |      |                  |      |                  |      |                  |
        |    line 4*n      |      |    line 4*n      |      |    line 4*n      |      |    line 4*n      |      |yyyyyyyyyyyyyyyyyy|
        |------------------|      |------------------|      |------------------|      |------------------|      |------------------|

    n- max band number
                                                STAGE 1                              STAGE 2                              STAGE 3                  STAGE 4
    |-|-|--|----|---------|----------------------|  |----------------|----------------|  |----------------|----------------|  |---------------------|  |----------------------|
 l0 |0| |  |    |decom_h-1|                      |  |    lf_lf_l0    |    hf_lf_l0    |  |    lf_lf_l0    |    hf_lf_l0    |  |        lf_l0        |  |        line_0        |
    |-|-|--|----|---------|         n - 2        |  |----------------|                |  |                |                |  |                     |  |                      |
 l1 |   n - 4   |  n - 3  |                      |  |    lf_lf_l1    |    hf_lf_l1    |  |    lf_lf_l1    |    hf_lf_l1    |  |        lf_l1        |  |        line_1        |
    |-----------|---------|----------------------|=>|----------------|----------------|=>|----------------|----------------|=>|---------------------|=>|                      |
 l2 |                     |                      |  |    lf_hf_l0    |    hf_hf_l0    |  |    lf_hf_l0    |    hf_hf_l0    |  |        hf_l0        |  |        line_2        |
    |         n - 1       |          n           |  |                |                |  |                |                |  |                     |  |                      |
 l3 |                     |                      |  |    lf_hf_l1    |    hf_hf_l1    |  |    lf_hf_l1    |    hf_hf_l1    |  |        hf_l1        |  |        line_3        |
    |---------------------|----------------------|  |----------------|----------------|  |----------------|----------------|  |---------------------|  |----------------------|
buffer_tmp Required size:
    Input for stage1 and stage2:    3 * V1_len
    Output for stage 1 and stage2:  4 * V1_len
    Output for stage 3:             3 * component->width
    */
void transform_component_line_V2_Hx(const pi_component_t* const component, int16_t* buffer_in[MAX_BANDS_PER_COMPONENT_NUM],
                                    int16_t* buffer_in_prev[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buffer_out[8],
                                    uint32_t precinct_line_idx, int32_t* buffer_tmp, uint32_t precinct_num, uint32_t idwt_idx,
                                    uint8_t shift) {
    uint32_t V1_len = (component->width / 2) + (component->width & 1);
    int32_t* tmp_buffer_out = buffer_tmp + 3 * V1_len;

    int32_t* V1_buffer_out[4] = {
        tmp_buffer_out + ((2 * precinct_line_idx + 0) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 1) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 2) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 3) % 4) * V1_len,
    };

    //STAGE 1 and STAGE 2
    //buffer_tmp Required size: 3 * V1_len, ~1.5 * component->width
    //V1_buffer_out Required size: 4 * V1_len, ~2 * component->width
    //DO NOT MODIFY tmp_buffer_out 1st and 2nd line!!!!!
    transform_component_line_V1_Hx(
        component, buffer_in, V1_buffer_out, precinct_line_idx, buffer_tmp, precinct_num, idwt_idx, V1_len, shift);

    tmp_buffer_out += 4 * V1_len;

    //Corner case: height is smaller or equal to 4
    if (component->height <= 4) {
        //stage 3
        int32_t* lf_lf_l0 = V1_buffer_out[2];
        int16_t* hf_lf_l0 = buffer_in[component->bands_num - 3];

        int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;
        idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

        int16_t* lf_hf_l0 = buffer_in[component->bands_num - 2];
        int16_t* hf_hf_l0 = buffer_in[component->bands_num - 1];

        int32_t* hf_l0 = tmp_buffer_out + 1 * component->width;
        int32_t* hf_l1 = tmp_buffer_out + 2 * component->width;
        idwt_horizontal_line_lf16_hf16(lf_hf_l0, hf_hf_l0, hf_l0, component->width, shift);

        if (component->height == 4) {
            int16_t* lf_hf_l1 = buffer_in[component->bands_num - 2] + component->bands[component->bands_num - 2].width;
            int16_t* hf_hf_l1 = buffer_in[component->bands_num - 1] + component->bands[component->bands_num - 1].width;
            idwt_horizontal_line_lf16_hf16(lf_hf_l1, hf_hf_l1, hf_l1, component->width, shift);
        }
        else {
            hf_l1 = NULL;
        }

        //stage 4
        idwt_vertical_line(
            lf_l0, NULL, hf_l0, buffer_out + 2, component->width, 1 /*1st prec*/, 0 /*last prec*/, component->height);

        //stage 3
        lf_lf_l0 = V1_buffer_out[3];
        hf_lf_l0 = buffer_in[component->bands_num - 3] + component->bands[component->bands_num - 3].width;
        idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

        //stage 4
        idwt_vertical_line(
            lf_l0, hf_l0, hf_l1, buffer_out + 4, component->width, 0 /*1st prec*/, 1 /*last prec*/, component->height);

        return;
    }

    //Corner case: first precinct in component
    if (precinct_line_idx == 0) {
        //stage 3
        int32_t* lf_lf_l0 = V1_buffer_out[2];
        int16_t* hf_lf_l0 = buffer_in[component->bands_num - 3];

        int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;
        idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

        int16_t* lf_hf_l0 = buffer_in[component->bands_num - 2];
        int16_t* hf_hf_l0 = buffer_in[component->bands_num - 1];

        int32_t* hf_l0 = tmp_buffer_out + 1 * component->width;
        int32_t* hf_l1 = tmp_buffer_out + 2 * component->width;
        idwt_horizontal_line_lf16_hf16(lf_hf_l0, hf_hf_l0, hf_l0, component->width, shift);

        int16_t* lf_hf_l1 = buffer_in[component->bands_num - 2] + component->bands[component->bands_num - 2].width;
        int16_t* hf_hf_l1 = buffer_in[component->bands_num - 1] + component->bands[component->bands_num - 1].width;
        idwt_horizontal_line_lf16_hf16(lf_hf_l1, hf_hf_l1, hf_l1, component->width, shift);

        //stage 4
        idwt_vertical_line(
            lf_l0, NULL, hf_l0, buffer_out + 2, component->width, 1 /*1st prec*/, 0 /*last prec*/, component->height);

        return;
    }

    //Corner case: last precinct in component
    if (precinct_line_idx == (precinct_num - 1)) {
        int32_t early_skip = (component->height % 4) == 1 || (component->height % 4) == 2;
        for (uint32_t i = 0; i < 2; i++) {
            //stage 3
            int32_t* lf_lf_l0 = V1_buffer_out[i + 1];
            int16_t* hf_lf_l0 = i ? buffer_in[component->bands_num - 3]
                                  : (buffer_in_prev[component->bands_num - 3] + component->bands[component->bands_num - 3].width);
            int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;

            idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

            int32_t* hf_l0 = tmp_buffer_out + (1 + ((i + 0) % 2)) * component->width;
            int32_t* hf_l1 = tmp_buffer_out + (1 + ((i + 1) % 2)) * component->width;

            //stage 4
            idwt_vertical_line(lf_l0,
                               hf_l0,
                               hf_l1,
                               buffer_out,
                               component->width,
                               0 /*1st prec*/,
                               i && early_skip /*last prec*/,
                               component->height);

            buffer_out += 2;

            if (i && early_skip) {
                return;
            }

            if (((component->height % 4) == 1) || (i == 1 && ((component->height % 4) == 3))) {
                continue;
            }

            int16_t* hf_hf_l0 = buffer_in[component->bands_num - 1] + i * component->bands[component->bands_num - 1].width;
            int16_t* lf_hf_l0 = buffer_in[component->bands_num - 2] + i * component->bands[component->bands_num - 2].width;
            idwt_horizontal_line_lf16_hf16(lf_hf_l0, hf_hf_l0, hf_l0, component->width, shift);
        }

        //stage 3
        int32_t* lf_lf_l0 = V1_buffer_out[3];
        int16_t* hf_lf_l0 = buffer_in[component->bands_num - 3] + component->bands[component->bands_num - 3].width;

        int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;
        idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

        int32_t* hf_l0 = tmp_buffer_out + 1 * component->width;
        int32_t* hf_l1 = tmp_buffer_out + 2 * component->width;

        //stage 4
        idwt_vertical_line(lf_l0, hf_l0, hf_l1, buffer_out, component->width, 0 /*1st prec*/, 1 /*last prec*/, component->height);

        return;
    }

    for (uint32_t i = 0; i < 2; i++) {
        //stage 3
        int32_t* lf_lf_l0 = V1_buffer_out[i + 1];
        int16_t* hf_lf_l0 = i ? buffer_in[component->bands_num - 3]
                              : (buffer_in_prev[component->bands_num - 3] + component->bands[component->bands_num - 3].width);
        int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;

        idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

        int32_t* hf_l0 = tmp_buffer_out + (1 + ((i + 0) % 2)) * component->width;
        int32_t* hf_l1 = tmp_buffer_out + (1 + ((i + 1) % 2)) * component->width;

        //stage 4
        idwt_vertical_line(lf_l0, hf_l0, hf_l1, buffer_out, component->width, 0 /*1st prec*/, 0 /*last prec*/, component->height);

        int16_t* hf_hf_l0 = buffer_in[component->bands_num - 1] + i * component->bands[component->bands_num - 1].width;
        int16_t* lf_hf_l0 = buffer_in[component->bands_num - 2] + i * component->bands[component->bands_num - 2].width;
        idwt_horizontal_line_lf16_hf16(lf_hf_l0, hf_hf_l0, hf_l0, component->width, shift);

        buffer_out += 2;
    }
}

void new_transform_component_line(const pi_component_t* const component, int16_t* buffer_in[MAX_BANDS_PER_COMPONENT_NUM],
                                  int16_t* buffer_in_prev[MAX_BANDS_PER_COMPONENT_NUM], transform_lines_t* lines,
                                  uint32_t precinct_line_idx, int32_t* buffer_tmp, uint32_t precinct_num, uint8_t shift) {
    int decom_h = component->decom_h;
    int decom_v = component->decom_v;

    lines->offset = 0;

    if (decom_v == 0) {
        inv_transform_V0_ptr_t inv_transform_V0_Hn = inv_transform_V0_get_function_ptr(decom_h);
        if (decom_h == 0) {
            for (uint32_t idx = 0; idx < component->width; idx++) {
                lines->buffer_out[0][idx] = (int32_t)(buffer_in[0][idx]) << shift;
            }
        }
        else {
            inv_transform_V0_Hn(component, buffer_in, lines->buffer_out[0], buffer_tmp, shift);
        }
        lines->line_start = 0;
        lines->line_stop = 0;
    }
    else if (decom_v == 1) {
        transform_component_line_V1_Hx(component,
                                       buffer_in,
                                       lines->buffer_out,
                                       precinct_line_idx,
                                       buffer_tmp,
                                       precinct_num,
                                       decom_h,
                                       component->width,
                                       shift);
        if (precinct_line_idx == 0) {
            lines->line_start = 2;
            if (component->height == 2) {
                lines->line_stop = 3;
            }
            else {
                lines->line_stop = 2;
            }
        }
        else if (precinct_line_idx == (precinct_num - 1)) {
            uint32_t v1_leftover[2] = {3, 2};
            lines->line_start = 1;
            lines->line_stop = MIN(v1_leftover[component->height % 2], component->height);
            lines->offset = -1;
        }
        else {
            lines->line_start = 1;
            lines->line_stop = 2;
            lines->offset = -1;
        }
    }
    else if (decom_v == 2) {
        transform_component_line_V2_Hx(component,
                                       buffer_in,
                                       buffer_in_prev,
                                       lines->buffer_out,
                                       precinct_line_idx,
                                       buffer_tmp,
                                       precinct_num,
                                       (decom_h - 1),
                                       shift);
        if (precinct_line_idx == 0) {
            lines->line_start = 4;
            if (component->height <= 4) {
                lines->line_stop = 3 + component->height;
            }
            else {
                lines->line_stop = 4;
            }
        }
        else if (precinct_line_idx == (precinct_num - 1)) {
            uint32_t v2_leftover[4] = {7, 4, 5, 6};
            lines->line_start = 1;
            lines->line_stop = MIN(v2_leftover[component->height % 4], component->height);
            lines->offset = -3;
        }
        else {
            lines->line_start = 1;
            lines->line_stop = 4;
            lines->offset = -3;
        }
    }
}

/*
  * n- max band number
       |-|-|--|----|---------|----------------------|  |------------------|
       |0| |  |    |         |                      |  |                  |
prev_2 |-|-|--|----|---------|----------------------|=>|------------------|
       |      buf_hf_1       |       buf_hf_2       |  |      hf_0        |
       |---------------------|----------------------|  |------------------|

       |-|-|--|----|---------|----------------------|  |------------------|
       |0| |  |    |         |        decom_h       |  |        lf        |
prev_1 |-|-|--|----|---------|----------------------|=>|------------------|
       |      buf_hf_2       |       buf_hf_3       |  |      hf_1        |
       |---------------------|----------------------|  |------------------|
*/
void transform_component_line_V1_Hx_recalc(const pi_component_t* const component,
                                           int16_t* buffer_in_prev_2[MAX_BANDS_PER_COMPONENT_NUM],
                                           int16_t* buffer_in_prev_1[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buffer_out[4],
                                           uint32_t precinct_line_idx, int32_t* buffer_tmp, uint32_t idwt_idx, uint32_t len,
                                           uint8_t shift) {
    inv_transform_V0_ptr_t inv_transform_V0_Hn = inv_transform_V0_get_function_ptr(idwt_idx);
    int32_t* lf = buffer_tmp + 2 * len;
    int32_t* lf_buff_tmp = buffer_tmp + 1 * len;
    inv_transform_V0_Hn(component, buffer_in_prev_1, lf, lf_buff_tmp, shift);

    int32_t* hf_0 = buffer_tmp + ((precinct_line_idx + 1) % 2) * len;
    if (precinct_line_idx > 1) {
        int16_t* buf_hf_1 = buffer_in_prev_2[idwt_idx + 1];
        int16_t* buf_hf_2 = buffer_in_prev_2[idwt_idx + 2];
        idwt_horizontal_line_lf16_hf16(buf_hf_1, buf_hf_2, hf_0, len, shift);
    }

    int16_t* buf_hf_3 = buffer_in_prev_1[idwt_idx + 1];
    int16_t* buf_hf_4 = buffer_in_prev_1[idwt_idx + 2];
    int32_t* hf_1 = buffer_tmp + (precinct_line_idx % 2) * len;
    idwt_horizontal_line_lf16_hf16(buf_hf_3, buf_hf_4, hf_1, len, shift);

    idwt_vertical_line_recalc(lf, hf_0, hf_1, buffer_out, len, precinct_line_idx);
}

void transform_component_line_V2_Hx_recalc(const pi_component_t* const component,
                                           int16_t* buffer_in_prev_2[MAX_BANDS_PER_COMPONENT_NUM],
                                           int16_t* buffer_in_prev_1[MAX_BANDS_PER_COMPONENT_NUM], int32_t* buffer_out[8],
                                           uint32_t precinct_line_idx, int32_t* buffer_tmp, uint32_t idwt_idx, uint8_t shift) {
    uint32_t V1_len = (component->width / 2) + (component->width & 1);
    int32_t* tmp_buffer_out = buffer_tmp + 3 * V1_len;

    int32_t* V1_buffer_out[4] = {
        tmp_buffer_out + ((2 * precinct_line_idx + 0) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 1) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 2) % 4) * V1_len,
        tmp_buffer_out + ((2 * precinct_line_idx + 3) % 4) * V1_len,
    };

    transform_component_line_V1_Hx_recalc(
        component, buffer_in_prev_2, buffer_in_prev_1, V1_buffer_out, precinct_line_idx, buffer_tmp, idwt_idx, V1_len, shift);

    tmp_buffer_out += 4 * V1_len;

    int32_t* hf_l1 = tmp_buffer_out + 2 * component->width;
    if (precinct_line_idx > 1) {
        int16_t* hf_hf_l1 = buffer_in_prev_2[component->bands_num - 1] + component->bands[component->bands_num - 1].width;
        int16_t* lf_hf_l1 = buffer_in_prev_2[component->bands_num - 2] + component->bands[component->bands_num - 2].width;
        idwt_horizontal_line_lf16_hf16(lf_hf_l1, hf_hf_l1, hf_l1, component->width, shift);
    }

    int32_t* lf_lf_l0 = V1_buffer_out[0];
    int16_t* hf_lf_l0 = buffer_in_prev_1[component->bands_num - 3];
    int32_t* lf_l0 = tmp_buffer_out + 0 * component->width;
    idwt_horizontal_line_lf32_hf16(lf_lf_l0, hf_lf_l0, lf_l0, component->width, shift);

    int32_t* hf_l0 = tmp_buffer_out + 1 * component->width;
    int16_t* hf_hf_l0 = buffer_in_prev_1[component->bands_num - 1];
    int16_t* lf_hf_l0 = buffer_in_prev_1[component->bands_num - 2];
    idwt_horizontal_line_lf16_hf16(lf_hf_l0, hf_hf_l0, hf_l0, component->width, shift);

    idwt_vertical_line_recalc(lf_l0, hf_l1, hf_l0, buffer_out, component->width, precinct_line_idx);

    int16_t* hf_hf_l1 = buffer_in_prev_1[component->bands_num - 1] + 1 * component->bands[component->bands_num - 1].width;
    int16_t* lf_hf_l1 = buffer_in_prev_1[component->bands_num - 2] + 1 * component->bands[component->bands_num - 2].width;
    idwt_horizontal_line_lf16_hf16(lf_hf_l1, hf_hf_l1, hf_l1, component->width, shift);
}

void new_transform_component_line_recalc(const pi_component_t* const component,
                                         int16_t* buffer_in_prev_2[MAX_BANDS_PER_COMPONENT_NUM],
                                         int16_t* buffer_in_prev_1[MAX_BANDS_PER_COMPONENT_NUM], int32_t** buffer_out,
                                         uint32_t precinct_line_idx, int32_t* buffer_tmp, uint8_t shift) {
    int decom_h = component->decom_h;
    int decom_v = component->decom_v;

    if (decom_v == 0 || precinct_line_idx == 0) {
        return;
    }
    else if (decom_v == 1) {
        transform_component_line_V1_Hx_recalc(component,
                                              buffer_in_prev_2,
                                              buffer_in_prev_1,
                                              buffer_out,
                                              precinct_line_idx,
                                              buffer_tmp,
                                              decom_h,
                                              component->width,
                                              shift);
    }
    else if (decom_v == 2) {
        transform_component_line_V2_Hx_recalc(
            component, buffer_in_prev_2, buffer_in_prev_1, buffer_out, precinct_line_idx, buffer_tmp, (decom_h - 1), shift);
    }
}
