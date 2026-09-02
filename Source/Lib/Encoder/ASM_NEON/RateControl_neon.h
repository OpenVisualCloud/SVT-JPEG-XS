/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __RATE_CONTROL_NEON_H__
#define __RATE_CONTROL_NEON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gc_histogram_16_neon(const uint8_t* data, uint32_t width, uint16_t* hist);

#ifdef __cplusplus
}
#endif

#endif /*__RATE_CONTROL_NEON_H__*/
