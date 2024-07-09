/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>

#include "GcStageProcess.h"
#include "SvtUtility.h"
#include "common_dsp_rtcd.h"
#include "encoder_dsp_rtcd.h"
#include "Codestream.h"
#include "EncDec.h"
#include "Dwt.h"
#include "NltEnc.h"
#include "PreRcStageProcess.h"
#include "Threads/SvtThreads.h"

typedef void (*convert_fn)(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3, uint32_t line_width);

void gc_precinct_stage_scalar_loop_c(uint32_t line_groups_num, uint16_t* coeff_data_ptr_16bit, uint8_t* gcli_data_ptr) {
    for (uint32_t g = 0; g < line_groups_num; g++) {
        uint16_t merge_or = coeff_data_ptr_16bit[0];
        merge_or |= coeff_data_ptr_16bit[1];
        merge_or |= coeff_data_ptr_16bit[2];
        merge_or |= coeff_data_ptr_16bit[3];
        merge_or <<= 1; //Remove sign bit

        if (merge_or) {
            gcli_data_ptr[0] = svt_log2_32(merge_or); //MSB
            assert(gcli_data_ptr[0] <= TRUNCATION_MAX);
        }
        else {
            gcli_data_ptr[0] = 0;
        }
        coeff_data_ptr_16bit += GROUP_SIZE;
        gcli_data_ptr++;
    }
}

void gc_precinct_stage_scalar_c(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size, uint32_t width) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    uint32_t line_groups_num = width / GROUP_SIZE;
    uint32_t line_groups_leftover = width % GROUP_SIZE;

    if (line_groups_num) {
        gc_precinct_stage_scalar_loop(line_groups_num, coeff_data_ptr_16bit, gcli_data_ptr);
    }

    if (line_groups_leftover) {
        coeff_data_ptr_16bit += line_groups_num * GROUP_SIZE;
        gcli_data_ptr += line_groups_num;
        uint16_t merge_or = 0;
        for (uint32_t i = 0; i < line_groups_leftover; i++) {
            merge_or |= coeff_data_ptr_16bit[i];
        }
        merge_or <<= 1; //Remove sign bit
        if (merge_or) {
            gcli_data_ptr[0] = svt_log2_32(merge_or); //MSB
            assert(gcli_data_ptr[0] <= TRUNCATION_MAX);
        }
        else {
            gcli_data_ptr[0] = 0;
        }
    }
}

void gc_precinct_sigflags_max_c(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                uint32_t gcli_width) {
    uint32_t group_number = gcli_width / group_sign_size;
    uint32_t size_leftover = gcli_width % group_sign_size;
    uint32_t group = 0;
    for (; group < group_number; group++) {
        uint8_t* in_group = &gcli_data_ptr[group * group_sign_size];
        uint8_t max = 0;
        for (uint32_t j = 0; j < group_sign_size; j++) {
            if (in_group[j] > max) {
                max = in_group[j];
            }
        }
        significance_data_max_ptr[group] = max;
    }

    //Leftover last column
    if (size_leftover) {
        uint8_t* in_group = &gcli_data_ptr[group * group_sign_size];
        uint8_t max = 0;
        for (uint32_t j = 0; j < size_leftover; j++) {
            if (in_group[j] > max) {
                max = in_group[j];
            }
        }
        significance_data_max_ptr[group] = max;
    }
}

void precinct_component_calculate_dwt_V0(struct PictureControlSet* pcs_ptr, precinct_enc_t* precinct, uint32_t comp_id,
                                         struct precinct_calc_dwt_buff_tmp* buffers_tmp_dwt, const void* plane_buffer_in) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;
    pi_enc_t* pi_enc = &enc_common->pi_enc;
    assert(pi->components[comp_id].decom_v == 0);
    const pi_component_t* const component = &(pi->components[comp_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[comp_id]);
    uint32_t plane_width = pi->components[comp_id].width;
    uint8_t param_out_Fq = enc_common->picture_header_dynamic.hdr_Fq;
    uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;
    assert(input_bit_depth < 32);

    uint16_t* buffer_out_16bit = (uint16_t*)precinct->coeff_buff_ptr_16bit[comp_id];

    transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(pi->components[comp_id].decom_h);
    assert(transform_V0_Hn != NULL);
    //transform_V0_H1, transform_V0_H2, transform_V0_H3, transform_V0_H4, transform_V0_H5
    transform_V0_Hn(component,
                    component_enc,
                    (const int32_t*)plane_buffer_in,
                    input_bit_depth,
                    &enc_common->picture_header_dynamic,
                    buffer_out_16bit,
                    param_out_Fq,
                    plane_width,
                    buffers_tmp_dwt->buffer_tmp);
}

