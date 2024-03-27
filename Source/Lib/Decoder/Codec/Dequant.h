/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DEQUANT_H
#define _DEQUANT_H

#include <stdlib.h>
#include "SvtType.h"
#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

void dequant_c(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type);

#ifdef __cplusplus
}
#endif

#endif /*_DEQUANT_H*/
