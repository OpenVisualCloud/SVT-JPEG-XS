/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_TYPE_H_
#define _SVT_TYPE_H_
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

// Quantization type
//
typedef enum _QUANT_TYPE_ {
    QUANT_TYPE_DEADZONE = 0,
    QUANT_TYPE_UNIFORM,
    QUANT_TYPE_MAX,
} QUANT_TYPE;

typedef enum CodingModeFlag {
    CODING_MODE_FLAG_VERTICAL_PRED = 1,
    CODING_MODE_FLAG_SIGNIFICANCE = 2,
    CODING_MODE_FLAG_INVALID = 0xFF
} CodingModeFlag;

#endif /*_SVT_TYPE_H_*/