/*Load first line and precalculate HF from previous precinct if not first precinct.*/
void precinct_component_calculate_dwt_V1_precalculate_slice(
    struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
    struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, const void** plane_buffer_in) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;
    assert(pi->components[comp_id].decom_v == 1 && enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY);
    uint32_t plane_width = pi->components[comp_id].width;
    uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;

    /*For mixing vertical V2 with 420 some components have V1 and need divide global index of precinct line:*/

    uint32_t line_idx = prec_idx * pi->components[comp_id].precinct_height;

    //Read first line on last position:
    nlt_input_scaling_line(plane_buffer_in[2],
                           buffers_dwt_per_component->V1[comp_id].line_first,
                           plane_width,
                           &enc_common->picture_header_dynamic,
                           input_bit_depth);

    //Precalculate previous
    if (line_idx > 0) {
        assert(line_idx >= 2);
        nlt_input_scaling_line(
            plane_buffer_in[0], buffers_dwt_tmp->V1.line_1, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
        nlt_input_scaling_line(
            plane_buffer_in[1], buffers_dwt_tmp->V1.line_2, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);

        //Precalculate previous line
        transform_V1_Hx_precinct_recalc_HF_prev(plane_width,
                                                buffers_dwt_per_component->V1[comp_id].in_tmp_line_HF_prev,
                                                buffers_dwt_tmp->V1.line_1,
                                                buffers_dwt_tmp->V1.line_2,
                                                buffers_dwt_per_component->V1[comp_id].line_first);
    }
}

void precinct_component_calculate_dwt_V1(struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx,
                                         uint16_t* buffer_out_16bit, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                         struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                         const void** plane_buffer_in) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;
    pi_enc_t* pi_enc = &enc_common->pi_enc;
    assert(pi->components[comp_id].decom_v == 1 && enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY);
    const pi_component_t* const component = &(pi->components[comp_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[comp_id]);
    uint32_t plane_width = pi->components[comp_id].width;
    uint32_t plane_height = pi->components[comp_id].height;
    uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;

    transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(pi->components[comp_id].decom_h);
    uint32_t line_idx = prec_idx * pi->components[comp_id].precinct_height;

    //Precalculate first precinct on beginning of slice then we can reuse data from previous precinct

    //Read next 2 lines and reuse last one as first
    if ((line_idx + 1) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[3], buffers_dwt_tmp->V1.line_1, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }
    if ((line_idx + 2) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[4], buffers_dwt_tmp->V1.line_2, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }

    transform_V1_Hx_precinct(component,
                             component_enc,
                             transform_V0_Hn,
                             pi->components[comp_id].decom_h,
                             line_idx,
                             plane_width,
                             plane_height,
                             buffers_dwt_per_component->V1[comp_id].line_first,
                             buffers_dwt_tmp->V1.line_1,
                             buffers_dwt_tmp->V1.line_2,
                             buffer_out_16bit,
                             enc_common->picture_header_dynamic.hdr_Fq,
                             buffers_dwt_per_component->V1[comp_id].in_tmp_line_HF_prev,
                             buffers_dwt_tmp->V1.out_tmp_line_HF_next,
                             buffers_dwt_tmp->buffer_tmp);

    int32_t* tmp;
    /*Move lines last one will be first for next precinct*/
    tmp = buffers_dwt_per_component->V1[comp_id].line_first;
    buffers_dwt_per_component->V1[comp_id].line_first = buffers_dwt_tmp->V1.line_2;
    buffers_dwt_tmp->V1.line_2 = tmp;

    /*Swap lines with HF*/
    tmp = buffers_dwt_per_component->V1[comp_id].in_tmp_line_HF_prev;
    buffers_dwt_per_component->V1[comp_id].in_tmp_line_HF_prev = buffers_dwt_tmp->V1.out_tmp_line_HF_next;
    buffers_dwt_tmp->V1.out_tmp_line_HF_next = tmp;
}

