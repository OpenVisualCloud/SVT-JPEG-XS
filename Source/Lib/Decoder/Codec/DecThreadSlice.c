/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecThreads.h"
#include "DecThreadSlice.h"
#include "DecThreadFinal.h"

/*Create input buffer item.*/
SvtJxsErrorType_t universal_frame_task_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    TaskCalculateFrame* input_buffer;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(input_buffer, 1, sizeof(TaskCalculateFrame));
    *object_dbl_ptr = (void_ptr)input_buffer;

    return SvtJxsErrorNone;
}

/*Destroy input buffer item.*/
void universal_frame_task_creator_destroy(void_ptr p) {
    TaskCalculateFrame* obj = (TaskCalculateFrame*)p;
    SVT_FREE(obj);
}

/*Destroy thread context.*/
static void universal_thread_context_dctor(void_ptr p) {
    ThreadContext_t* thread_contxt_ptr = (ThreadContext_t*)p;
    if (thread_contxt_ptr->priv) {
        UniversalThreadContext* obj = (UniversalThreadContext*)thread_contxt_ptr->priv;
        svt_jpeg_xs_dec_thread_context_free(obj->dec_thread_context, &obj->dec_api_prv->dec_common.pi);
        SVT_FREE(obj);
    }
}

/*Create thread context.*/
SvtJxsErrorType_t universal_thread_context_ctor(ThreadContext_t* thread_context_ptr, svt_jpeg_xs_decoder_api_prv_t* dec_api_prv,
                                                int idx) {
    UniversalThreadContext* context_ptr;
    SVT_CALLOC(context_ptr, 1, sizeof(UniversalThreadContext));
    thread_context_ptr->priv = context_ptr;
    thread_context_ptr->dctor = universal_thread_context_dctor;
    context_ptr->dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api_prv;
    context_ptr->universal_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(
        context_ptr->dec_api_prv->universal_buffer_resource_ptr, idx);
    context_ptr->final_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
        context_ptr->dec_api_prv->final_buffer_resource_ptr, idx);
    context_ptr->process_idx = idx;
    context_ptr->dec_thread_context = svt_jpeg_xs_dec_thread_context_alloc(&dec_api_prv->dec_common.pi);
    if (context_ptr->dec_thread_context == NULL) {
        return SvtJxsErrorDecoderInternal;
    }
    return SvtJxsErrorNone;
}

void* thread_universal_stage_kernel(void* input_ptr) {
    ThreadContext_t* thread_ctx = (ThreadContext_t*)input_ptr;
    UniversalThreadContext* universal_ctx = (UniversalThreadContext*)thread_ctx->priv;
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv =
        universal_ctx->dec_api_prv; //In future will receive universal_stage_context_ptr_array
    svt_jpeg_xs_decoder_thread_context* dec_thread_context = universal_ctx->dec_thread_context;

    for (;;) {
        ObjectWrapper_t* input_wrapper_ptr;
        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s][ThreadId %i] Before SVT_GET_FULL_OBJECT\n", __FUNCTION__, universal_ctx->process_idx);
        }
        SVT_GET_FULL_OBJECT(/*dec_api_prv->universal_consumer_fifo_ptr*/ universal_ctx->universal_consumer_fifo_ptr,
                            &input_wrapper_ptr);
        TaskCalculateFrame* input_buffer_ptr = (TaskCalculateFrame*)input_wrapper_ptr->object_ptr;
        svt_jpeg_xs_decoder_instance_t* dec_ctx = input_buffer_ptr->wrapper_ptr_decoder_ctx->object_ptr;

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr,
                    "[%s][ThreadId %i] Get frame  %i from work thread\n",
                    __FUNCTION__,
                    universal_ctx->process_idx,
                    (int)dec_ctx->frame_num);
        }

        SvtJxsErrorType_t ret_decode = SvtJxsErrorNone;
        /*Check that other slice or header did not have error while decoding.*/
        if (input_buffer_ptr->frame_error == 0) {
            uint32_t out_slice_size;
            ret_decode = svt_jpeg_xs_decode_slice(dec_ctx,
                                                  dec_thread_context,
                                                  input_buffer_ptr->bitstream_buf,
                                                  input_buffer_ptr->bitstream_buf_size,
                                                  input_buffer_ptr->slice_id,
                                                  &out_slice_size,
                                                  &input_buffer_ptr->image_buffer,
                                                  dec_api_prv->verbose);
            if (ret_decode < 0) {
                input_buffer_ptr->frame_error = ret_decode;
            }
        }

        if (dec_api_prv->verbose >= VERBOSE_WARNINGS) {
            if (ret_decode >= 0 && ret_decode != (int)input_buffer_ptr->bitstream_buf_size) {
                fprintf(stderr,
                        "[%s:Process ID: %i] WARNING frame %i !!! Unexpected size of frame, expected: %i get: %i\n",
                        __FUNCTION__,
                        universal_ctx->process_idx,
                        (int)dec_ctx->frame_num,
                        (int)input_buffer_ptr->bitstream_buf_size,
                        ret_decode);
            }
        }

        if (ret_decode < 0) {
            if (dec_api_prv->verbose >= VERBOSE_ERRORS) {
                fprintf(stderr,
                        "[%s:Process ID: %i] %i, %p, %i HANDLE ERROR!!!\n",
                        __FUNCTION__,
                        universal_ctx->process_idx,
                        (int)dec_ctx->frame_num,
                        input_buffer_ptr->bitstream_buf,
                        (int)input_buffer_ptr->bitstream_buf_size);
            }
        }

        ObjectWrapper_t* universal_wrapper_ptr = NULL;
        SvtJxsErrorType_t ret = svt_jxs_get_empty_object(universal_ctx->final_producer_fifo_ptr, &universal_wrapper_ptr);
        if (ret != SvtJxsErrorNone || universal_wrapper_ptr == NULL) {
            continue;
        }

        TaskFinalSync* buffer_output = (TaskFinalSync*)universal_wrapper_ptr->object_ptr;
        buffer_output->wrapper_ptr_decoder_ctx = input_buffer_ptr->wrapper_ptr_decoder_ctx;
        buffer_output->slice_id = input_buffer_ptr->slice_id;
        buffer_output->frame_error = input_buffer_ptr->frame_error;

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr,
                    "[%s][ThreadId %i] Send frame  %i from work thread\n",
                    __FUNCTION__,
                    universal_ctx->process_idx,
                    (int)dec_ctx->frame_num);
        }

        svt_jxs_post_full_object(universal_wrapper_ptr);
        svt_jxs_release_object(input_wrapper_ptr);
    }

    return NULL;
}
