/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PACK_STAGE_H_
#define _PACK_STAGE_H_

#include "Definitions.h"
#ifdef __cplusplus
extern "C" {
#endif
/***************************************
 * Extern Function Declaration
 ***************************************/
SvtJxsErrorType_t pack_stage_context_ctor(ThreadContext_t *thread_context_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv,
                                          int idx);

extern void *pack_stage_kernel(void *input_ptr);
#ifdef __cplusplus
}
#endif
#endif /*_PACK_STAGE_H_*/