/*Load first line and precalculate HF from previous precinct if not first precinct.*/
void precinct_component_calculate_dwt_V2_precalculate_slice(
    struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
    struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, const void** plane_buffer_in) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;
    // assert(pi->components[comp_id].decom_v == 2 && enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY); //TODO: Uncomment
    const pi_component_t* const component = &(pi->components[comp_id]);
    uint32_t plane_width = pi->components[comp_id].width;
    uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;
    uint32_t plane_height = pi->components[comp_id].height;
    uint32_t line_idx = prec_idx * pi->components[comp_id].precinct_height;

    //Precalculate first precinct on beginning of slice then we can reuse data from previous precinct
    int32_t* line_p6 = buffers_dwt_tmp->buffer_tmp +
        (7 * plane_width / 2 + 2);            //Add minimal size for transform_V2_Hx_precinct_recalc()
    int32_t* line_p5 = line_p6 + plane_width; //Line previous precinct
    int32_t* line_p4 = line_p5 + plane_width; //Line previous precinct
    int32_t* line_p3 = line_p4 + plane_width; //Line previous precinct

    int32_t* line_p2 = buffers_dwt_tmp->V2.line_3;                   //Line previous precinct
    int32_t* line_p1 = buffers_dwt_tmp->V2.line_4;                   //Line previous precinct
    int32_t* line_0 = buffers_dwt_tmp->V2.line_5;                    //Line previous precinct
    int32_t* line_1 = buffers_dwt_tmp->V2.line_6;                    //Line previous precinct
    int32_t* line_2 = buffers_dwt_per_component->V2[comp_id].line_2; //Line previous precinct

    if (line_idx >= 6)
        nlt_input_scaling_line(plane_buffer_in[0], line_p6, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx >= 5)
        nlt_input_scaling_line(plane_buffer_in[1], line_p5, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx >= 4)
        nlt_input_scaling_line(plane_buffer_in[2], line_p4, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx >= 3)
        nlt_input_scaling_line(plane_buffer_in[3], line_p3, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx >= 2)
        nlt_input_scaling_line(plane_buffer_in[4], line_p2, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx >= 1)
        nlt_input_scaling_line(plane_buffer_in[5], line_p1, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    nlt_input_scaling_line(plane_buffer_in[6], line_0, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx + 1 < plane_height)
        nlt_input_scaling_line(plane_buffer_in[7], line_1, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    if (line_idx + 2 < plane_height)
        nlt_input_scaling_line(plane_buffer_in[8], line_2, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);

    transform_V2_Hx_precinct_recalc(component,
                                    pi->components[comp_id].decom_h,
                                    line_idx,
                                    plane_width,
                                    plane_height,
                                    line_p6,
                                    line_p5,
                                    line_p4,
                                    line_p3,
                                    line_p2,
                                    line_p1,
                                    line_0,
                                    line_1,
                                    line_2,
                                    buffers_dwt_per_component->V2[comp_id].buffer_prev,
                                    buffers_dwt_per_component->V2[comp_id].buffer_on_place,
                                    buffers_dwt_tmp->buffer_tmp);
}

void precinct_component_calculate_dwt_V2(struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx,
                                         uint16_t* buffer_out_16bit, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                         struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                         const void** plane_buffer_in) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;
    pi_enc_t* pi_enc = &enc_common->pi_enc;
    assert(pi->components[comp_id].decom_v == 2 && (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY));
    const pi_component_t* const component = &(pi->components[comp_id]);
    const pi_enc_component_t* const component_enc = &(pi_enc->components[comp_id]);
    uint32_t plane_width = pi->components[comp_id].width;
    uint32_t plane_height = pi->components[comp_id].height;
    uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;
    transform_V0_ptr_t transform_V0_Hn_sub_1 = transform_V0_get_function_ptr(pi->components[comp_id].decom_h - 1);
    uint32_t line_idx = prec_idx * pi->components[comp_id].precinct_height;

    //Read next 2 lines and reuse last one as first
    if ((line_idx + 3) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[9], buffers_dwt_tmp->V2.line_3, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }
    if ((line_idx + 4) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[10], buffers_dwt_tmp->V2.line_4, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }
    if ((line_idx + 5) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[11], buffers_dwt_tmp->V2.line_5, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }
    if ((line_idx + 6) < plane_height) {
        nlt_input_scaling_line(
            plane_buffer_in[12], buffers_dwt_tmp->V2.line_6, plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
    }

    transform_V2_Hx_precinct(component,
                             component_enc,
                             transform_V0_Hn_sub_1,
                             pi->components[comp_id].decom_h,
                             line_idx,
                             plane_width,
                             plane_height,
                             buffers_dwt_per_component->V2[comp_id].line_2,
                             buffers_dwt_tmp->V2.line_3,
                             buffers_dwt_tmp->V2.line_4,
                             buffers_dwt_tmp->V2.line_5,
                             buffers_dwt_tmp->V2.line_6,
                             buffer_out_16bit,
                             enc_common->picture_header_dynamic.hdr_Fq,
                             buffers_dwt_per_component->V2[comp_id].buffer_prev,
                             buffers_dwt_tmp->V2.buffer_next,
                             buffers_dwt_per_component->V2[comp_id].buffer_on_place,
                             buffers_dwt_tmp->buffer_tmp);

    int32_t* tmp;
    /*Move lines last one will be first for next precinct*/
    tmp = buffers_dwt_per_component->V2[comp_id].line_2;
    buffers_dwt_per_component->V2[comp_id].line_2 = buffers_dwt_tmp->V2.line_6;
    buffers_dwt_tmp->V2.line_6 = tmp;

    /*Swap lines with HF*/
    tmp = buffers_dwt_per_component->V2[comp_id].buffer_prev;
    buffers_dwt_per_component->V2[comp_id].buffer_prev = buffers_dwt_tmp->V2.buffer_next;
    buffers_dwt_tmp->V2.buffer_next = tmp;
}

void set_planar_input_pointers(uint32_t line_idx, const void* plane_buffer_in[13], struct PictureControlSet* pcs_ptr,
                               uint32_t comp) {
    const uint8_t* buffer_in_base_addr = pcs_ptr->enc_input.image.data_yuv[comp];
    const uint32_t plane_stride = (uint32_t)pcs_ptr->enc_input.image.stride[comp];
    const uint32_t pixel_size = pcs_ptr->enc_common->bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    if (pcs_ptr->enc_common->pi.components[comp].decom_v == 0) {
        plane_buffer_in[0] = buffer_in_base_addr + pixel_size * line_idx * plane_stride;
    }
    else if (pcs_ptr->enc_common->pi.components[comp].decom_v == 1) {
        plane_buffer_in[0] = buffer_in_base_addr + pixel_size * (line_idx - 2) * plane_stride;
        plane_buffer_in[1] = buffer_in_base_addr + pixel_size * (line_idx - 1) * plane_stride;
        plane_buffer_in[2] = buffer_in_base_addr + pixel_size * (line_idx - 0) * plane_stride;
        plane_buffer_in[3] = buffer_in_base_addr + pixel_size * (line_idx + 1) * plane_stride;
        plane_buffer_in[4] = buffer_in_base_addr + pixel_size * (line_idx + 2) * plane_stride;
    }
    else if (pcs_ptr->enc_common->pi.components[comp].decom_v == 2) {
        plane_buffer_in[0] = buffer_in_base_addr + pixel_size * (line_idx - 6) * plane_stride;
        plane_buffer_in[1] = buffer_in_base_addr + pixel_size * (line_idx - 5) * plane_stride;
        plane_buffer_in[2] = buffer_in_base_addr + pixel_size * (line_idx - 4) * plane_stride;
        plane_buffer_in[3] = buffer_in_base_addr + pixel_size * (line_idx - 3) * plane_stride;
        plane_buffer_in[4] = buffer_in_base_addr + pixel_size * (line_idx - 2) * plane_stride;
        plane_buffer_in[5] = buffer_in_base_addr + pixel_size * (line_idx - 1) * plane_stride;
        plane_buffer_in[6] = buffer_in_base_addr + pixel_size * (line_idx - 0) * plane_stride;
        plane_buffer_in[7] = buffer_in_base_addr + pixel_size * (line_idx + 1) * plane_stride;
        plane_buffer_in[8] = buffer_in_base_addr + pixel_size * (line_idx + 2) * plane_stride;
        plane_buffer_in[9] = buffer_in_base_addr + pixel_size * (line_idx + 3) * plane_stride;
        plane_buffer_in[10] = buffer_in_base_addr + pixel_size * (line_idx + 4) * plane_stride;
        plane_buffer_in[11] = buffer_in_base_addr + pixel_size * (line_idx + 5) * plane_stride;
        plane_buffer_in[12] = buffer_in_base_addr + pixel_size * (line_idx + 6) * plane_stride;
    }
}

//packed 8bit rgb -> planar 8bit rgb, aka ffmpeg  RGB24 -> RGBP
void convert_packed_to_planar_rgb_8bit_c(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                         uint32_t line_width) {
    const uint8_t* in = in_rgb;
    uint8_t* out_c1 = out_comp1;
    uint8_t* out_c2 = out_comp2;
    uint8_t* out_c3 = out_comp3;

    for (uint32_t pix = 0; pix < line_width; pix++) {
        out_c1[pix] = in[0];
        out_c2[pix] = in[1];
        out_c3[pix] = in[2];
        in += 3;
    }
}

//packed 16bit rgb -> planar 16bit rgb, aka ffmpeg  RGB48 -> RGBP10le
void convert_packed_to_planar_rgb_16bit_c(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                          uint32_t line_width) {
    const uint16_t* in = in_rgb;
    uint16_t* out_c1 = out_comp1;
    uint16_t* out_c2 = out_comp2;
    uint16_t* out_c3 = out_comp3;

    for (uint32_t pix = 0; pix < line_width; pix++) {
        out_c1[pix] = in[0];
        out_c2[pix] = in[1];
        out_c3[pix] = in[2];
        in += 3;
    }
}

void set_packed_input_pointers_precalc(uint32_t line_idx, void* plane_buffer_in[3][13], struct PictureControlSet* pcs_ptr,
                                       void* buffer_tmp, convert_fn packed_to_planar_fn) {
    const uint8_t* buffer_in_base_addr = pcs_ptr->enc_input.image.data_yuv[0];
    const uint32_t plane_stride = (uint32_t)pcs_ptr->enc_input.image.stride[0];
    const uint32_t pixel_size = pcs_ptr->enc_common->bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    uint32_t plane_height = pcs_ptr->enc_common->pi.components[0].height;
    uint32_t plane_width = pcs_ptr->enc_common->pi.components[0].width;

    if (pcs_ptr->enc_common->pi.decom_v == 0) {
        return;
    }
    else if (pcs_ptr->enc_common->pi.decom_v == 1) {
        plane_buffer_in[0][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 0 * pixel_size;
        plane_buffer_in[1][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 1 * pixel_size;
        plane_buffer_in[2][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 2 * pixel_size;

        plane_buffer_in[0][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 3 * pixel_size;
        plane_buffer_in[1][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 4 * pixel_size;
        plane_buffer_in[2][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 5 * pixel_size;

        plane_buffer_in[0][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 6 * pixel_size;
        plane_buffer_in[1][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 7 * pixel_size;
        plane_buffer_in[2][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 8 * pixel_size;

        if (line_idx > 0) {
            assert(line_idx >= 2);
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 2) * plane_stride * pixel_size,
                                plane_buffer_in[0][0],
                                plane_buffer_in[1][0],
                                plane_buffer_in[2][0],
                                plane_width);
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 1) * plane_stride * pixel_size,
                                plane_buffer_in[0][1],
                                plane_buffer_in[1][1],
                                plane_buffer_in[2][1],
                                plane_width);
        }
        packed_to_planar_fn(buffer_in_base_addr + (line_idx - 0) * plane_stride * pixel_size,
                            plane_buffer_in[0][2],
                            plane_buffer_in[1][2],
                            plane_buffer_in[2][2],
                            plane_width);
    }
    else if (pcs_ptr->enc_common->pi.decom_v == 2) {
        plane_buffer_in[0][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 0 * pixel_size;
        plane_buffer_in[1][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 1 * pixel_size;
        plane_buffer_in[2][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 2 * pixel_size;

        plane_buffer_in[0][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 3 * pixel_size;
        plane_buffer_in[1][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 4 * pixel_size;
        plane_buffer_in[2][1] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 5 * pixel_size;

        plane_buffer_in[0][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 6 * pixel_size;
        plane_buffer_in[1][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 7 * pixel_size;
        plane_buffer_in[2][2] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 8 * pixel_size;

        plane_buffer_in[0][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 9 * pixel_size;
        plane_buffer_in[1][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 10 * pixel_size;
        plane_buffer_in[2][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 11 * pixel_size;

        plane_buffer_in[0][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 12 * pixel_size;
        plane_buffer_in[1][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 13 * pixel_size;
        plane_buffer_in[2][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 14 * pixel_size;

        plane_buffer_in[0][5] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 15 * pixel_size;
        plane_buffer_in[1][5] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 16 * pixel_size;
        plane_buffer_in[2][5] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 17 * pixel_size;

        plane_buffer_in[0][6] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 18 * pixel_size;
        plane_buffer_in[1][6] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 19 * pixel_size;
        plane_buffer_in[2][6] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 20 * pixel_size;

        plane_buffer_in[0][7] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 21 * pixel_size;
        plane_buffer_in[1][7] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 22 * pixel_size;
        plane_buffer_in[2][7] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 23 * pixel_size;

        plane_buffer_in[0][8] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 24 * pixel_size;
        plane_buffer_in[1][8] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 25 * pixel_size;
        plane_buffer_in[2][8] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 26 * pixel_size;

        if (line_idx >= 6) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 6) * plane_stride * pixel_size,
                                plane_buffer_in[0][0],
                                plane_buffer_in[1][0],
                                plane_buffer_in[2][0],
                                plane_width);
        }
        if (line_idx >= 5) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 5) * plane_stride * pixel_size,
                                plane_buffer_in[0][1],
                                plane_buffer_in[1][1],
                                plane_buffer_in[2][1],
                                plane_width);
        }
        if (line_idx >= 4) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 4) * plane_stride * pixel_size,
                                plane_buffer_in[0][2],
                                plane_buffer_in[1][2],
                                plane_buffer_in[2][2],
                                plane_width);
        }
        if (line_idx >= 3) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 3) * plane_stride * pixel_size,
                                plane_buffer_in[0][3],
                                plane_buffer_in[1][3],
                                plane_buffer_in[2][3],
                                plane_width);
        }
        if (line_idx >= 2) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 2) * plane_stride * pixel_size,
                                plane_buffer_in[0][4],
                                plane_buffer_in[1][4],
                                plane_buffer_in[2][4],
                                plane_width);
        }
        if (line_idx >= 1) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx - 1) * plane_stride * pixel_size,
                                plane_buffer_in[0][5],
                                plane_buffer_in[1][5],
                                plane_buffer_in[2][5],
                                plane_width);
        }
        packed_to_planar_fn(buffer_in_base_addr + (line_idx - 0) * plane_stride * pixel_size,
                            plane_buffer_in[0][6],
                            plane_buffer_in[1][6],
                            plane_buffer_in[2][6],
                            plane_width);
        if (line_idx + 1 < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 1) * plane_stride * pixel_size,
                                plane_buffer_in[0][7],
                                plane_buffer_in[1][7],
                                plane_buffer_in[2][7],
                                plane_width);
        }
        if (line_idx + 2 < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 2) * plane_stride * pixel_size,
                                plane_buffer_in[0][8],
                                plane_buffer_in[1][8],
                                plane_buffer_in[2][8],
                                plane_width);
        }
    }
}

