/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef QuantInvQuant_h
#define QuantInvQuant_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "SvtType.h"
#include "Definitions.h"

void quantization_c(uint16_t* coeff_16bit, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli,
                    QUANT_TYPE quant_type);

#ifdef __cplusplus
}
#endif
#endif // QuantInvQuant_h
