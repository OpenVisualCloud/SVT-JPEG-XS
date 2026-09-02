/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __GC_STAGE_NEON_H__
#define __GC_STAGE_NEON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gc_precinct_stage_scalar_loop_neon(uint32_t line_groups_num, uint16_t* coeff_data_ptr_16bit, uint8_t* gcli_data_ptr);

void gc_precinct_sigflags_max_neon(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                   uint32_t gcli_width);

#ifdef __cplusplus
}
#endif

#endif /*__GC_STAGE_NEON_H__*/