void set_packed_input_pointers_calc(uint32_t line_idx, void* plane_buffer_in[3][13], struct PictureControlSet* pcs_ptr,
                                    void* buffer_tmp, convert_fn packed_to_planar_fn) {
    const uint8_t* buffer_in_base_addr = pcs_ptr->enc_input.image.data_yuv[0];
    const uint32_t plane_stride = (uint32_t)pcs_ptr->enc_input.image.stride[0];
    const uint32_t pixel_size = pcs_ptr->enc_common->bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    uint32_t plane_height = pcs_ptr->enc_common->pi.components[0].height;
    uint32_t plane_width = pcs_ptr->enc_common->pi.components[0].width;

    if (pcs_ptr->enc_common->pi.decom_v == 0) {
        plane_buffer_in[0][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 0 * pixel_size;
        plane_buffer_in[1][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 1 * pixel_size;
        plane_buffer_in[2][0] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 2 * pixel_size;
        packed_to_planar_fn(buffer_in_base_addr + line_idx * plane_stride * pixel_size,
                            plane_buffer_in[0][0],
                            plane_buffer_in[1][0],
                            plane_buffer_in[2][0],
                            plane_width);
    }
    else if (pcs_ptr->enc_common->pi.decom_v == 1) {
        plane_buffer_in[0][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 0 * pixel_size;
        plane_buffer_in[1][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 1 * pixel_size;
        plane_buffer_in[2][3] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 2 * pixel_size;

        plane_buffer_in[0][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 3 * pixel_size;
        plane_buffer_in[1][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 4 * pixel_size;
        plane_buffer_in[2][4] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 5 * pixel_size;

        if ((line_idx + 1) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 1) * plane_stride * pixel_size,
                                plane_buffer_in[0][3],
                                plane_buffer_in[1][3],
                                plane_buffer_in[2][3],
                                plane_width);
        }

        if ((line_idx + 2) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 2) * plane_stride * pixel_size,
                                plane_buffer_in[0][4],
                                plane_buffer_in[1][4],
                                plane_buffer_in[2][4],
                                plane_width);
        }
    }
    else if (pcs_ptr->enc_common->pi.decom_v == 2) {
        plane_buffer_in[0][9] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 0 * pixel_size;
        plane_buffer_in[1][9] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 1 * pixel_size;
        plane_buffer_in[2][9] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 2 * pixel_size;

        plane_buffer_in[0][10] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 3 * pixel_size;
        plane_buffer_in[1][10] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 4 * pixel_size;
        plane_buffer_in[2][10] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 5 * pixel_size;

        plane_buffer_in[0][11] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 6 * pixel_size;
        plane_buffer_in[1][11] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 7 * pixel_size;
        plane_buffer_in[2][11] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 8 * pixel_size;

        plane_buffer_in[0][12] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 9 * pixel_size;
        plane_buffer_in[1][12] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 10 * pixel_size;
        plane_buffer_in[2][12] = (uint8_t*)buffer_tmp + pcs_ptr->enc_common->pi.width * 11 * pixel_size;

        if ((line_idx + 3) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 3) * plane_stride * pixel_size,
                                plane_buffer_in[0][9],
                                plane_buffer_in[1][9],
                                plane_buffer_in[2][9],
                                plane_width);
        }
        if ((line_idx + 4) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 4) * plane_stride * pixel_size,
                                plane_buffer_in[0][10],
                                plane_buffer_in[1][10],
                                plane_buffer_in[2][10],
                                plane_width);
        }
        if ((line_idx + 5) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 5) * plane_stride * pixel_size,
                                plane_buffer_in[0][11],
                                plane_buffer_in[1][11],
                                plane_buffer_in[2][11],
                                plane_width);
        }
        if ((line_idx + 6) < plane_height) {
            packed_to_planar_fn(buffer_in_base_addr + (line_idx + 6) * plane_stride * pixel_size,
                                plane_buffer_in[0][12],
                                plane_buffer_in[1][12],
                                plane_buffer_in[2][12],
                                plane_width);
        }
    }
}

