/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_THREAD_SLICE_H_
#define _DECODER_THREAD_SLICE_H_

#include "Threads/SystemResourceManager.h"
#include "DecHandle.h"
#include "DecThreads.h"

typedef struct {
    const uint8_t* bitstream_buf;
    size_t bitstream_buf_size;

    ObjectWrapper_t* wrapper_ptr_decoder_ctx;
    svt_jpeg_xs_image_buffer_t image_buffer;
    uint32_t slice_id;
    SvtJxsErrorType_t frame_error;
} TaskCalculateFrame;

typedef struct UniversalThreadContext {
    int process_idx;
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv;
    Fifo_t* universal_consumer_fifo_ptr;
    Fifo_t* final_producer_fifo_ptr;

    svt_jpeg_xs_decoder_thread_context* dec_thread_context;
} UniversalThreadContext;

void* thread_universal_stage_kernel(void* input_ptr);

/*Allocate queues and contexts for multithreading*/
SvtJxsErrorType_t universal_frame_task_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);
void universal_frame_task_creator_destroy(void_ptr p);
SvtJxsErrorType_t universal_thread_context_ctor(ThreadContext_t* thread_context_ptr, svt_jpeg_xs_decoder_api_prv_t* dec_api_prv,
                                                int idx);

#endif /*_DECODER_THREAD_SLICE_H_*/
