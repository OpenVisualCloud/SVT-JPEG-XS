/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>

#include "DwtInput.h"
#include "EncHandle.h"
#include "InitStageProcess.h"
#include "PictureControlSet.h"
#include "Threads/SystemResourceManager.h"
#include "SvtLog.h"
#include "Threads/SvtObject.h"
#include "SvtUtility.h"
#include "common_dsp_rtcd.h"
#include "PreRcStageProcess.h"
#include "PackIn.h"

typedef struct InitStageContext {
    Fifo_t *dwt_stage_input_fifo_ptr;
    Fifo_t *pack_input_buffer_fifo_ptr;
    Fifo_t *picture_control_set_fifo_ptr;
    svt_jpeg_xs_encoder_api_prv_t *enc_api_prv;
} InitStageContext;

SvtJxsErrorType_t input_item_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    EncoderInputItem *input_item;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(input_item, 1, sizeof(EncoderInputItem));
    *object_dbl_ptr = (void_ptr)input_item;

    return SvtJxsErrorNone;
}

void input_item_destroyer(void_ptr p) {
    EncoderInputItem *obj = (EncoderInputItem *)p;
    SVT_FREE(obj);
}

static void init_stage_context_dctor(void_ptr p) {
    ThreadContext_t *thread_contxt_ptr = (ThreadContext_t *)p;

    if (thread_contxt_ptr->priv) {
        InitStageContext *obj = (InitStageContext *)thread_contxt_ptr->priv;
        //SequenceControlSet* enc_common = &obj->enc_handle_ptr->enc_common;
        //pi_t* pi = &enc_common->pi;
        SVT_FREE_ARRAY(obj);
    }
}

/************************************************
 * Resource Coordination Context Constructor
 ************************************************/
SvtJxsErrorType_t init_stage_context_ctor(ThreadContext_t *thread_contxt_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv) {
    SvtJxsErrorType_t error = SvtJxsErrorNone;
    svt_jpeg_xs_encoder_common_t *enc_common = &enc_api_prv->enc_common;
    //pi_t* pi = &enc_common->pi;
    InitStageContext *context_ptr;
    SVT_CALLOC_ARRAY(context_ptr, 1);
    thread_contxt_ptr->priv = context_ptr;
    thread_contxt_ptr->dctor = init_stage_context_dctor;

    // InitStage works with ParentPCS
    context_ptr->picture_control_set_fifo_ptr = svt_system_resource_get_producer_fifo(enc_api_prv->picture_control_set_pool_ptr,
                                                                                      0);

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        context_ptr->dwt_stage_input_fifo_ptr = svt_system_resource_get_producer_fifo(enc_api_prv->dwt_input_resource_ptr, 0);
    }

    if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY || enc_common->cpu_profile == CPU_PROFILE_CPU) {
        context_ptr->pack_input_buffer_fifo_ptr = svt_system_resource_get_producer_fifo(enc_api_prv->pack_input_resource_ptr, 0);
    }

    context_ptr->enc_api_prv = enc_api_prv;

    return error;
}

