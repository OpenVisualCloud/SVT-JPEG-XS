/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>

#include "EncHandle.h"
#include "FinalStageProcess.h"
#include "PackOut.h"
#include "PictureControlSet.h"
#include "Threads/SystemResourceManager.h"
#include "SvtLog.h"
#include "Threads/SvtObject.h"
#include "SvtUtility.h"
#include "common_dsp_rtcd.h"

typedef struct FinalStageContext {
    Fifo_t *input_buffer_fifo_ptr;
    svt_jpeg_xs_encoder_api_prv_t *enc_api_prv;
} FinalStageContext;

SvtJxsErrorType_t output_item_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    EncoderOutputItem *output_item;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(output_item, 1, sizeof(EncoderOutputItem));
    *object_dbl_ptr = (void_ptr)output_item;

    return SvtJxsErrorNone;
}

void output_item_destroyer(void_ptr p) {
    EncoderOutputItem *obj = (EncoderOutputItem *)p;
    SVT_FREE(obj);
}

static void final_stage_context_dctor(void_ptr p) {
    ThreadContext_t *thread_contxt_ptr = (ThreadContext_t *)p;
    if (thread_contxt_ptr->priv) {
        FinalStageContext *obj = (FinalStageContext *)thread_contxt_ptr->priv;
        SVT_FREE_ARRAY(obj);
    }
}

/************************************************
 * Resource Coordination Context Constructor
 ************************************************/
SvtJxsErrorType_t final_stage_context_ctor(ThreadContext_t *thread_contxt_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv) {
    FinalStageContext *context_ptr;
    SVT_CALLOC_ARRAY(context_ptr, 1);
    thread_contxt_ptr->priv = context_ptr;
    thread_contxt_ptr->dctor = final_stage_context_dctor;

    context_ptr->input_buffer_fifo_ptr = svt_system_resource_get_consumer_fifo(enc_api_prv->pack_output_resource_ptr, 0);
    context_ptr->enc_api_prv = enc_api_prv;
    return SvtJxsErrorNone;
}

