/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>
#include "Enc_avx512.h"
#include "DwtInput.h"
#include "DwtStageProcess.h"
#include "EncHandle.h"
#include "PictureControlSet.h"
#include "Threads/SystemResourceManager.h"
#include "SvtLog.h"
#include "Threads/SvtObject.h"
#include "Threads/SvtThreads.h"
#include "SvtUtility.h"
#include "encoder_dsp_rtcd.h"
#include "Codestream.h"
#include "NltEnc.h"
#include "Dwt.h"

typedef struct DwtStageContext {
    svt_jpeg_xs_encoder_common_t* enc_common;
    Fifo_t* dwt_stage_input_fifo_ptr;
    int process_idx;
    int32_t* buffers_tmp; //Buffers to calculate DWT for one component
} DwtStageContext_t;

static void dwt_stage_context_dctor(void_ptr p) {
    ThreadContext_t* thread_contxt_ptr = (ThreadContext_t*)p;
    if (thread_contxt_ptr->priv) {
        DwtStageContext_t* obj = (DwtStageContext_t*)thread_contxt_ptr->priv;
        SVT_FREE_ALIGNED_ARRAY(obj->buffers_tmp);
        SVT_FREE_ARRAY(obj);
    }
}

/************************************************
 * dwt Context Constructor
 ************************************************/
SvtJxsErrorType_t dwt_stage_context_ctor(ThreadContext_t* thread_context_ptr, svt_jpeg_xs_encoder_api_prv_t* enc_api_prv,
                                         int idx) {
    svt_jpeg_xs_encoder_common_t* enc_common = &enc_api_prv->enc_common;
    DwtStageContext_t* context_ptr;
    SVT_CALLOC_ARRAY(context_ptr, 1);
    thread_context_ptr->priv = context_ptr;
    thread_context_ptr->dctor = dwt_stage_context_dctor;

    context_ptr->dwt_stage_input_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(enc_api_prv->dwt_input_resource_ptr, idx);

    context_ptr->process_idx = idx;

    context_ptr->enc_common = enc_common;
    pi_t* pi = &enc_common->pi;

    size_t buffers_tmp_size = 0;
    /*if (enc_common->cpu_profile == CPU_PROFILE_LOW_CPU && pi->decom_v == 0) {
        buffers_tmp_size = (size_t)picture_info_ptr->input_luma_width * 3 / 2;
    }*/
    if (pi->decom_v == 1) {
        /*Allocate buffer for V1:
         *3 lines on convert input,
         *1 - previous HF
         *1 - actual HF
         *2.5 temp for Vertical and Horizontal temp
         *Summary: 7.5 line == 15/2*width
         */
        buffers_tmp_size = (size_t)pi->width * 15 / 2;
    }
    else if (pi->decom_v == 2) {
        /*Allocate 2 buffers for biggest component size.
         5 lines,
         tmp buffer
         prev, next, on_place
         buffers_tmp = Size: 5 * plane_width + (5 * plane_width + 1) + 3 * plane_width / 2 + plane_width + 1 + width + 1;
          buffers_tmp = Size: 27 * plane_width /2 +  3;*/
        //TODO: For some configurations V and H second buffer can be smaller
        buffers_tmp_size = (size_t)pi->width * 27 / 2 + 3;
    }

    if (buffers_tmp_size > 0) {
        SVT_MALLOC_ALIGNED_ARRAY(context_ptr->buffers_tmp, buffers_tmp_size);
    }
    else {
        context_ptr->buffers_tmp = NULL;
    }

    return SvtJxsErrorNone;
}

/************************************************
 * dwt transformation Kernel
 *************************************************/
