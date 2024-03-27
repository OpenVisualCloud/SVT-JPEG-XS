/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __DWT_DECODER_H__
#define __DWT_DECODER_H__
#include <stdint.h>
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct transform_lines {
    //buffer_out indexing [line_start .. line_stop]
    uint32_t line_start;
    uint32_t line_stop;
    int32_t offset;
    int32_t* buffer_out[8];
} transform_lines_t;

void new_transform_component_line(const pi_component_t* const component, int16_t* buffer_in[MAX_BANDS_PER_COMPONENT_NUM],
                                  int16_t* buffer_in_prev[MAX_BANDS_PER_COMPONENT_NUM], transform_lines_t* lines,
                                  uint32_t line_idx, int32_t* buffer_tmp, uint32_t precinct_num, uint8_t shift);

void new_transform_component_line_recalc(const pi_component_t* const component,
                                         int16_t* buffer_in_prev_2[MAX_BANDS_PER_COMPONENT_NUM],
                                         int16_t* buffer_in_prev_1[MAX_BANDS_PER_COMPONENT_NUM], int32_t** buffer_out,
                                         uint32_t precinct_line_idx, int32_t* buffer_tmp, uint8_t shift);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /*__DWT_DECODER_H__*/
