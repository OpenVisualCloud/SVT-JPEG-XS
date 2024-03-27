/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_THREAD_FINAL_H_
#define _DECODER_THREAD_FINAL_H_

#include "Threads/SystemResourceManager.h"
#include "DecThreads.h"
#include "SvtJpegxsDec.h"

typedef struct {
    ObjectWrapper_t* wrapper_ptr_decoder_ctx;
    uint32_t slice_id;
    int32_t frame_error;
} TaskFinalSync;

void* thread_final_stage_kernel(void* input_ptr);

/*Allocate queues and contexts for multithreading*/
SvtJxsErrorType_t final_sync_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);
void final_sync_destroyer(void_ptr p);

#endif /*_DECODER_THREAD_FINAL_H_*/
