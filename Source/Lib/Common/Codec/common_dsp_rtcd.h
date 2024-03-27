/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// This file is generated. Do not edit.
#ifndef COMMON_DSP_RTCD_H_
#define COMMON_DSP_RTCD_H_

#include "Definitions.h"

#ifdef RTCD_C
#define RTCD_EXTERN // CHKN RTCD call in effect. declare the function pointers in  encHandle.
#else
#define RTCD_EXTERN extern // CHKN run time externing the function pointers.
#endif

/**************************************
 * Instruction Set Support
 **************************************/

#ifdef _WIN32
#include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Helper Functions
#ifdef ARCH_X86_64
CPU_FLAGS get_cpu_flags();
#endif
void setup_common_rtcd_internal(CPU_FLAGS flags);
uint32_t log2_32_c(uint32_t x);
RTCD_EXTERN uint32_t (*svt_log2_32)(uint32_t x);

#ifdef ARCH_X86_64
uint32_t Log2_32_ASM(uint32_t x);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
