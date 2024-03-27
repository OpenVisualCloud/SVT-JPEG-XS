/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _FINAL_STAGE_H_
#define _FINAL_STAGE_H_

#include "Definitions.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    svt_jpeg_xs_frame_t enc_input;
    uint64_t frame_number;
    int32_t frame_error;
} EncoderOutputItem;

/***************************************
 * Extern Function Declaration
 ***************************************/
SvtJxsErrorType_t output_item_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);
void output_item_destroyer(void_ptr p);

SvtJxsErrorType_t final_stage_context_ctor(ThreadContext_t *thread_context_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv);

extern void *final_stage_kernel(void *input_ptr);
#ifdef __cplusplus
}
#endif
#endif /*_FINAL_STAGE_H_*/
