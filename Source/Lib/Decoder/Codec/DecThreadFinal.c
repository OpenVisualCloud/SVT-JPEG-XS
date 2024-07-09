/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecThreads.h"
#include "DecThreadFinal.h"
#include "Threads/SvtThreads.h"
#include "DecHandle.h"

SvtJxsErrorType_t final_sync_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    TaskFinalSync* input_buffer;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(input_buffer, 1, sizeof(TaskFinalSync));
    *object_dbl_ptr = (void_ptr)input_buffer;

    return SvtJxsErrorNone;
}

void final_sync_destroyer(void_ptr p) {
    TaskFinalSync* obj = (TaskFinalSync*)p;
    SVT_FREE(obj);
}

void* thread_final_stage_kernel(void* input_ptr) {
    ThreadContext_t* thread_ctx = (ThreadContext_t*)input_ptr;
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)thread_ctx;
    OutItem* sync_output_ringbuffer = dec_api_prv->sync_output_ringbuffer;
    uint32_t sync_output_ringbuffer_size = dec_api_prv->sync_output_ringbuffer_size;
    CondVar* sync_output_ringbuffer_left = &dec_api_prv->sync_output_ringbuffer_left;

    /*Callback frame is ready to get.*/
    svt_jpeg_xs_decoder_api_t* callback_decoder_ctx = dec_api_prv->callback_decoder_ctx;
    void* callback_get_context = dec_api_prv->callback_get_data_available_context;
    void (*callback_get)(svt_jpeg_xs_decoder_api_t*, void*) = dec_api_prv->callback_get_data_available;

    uint64_t buffer_begin_id = 0;
    memset(sync_output_ringbuffer, 0, sync_output_ringbuffer_size * sizeof(OutItem));

    for (;;) {
        ObjectWrapper_t* input_wrapper_ptr;
        // Get the Next svt Input Buffer [BLOCKING]
        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s] Before SVT_GET_FULL_OBJECT\n", __FUNCTION__);
        }
        SVT_GET_FULL_OBJECT(dec_api_prv->final_consumer_fifo_ptr, &input_wrapper_ptr);
        TaskFinalSync* input_buffer_ptr = (TaskFinalSync*)input_wrapper_ptr->object_ptr;
        ObjectWrapper_t* wrapper_ptr_decoder_ctx = input_buffer_ptr->wrapper_ptr_decoder_ctx;
        svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;
        picture_header_const_t* picture_header_const = &dec_api_prv->dec_common.picture_header_const;

        OutItem* item = &sync_output_ringbuffer[dec_ctx->sync_output_frame_idx];

        //If Slice thread exited with error, release thread that is waiting for it to be done
        if (!dec_ctx->sync_slices_idwt) {
            svt_jxs_set_cond_var(&dec_ctx->map_slices_decode_done[input_buffer_ptr->slice_id], SYNC_OK);
        }
        else {
            if (input_buffer_ptr->frame_error) {
                svt_jxs_set_cond_var(&dec_ctx->map_slices_decode_done[input_buffer_ptr->slice_id], SYNC_ERROR);
            }
        }

        if (item->in_use == 0) {
            item->in_use = 1;
            item->ready_to_send = 0;
            item->received_slices = 1;
            item->frame_num = dec_ctx->frame_num;
            item->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;
            item->sync_output_frame_idx = dec_ctx->sync_output_frame_idx;
            item->dec_input = dec_ctx->dec_input;
            item->frame_error_slice = 0;
            item->frame_error = input_buffer_ptr->frame_error;
            item->slice_next_to_recalc = 0;
        }
        else {
            assert(item->ready_to_send == 0);
            assert(item->frame_num == dec_ctx->frame_num);
            assert(item->wrapper_ptr_decoder_ctx == wrapper_ptr_decoder_ctx);
            assert(item->sync_output_frame_idx == dec_ctx->sync_output_frame_idx);
            assert(item->dec_input.user_prv_ctx_ptr == dec_ctx->dec_input.user_prv_ctx_ptr);
            item->received_slices++;
            if (input_buffer_ptr->frame_error &&
                (item->frame_error == 0 || item->frame_error_slice > input_buffer_ptr->slice_id)) {
                //Keep only first error
                item->frame_error = input_buffer_ptr->frame_error;
                item->frame_error_slice = input_buffer_ptr->slice_id;
            }
        }

        if (!dec_ctx->sync_slices_idwt) {
            /*IDWT between slices. Only when universal thread not calculate fully IDWT*/
            if (item->frame_error == 0) {
                while ((item->slice_next_to_recalc < dec_ctx->sync_num_slices_to_receive) &&
                       dec_ctx->map_slices_decode_done[item->slice_next_to_recalc].val) {
                    SvtJxsErrorType_t error = svt_jpeg_xs_decode_final_slice_overlap(
                        dec_ctx, &item->dec_input.image, item->slice_next_to_recalc);
                    if (error) {
                        if (item->slice_next_to_recalc > input_buffer_ptr->slice_id) {
                            item->frame_error_slice = item->slice_next_to_recalc;
                            item->frame_error = error;
                        }
                        break;
                    }
                    item->slice_next_to_recalc++;
                }
            }
        }

        if (item->received_slices >= dec_ctx->sync_num_slices_to_receive) {
            /*Finish frame.*/
            item->ready_to_send = 1;

            if ((item->frame_error == 0) && picture_header_const->hdr_Cpih) {
                item->frame_error = svt_jpeg_xs_decode_final(dec_ctx, &item->dec_input.image);
            }
            /*if (item->frame_error < 0) {
                Release output buffer when error
                item->image_buffer = NULL;
            }*/
            //Release Decoder Context
            svt_jxs_release_object(wrapper_ptr_decoder_ctx);

            if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
                fprintf(stderr, "[%s] Get frame  %i Final thread\n", __FUNCTION__, (int)item->frame_num);
            }
        }

        //Release actual input task
        svt_jxs_release_object(input_wrapper_ptr);

        while (sync_output_ringbuffer[buffer_begin_id].ready_to_send) {
            item = &sync_output_ringbuffer[buffer_begin_id];
            item->ready_to_send = 0;
            item->in_use = 0;
            svt_jxs_add_cond_var(sync_output_ringbuffer_left, 1); //Increment number of elements to use.
            ObjectWrapper_t* final_wrapper_ptr = NULL;

            SvtJxsErrorType_t ret = svt_jxs_get_empty_object(dec_api_prv->output_producer_fifo_ptr, &final_wrapper_ptr);
            if (ret != SvtJxsErrorNone || final_wrapper_ptr == NULL) {
                break;
            }

            TaskOutFrame* buffer_output = (TaskOutFrame*)final_wrapper_ptr->object_ptr;
            buffer_output->frame_num = item->frame_num;
            buffer_output->dec_input = item->dec_input;
            buffer_output->dec_input.bitstream.ready_to_release = 1;
            buffer_output->dec_input.image.ready_to_release = 1;
            buffer_output->frame_error = item->frame_error;

            if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
                fprintf(stderr, "[%s] Send frame  %i Final thread\n", __FUNCTION__, (int)item->frame_num);
            }

            svt_jxs_post_full_object(final_wrapper_ptr);
            /*Callback frame is ready to get.*/
            if (callback_get) {
                callback_get(callback_decoder_ctx, callback_get_context);
            }

            buffer_begin_id = (buffer_begin_id + 1) % (sync_output_ringbuffer_size);
        }
    }
    return NULL;
}
