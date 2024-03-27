/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _INIT_STAGE_H_
#define _INIT_STAGE_H_

#include "Definitions.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t frame_number;
    svt_jpeg_xs_frame_t enc_input;
} EncoderInputItem;

/***************************************
 * Extern Function Declaration
 ***************************************/
SvtJxsErrorType_t input_item_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);
void input_item_destroyer(void_ptr p);

SvtJxsErrorType_t init_stage_context_ctor(ThreadContext_t *thread_contxt_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv);

extern void *init_stage_kernel(void *input_ptr);
#ifdef __cplusplus
}
#endif
#endif /*_INIT_STAGE_H_*/
