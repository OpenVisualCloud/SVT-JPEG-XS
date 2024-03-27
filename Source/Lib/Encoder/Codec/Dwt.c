/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dwt.h"
#include <assert.h>
#include "encoder_dsp_rtcd.h"
#include "NltEnc.h"

/* Cacluate Horizontal DWT for one line.
 * For one line stride is not required, becasue in line step is always 1.
 * Pointer "in" can be that same like "out_hf" or "out_lf" because calculation will not override used data.
 */
void dwt_horizontal_line_c(int32_t* out_lf, int32_t* out_hf, const int32_t* in, uint32_t len) {
    assert((len >= 2) && "[dwt_horizontal_line_c()] ERROR: Length is too small!");

    if (len == 2) {
        out_hf[0] = in[1] - in[0];
        out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);
        return;
    }

    out_hf[0] = in[1] - ((in[0] + in[2]) >> 1);
    out_lf[0] = in[0] + ((out_hf[0] + 1) >> 1);

    const uint32_t count = ((len - 1) / 2);
    for (uint32_t id = 1; id < count; id++) {
        out_hf[id] = in[id * 2 + 1] - ((in[id * 2] + in[id * 2 + 2]) >> 1);
        out_lf[id] = in[id * 2] + ((out_hf[id - 1] + out_hf[id] + 2) >> 2);
    }

    if (!(len & 1)) {
        out_hf[len / 2 - 1] = in[len - 1] - in[len - 2];
        out_lf[len / 2 - 1] = in[len - 2] + ((out_hf[len / 2 - 2] + out_hf[len / 2 - 1] + 2) >> 2);
    }
    else { //if (len & 1){
        out_lf[len / 2] = in[len - 1] + ((out_hf[len / 2 - 1] + 1) >> 1);
    }
}

transform_V0_ptr_t transform_V0_get_function_ptr(uint8_t decom_h) {
    transform_V0_ptr_t ptrs[] = {NULL, transform_V0_H1, transform_V0_H2, transform_V0_H3, transform_V0_H4, transform_V0_H5};
    if (decom_h < sizeof(ptrs) / sizeof(ptrs[0])) {
        return ptrs[decom_h];
    }
    return NULL;
}

/*DWT Calculate Precinct 1 lines for Vertical 0 Horizontal X, and convert output to 16bit.
 * component            - Info about DWT component
 * component_enc        - Info about DWT component
 * buff_in              - Pointer on input buffer for input_bit_depth 32bit input, else convert 8 or 16bit input internal
 * input_bit_depth      - Input bit depth, if 0 then not convert input and use as 32bits
 * param_in_Bw          - Param to convert input 8bit,10bit.. bits to 32bits
 * buffer_out_16bit     - Pointer on precinct outpu 16bit to fill result with all bands
 * param_out_Fq         - param to convert output to 16 bit
 * width                - Width of line
 * buffer_tmp           - Temp buffer, required size: (1.5 * width) == (3*width/2)
 */
void transform_V0_H1(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp) {
    assert(component->bands_num >= 2);
    assert(component_enc->bands[/*component->bands_num - 2*/ 0].coeff_buff_tmp_pos_offset_16bit == 0);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_1 = component->bands[1].width;
    uint32_t width_0 = component->bands[0].width;
    assert(width - width_1 == component->bands[0].width);
    //For last band dwt to out buffer
    uint16_t* out_ptr_lf = buffer_out_16bit;
    uint16_t* out_ptr_hf = buffer_out_16bit + component_enc->bands[/*band_id*/ 1].coeff_buff_tmp_pos_offset_16bit;

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    if (input_bit_depth == 0) {
        /*Not convert input.*/
        dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
        image_shift(out_ptr_hf, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_lf, buffer_tmp, width_0, shift_out, offset_out);
    }
    else {
        nlt_input_scaling_line(buff_in, buffer_tmp, width, picture_hdr, input_bit_depth);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width);
        image_shift(out_ptr_hf, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_lf, buffer_tmp, width_0, shift_out, offset_out);
    }
}

