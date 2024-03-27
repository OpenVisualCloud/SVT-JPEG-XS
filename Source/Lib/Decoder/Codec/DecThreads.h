/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_THREADS_H_
#define _DECODER_THREADS_H_

#include "Threads/SystemResourceManager.h"
#include "SvtJpegxsDec.h"

typedef struct {
    svt_jpeg_xs_frame_t dec_input;
    uint64_t frame_num;
    SvtJxsErrorType_t frame_error;
    void* user_prv_ctx;
} TaskOutFrame;

typedef struct ThreadContext {
    DctorCall dctor;
    void_ptr priv;
} ThreadContext_t;

/*Allocate queues and contexts for multithreading*/
SvtJxsErrorType_t output_frame_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);
void output_frame_destroyer(void_ptr p);
SvtJxsErrorType_t pool_decoder_instance_create_ctor(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);
void pool_decoder_instance_destroy_ctor(void_ptr p);

#endif /*_DECODER_THREADS_H_*/
