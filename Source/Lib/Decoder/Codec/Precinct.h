/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PRECINCT_H
#define _PRECINCT_H

#include "decoder_dsp_rtcd.h"
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif

void inv_sign_c(uint16_t* in_out, uint32_t width);
void inv_precinct_calculate_data(precinct_t* precinct, const pi_t* const pi, int dq_type);

#ifdef __cplusplus
}
#endif

#endif /* _PRECINCT_H */
