/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_THREAD_INIT_H_
#define _DECODER_THREAD_INIT_H_

#include "Definitions.h"
#include "DecThreads.h"
#include "DecHandle.h"

typedef struct {
    svt_jpeg_xs_frame_t dec_input;
    SvtJxsErrorType_t flags;
} TaskInputBitstream;

void* thread_init_stage_kernel(void* input_ptr);

/*Allocate queues and contexts for multithreading*/
SvtJxsErrorType_t input_bitstream_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);
void input_bitstream_destroyer(void_ptr p);

SvtJxsErrorType_t internal_svt_jpeg_xs_decoder_send_packet(svt_jpeg_xs_decoder_api_prv_t* dec_api_prv,
                                                           svt_jpeg_xs_frame_t* dec_input, uint32_t* bytes_used);

#endif /*_DECODER_THREAD_INIT_H_*/
