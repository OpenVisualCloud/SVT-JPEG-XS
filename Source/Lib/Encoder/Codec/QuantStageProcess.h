/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _Q_STAGE_H_
#define _Q_STAGE_H_
#include "PrecinctEnc.h"
#include "PictureControlSet.h"

#ifdef __cplusplus
extern "C" {
#endif
/***************************************
 * Extern Function Declaration
 ***************************************/

void precinct_quantization(PictureControlSet *pcs_ptr, pi_t *pi, precinct_enc_t *precinct);

#ifdef __cplusplus
}
#endif
#endif /*_Q_STAGE_H_*/