void precinct_calculate_data(struct PictureControlSet* pcs_ptr, precinct_enc_t* precinct, PackInput_t* pack_input,
                             struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                             struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, uint32_t prec_idx_in_slice) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_t* pi = &enc_common->pi;

    //packed input image support
    if (pcs_ptr->enc_common->colour_format > COLOUR_FORMAT_PACKED_MIN &&
        pcs_ptr->enc_common->colour_format < COLOUR_FORMAT_PACKED_MAX) {
        convert_fn packed_to_planar_fn = pcs_ptr->enc_common->bit_depth == 8 ? convert_packed_to_planar_rgb_8bit
                                                                             : convert_packed_to_planar_rgb_16bit;

        const uint32_t line_idx = precinct->prec_idx * pi->components[0].precinct_height;
        void* plane_buffer_in[3][13] = {0};
        if (prec_idx_in_slice == 0 && pi->decom_v != 0) {
            set_packed_input_pointers_precalc(
                line_idx, plane_buffer_in, pcs_ptr, buffers_dwt_tmp->buffer_unpacked_color_formats, packed_to_planar_fn);

            for (uint32_t c = 0; c < pi->comps_num; ++c) {
                if (pi->components[c].decom_v == 1) {
                    precinct_component_calculate_dwt_V1_precalculate_slice(pcs_ptr,
                                                                           c,
                                                                           precinct->prec_idx,
                                                                           buffers_dwt_tmp,
                                                                           buffers_dwt_per_component,
                                                                           (const void**)plane_buffer_in[c]);
                }
                else if (pi->components[c].decom_v == 2) {
                    precinct_component_calculate_dwt_V2_precalculate_slice(pcs_ptr,
                                                                           c,
                                                                           precinct->prec_idx,
                                                                           buffers_dwt_tmp,
                                                                           buffers_dwt_per_component,
                                                                           (const void**)plane_buffer_in[c]);
                }
            }
        }

        set_packed_input_pointers_calc(
            line_idx, plane_buffer_in, pcs_ptr, buffers_dwt_tmp->buffer_unpacked_color_formats, packed_to_planar_fn);
        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            if (pi->components[c].decom_v == 0) {
                precinct_component_calculate_dwt_V0(pcs_ptr, precinct, c, buffers_dwt_tmp, (const void*)plane_buffer_in[c][0]);
            }
            else if (pi->components[c].decom_v == 1) {
                precinct_component_calculate_dwt_V1(pcs_ptr,
                                                    c,
                                                    precinct->prec_idx,
                                                    (uint16_t*)precinct->coeff_buff_ptr_16bit[c],
                                                    buffers_dwt_tmp,
                                                    buffers_dwt_per_component,
                                                    (const void**)plane_buffer_in[c]);
            }
            else if (pi->components[c].decom_v == 2) {
                precinct_component_calculate_dwt_V2(pcs_ptr,
                                                    c,
                                                    precinct->prec_idx,
                                                    (uint16_t*)precinct->coeff_buff_ptr_16bit[c],
                                                    buffers_dwt_tmp,
                                                    buffers_dwt_per_component,
                                                    (const void**)plane_buffer_in[c]);
            }

            for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
                struct band_data_enc* band = &(precinct->bands[c][b]);
                const uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                const uint32_t width = precinct->p_info->b_info[c][b].width;
                const uint32_t gcli_width = precinct->p_info->b_info[c][b].gcli_width;
                for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                    gc_precinct_stage_scalar(band->lines_common[line_idx].gcli_data_ptr,
                                             band->lines_common[line_idx].coeff_data_ptr_16bit,
                                             pi->coeff_group_size,
                                             width);
                    if (pcs_ptr->enc_common->coding_significance) {
                        //Precalculate Data Size
                        gc_precinct_sigflags_max(band->lines_common[line_idx].significance_data_max_ptr,
                                                 band->lines_common[line_idx].gcli_data_ptr,
                                                 pi->significance_group_size,
                                                 gcli_width);
                    }
                }
            }
        }
    } //planar input image support
    else {
        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            const void* plane_buffer_in[13] = {0};
            const uint32_t line_idx = precinct->prec_idx * pi->components[c].precinct_height;
            set_planar_input_pointers(line_idx, plane_buffer_in, pcs_ptr, c);

            if ((enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) && (prec_idx_in_slice == 0)) {
                //Precalculate only for first precinct in slice, then reuse common data for DWT
                if (pi->components[c].decom_v == 1) {
                    precinct_component_calculate_dwt_V1_precalculate_slice(
                        pcs_ptr, c, precinct->prec_idx, buffers_dwt_tmp, buffers_dwt_per_component, plane_buffer_in);
                }
                else if (pi->components[c].decom_v == 2) {
                    precinct_component_calculate_dwt_V2_precalculate_slice(
                        pcs_ptr, c, precinct->prec_idx, buffers_dwt_tmp, buffers_dwt_per_component, plane_buffer_in);
                }
            }

            if (pi->components[c].decom_v == 0) {
                precinct_component_calculate_dwt_V0(pcs_ptr, precinct, c, buffers_dwt_tmp, plane_buffer_in[0]);
            }
            else if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
                if (pi->components[c].decom_v == 1) {
                    precinct_component_calculate_dwt_V1(pcs_ptr,
                                                        c,
                                                        precinct->prec_idx,
                                                        (uint16_t*)precinct->coeff_buff_ptr_16bit[c],
                                                        buffers_dwt_tmp,
                                                        buffers_dwt_per_component,
                                                        plane_buffer_in);
                }
                else if (pi->components[c].decom_v == 2) {
                    precinct_component_calculate_dwt_V2(pcs_ptr,
                                                        c,
                                                        precinct->prec_idx,
                                                        (uint16_t*)precinct->coeff_buff_ptr_16bit[c],
                                                        buffers_dwt_tmp,
                                                        buffers_dwt_per_component,
                                                        plane_buffer_in);
                }
            }
            else if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
                /*Sync with DWT threads.*/
                if (pi->components[c].decom_v != 0) {
                    while (pack_input->sync_dwt_component_done_flag[c] == 0) {
                        svt_jxs_block_on_semaphore(pack_input->sync_dwt_semaphore);
                    }
                }
            }

            for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
                struct band_data_enc* band = &(precinct->bands[c][b]);
                const uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                const uint32_t width = precinct->p_info->b_info[c][b].width;
                const uint32_t gcli_width = precinct->p_info->b_info[c][b].gcli_width;
                for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                    gc_precinct_stage_scalar(band->lines_common[line_idx].gcli_data_ptr,
                                             band->lines_common[line_idx].coeff_data_ptr_16bit,
                                             pi->coeff_group_size,
                                             width);
                    if (pcs_ptr->enc_common->coding_significance) {
                        //Precalculate Data Size
                        gc_precinct_sigflags_max(band->lines_common[line_idx].significance_data_max_ptr,
                                                 band->lines_common[line_idx].gcli_data_ptr,
                                                 pi->significance_group_size,
                                                 gcli_width);
                    }
                }
            }
        }
    }
}

