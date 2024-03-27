/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef Quant_avx2_h
#define Quant_avx2_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "SvtType.h"
#include "Definitions.h"
#include "Codestream.h"

void quantization_avx2(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type);

#ifdef __cplusplus
}
#endif
#endif // Quant_avx2_h
