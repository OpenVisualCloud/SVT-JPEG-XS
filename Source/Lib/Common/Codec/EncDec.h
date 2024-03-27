/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _ENC_DEC_COMMON_CALCULATIONS_H_
#define _ENC_DEC_COMMON_CALCULATIONS_H_
#include "Pi.h"
#include <stdint.h>

#define TRUNCATION_MAX (15)

#ifdef __cplusplus
extern "C" {
#endif

uint8_t compute_truncation(uint8_t gain, uint8_t priority, uint8_t quantization, uint8_t refinement);
void precinct_compute_truncation_light(const pi_t *pi, precinct_t *precinct, uint8_t quantization, uint8_t refinement);
uint8_t precinct_compute_truncation(const pi_t *pi, precinct_t *prec, uint8_t quantization, uint8_t refinement);
const char *get_asm_level_name_str(CPU_FLAGS cpu_flags);
const char *svt_jpeg_xs_get_format_name(ColourFormat_t format);

#ifdef __cplusplus
}
#endif

#endif
