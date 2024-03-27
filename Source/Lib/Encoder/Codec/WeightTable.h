/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _WEIGHT_TABLE_H_
#define _WEIGHT_TABLE_H_
#include <Pi.h>

#ifdef __cplusplus
extern "C" {
#endif

int weight_table_calculate(pi_t *pi, uint8_t verbose, ColourFormat_t color_format);

#ifdef __cplusplus
}
#endif
#endif /*_WEIGHT_TABLE_H_*/
