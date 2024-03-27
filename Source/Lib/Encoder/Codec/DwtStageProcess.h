/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DWT_STAGE_H__
#define __DWT_STAGE_H__

#include "Definitions.h"
#include "EncHandle.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************
 * Extern Function Declaration
 ***************************************/
SvtJxsErrorType_t dwt_stage_context_ctor(ThreadContext_t *thread_context_ptr, svt_jpeg_xs_encoder_api_prv_t *enc_api_prv,
                                         int idx);

extern void *dwt_stage_kernel(void *input_ptr);

#ifdef __cplusplus
}
#endif

#endif /*__DWT_STAGE_H__*/
