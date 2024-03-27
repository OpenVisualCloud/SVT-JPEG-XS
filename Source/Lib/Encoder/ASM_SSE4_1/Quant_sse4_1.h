/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef Quant_sse4_1_h
#define Quant_sse4_1_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "SvtType.h"
#include "Definitions.h"
#include "Codestream.h"

void quantization_sse4_1(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type);
void quant_uniform_sse4_1(uint16_t* buf, uint32_t size, uint8_t* gclis, uint8_t gtli);

#ifdef __cplusplus
}
#endif
#endif // Quant_sse4_1_h