SvtJxsErrorType_t buffers_components_allocate(struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, pi_t* pi) {
    uint8_t decom_V1_exist = 0;
    uint8_t decom_V2_exist = 0;
    for (uint32_t i = 0; i < pi->comps_num; ++i) {
        if (pi->components[i].decom_v == 1) {
            decom_V1_exist = 1;
        }
        else if (pi->components[i].decom_v == 2) {
            decom_V2_exist = 1;
        }
    }

    uint32_t c = 0;
    if (decom_V1_exist) {
        for (; c < pi->comps_num; ++c) {
            SVT_CALLOC(buffers_dwt_per_component->V1[c].in_tmp_line_HF_prev, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(buffers_dwt_per_component->V1[c].line_first, 1, pi->width * sizeof(int32_t));
        }
    }
    for (; c < MAX_COMPONENTS_NUM; ++c) {
        buffers_dwt_per_component->V1[c].in_tmp_line_HF_prev = NULL;
        buffers_dwt_per_component->V1[c].line_first = NULL;
    }

    c = 0;
    if (decom_V2_exist) {
        for (; c < pi->comps_num; ++c) {
            SVT_CALLOC(buffers_dwt_per_component->V2[c].line_2, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(buffers_dwt_per_component->V2[c].buffer_prev, 1, (pi->width + 1) * sizeof(int32_t));
            SVT_CALLOC(buffers_dwt_per_component->V2[c].buffer_on_place, 1, (3 * pi->width / 2) * sizeof(int32_t));
        }
    }
    for (; c < MAX_COMPONENTS_NUM; ++c) {
        buffers_dwt_per_component->V2[c].line_2 = NULL;
        buffers_dwt_per_component->V2[c].buffer_prev = NULL;
        buffers_dwt_per_component->V2[c].buffer_on_place = NULL;
    }
    return SvtJxsErrorNone;
}

void buffers_components_free(struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component) {
    for (uint32_t c = 0; c < MAX_COMPONENTS_NUM; ++c) {
        SVT_FREE(buffers_dwt_per_component->V1[c].in_tmp_line_HF_prev);
        SVT_FREE(buffers_dwt_per_component->V1[c].line_first);
    }
    for (uint32_t c = 0; c < MAX_COMPONENTS_NUM; ++c) {
        SVT_FREE(buffers_dwt_per_component->V2[c].line_2);
        SVT_FREE(buffers_dwt_per_component->V2[c].buffer_prev);
        SVT_FREE(buffers_dwt_per_component->V2[c].buffer_on_place);
    }
}