/* Final Stage Kernel */
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
void *final_stage_kernel(void *input_ptr) {
    ThreadContext_t *enc_contxt_ptr = (ThreadContext_t *)input_ptr;
    FinalStageContext *context_ptr = (FinalStageContext *)enc_contxt_ptr->priv;
    svt_jpeg_xs_encoder_api_prv_t *enc_api_prv = context_ptr->enc_api_prv;
    ObjectWrapper_t **sync_output_ringbuffer = enc_api_prv->sync_output_ringbuffer;
    uint32_t sync_output_ringbuffer_size = enc_api_prv->sync_output_ringbuffer_size;
    CondVar *sync_output_ringbuffer_left = &enc_api_prv->sync_output_ringbuffer_left;

    uint64_t ring_buffer_index = 0;
    PictureControlSet *pcs_ptr;
    PackOutput *pack_result;
    ObjectWrapper_t *input_wrapper_ptr;

    svt_jpeg_xs_encoder_api_t *callback_encoder_ctx = enc_api_prv->callback_encoder_ctx;
    void (*callback_get)(svt_jpeg_xs_encoder_api_t *, void *) = enc_api_prv->callback_get_data_available;
    void *callback_get_context = enc_api_prv->callback_get_data_available_context;

    for (;;) {
        // Get the Next svt Input Buffer [BLOCKING]
        SVT_GET_FULL_OBJECT(context_ptr->input_buffer_fifo_ptr, &input_wrapper_ptr);

        pack_result = (PackOutput *)input_wrapper_ptr->object_ptr;

        if (pack_result->pcs_wrapper_ptr == NULL) {
            fprintf(stderr, "FATAL ERROR [%s:%i] Final thread pcs_wrapper_ptr is NULL\n", __func__, __LINE__);
            svt_release_object(input_wrapper_ptr);
            continue;
        }

        pcs_ptr = (PictureControlSet *)pack_result->pcs_wrapper_ptr->object_ptr;
        pcs_ptr->slice_cnt++;
        pcs_ptr->frame_error |= pack_result->slice_error;

#ifdef FLAG_DEADLOCK_DETECT
        printf("Receive Frame=%llu slice_idx=%d\n", pcs_ptr->frame_number, pack_result->slice_idx);
#endif

        if (pcs_ptr->enc_common->slice_packetization_mode) {
            pcs_ptr->slice_ready_to_release_arr[pack_result->slice_idx] = 1;
        }

        if (sync_output_ringbuffer[pcs_ptr->frame_number % sync_output_ringbuffer_size] == NULL) {
            sync_output_ringbuffer[pcs_ptr->frame_number % sync_output_ringbuffer_size] = pack_result->pcs_wrapper_ptr;
        }
        else {
            ObjectWrapper_t *pcs_ringbuffer_obj = sync_output_ringbuffer[pcs_ptr->frame_number % sync_output_ringbuffer_size];
            PictureControlSet *pcs_ringbuffer_ptr = (PictureControlSet *)pcs_ringbuffer_obj->object_ptr;
            if ((pcs_ringbuffer_obj != pack_result->pcs_wrapper_ptr) ||
                (pcs_ringbuffer_ptr->frame_number != pcs_ptr->frame_number)) {
                fprintf(stderr, "FATAL ERROR [%s:%i] Final thread ring is full\n", __func__, __LINE__);
                // TODO: Return internal error
                continue;
            }
        }

        while (sync_output_ringbuffer[ring_buffer_index % sync_output_ringbuffer_size] != NULL) {
            ObjectWrapper_t *pcs_ring_wrapper_ptr = sync_output_ringbuffer[ring_buffer_index % sync_output_ringbuffer_size];
            PictureControlSet *pcs_ring = pcs_ring_wrapper_ptr->object_ptr;

            if (pcs_ring->enc_common->slice_packetization_mode) {
                while ((pcs_ring->slice_released_idx < pcs_ring->enc_common->pi.slice_num) &&
                       pcs_ring->slice_ready_to_release_arr[pcs_ring->slice_released_idx]) {
                    //Release picture header
                    if (pcs_ring->slice_released_idx == 0) {
                        ObjectWrapper_t *output_item_wrapper_ptr;
                        svt_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr, &output_item_wrapper_ptr);
                        EncoderOutputItem *output_item = (EncoderOutputItem *)output_item_wrapper_ptr->object_ptr;
                        output_item->enc_input = pcs_ring->enc_input; //Copy structure
                        output_item->enc_input.bitstream.used_size = pcs_ring->enc_common->frame_header_length_bytes;
                        pcs_ring->bitstream_release_offset = output_item->enc_input.bitstream.used_size;
                        output_item->frame_number = pcs_ring->frame_number;
                        output_item->frame_error = pcs_ring->frame_error;
                        output_item->enc_input.bitstream.last_packet_in_frame = 0;
                        output_item->enc_input.bitstream.ready_to_release = 0;
                        output_item->enc_input.image.ready_to_release = 0;
#ifdef FLAG_DEADLOCK_DETECT
                        printf("[%s:%i] Return Frame=%llu HEADER\n", __func__, __LINE__, pcs_ring->frame_number);
#endif
                        svt_post_full_object(output_item_wrapper_ptr);

                        if (callback_get) {
                            callback_get(callback_encoder_ctx, callback_get_context);
                        }
                    }

                    ObjectWrapper_t *output_item_wrapper_ptr;
                    svt_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr, &output_item_wrapper_ptr);
                    EncoderOutputItem *output_item = (EncoderOutputItem *)output_item_wrapper_ptr->object_ptr;
                    output_item->enc_input = pcs_ring->enc_input; //Copy structure
                    output_item->enc_input.bitstream.buffer += pcs_ring->bitstream_release_offset;
                    output_item->enc_input.bitstream.used_size = pcs_ring->enc_common->slice_sizes[pcs_ring->slice_released_idx];
                    pcs_ring->bitstream_release_offset += output_item->enc_input.bitstream.used_size;
                    output_item->enc_input.bitstream.last_packet_in_frame = 0;
                    output_item->enc_input.bitstream.ready_to_release = 0;
                    output_item->enc_input.image.ready_to_release = 0;
                    output_item->frame_number = pcs_ring->frame_number;
                    output_item->frame_error = pcs_ring->frame_error;
                    if (pcs_ring->slice_released_idx == (pcs_ring->enc_common->pi.slice_num - 1)) {
                        output_item->enc_input.bitstream.last_packet_in_frame = 1;
                        output_item->enc_input.bitstream.ready_to_release = 1;
                        output_item->enc_input.image.ready_to_release = 1;
                    }
#ifdef FLAG_DEADLOCK_DETECT
                    printf("[%s:%i] Return Frame=%llu slice_idx=%d\n",
                           __func__,
                           __LINE__,
                           pcs_ring->frame_number,
                           pcs_ring->slice_released_idx);
#endif
                    svt_post_full_object(output_item_wrapper_ptr);

                    if (callback_get) {
                        callback_get(callback_encoder_ctx, callback_get_context);
                    }
                    pcs_ring->slice_released_idx++;
                }
            }

            if (pcs_ring->slice_cnt == pcs_ring->enc_common->pi.slice_num) {
                if (!pcs_ring->enc_common->slice_packetization_mode) {
#ifdef FLAG_DEADLOCK_DETECT
                    printf("08[%s:%i] Return full frame: %llu\n", __func__, __LINE__, pcs_ring->frame_number);
#endif
                    ObjectWrapper_t *output_item_wrapper_ptr;
                    svt_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr, &output_item_wrapper_ptr);
                    EncoderOutputItem *output_item = (EncoderOutputItem *)output_item_wrapper_ptr->object_ptr;

                    output_item->enc_input = pcs_ring->enc_input; //Copy structure
                    output_item->frame_number = pcs_ring->frame_number;
                    output_item->frame_error = pcs_ring->frame_error;
                    output_item->enc_input.bitstream.last_packet_in_frame = 1;
                    output_item->enc_input.bitstream.ready_to_release = 1;
                    output_item->enc_input.image.ready_to_release = 1;

                    svt_post_full_object(output_item_wrapper_ptr);
                    if (callback_get) {
                        callback_get(callback_encoder_ctx, callback_get_context);
                    }
                }

                //Release the pcs wrapper
                svt_release_object(pcs_ring_wrapper_ptr);
                /* Release the input picture
                * From this moment input yuv is no longer used and can be release by callback to application.
                * RELEASE: (pcs_ring->image_buffer);
                * if (callback_send) {
                *    callback_send(callback_encoder_ctx, callback_send_context);
                * }
                */

                sync_output_ringbuffer[ring_buffer_index % sync_output_ringbuffer_size] = NULL;
                ring_buffer_index = (ring_buffer_index + 1) % sync_output_ringbuffer_size;
                svt_add_cond_var(sync_output_ringbuffer_left, 1); //Increment number of elements to use.
            }
            else {
                break;
            }
        }

        svt_release_object(input_wrapper_ptr);
    }
    return NULL;
}
