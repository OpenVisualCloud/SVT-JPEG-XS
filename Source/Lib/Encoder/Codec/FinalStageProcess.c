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
#include "BitstreamWriter.h"

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

    context_ptr->input_buffer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(enc_api_prv->pack_output_resource_ptr, 0);
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
            SVT_FATAL("FATAL ERROR [%s:%i] Final thread pcs_wrapper_ptr is NULL\n", __func__, __LINE__);
            svt_jxs_release_object(input_wrapper_ptr);
            continue;
        }

        pcs_ptr = (PictureControlSet *)pack_result->pcs_wrapper_ptr->object_ptr;
        pcs_ptr->slice_cnt++;
        pcs_ptr->frame_error |= pack_result->slice_error;
        if (pcs_ptr->enc_common->lossless_enable) {
            pcs_ptr->slice_real_bytes_arr[pack_result->slice_idx] = pack_result->slice_real_bytes;
        }

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
                SVT_FATAL("FATAL ERROR [%s:%i] Final thread ring is full\n", __func__, __LINE__);
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
                        ObjectWrapper_t *output_item_wrapper_ptr = NULL;
                        SvtJxsErrorType_t ret = svt_jxs_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr,
                                                                         &output_item_wrapper_ptr);
                        if (ret != SvtJxsErrorNone || output_item_wrapper_ptr == NULL) {
                            break;
                        }
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
                        svt_jxs_post_full_object(output_item_wrapper_ptr);

                        if (callback_get) {
                            callback_get(callback_encoder_ctx, callback_get_context);
                        }
                    }

                    ObjectWrapper_t *output_item_wrapper_ptr = NULL;
                    SvtJxsErrorType_t ret = svt_jxs_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr,
                                                                     &output_item_wrapper_ptr);
                    if (ret != SvtJxsErrorNone || output_item_wrapper_ptr == NULL) {
                        break;
                    }
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
                    svt_jxs_post_full_object(output_item_wrapper_ptr);

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
                    ObjectWrapper_t *output_item_wrapper_ptr = NULL;
                    SvtJxsErrorType_t ret = svt_jxs_get_empty_object(enc_api_prv->output_queue_producer_fifo_ptr,
                                                                     &output_item_wrapper_ptr);
                    if (ret != SvtJxsErrorNone || output_item_wrapper_ptr == NULL) {
                        break;
                    }
                    if (pcs_ring->enc_common->lossless_enable && !pcs_ring->frame_error) {
                        /* Every slice was packed into a generous, non-overlapping window (never
                         * more than its window, per the assert in PackStageProcess.c); real bytes
                         * written are almost always less. Walk slices in order and left-shift each
                         * one's already-fully-written, immutable bytes to close the gap left by the
                         * previous slices' unused window tail. Single serialized pass over completed
                         * data (this only runs once slice_cnt == slice_num, i.e. every slice for this
                         * frame has already finished packing), so there is no concurrency hazard.
                         * Skipped when frame_error is set: slice_real_bytes_arr[] is only trustworthy
                         * for slices that finished packing inside their allotted window, and a release
                         * build cannot rely on the packer's own assert to guarantee that (see
                         * PackStageProcess.c). Reading a corrupt/oversized slice_real here would walk
                         * memmove() past the buffer instead of just leaving the frame marked as failed. */
                        svt_jpeg_xs_encoder_common_t *enc_common = pcs_ring->enc_common;
                        uint8_t *buffer = pcs_ring->enc_input.bitstream.buffer;
                        uint32_t real_offset = enc_common->frame_header_length_bytes;
                        uint32_t window_offset = enc_common->frame_header_length_bytes;
                        for (uint32_t idx = 0; idx < enc_common->pi.slice_num; ++idx) {
                            uint32_t slice_real = pcs_ring->slice_real_bytes_arr[idx];
                            if (window_offset != real_offset) {
                                memmove(buffer + real_offset, buffer + window_offset, slice_real);
                            }
                            real_offset += slice_real;
                            window_offset += enc_common->slice_sizes[idx];
                        }
                        /*Patch the true final size into the compacted hdr_Lcod field bytes.*/
                        /* size must be set: the writer drops out-of-bounds writes, so a zero size
                         * would silently discard this patch. Not bitstream_writer_init(), which
                         * 0xFF-fills the whole buffer in debug builds. */
                        bitstream_writer_t lcod_patch;
                        memset(&lcod_patch, 0, sizeof(lcod_patch));
                        lcod_patch.mem = buffer;
                        lcod_patch.size = real_offset;
                        lcod_patch.offset = enc_common->hdr_Lcod_byte_offset;
                        write_32_bits(&lcod_patch, real_offset);
                        pcs_ring->enc_input.bitstream.used_size = real_offset;
                    }

                    EncoderOutputItem *output_item = (EncoderOutputItem *)output_item_wrapper_ptr->object_ptr;

                    output_item->enc_input = pcs_ring->enc_input; //Copy structure
                    output_item->frame_number = pcs_ring->frame_number;
                    output_item->frame_error = pcs_ring->frame_error;
                    output_item->enc_input.bitstream.last_packet_in_frame = 1;
                    output_item->enc_input.bitstream.ready_to_release = 1;
                    output_item->enc_input.image.ready_to_release = 1;

                    svt_jxs_post_full_object(output_item_wrapper_ptr);
                    if (callback_get) {
                        callback_get(callback_encoder_ctx, callback_get_context);
                    }
                }

                //Release the pcs wrapper
                svt_jxs_release_object(pcs_ring_wrapper_ptr);
                /* Release the input picture
                * From this moment input yuv is no longer used and can be release by callback to application.
                * RELEASE: (pcs_ring->image_buffer);
                * if (callback_send) {
                *    callback_send(callback_encoder_ctx, callback_send_context);
                * }
                */

                sync_output_ringbuffer[ring_buffer_index % sync_output_ringbuffer_size] = NULL;
                ring_buffer_index = (ring_buffer_index + 1) % sync_output_ringbuffer_size;
                svt_jxs_add_cond_var(sync_output_ringbuffer_left, 1); //Increment number of elements to use.
            }
            else {
                break;
            }
        }

        svt_jxs_release_object(input_wrapper_ptr);
    }
    return NULL;
}