void* dwt_stage_kernel(void* input_ptr) {
    ThreadContext_t* enc_contxt_ptr = (ThreadContext_t*)input_ptr;
    DwtStageContext_t* context_ptr = (DwtStageContext_t*)enc_contxt_ptr->priv;

    ObjectWrapper_t* input_wrapper;
    int32_t* buffers_tmp = context_ptr->buffers_tmp;

    for (;;) {
        // Get the Next dwt Input Buffer [BLOCKING]
        SVT_GET_FULL_OBJECT(context_ptr->dwt_stage_input_fifo_ptr, &input_wrapper);
        DwtInput* in = (DwtInput*)input_wrapper->object_ptr;
        ObjectWrapper_t* in_pcs_wrapper_ptr = in->pcs_wrapper_ptr;
        uint32_t component_id = in->component_id;
        volatile PackInput_t* list_slice_next = in->list_slices;

        PictureControlSet* pcs_ptr = (PictureControlSet*)in_pcs_wrapper_ptr->object_ptr;
        svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
        const pi_t* const pi = &enc_common->pi;

        uint16_t* buffer_out_16bit = pcs_ptr->coeff_buff_ptr_16bit[component_id];
        svt_jpeg_xs_image_buffer_t* image_buffer = &pcs_ptr->enc_input.image;

        const uint8_t* plane_buffer_in = image_buffer->data_yuv[component_id];
        uint16_t plane_stride = image_buffer->stride[component_id];

        uint8_t input_bit_depth = (uint8_t)enc_common->bit_depth;
        const uint32_t pixel_size = input_bit_depth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);
        uint32_t plane_width = pi->components[component_id].width;
        uint32_t plane_height = pi->components[component_id].height;

        const pi_component_t* const component = &(pi->components[component_id]);
        const pi_enc_t* const pi_enc = &enc_common->pi_enc;
        const pi_enc_component_t* const component_enc = &(pi_enc->components[component_id]);
        const uint32_t precinct_height = pi->components[component_id].precinct_height;
        const uint32_t slice_height = pi->precincts_per_slice * precinct_height;
        int decom_h = component->decom_h;
        int decom_v = component->decom_v;

        // Release the Input Results
        svt_jxs_release_object(input_wrapper);
        assert(enc_common->cpu_profile == CPU_PROFILE_CPU);

        if (decom_v == 0) {
            /* For V0 directly calculate DWT in slice thread.
            if (enc_common->cpu_profile != CPU_PROFILE_LOW_LATENCY) {
                transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(decom_h);
                assert(transform_V0_Hn != NULL);

                for (uint32_t line = 0; line < plane_height; line += 1) {
                    //Add offset to buffers
                    uint16_t* buffer_out_16bit_offset = buffer_out_16bit + line * pi_enc->coeff_buff_tmp_size_precinct[component_id];
                    const int32_t* plane_buffer_in_offset;
                    if (input_bit_depth == 0) {
                        //Not convert input
                        plane_buffer_in_offset = plane_buffer_in + line * plane_width;
                    }
                    else if (input_bit_depth <= 8) {
                        plane_buffer_in_offset = (int32_t*)(((uint8_t*)plane_buffer_in) + line * plane_stride);
                    }
                    else {
                        plane_buffer_in_offset = (int32_t*)(((uint16_t*)plane_buffer_in) + line * plane_stride);
                    }
                    //transform_V0_H1, transform_V0_H2, transform_V0_H3, transform_V0_H4, transform_V0_H5
                    transform_V0_Hn(component, component_enc, plane_buffer_in_offset, input_bit_depth, enc_common->picture_header_dynamic.hdr_Bw, buffer_out_16bit_offset, enc_common->picture_header_dynamic.hdr_Fq, plane_width, buffers_tmp);
                }
            }
            */
            assert(0);
            continue;
        }
        else if (decom_v == 1) {
            uint8_t line_begin = 0;
            int32_t* line_x[3];
            //Size temp total 7.5 width
            line_x[0] = buffers_tmp + 0 * plane_width;
            line_x[1] = buffers_tmp + 1 * plane_width;
            line_x[2] = buffers_tmp + 2 * plane_width;

            int32_t* in_tmp_line_HF_prev = buffers_tmp + 3 * plane_width;
            int32_t* out_tmp_line_HF_next = buffers_tmp + 4 * plane_width;
            int32_t* buffer_tmp = buffers_tmp + 5 * plane_width; //Last 2.5 width

            uint32_t precinct_offset = pi_enc->coeff_buff_tmp_size_precinct[component_id];
            transform_V0_ptr_t transform_V0_Hn = transform_V0_get_function_ptr(decom_h);

            //Read first line on last position:
            line_begin = 2;
            nlt_input_scaling_line(
                plane_buffer_in, line_x[(line_begin + 0) % 3], plane_width, &enc_common->picture_header_dynamic, input_bit_depth);
            line_begin = 0; //First line is on last position to reuse

            for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 2) {
                //Read next 2 lines and reuse last one as first
                line_begin = (line_begin + 2) % 3;
                if ((line_idx + 1) < plane_height) {
                    nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 1) * plane_stride,
                                           line_x[(line_begin + 1) % 3],
                                           plane_width,
                                           &enc_common->picture_header_dynamic,
                                           input_bit_depth);
                }
                if ((line_idx + 2) < plane_height) {
                    nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 2) * plane_stride,
                                           line_x[(line_begin + 2) % 3],
                                           plane_width,
                                           &enc_common->picture_header_dynamic,
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
                                         enc_common->picture_header_dynamic.hdr_Fq,
                                         in_tmp_line_HF_prev,
                                         out_tmp_line_HF_next,
                                         buffer_tmp);

                /*Swap lines*/
                int32_t* tmp = in_tmp_line_HF_prev;
                in_tmp_line_HF_prev = out_tmp_line_HF_next;
                out_tmp_line_HF_next = tmp;

                //Send sync after finish Slice
                if (line_idx && (((line_idx + 2) % slice_height) == 0)) {
                    volatile PackInput_t* list_slice_next_old = list_slice_next;
                    list_slice_next = list_slice_next->sync_dwt_list_next;
                    Handle_t sync_dwt_semaphore = list_slice_next_old->sync_dwt_semaphore;
                    //After set flag list item can be not longer actual for last component. First get next item
                    list_slice_next_old->sync_dwt_component_done_flag[component_id] = 1;
                    svt_jxs_post_semaphore(sync_dwt_semaphore);
                }
            }
            //Send sync after finish last Slice
            if (list_slice_next) {
                volatile PackInput_t* list_slice_next_old = list_slice_next;
                list_slice_next = list_slice_next->sync_dwt_list_next;
                Handle_t sync_dwt_semaphore = list_slice_next_old->sync_dwt_semaphore;
                //After set flag list item can be not longer actual for last component. First get next item
                list_slice_next_old->sync_dwt_component_done_flag[component_id] = 1;
                svt_jxs_post_semaphore(sync_dwt_semaphore);
            }
            assert(list_slice_next == NULL);
            continue;
        }
        assert(decom_v == 2);

        int32_t* line_2 = buffers_tmp;
        int32_t* line_3 = buffers_tmp + plane_width;
        int32_t* line_4 = buffers_tmp + 2 * plane_width;
        int32_t* line_5 = buffers_tmp + 3 * plane_width;
        int32_t* line_6 = buffers_tmp + 4 * plane_width;
        int32_t* buffer_tmp = buffers_tmp + 5 * plane_width;                              //Temporary Size:  (5 * width + 1)
        int32_t* buffer_on_place = buffers_tmp + 5 * plane_width + (5 * plane_width + 1); //Temporary Size:  3*width/2
        int32_t* buffer_prev = buffers_tmp + 5 * plane_width + (5 * plane_width + 1) +
            3 * plane_width / 2; //Temporary Size:  >width + 1
        int32_t* buffer_next = buffers_tmp + 5 * plane_width + (5 * plane_width + 1) + 3 * plane_width / 2 + plane_width +
            1; //Temporary Size:  >width + 1
        uint32_t precinct_offset = pi_enc->coeff_buff_tmp_size_precinct[component_id];

        transform_V0_ptr_t transform_V0_Hn_sub_1 = transform_V0_get_function_ptr(decom_h - 1);

        nlt_input_scaling_line(plane_buffer_in + pixel_size * 0 * plane_stride,
                               line_4,
                               plane_width,
                               &enc_common->picture_header_dynamic,
                               input_bit_depth);
        nlt_input_scaling_line(plane_buffer_in + pixel_size * 1 * plane_stride,
                               line_5,
                               plane_width,
                               &enc_common->picture_header_dynamic,
                               input_bit_depth);
        nlt_input_scaling_line(plane_buffer_in + pixel_size * 2 * plane_stride,
                               line_6,
                               plane_width,
                               &enc_common->picture_header_dynamic,
                               input_bit_depth);
        transform_V2_Hx_precinct_recalc_prec_0(plane_width,
                                               line_4, //0
                                               line_5, //1
                                               line_6, //2
                                               buffer_prev,
                                               buffer_on_place,
                                               buffer_tmp);

        for (uint32_t line_idx = 0; line_idx < plane_height; line_idx += 4) {
            int32_t* tmp = line_2;
            line_2 = line_6;
            line_6 = tmp;

            if (3 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 3) * plane_stride,
                                       line_3,
                                       plane_width,
                                       &enc_common->picture_header_dynamic,
                                       input_bit_depth);
            if (4 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 4) * plane_stride,
                                       line_4,
                                       plane_width,
                                       &enc_common->picture_header_dynamic,
                                       input_bit_depth);
            if (5 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 5) * plane_stride,
                                       line_5,
                                       plane_width,
                                       &enc_common->picture_header_dynamic,
                                       input_bit_depth);
            if (6 + line_idx < plane_height)
                nlt_input_scaling_line(plane_buffer_in + pixel_size * (line_idx + 6) * plane_stride,
                                       line_6,
                                       plane_width,
                                       &enc_common->picture_header_dynamic,
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

                                     enc_common->picture_header_dynamic.hdr_Fq,
                                     buffer_prev,
                                     buffer_next,
                                     buffer_on_place,
                                     buffer_tmp);

            /*Swap buffers*/
            /* int32_t* */ tmp = buffer_prev;
            buffer_prev = buffer_next;
            buffer_next = tmp;

            //Send sync after finish Slice
            if (line_idx && (((line_idx + 4) % slice_height) == 0)) {
                volatile PackInput_t* list_slice_next_old = list_slice_next;
                list_slice_next = list_slice_next->sync_dwt_list_next;
                Handle_t sync_dwt_semaphore = list_slice_next_old->sync_dwt_semaphore;
                //After set flag list item can be not longer actual for last component. First get next item
                list_slice_next_old->sync_dwt_component_done_flag[component_id] = 1;
                svt_jxs_post_semaphore(sync_dwt_semaphore);
            }
        }
        //Send sync after finish last Slice
        if (list_slice_next) {
            volatile PackInput_t* list_slice_next_old = list_slice_next;
            list_slice_next = list_slice_next->sync_dwt_list_next;
            Handle_t sync_dwt_semaphore = list_slice_next_old->sync_dwt_semaphore;
            //After set flag list item can be not longer actual for last component. First get next item
            list_slice_next_old->sync_dwt_component_done_flag[component_id] = 1;
            svt_jxs_post_semaphore(sync_dwt_semaphore);
        }
        assert(list_slice_next == NULL);
    }
    return NULL;
}
