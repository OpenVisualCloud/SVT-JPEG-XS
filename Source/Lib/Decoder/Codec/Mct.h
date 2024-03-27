/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _MCT_H
#define _MCT_H

#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif
int32_t get_cfa_pattern(const picture_header_dynamic_t* picture_header_dynamic);
void mct_inverse_transform(int32_t* out_comps[MAX_COMPONENTS_NUM], const pi_t* pi,
                           const picture_header_dynamic_t* picture_header_dynamic, uint8_t hdr_Cpih);

void mct_inverse_transform_precinct(int32_t* out_comps[MAX_COMPONENTS_NUM],
                                    const picture_header_dynamic_t* picture_header_dynamic, int32_t w, int32_t h,
                                    uint8_t hdr_Cpih);

#ifdef __cplusplus
}
#endif

#endif /*_MCT_H*/