#ifndef NDEBUG
static int32_t validate_yuv_range(pi_t *pi, svt_jpeg_xs_image_buffer_t *image_buffer, uint8_t input_bit_depth, uint64_t frame,
                                  ColourFormat_t format) {
    /*Check input YUV*/
    if (input_bit_depth > 8) {
        uint32_t test_input_range = ~(((uint32_t)1 << input_bit_depth) - 1);
        uint32_t comps_num = pi->comps_num;
        if (format > COLOUR_FORMAT_PACKED_MIN && format < COLOUR_FORMAT_PACKED_MAX) {
            comps_num = 1;
        }

        for (uint32_t component_id = 0; component_id < comps_num; ++component_id) {
            uint16_t *plane_buffer_in = (uint16_t *)image_buffer->data_yuv[component_id];
            uint16_t plane_stride = image_buffer->stride[component_id];
            for (uint32_t y = 0; y < pi->components[component_id].height; ++y) {
                uint32_t width = pi->components[component_id].width;
                if (format > COLOUR_FORMAT_PACKED_MIN && format < COLOUR_FORMAT_PACKED_MAX) {
                    width *= 3;
                }
                for (uint32_t x = 0; x < width; ++x) {
                    if (plane_buffer_in[x] & test_input_range) {
                        fprintf(stderr,
                                "Warning: Invalid YUV have values out of a range %u bits!! Frame: %lu, Component: %u, x: %u, y: "
                                "%u!!\n",
                                input_bit_depth,
                                (unsigned long)frame,
                                component_id,
                                x,
                                y);
                        return -1;
                    }
                }
                plane_buffer_in += plane_stride;
            }
        }
    }
    return 0;
}
#endif

/* Init Stage Kernel */
/*********************************************************************************
 *
 * @brief
 *  The Resource Coordination Process is the first stage that input pictures
 *  this process is a single threaded, picture-based process that handles one
 *picture at a time in display order
 *
 * @par Description:
 *  Input picture samples are available once the input_buffer_fifo_ptr
 *queue gets any items The Resource Coordination Process assembles the input
 *information and creates the appropriate buffers that would travel with the
 *input picture all along the encoding pipeline and passes this data along with
 *the current encoder settings to the picture analysis process Encoder settings
 *include, but are not limited to QPs, picture type, encoding parameters that
 *change per picture sequence
 *
 * @param[out] Input picture in Picture buffers
 *  Initialized picture level (PictureParentControlSet) / sequence level
 *  (SequenceControlSet if it's the initial picture) structures
 *
 * @param[out] Settings
 *  Encoder settings include picture timing and order settings (POC) resolution
 *settings, sequence level parameters (if it is the initial picture) and other
 *encoding parameters such as QP, Bitrate, picture type ...
 *
 ********************************************************************************/
