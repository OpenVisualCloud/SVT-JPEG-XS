/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef group_coding_sse4_1_h
#define group_coding_sse4_1_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "SvtType.h"
#include "Definitions.h"
#include "Codestream.h"

void gc_precinct_sigflags_max_sse4_1(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                     uint32_t gcli_width);

#ifdef __cplusplus
}
#endif
#endif // group_coding_sse4_1_h