void transform_V0_H2(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp) {
    assert((width >= 3) && "[transform_V0_H2()] ERROR: Length is too small!");
    assert(component->bands_num >= 3);
    assert(component_enc->bands[/*component->bands_num - 3*/ 0].coeff_buff_tmp_pos_offset_16bit == 0);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_2 = component->bands[2].width;
    uint32_t width_1 = component->bands[1].width;
    uint32_t width_0 = component->bands[0].width;
    uint32_t width_01 = width - width_2;
    assert(width - width_2 - width_1 == component->bands[0].width);

    uint16_t* out_ptr_2 = buffer_out_16bit + component_enc->bands[2].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_1 = buffer_out_16bit + component_enc->bands[1].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_0 = buffer_out_16bit; //For last band dwt to out buffer

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    if (input_bit_depth == 0) {
        /*Not convert input.*/
        dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
    else {
        nlt_input_scaling_line(buff_in, buffer_tmp, width, picture_hdr, input_bit_depth);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
}

void transform_V0_H3(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp) {
    assert((width >= 4) && "[transform_V0_H3()] ERROR: Length is too small!");
    assert(component->bands_num >= 4);
    assert(component_enc->bands[/*component->bands_num - 4*/ 0].coeff_buff_tmp_pos_offset_16bit == 0);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_3 = component->bands[3].width;
    uint32_t width_2 = component->bands[2].width;
    uint32_t width_1 = component->bands[1].width;
    uint32_t width_0 = component->bands[0].width;
    uint32_t width_012 = width - width_3;
    uint32_t width_01 = width_012 - width_2;
    assert(width - width_1 - width_2 - width_3 == component->bands[0].width);

    uint16_t* out_ptr_3 = buffer_out_16bit + component_enc->bands[3].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_2 = buffer_out_16bit + component_enc->bands[2].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_1 = buffer_out_16bit + component_enc->bands[1].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_0 = buffer_out_16bit; //For last band dwt to out buffer

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    if (input_bit_depth == 0) {
        /*Not convert input.*/
        dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
    else {
        nlt_input_scaling_line(buff_in, buffer_tmp, width, picture_hdr, input_bit_depth);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
}

void transform_V0_H4(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp) {
    assert((width >= 5) && "[transform_V0_H4()] ERROR: Length is too small!");
    assert(component->bands_num >= 5);
    assert(component_enc->bands[/*component->bands_num - 5*/ 0].coeff_buff_tmp_pos_offset_16bit == 0);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_4 = component->bands[4].width;
    uint32_t width_3 = component->bands[3].width;
    uint32_t width_2 = component->bands[2].width;
    uint32_t width_1 = component->bands[1].width;
    uint32_t width_0 = component->bands[0].width;
    uint32_t width_0123 = width - width_4;
    uint32_t width_012 = width_0123 - width_3;
    uint32_t width_01 = width_012 - width_2;
    assert(width - width_1 - width_2 - width_3 - width_4 == component->bands[0].width);

    uint16_t* out_ptr_4 = buffer_out_16bit + component_enc->bands[4].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_3 = buffer_out_16bit + component_enc->bands[3].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_2 = buffer_out_16bit + component_enc->bands[2].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_1 = buffer_out_16bit + component_enc->bands[1].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_0 = buffer_out_16bit; //For last band dwt to out buffer

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    if (input_bit_depth == 0) {
        /*Not convert input.*/
        dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
        image_shift(out_ptr_4, out32_bit, width_4, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_0123);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
    else {
        nlt_input_scaling_line(buff_in, buffer_tmp, width, picture_hdr, input_bit_depth);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width);
        image_shift(out_ptr_4, out32_bit, width_4, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_0123);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
}

void transform_V0_H5(const pi_component_t* const component, const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                     uint8_t input_bit_depth, picture_header_dynamic_t* picture_hdr, uint16_t* buffer_out_16bit,
                     uint8_t param_out_Fq, uint32_t width, int32_t* buffer_tmp) {
    assert((width >= 6) && "[transform_V0_H5()] ERROR: Length is too small!");
    assert(component->bands_num >= 6);
    assert(component_enc->bands[/*component->bands_num - 6*/ 0].coeff_buff_tmp_pos_offset_16bit == 0);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_5 = component->bands[5].width;
    uint32_t width_4 = component->bands[4].width;
    uint32_t width_3 = component->bands[3].width;
    uint32_t width_2 = component->bands[2].width;
    uint32_t width_1 = component->bands[1].width;
    uint32_t width_0 = component->bands[0].width;
    uint32_t width_01234 = width - width_5;
    uint32_t width_0123 = width_01234 - width_4;
    uint32_t width_012 = width_0123 - width_3;
    uint32_t width_01 = width_012 - width_2;
    assert(width - width_1 - width_2 - width_3 - width_4 - width_5 == component->bands[0].width);

    uint16_t* out_ptr_5 = buffer_out_16bit + component_enc->bands[5].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_4 = buffer_out_16bit + component_enc->bands[4].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_3 = buffer_out_16bit + component_enc->bands[3].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_2 = buffer_out_16bit + component_enc->bands[2].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_1 = buffer_out_16bit + component_enc->bands[1].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_0 = buffer_out_16bit; //For last band dwt to out buffer

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    if (input_bit_depth == 0) {
        /*Not convert input.*/
        dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
        image_shift(out_ptr_5, out32_bit, width_5, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01234);
        image_shift(out_ptr_4, out32_bit, width_4, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_0123);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
    else {
        nlt_input_scaling_line(buff_in, buffer_tmp, width, picture_hdr, input_bit_depth);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width);
        image_shift(out_ptr_5, out32_bit, width_5, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01234);
        image_shift(out_ptr_4, out32_bit, width_4, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_0123);
        image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_012);
        image_shift(out_ptr_2, out32_bit, width_2, shift_out, offset_out);
        dwt_horizontal_line(buffer_tmp, out32_bit, buffer_tmp, width_01);
        image_shift(out_ptr_1, out32_bit, width_1, shift_out, offset_out);
        image_shift(out_ptr_0, buffer_tmp, width_0, shift_out, offset_out);
    }
}

/*DWT horizontal 1 time for High Frequency (down part) after Vertical transofrmation with convert output to 16bit
 * component            - Info about DWT component
 * component_enc        - Info about DWT component
 * buff_in              - input coefficiences after Vertial trancsformation
 * buffer_out_16bit     - Pointer on precinct outpu 16bit, fill 2 bands: {start_band, start_band+1}
 * param_out_Fq         - param to convert output to 16 bit
 * width                - Width of line
 * start_band           - start band to get offsets for precinct where copy ouput bands
 * buffer_tmp           - Temp buffer, required size: 3*width/2
 */
static void transform_V1_H1_down_convert_output(const pi_component_t* const component,
                                                const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                                                uint16_t* buffer_out_16bit, uint8_t param_out_Fq, uint32_t width,
                                                uint8_t band_start, int32_t* buffer_tmp) {
    /* Bands:
    * Name indexing: for (band_start == 2)
    *  0        |1    UP   Low Freaquency
    *  ---------------
    *  2        |3    DOWN High Frequency
    */

    assert(component->bands_num >= 4);
    //buffer_tmp 1 line size 3*width/2
    int32_t* out32_bit = buffer_tmp + width;

    uint32_t width_3 = component->bands[band_start + 1].width;
    uint32_t width_2 = component->bands[band_start].width;
    assert(width - width_3 == component->bands[band_start].width);
    //For last band dwt to out buffer
    uint16_t* out_ptr_2 = buffer_out_16bit + component_enc->bands[band_start].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_3 = buffer_out_16bit + component_enc->bands[band_start + 1].coeff_buff_tmp_pos_offset_16bit;

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    /*Not convert input.*/
    dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
    image_shift(out_ptr_3, out32_bit, width_3, shift_out, offset_out);  //HF
    image_shift(out_ptr_2, buffer_tmp, width_2, shift_out, offset_out); //LF
}

/*Optimization Vertical lines loops to AVX*/
void transform_vertical_loop_hf_line_0_c(uint32_t width, int32_t* out_hf, const int32_t* line_0, const int32_t* line_1) {
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
    }
}

void transform_vertical_loop_lf_line_0_c(uint32_t width, int32_t* out_lf, const int32_t* in_hf, const int32_t* line_0) {
    for (uint32_t i = 0; i < width; i++) {
        out_lf[i] = line_0[i] + ((in_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_0_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_0,
                                            const int32_t* line_1, const int32_t* line_2) {
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((out_hf[i] + 1) >> 1);
    }
}

void transform_vertical_loop_lf_hf_line_x_prev_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* line_p6,
                                                 const int32_t* line_p5, const int32_t* line_p4, const int32_t* line_p3,
                                                 const int32_t* line_p2) {
    for (uint32_t i = 0; i < width; i++) {
        int32_t out_tmp_line_56_HF_next_local = line_p5[i] - ((line_p6[i] + line_p4[i]) >> 1);
        out_hf[i] = line_p3[i] - ((line_p4[i] + line_p2[i]) >> 1);
        out_lf[i] = line_p4[i] + ((out_tmp_line_56_HF_next_local + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_x_c(uint32_t width, int32_t* out_lf, int32_t* out_hf, const int32_t* in_hf_prev,
                                               const int32_t* line_0, const int32_t* line_1, const int32_t* line_2) {
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

void transform_vertical_loop_lf_hf_hf_line_last_even_c(uint32_t width, int32_t* out_lf, int32_t* out_hf,
                                                       const int32_t* in_hf_prev, const int32_t* line_0, const int32_t* line_1) {
    for (uint32_t i = 0; i < width; i++) {
        out_hf[i] = line_1[i] - line_0[i];
        out_lf[i] = line_0[i] + ((in_hf_prev[i] + out_hf[i] + 2) >> 2);
    }
}

/*DWT Calculate Precinct 2 lines recalculate High Frequency only for precinct, to use later in transform_V1_Hx_precinct()
 * width                - Width of line
 * out_tmp_line_HF_next - buffer to keep and output middle calculation of High Frequency for precinct,
 *                        use in transform_V1_Hx_precinct() as in_tmp_line_HF_prev
 * line_0               - input first line of precinct (input converted to 32 bits) have to be consistent with line_idx
 * line_1               - input socound line of precinct (if exist)
 * line_2               - input 1 line after precinct (if exist)
 *                        Remember that precalculate HF for previous precinct need call with lines for Previous precinct.
 * Sample of use in UT: TestDwtFrame.cc
 */
void transform_V1_Hx_precinct_recalc_HF_prev_c(uint32_t width, int32_t* out_tmp_line_HF_next, const int32_t* line_0,
                                               const int32_t* line_1, const int32_t* line_2) {
    for (uint32_t i = 0; i < width; i++) {
        out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
    }
}

/*DWT Calculate Precinct 2 lines for Vertical 1 Horizontal All, and convert output to 16bit.
 * component            - Info about DWT component
 * component_enc        - Info about DWT component
 * transform_v0_hx      - Pointer on function to Horizontal transform
                          get from transform_v0_hx = transform_V0_get_function_ptr(decom_h);
 * decom_h              - Number of horizontal decomposiitions, have to be consistent with transform_v0_hx pointer
 * line_idx             - Index of line on image where precinct start
 * width                - Width of line
 * height               - Height of line
 * line_0               - input 1' line of precinct (input converted to 32 bits) have to be consistent with line_idx
 * line_1               - input 2' line of precinct (if exist)
 * line_2               - input 1 line after precinct (if exist)
 * buffer_out_16bit     - Pointer on precinct outpu 16bit to fill result with all bands
 * param_out_Fq         - param to convert output to 16 bit
 * in_tmp_line_HF_prev  - Previous precinct middle calculation of High Frequency. Ignore for line_idx == 0;
 *                        Can be used from previus precinct get from parameter out_tmp_line_HF_next,
 *                        or recalculate HF in middle of image by: transform_V1_Hx_precinct_recalc_HF_prev()
 * out_tmp_line_HF_next - buffer to keep and output middle calculation of High Frequency for precinct.
 * buffer_tmp           - Temp buffer, required size: (2.5 * width) == (5*width/2)
 */
void transform_V1_Hx_precinct(const pi_component_t* const component, const pi_enc_component_t* const component_enc,
                              transform_V0_ptr_t transform_v0_hx, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                              uint32_t height, const int32_t* line_0, const int32_t* line_1, const int32_t* line_2,
                              uint16_t* buffer_out_16bit, uint8_t param_out_Fq, int32_t* in_tmp_line_HF_prev,
                              int32_t* out_tmp_line_HF_next, int32_t* buffer_tmp) {
    assert((line_idx % 2) == 0);
    assert((height >= 2) && "[transform_V1_Hx_precinct()] ERROR: Length is too small! For 2 use different function");
    assert(component_enc->bands[0].coeff_buff_tmp_pos_offset_16bit == 0);
    assert(decom_h >= 1);

    uint8_t bands_num = 3 + decom_h; /*4*/   //Can not get value  component->bands_num becasue can be called from H2 caculation
    uint8_t band_start_down = bands_num - 2; /*2*/
    assert(component->bands[bands_num - 1 /*3*/].height == component->bands[bands_num - 2 /*2*/].height);
    assert(component->bands[bands_num - 3 /*1*/].height + component->bands[bands_num - 1 /*3*/].height == height);

    /*Sample transform_v0_hx = transform_V0_H1*/
    /* Bands:
    *  [0:(X-3)]|(X-2)
    *  ---------------
    *  (X-1)    |(X)
    * Name indexing:
    *  0        |1    UP   Low Freaquency
    *  ---------------
    *  2        |3    DOWN High Frequency
    */

    /*Buffer temp size 2.5*width
     * 1 on low lines TEMP,
     * 1.5 on DWT V0Hx TEMP
     */
    int32_t* out_01_low = buffer_tmp;
    int32_t* buffer_tmp_ver = buffer_tmp + 1 * width;

    if (line_idx == 0) {
        /*First 2 lines*/
        if (height == 2) {
            /*for (uint32_t i = 0; i < width; i++) {
                out_tmp_line_HF_next[i] = line_1[i] - line_0[i];
            }*/
            transform_vertical_loop_hf_line_0(width, out_tmp_line_HF_next, line_0, line_1);

            transform_V1_H1_down_convert_output(component,
                                                component_enc,
                                                out_tmp_line_HF_next,
                                                buffer_out_16bit,
                                                param_out_Fq,
                                                width,
                                                band_start_down,
                                                buffer_tmp_ver);

            /*for (uint32_t i = 0; i < width; i++) {
                out_01_low[i] = line_0[i] + ((out_tmp_line_HF_next[i] + 1) >> 1);
            }*/
            transform_vertical_loop_lf_line_0(width, out_01_low, out_tmp_line_HF_next, line_0);

            transform_v0_hx(component, component_enc, out_01_low, 0, NULL, buffer_out_16bit, param_out_Fq, width, buffer_tmp_ver);
        }
        else {
            /*for (uint32_t i = 0; i < width; i++) {
                out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
                out_01_low[i] = line_0[i] + ((out_tmp_line_HF_next[i] + 1) >> 1);
            }*/
            transform_vertical_loop_lf_hf_line_0(width, out_01_low, out_tmp_line_HF_next, line_0, line_1, line_2);

            transform_V1_H1_down_convert_output(component,
                                                component_enc,
                                                out_tmp_line_HF_next,
                                                buffer_out_16bit,
                                                param_out_Fq,
                                                width,
                                                band_start_down,
                                                buffer_tmp_ver);
            transform_v0_hx(component, component_enc, out_01_low, 0, NULL, buffer_out_16bit, param_out_Fq, width, buffer_tmp_ver);
        }
    }
    else if ((line_idx + 2 < height)) {
        /*Next 2 lines*/
        /*for (uint32_t i = 0; i < width; i++) {
            out_tmp_line_HF_next[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
            out_01_low[i] = line_0[i] + ((in_tmp_line_HF_prev[i] + out_tmp_line_HF_next[i] + 2) >> 2);
        }*/
        transform_vertical_loop_lf_hf_hf_line_x(
            width, out_01_low, out_tmp_line_HF_next, in_tmp_line_HF_prev, line_0, line_1, line_2);

        transform_V1_H1_down_convert_output(component,
                                            component_enc,
                                            out_tmp_line_HF_next,
                                            buffer_out_16bit,
                                            param_out_Fq,
                                            width,
                                            band_start_down,
                                            buffer_tmp_ver);
        transform_v0_hx(component, component_enc, out_01_low, 0, NULL, buffer_out_16bit, param_out_Fq, width, buffer_tmp_ver);
    }
    else /*if (line + 2 >= height)*/ {
        /*Last 2 lines*/
        if (!(height & 1)) {
            /*for (uint32_t i = 0; i < width; i++) {
                out_tmp_line_HF_next[i] = line_1[i] - line_0[i];
                out_01_low[i] = line_0[i] + ((in_tmp_line_HF_prev[i] + out_tmp_line_HF_next[i] + 2) >> 2);
            }*/
            transform_vertical_loop_lf_hf_hf_line_last_even(
                width, out_01_low, out_tmp_line_HF_next, in_tmp_line_HF_prev, line_0, line_1);

            transform_V1_H1_down_convert_output(component,
                                                component_enc,
                                                out_tmp_line_HF_next,
                                                buffer_out_16bit,
                                                param_out_Fq,
                                                width,
                                                band_start_down,
                                                buffer_tmp_ver);
            transform_v0_hx(component, component_enc, out_01_low, 0, NULL, buffer_out_16bit, param_out_Fq, width, buffer_tmp_ver);
        }
        else { //if (len & 1){
            /*for (uint32_t i = 0; i < width; i++) {
                out_01_low[i] = line_0[i] + ((in_tmp_line_HF_prev[i] + 1) >> 1);
            }*/
            transform_vertical_loop_lf_line_0(width, out_01_low, in_tmp_line_HF_prev, line_0);

            transform_v0_hx(component, component_enc, out_01_low, 0, NULL, buffer_out_16bit, param_out_Fq, width, buffer_tmp_ver);
        }
    }
}

/* Precalculcations for transform_V2_Hx_precinct_recalc()
 * buffer_next       SIZE: (width + 1)  -Precalculate buffer for calculate precinct.
 * buffer_on_place   SIZE: (3*width/2)   - Always that same memory to use previous calculation
 * buffer_tmp        SIZE: (1*width) - Memory with minimum allcoation
 */
static void transform_V2_Hx_precinct_recalc_prev(uint32_t width, const int32_t* line_p6, const int32_t* line_p5,
                                                 const int32_t* line_p4, const int32_t* line_p3, const int32_t* line_p2,
                                                 int32_t* buffer_next, int32_t* buffer_on_place, int32_t* buffer_tmp) {
    /* Name indexing sample V2 H2:
    *  0   | 1  |4    UP   Low Freaquency
    *  2   | 3  |
    *  ---------------
    *  5        |6    DOWN High Frequency
    */

    /*Buffer on place size: 1.5*width == (3*width/2)*/
    int32_t* in_tmp_line_56_HF_prev_on_place = buffer_on_place;  //Size width
    int32_t* out_tmp_4_32bit_on_place = buffer_on_place + width; //Size width/2

    /*Buffer temp size (1*width)
    * 1             buffer_tmp_01234_lf
    */
    int32_t* buffer_tmp_01234_lf_line = buffer_tmp;                  //Size: 1 line
    int32_t* out_tmp_0123_line_next = buffer_next + (width + 1) / 2; //Size (width+1)/2

    /*for (uint32_t i = 0; i < width; i++) {
        int32_t out_tmp_line_56_HF_next_local = line_p5[i] - ((line_p6[i] + line_p4[i]) >> 1);
        in_tmp_line_56_HF_prev_on_place[i] = line_p3[i] - ((line_p4[i] + line_p2[i]) >> 1);
        buffer_tmp_01234_lf_line[i] = line_p4[i] + ((out_tmp_line_56_HF_next_local + in_tmp_line_56_HF_prev_on_place[i] + 2) >> 2);
    }*/
    transform_vertical_loop_lf_hf_line_x_prev(
        width, buffer_tmp_01234_lf_line, in_tmp_line_56_HF_prev_on_place, line_p6, line_p5, line_p4, line_p3, line_p2);

    dwt_horizontal_line(out_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
}

/* Precalculcations for transform_V2_Hx_precinct_recalc()
 * buffer_next       SIZE: (width + 1)  -Precalculate buffer for calculate precinct.
 * buffer_on_place   SIZE: (3*width/2)   - Always that same memory to use previous calculation
 * buffer_tmp        SIZE: (5*width/2 + 1) - Memory with minimum allcoation
 */
static void transform_V2_Hx_precinct_recalc_next(const pi_component_t* const component, uint8_t decom_h, uint32_t line_idx,
                                                 uint32_t width, uint32_t height, const int32_t* line_2, const int32_t* line_3,
                                                 const int32_t* line_4, const int32_t* line_5, const int32_t* line_6,
                                                 int32_t* buffer_prev, int32_t* buffer_next, int32_t* buffer_on_place,
                                                 int32_t* buffer_tmp) {
    assert((line_idx % 4) == 0);

    /*Sample transform_v0_hx = transform_V0_H1*/
    /* Bands:
    *  [0:(X-6)]|(X-5)|(X-2)
    * ----------------
    *    (X-4)  |(X-3)|
    *  ---------------------
    *    (X-1)        |(X)
    * Name indexing sample V2 H2:
    *  0   | 1  |4    UP   Low Freaquency
    *  2   | 3  |
    *  ---------------
    *  5        |6    DOWN High Frequency
    */

    uint8_t bands_num = 5 + decom_h;         /*7*/
    uint8_t band_start_down = bands_num - 2; /*5*/
    assert(bands_num == component->bands_num);

    uint32_t width_0123_5 = component->bands[band_start_down].width;

    /*Buffer on place size: 1.5*width == (3*width/2)*/
    int32_t* in_tmp_line_56_HF_prev_on_place = buffer_on_place;  //Size width
    int32_t* out_tmp_4_32bit_on_place = buffer_on_place + width; //Size width/2

    /*Buffer temp size (5*width + 1)
    * 1             buffer_tmp_01234_lf
    * 1             out_tmp_line_56_HF_next_local
    * 0.5RoundUp    out_ptr_0123_line1
    */
    int32_t* buffer_tmp_01234_lf_line = buffer_tmp;              //Size: 1 line
    int32_t* out_tmp_line_56_HF_next_local = buffer_tmp + width; //Size width
    int32_t* out_ptr_0123_line1 = buffer_tmp + 2 * width;        //Size (width+1)/2

    int32_t* in_tmp_0123_line_prev = buffer_prev + (width + 1) / 2; //Size (width+1)/2

    int32_t* out_tmp_line_V1_HF_next = buffer_next;                  //Size (width+1)/2
    int32_t* out_tmp_0123_line_next = buffer_next + (width + 1) / 2; //Size (width+1)/2

    if (line_idx < 4 * ((height - 3) / 4)) {
        /*for (uint32_t i = 0; i < width; i++) {
            out_tmp_line_56_HF_next_local[i] = line_3[i] - ((line_2[i] + line_4[i]) >> 1);
            buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + out_tmp_line_56_HF_next_local[i] + 2) >> 2);
        }*/
        transform_vertical_loop_lf_hf_hf_line_x(width,
                                                buffer_tmp_01234_lf_line,
                                                out_tmp_line_56_HF_next_local,
                                                in_tmp_line_56_HF_prev_on_place,
                                                line_2,
                                                line_3,
                                                line_4);

        dwt_horizontal_line(out_ptr_0123_line1, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);

        //THIS IS NEXT PRECINCT
        /*for (uint32_t i = 0; i < width; i++) {
            in_tmp_line_56_HF_prev_on_place[i] = line_5[i] - ((line_4[i] + line_6[i]) >> 1);
            buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + in_tmp_line_56_HF_prev_on_place[i] + 2) >> 2);
        }*/
        transform_vertical_loop_lf_hf_hf_line_x(width,
                                                buffer_tmp_01234_lf_line,
                                                in_tmp_line_56_HF_prev_on_place,
                                                out_tmp_line_56_HF_next_local,
                                                line_4,
                                                line_5,
                                                line_6);

        dwt_horizontal_line(out_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
    }
    else {
        /*Depend on high this case can calculate last precinct, or 2 last precincts
            Possible lines to end: 3,4,5,6*/
        uint8_t one_before_last_precinct = height > line_idx + 4;

        if (one_before_last_precinct) {
            //Calculate for one before last precinct for 2 precincts at end
            assert(((height - (4 * ((height - 3) / 4))) > 4) > 0);
            //Calculate one before last precinct
            /*for (uint32_t i = 0; i < width; i++) {
                out_tmp_line_56_HF_next_local[i] = line_3[i] - ((line_2[i] + line_4[i]) >> 1);
                buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + out_tmp_line_56_HF_next_local[i] + 2) >> 2);
            }*/
            transform_vertical_loop_lf_hf_hf_line_x(width,
                                                    buffer_tmp_01234_lf_line,
                                                    out_tmp_line_56_HF_next_local,
                                                    in_tmp_line_56_HF_prev_on_place,
                                                    line_2,
                                                    line_3,
                                                    line_4);

            dwt_horizontal_line(out_ptr_0123_line1, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);

            if (!(height & 1)) {
                /*for (uint32_t i = 0; i < width; i++) {
                    in_tmp_line_56_HF_prev_on_place[i] = line_5[i] - line_4[i];
                    buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + in_tmp_line_56_HF_prev_on_place[i] + 2) >> 2);
                }*/
                transform_vertical_loop_lf_hf_hf_line_last_even(width,
                                                                buffer_tmp_01234_lf_line,
                                                                in_tmp_line_56_HF_prev_on_place,
                                                                out_tmp_line_56_HF_next_local,
                                                                line_4,
                                                                line_5);
            }
            else { //if (len & 1){
                /*for (uint32_t i = 0; i < width; i++) {
                    buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + 1) >> 1);
                }*/
                transform_vertical_loop_lf_line_0(width, buffer_tmp_01234_lf_line, out_tmp_line_56_HF_next_local, line_4);
            }
            dwt_horizontal_line(out_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
        }
    }

    transform_V1_Hx_precinct_recalc_HF_prev(
        width_0123_5, out_tmp_line_V1_HF_next, in_tmp_0123_line_prev, out_ptr_0123_line1, out_tmp_0123_line_next);
}

/*DWT horizontal 1 time for High Frequency (down part) after Vertical transofrmation with convert output to 16bit
 * component - Info about DWT component
 * component_enc - Info about DWT component
 * buff_in - input coefficiences after Vertial trancsformation
 * buffer_out_16bit - outpu 16bit 2 bands
 * param_out_Fq - param to convert output to 16 bit
 * width - Width of line
 * start_band - start band to get offsets for precinct where copy ouput bands
 * buffer_tmp - Temp buffer, required size: 3*width/2
 */
static void transform_V2_H1_down_line_convert_output(const pi_component_t* const component,
                                                     const pi_enc_component_t* const component_enc, const int32_t* buff_in,
                                                     uint16_t* buffer_out_16bit, uint8_t param_out_Fq, uint8_t line_in_precinct,
                                                     uint32_t width, uint8_t band_start, int32_t* buffer_tmp) {
    assert(component->bands_num >= 4);

    //buffer_tmp 1 line size 3*width/2
    //buffer_tmp                             //Size: 1 * width
    int32_t* out32_bit = buffer_tmp + width; //Size: 0.5 * width

    /* Bands:
    * Name indexing sample V2 H2:
    *  0   | 1  |4    UP   Low Freaquency
    *  2   | 3  |
    *  ---------------
    *  5        |6    DOWN High Frequency
    */

    uint32_t width_6 = component->bands[band_start + 1].width;
    uint32_t width_5 = component->bands[band_start].width;
    assert(width - width_6 == component->bands[band_start].width);
    //For last band dwt to out buffer
    uint16_t* out_ptr_5 = buffer_out_16bit + component_enc->bands[band_start].coeff_buff_tmp_pos_offset_16bit;
    uint16_t* out_ptr_6 = buffer_out_16bit + component_enc->bands[band_start + 1].coeff_buff_tmp_pos_offset_16bit;

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    dwt_horizontal_line(buffer_tmp, out32_bit, buff_in, width);
    image_shift(out_ptr_6 + line_in_precinct * width_6, out32_bit, width_6, shift_out, offset_out);
    image_shift(out_ptr_5 + line_in_precinct * width_5, buffer_tmp, width_5, shift_out, offset_out);
}

/*DWT Calculate Precinct 4 lines recalculate middle calculations for precinct, to use later in transform_V2_Hx_precinct()
 * Calcualations vbefore first precinct.
 * width                - Width of line
 * line_0              - input 1' line of precinct 0
 * line_1              - input 2' line of precinct 0
 * line_2              - input 3' line of precinct 0
 *                        Remember that precalculate HF for previous precinct need call with lines for Previous precinct.
 * buffer_next          - buffer to keep and output middle calculation for precinct,
 *                        use in transform_V2_Hx_precinct() as buffer_prev
 *                        Size: (width + 1)
 * buffer_on_place      - buffer to keep and output middle calculation for precinct,
 *                        use in transform_V2_Hx_precinct() as buffer_on_place
 *                        Size: (3*width/2)
 * buffer_tmp           - Temp buffer, required minimum
 *                        Size: (width)
 * Sample of use in UT: TestDwtFrame.cc
 */

void transform_V2_Hx_precinct_recalc_prec_0(uint32_t width, const int32_t* line_0, const int32_t* line_1, const int32_t* line_2,
                                            int32_t* buffer_next, int32_t* buffer_on_place, int32_t* buffer_tmp) {
    /*Name indexing sample V2 H2:
    *  0   | 1  |4    UP   Low Freaquency
    *  2   | 3  |
    *  ---------------
    *  5        |6    DOWN High Frequency
    */

    /*Buffer on place size: 1.5*width == (3*width/2)*/
    int32_t* in_tmp_line_56_HF_prev_on_place = buffer_on_place;  //Size width
    int32_t* out_tmp_4_32bit_on_place = buffer_on_place + width; //Size width/2

    /*Buffer temp size (width)
    * 1             buffer_tmp_01234_lf
    */
    int32_t* buffer_tmp_01234_lf_line = buffer_tmp;                 //Size: 1 line
    int32_t* in_tmp_0123_line_next = buffer_next + (width + 1) / 2; //Size (width+1)/2

    //First line
    /*for (uint32_t i = 0; i < width; i++) {
        in_tmp_line_56_HF_prev_on_place[i] = line_1[i] - ((line_0[i] + line_2[i]) >> 1);
        buffer_tmp_01234_lf_line[i] = line_0[i] + ((in_tmp_line_56_HF_prev_on_place[i] + 1) >> 1);
    }*/
    transform_vertical_loop_lf_hf_line_0(
        width, buffer_tmp_01234_lf_line, in_tmp_line_56_HF_prev_on_place, line_0, line_1, line_2);

    /*UP LINES*/
    dwt_horizontal_line(in_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
}

/*DWT Calculate Precinct 4 lines recalculate middle calculations for precinct, to use later in transform_V2_Hx_precinct()
 * component            - Info about DWT component
 * decom_h              - Number of horizontal decomposiitions, have to be consistent with transform_v0_hx pointer
 * line_idx             - Index of line on image where precinct start
 * width                - Width of line
 * height               - Height of line
 * line_p6              - input 6 line before precinct (if exist)
 * line_p5              - input 5 line before precinct (if exist)
 * line_p4              - input 4 line before precinct (if exist)
 * line_p3              - input 3 line before precinct (if exist)
 * line_p2              - input 2 line before precinct (if exist)
 * line_p1              - input 1 line before precinct (if exist)
 * line_0               - input 1' line of precinct (input converted to 32 bits) have to be consistent with line_idx
 * line_1               - input 2' line of precinct (if exist)
 * line_2               - input 3' line after precinct (if exist)
 *                        Remember that precalculate HF for previous precinct need call with lines for Previous precinct.
 * buffer_next          - buffer to keep and output middle calculation for precinct,
 *                        use in transform_V2_Hx_precinct() as buffer_prev
 *                        Size: (width + 1)
 * buffer_on_place      - buffer to keep and output middle calculation for precinct,
 *                        use in transform_V2_Hx_precinct() as buffer_on_place
 *                        Size: (3*width/2)
 * buffer_tmp           - Temp buffer, required minimum
 *                        Size: (7*width/2 + 2)
 * Sample of use in UT: TestDwtFrame.cc
 */
void transform_V2_Hx_precinct_recalc(const pi_component_t* const component, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                                     uint32_t height, const int32_t* line_p6, const int32_t* line_p5, const int32_t* line_p4,
                                     const int32_t* line_p3, const int32_t* line_p2, const int32_t* line_p1,
                                     const int32_t* line_0, const int32_t* line_1, const int32_t* line_2, int32_t* buffer_next,
                                     int32_t* buffer_on_place, int32_t* buffer_tmp) {
    int32_t* buffer_tmp_prev = buffer_tmp;
    buffer_tmp += width + 1; //Leave in temp place on buffer: buffer_tmp_prev

    if (line_idx == 0) {
        transform_V2_Hx_precinct_recalc_prec_0(width, line_0, line_1, line_2, buffer_next, buffer_on_place, buffer_tmp);
    }
    else {
        if (line_idx == 4) {
            transform_V2_Hx_precinct_recalc_prec_0(
                width, line_p4, line_p3, line_p2, buffer_tmp_prev, buffer_on_place, buffer_tmp);
        }
        else {
            transform_V2_Hx_precinct_recalc_prev(
                width, line_p6, line_p5, line_p4, line_p3, line_p2, buffer_tmp_prev, buffer_on_place, buffer_tmp);
        }

        transform_V2_Hx_precinct_recalc_next(component,
                                             decom_h,
                                             line_idx - 4,
                                             width,
                                             height,
                                             line_p2,
                                             line_p1,
                                             line_0,
                                             line_1,
                                             line_2,
                                             buffer_tmp_prev,
                                             buffer_next,
                                             buffer_on_place,
                                             buffer_tmp);
    }
}

/*DWT Calculate Precinct 4 lines for Vertical 2 Horizontal All, and convert output to 16bit.
 * component            - Info about DWT component
 * component_enc        - Info about DWT component
 * transform_V0_Hn_sub_1- Pointer on function to Horizontal transform
 *                        get from transform_V0_Hn_sub_1 = transform_V0_get_function_ptr(decom_h -1);
 * decom_h              - Number of horizontal decomposiitions, have to be consistent with transform_v0_hx pointer
 * line_idx             - Index of line on image where precinct start
 * width                - Width of line
 * height               - Height of line
 * line_2               - input 3' line of precinct (input converted to 32 bits) have to be consistent with line_idx
 * line_3               - input 4' line of precinct (if exist)
 * line_4               - input 1 line after precinct (if exist)
 * line_5               - input 2 line after precinct (if exist)
 * line_6               - input 3 line after precinct (if exist)
 * buffer_out_16bit     - Pointer on precinct outpu 16bit to fill result with all bands
 * param_out_Fq         - param to convert output to 16 bit
 * buffer_prev          - Previous precinct middle calculation
 *                        Size: (width + 1)
 *                        Can be used from previus precinct get from parameter buffer_next,
 *                        or recalculate HF in middle of image by:
 *                        transform_V2_Hx_precinct_recalc_prec_0() or transform_V2_Hx_precinct_recalc_prev()
 * buffer_next          - buffer to keep and output middle calculation.
 *                        Swap buffers prev and next between calls. Last next and New prev can not be override.
 *                        Size: (width + 1)
 * buffer_on_place      - middel calculations from previous precinct,
 *                        can be recalculated by transform_V2_Hx_precinct_recalc_prec_0() or transform_V2_Hx_precinct_recalc_prev()
 *                        Size: (3*width/2)
 * buffer_tmp           - Temp buffer, required minimum
 *                        Size: (5*width + 1)
 */
void transform_V2_Hx_precinct(const pi_component_t* const component, const pi_enc_component_t* const component_enc,
                              transform_V0_ptr_t transform_V0_Hn_sub_1, uint8_t decom_h, uint32_t line_idx, uint32_t width,
                              uint32_t height, const int32_t* line_2, const int32_t* line_3, const int32_t* line_4,
                              const int32_t* line_5, const int32_t* line_6, uint16_t* buffer_out_16bit, uint8_t param_out_Fq,
                              int32_t* buffer_prev, int32_t* buffer_next, int32_t* buffer_on_place, int32_t* buffer_tmp) {
    assert((line_idx % 4) == 0);
    /*Sample transform_v0_hx = transform_V0_H1*/
    /* Bands:
    *  [0:(X-6)]|(X-5)|(X-2)
    * ----------------
    *    (X-4)  |(X-3)|
    *  ---------------------
    *    (X-1)        |(X)
    * Name indexing sample V2 H2:
    *  0   | 1  |4    UP   Low Freaquency
    *  2   | 3  |
    *  ---------------
    *  5        |6    DOWN High Frequency
    */

    uint8_t bands_num = 5 + decom_h;             /*7*/
    uint8_t band_start_down = bands_num - 2;     /*5*/
    uint8_t band_start_up_right = bands_num - 3; /*4*/
    assert(bands_num == component->bands_num);

    uint32_t heigh_0123_4 = component->bands[bands_num - 3].height;
    assert(heigh_0123_4 == component->bands[bands_num - 4].height + component->bands[bands_num - 6].height);
    assert((heigh_0123_4 == component->bands[bands_num - 1].height) ||
           (heigh_0123_4 == component->bands[bands_num - 1].height + 1));
    assert(height == (heigh_0123_4 + component->bands[bands_num - 1].height));
    assert(height >= 3);

    uint32_t width_0123_5 = component->bands[band_start_down].width;
    uint32_t width_4 = component->bands[band_start_up_right].width;
    uint32_t offset_4 = component_enc->bands[band_start_up_right].coeff_buff_tmp_pos_offset_16bit; //HF
    assert((width == (width_0123_5 + /*width_4_6*/ component->bands[bands_num - 1].width)));

    int32_t shift_out = param_out_Fq;
    int32_t offset_out = 1 << (param_out_Fq - 1);

    /*Buffer on place size: 1.5*width == (3*width/2)*/
    int32_t* in_tmp_line_56_HF_prev_on_place = buffer_on_place;  //Size width
    int32_t* out_tmp_4_32bit_on_place = buffer_on_place + width; //Size width/2

    /*Buffer temp size (5*width + 1)
    * 1             buffer_tmp_01234_lf
    * 1             out_tmp_line_56_HF_next_local
    * 2.5           on DWT V1Hx TEMP
    *               -inside  1.5 on DWT V0Hx TEMP
    * 0.5RoundUp    out_ptr_0123_line1
    */
    int32_t* buffer_tmp_01234_lf_line = buffer_tmp;                       //Size: 1 line
    int32_t* out_tmp_line_56_HF_next_local = buffer_tmp + width;          //Size width
    int32_t* buffer_tmp_v1 = buffer_tmp + 2 * width;                      //Size: 2.5 line
    int32_t* out_ptr_0123_line1 = buffer_tmp + 2 * width + width * 5 / 2; //Size (width+1)/2

    int32_t* in_tmp_line_V1_HF_prev = buffer_prev;                  //Size (width+1)/2
    int32_t* in_tmp_0123_line_prev = buffer_prev + (width + 1) / 2; //Size (width+1)/2

    int32_t* out_tmp_line_V1_HF_next = buffer_next;                  //Size (width+1)/2
    int32_t* out_tmp_0123_line_next = buffer_next + (width + 1) / 2; //Size (width+1)/2

    if (line_idx < 4 * ((height - 3) / 4)) {
        assert(((line_idx / 2)) < heigh_0123_4);
        assert((line_idx / 2) + 1 < component->bands[bands_num - 1].height);
        assert(((line_idx / 2) + 2) < component->bands[bands_num - 1].height);
        assert((line_idx / 2) + 1 < heigh_0123_4);
        assert(((line_idx / 2) + 2) < heigh_0123_4);

        //Calculate down, for some resolution down (HF) can be odd and up (LF) even
        transform_V2_H1_down_line_convert_output(component,
                                                 component_enc,
                                                 in_tmp_line_56_HF_prev_on_place,
                                                 buffer_out_16bit,
                                                 param_out_Fq,
                                                 0,
                                                 width,
                                                 band_start_down,
                                                 buffer_tmp_v1);
        image_shift(buffer_out_16bit + offset_4 + 0 * width_4, out_tmp_4_32bit_on_place, width_4, shift_out, offset_out);

        /*for (uint32_t i = 0; i < width; i++) {
            out_tmp_line_56_HF_next_local[i] = line_3[i] - ((line_2[i] + line_4[i]) >> 1);
            buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + out_tmp_line_56_HF_next_local[i] + 2) >> 2);
        }*/
        transform_vertical_loop_lf_hf_hf_line_x(width,
                                                buffer_tmp_01234_lf_line,
                                                out_tmp_line_56_HF_next_local,
                                                in_tmp_line_56_HF_prev_on_place,
                                                line_2,
                                                line_3,
                                                line_4);

        /*DOWN LINES*/
        transform_V2_H1_down_line_convert_output(component,
                                                 component_enc,
                                                 out_tmp_line_56_HF_next_local,
                                                 buffer_out_16bit,
                                                 param_out_Fq,
                                                 1,
                                                 width,
                                                 band_start_down,
                                                 buffer_tmp_v1);

        dwt_horizontal_line(out_ptr_0123_line1, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
        image_shift(buffer_out_16bit + offset_4 + 1 * width_4, out_tmp_4_32bit_on_place, width_4, shift_out, offset_out);

        //THIS IS NEXT PRECINCT
        /*for (uint32_t i = 0; i < width; i++) {
            in_tmp_line_56_HF_prev_on_place[i] = line_5[i] - ((line_4[i] + line_6[i]) >> 1);
            buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + in_tmp_line_56_HF_prev_on_place[i] + 2) >> 2);
        }*/
        transform_vertical_loop_lf_hf_hf_line_x(width,
                                                buffer_tmp_01234_lf_line,
                                                in_tmp_line_56_HF_prev_on_place,
                                                out_tmp_line_56_HF_next_local,
                                                line_4,
                                                line_5,
                                                line_6);

        dwt_horizontal_line(out_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
    }
    else /*(line_idx >= 4*((height-3)/4))*/ {
        //Calculate down, for some resolution down (HF) can be odd and up (LF) even
        transform_V2_H1_down_line_convert_output(component,
                                                 component_enc,
                                                 in_tmp_line_56_HF_prev_on_place,
                                                 buffer_out_16bit,
                                                 param_out_Fq,
                                                 0,
                                                 width,
                                                 band_start_down,
                                                 buffer_tmp_v1);

        image_shift(buffer_out_16bit + offset_4 + 0 * width_4, out_tmp_4_32bit_on_place, width_4, shift_out, offset_out);

        /*Depend on high this case can calculate last precinct, or 2 last precincts
            Possible lines to end: 3,4,5,6*/
        uint8_t calc_last_2_precincts = (height - (4 * ((height - 3) / 4))) > 4;
        uint8_t last_precinct = height <= (line_idx + 4);
        uint8_t one_before_last_precinct = height > line_idx + 4;
        assert(last_precinct != one_before_last_precinct);

        if (one_before_last_precinct) {
            //Calculate for one before last precinct for 2 precincts at end
            assert(calc_last_2_precincts > 0);
            //Calculate one before last precinct
            /*for (uint32_t i = 0; i < width; i++) {
                out_tmp_line_56_HF_next_local[i] = line_3[i] - ((line_2[i] + line_4[i]) >> 1);
                buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + out_tmp_line_56_HF_next_local[i] + 2) >> 2);
            }*/
            transform_vertical_loop_lf_hf_hf_line_x(width,
                                                    buffer_tmp_01234_lf_line,
                                                    out_tmp_line_56_HF_next_local,
                                                    in_tmp_line_56_HF_prev_on_place,
                                                    line_2,
                                                    line_3,
                                                    line_4);

            /*DOWN LINES*/
            transform_V2_H1_down_line_convert_output(component,
                                                     component_enc,
                                                     out_tmp_line_56_HF_next_local,
                                                     buffer_out_16bit,
                                                     param_out_Fq,
                                                     1,
                                                     width,
                                                     band_start_down,
                                                     buffer_tmp_v1);

            dwt_horizontal_line(out_ptr_0123_line1, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
            image_shift(
                buffer_out_16bit /*3*/ + offset_4 + 1 * width_4, out_tmp_4_32bit_on_place, width_4, shift_out, offset_out);

            if (!(height & 1)) {
                /*for (uint32_t i = 0; i < width; i++) {
                    in_tmp_line_56_HF_prev_on_place[i] = line_5[i] - line_4[i];
                    buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + in_tmp_line_56_HF_prev_on_place[i] + 2) >> 2);
                }*/
                transform_vertical_loop_lf_hf_hf_line_last_even(width,
                                                                buffer_tmp_01234_lf_line,
                                                                in_tmp_line_56_HF_prev_on_place,
                                                                out_tmp_line_56_HF_next_local,
                                                                line_4,
                                                                line_5);
            }
            else { //if (len & 1){
                /*for (uint32_t i = 0; i < width; i++) {
                    buffer_tmp_01234_lf_line[i] = line_4[i] + ((out_tmp_line_56_HF_next_local[i] + 1) >> 1);
                }*/
                transform_vertical_loop_lf_line_0(width, buffer_tmp_01234_lf_line, out_tmp_line_56_HF_next_local, line_4);
            }
            dwt_horizontal_line(out_tmp_0123_line_next, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
        }
        else if (!calc_last_2_precincts && last_precinct) {
            //Calculate for last precinct for 1 precincts at end
            //End 1 or 2 lines
            if (!(height & 1)) {
                /*for (uint32_t i = 0; i < width; i++) {
                    out_tmp_line_56_HF_next_local[i] = line_3[i] - line_2[i];
                    buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + out_tmp_line_56_HF_next_local[i] + 2) >> 2);
                }*/
                transform_vertical_loop_lf_hf_hf_line_last_even(width,
                                                                buffer_tmp_01234_lf_line,
                                                                out_tmp_line_56_HF_next_local,
                                                                in_tmp_line_56_HF_prev_on_place,
                                                                line_2,
                                                                line_3);

                transform_V2_H1_down_line_convert_output(component,
                                                         component_enc,
                                                         out_tmp_line_56_HF_next_local,
                                                         buffer_out_16bit,
                                                         param_out_Fq,
                                                         1,
                                                         width,
                                                         band_start_down,
                                                         buffer_tmp_v1);
            }
            else { //if (len & 1){
                /*for (uint32_t i = 0; i < width; i++) {
                    buffer_tmp_01234_lf_line[i] = line_2[i] + ((in_tmp_line_56_HF_prev_on_place[i] + 1) >> 1);
                }*/
                transform_vertical_loop_lf_line_0(width, buffer_tmp_01234_lf_line, in_tmp_line_56_HF_prev_on_place, line_2);
            }

            dwt_horizontal_line(out_ptr_0123_line1, out_tmp_4_32bit_on_place, buffer_tmp_01234_lf_line, width);
            image_shift(buffer_out_16bit + offset_4 + 1 * width_4, out_tmp_4_32bit_on_place, width_4, shift_out, offset_out);
        }
    }

    transform_V1_Hx_precinct(component,
                             component_enc,
                             transform_V0_Hn_sub_1,
                             decom_h - 1,
                             (line_idx / 2),
                             width_0123_5,
                             heigh_0123_4,
                             in_tmp_0123_line_prev,
                             out_ptr_0123_line1,
                             out_tmp_0123_line_next, //LF again transformation Vertical
                             buffer_out_16bit,
                             param_out_Fq,
                             in_tmp_line_V1_HF_prev,
                             out_tmp_line_V1_HF_next,
                             buffer_tmp_v1);
}