void *init_stage_kernel(void *input_ptr) {
    ThreadContext_t *enc_contxt_ptr = (ThreadContext_t *)input_ptr;
    InitStageContext *context_ptr = (InitStageContext *)enc_contxt_ptr->priv;
    svt_jpeg_xs_encoder_api_prv_t *enc_api_prv = context_ptr->enc_api_prv;
    CondVar *sync_output_ringbuffer_left = &enc_api_prv->sync_output_ringbuffer_left;

    ObjectWrapper_t *pcs_wrapper_ptr = NULL;
    ObjectWrapper_t *input_wrapper_ptr, *dwt_input_wrapper_ptr = NULL;

    /*Callback available space in Input Queue*/
    svt_jpeg_xs_encoder_api_t *callback_encoder_ctx = enc_api_prv->callback_encoder_ctx;
    void (*callback_send)(svt_jpeg_xs_encoder_api_t *, void *) = enc_api_prv->callback_send_data_available;
    void *callback_send_context = enc_api_prv->callback_send_data_available_context;

    //Initial available input buffer
    if (callback_send) {
        callback_send(callback_encoder_ctx, callback_send_context);
    }

    for (;;) {
        // Get the Next svt Input Buffer [BLOCKING]
        SVT_GET_FULL_OBJECT(enc_api_prv->input_image_consumer_fifo_ptr, &input_wrapper_ptr);

        EncoderInputItem *input_item = (EncoderInputItem *)input_wrapper_ptr->object_ptr;
#ifdef FLAG_DEADLOCK_DETECT
        printf("01[%s:%i] frame: %03li\n", __func__, __LINE__, (size_t)input_item->frame_number);
#endif
        // Get a New PCS where we will hold the new input_picture
        SvtJxsErrorType_t ret = svt_get_empty_object(context_ptr->picture_control_set_fifo_ptr, &pcs_wrapper_ptr);
        if (ret != SvtJxsErrorNone || pcs_wrapper_ptr == NULL) {
            continue;
        }
        PictureControlSet *pcs_ptr = (PictureControlSet *)pcs_wrapper_ptr->object_ptr;
        pi_t *pi = &pcs_ptr->enc_common->pi;
        pcs_ptr->slice_cnt = 0;
        pcs_ptr->frame_error = 0;

        if (pcs_ptr->enc_common->slice_packetization_mode) {
            memset(pcs_ptr->slice_ready_to_release_arr, 0, pi->slice_num);
            pcs_ptr->slice_released_idx = 0;
        }

        // assign wavelet transformation buf
        pcs_ptr->enc_input = input_item->enc_input;
        pcs_ptr->frame_number = input_item->frame_number;

        //Locking bitstream buffer
        pcs_ptr->enc_input.bitstream.ready_to_release = 0;
        pcs_ptr->enc_input.image.ready_to_release = 0;

#ifndef NDEBUG
        /*Check input YUV*/
        uint8_t input_bit_depth = (uint8_t)enc_api_prv->enc_common.bit_depth;
        if (input_bit_depth > 8) {
            svt_jpeg_xs_image_buffer_t *image_buffer = &pcs_ptr->enc_input.image;
            validate_yuv_range(
                pi, image_buffer, input_bit_depth, input_item->frame_number, enc_api_prv->enc_common.colour_format);
        }
#endif

        svt_wait_cond_var(sync_output_ringbuffer_left, 0); //Wait until will be free place in ring buffer
        svt_add_cond_var(sync_output_ringbuffer_left, -1); //Decrement number of elements to use.

        SVT_DEBUG("%s, PCS out %lu\n", __func__, input_item->frame_number);
        if (pcs_ptr->enc_common->cpu_profile == CPU_PROFILE_CPU) {
            //CPU
            PackInput_t *list_slices = pre_rc_send_frame_to_pack_slices(
                pcs_ptr, context_ptr->pack_input_buffer_fifo_ptr, input_item->frame_number, pcs_wrapper_ptr);
#ifndef NDEBUG
            if (pi->decom_v != 0) {
                volatile PackInput_t *list_slice_next_tmp = list_slices;
                uint32_t count = 0;
                while (list_slice_next_tmp) {
                    list_slice_next_tmp = list_slice_next_tmp->sync_dwt_list_next;
                    count++;
                }
                assert(count == pi->slice_num);
            }
#endif

            for (uint32_t i = 0; i < pi->comps_num; i++) {
                if (pi->components[i].decom_v != 0) { //For CPU Profile and V = 0 ignore DWT thread.
                    ret = svt_get_empty_object(context_ptr->dwt_stage_input_fifo_ptr, &dwt_input_wrapper_ptr);
                    if (ret != SvtJxsErrorNone || dwt_input_wrapper_ptr == NULL) {
                        continue;
                    }

#ifdef FLAG_DEADLOCK_DETECT
                    printf("01[%s:%i] frame: %03li plane: %03d\n", __func__, __LINE__, (size_t)input_item->frame_number, i);
#endif
                    DwtInput *dwt_input_ptr = (DwtInput *)dwt_input_wrapper_ptr->object_ptr;
                    dwt_input_ptr->pcs_wrapper_ptr = pcs_wrapper_ptr;
                    dwt_input_ptr->component_id = i;
                    dwt_input_ptr->frame_num = input_item->frame_number;
                    dwt_input_ptr->list_slices = list_slices;
                    svt_post_full_object(dwt_input_wrapper_ptr);
                }
            }
        }
        else {
            assert(pcs_ptr->enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY);
            //LOW LATENCY
            pre_rc_send_frame_to_pack_slices(
                pcs_ptr, context_ptr->pack_input_buffer_fifo_ptr, input_item->frame_number, pcs_wrapper_ptr);
        }

        svt_release_object(input_wrapper_ptr);
    }

    return NULL;
}
