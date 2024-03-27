/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#define RTCD_C
#include "common_dsp_rtcd.h"

#ifdef ARCH_X86_64
// for get_cpu_flags
#include "cpuinfo.h"
#endif

/**************************************
 * Instruction Set Support
 **************************************/
#ifdef ARCH_X86_64
CPU_FLAGS get_cpu_flags() {
    CPU_FLAGS flags = 0;

    // safe to call multiple times, and thread safe
    // also correctly checks whether the OS saves AVX(2|512) registers
    cpuinfo_initialize();

    flags |= cpuinfo_has_x86_mmx() ? CPU_FLAGS_MMX : 0;
    flags |= cpuinfo_has_x86_sse() ? CPU_FLAGS_SSE : 0;
    flags |= cpuinfo_has_x86_sse2() ? CPU_FLAGS_SSE2 : 0;
    flags |= cpuinfo_has_x86_sse3() ? CPU_FLAGS_SSE3 : 0;
    flags |= cpuinfo_has_x86_ssse3() ? CPU_FLAGS_SSSE3 : 0;
    flags |= cpuinfo_has_x86_sse4_1() ? CPU_FLAGS_SSE4_1 : 0;
    flags |= cpuinfo_has_x86_sse4_2() ? CPU_FLAGS_SSE4_2 : 0;

    flags |= cpuinfo_has_x86_avx() ? CPU_FLAGS_AVX : 0;
    flags |= cpuinfo_has_x86_avx2() ? CPU_FLAGS_AVX2 : 0;

    flags |= cpuinfo_has_x86_avx512f() ? CPU_FLAGS_AVX512F : 0;
    flags |= cpuinfo_has_x86_avx512dq() ? CPU_FLAGS_AVX512DQ : 0;
    flags |= cpuinfo_has_x86_avx512cd() ? CPU_FLAGS_AVX512CD : 0;
    flags |= cpuinfo_has_x86_avx512bw() ? CPU_FLAGS_AVX512BW : 0;
    flags |= cpuinfo_has_x86_avx512vl() ? CPU_FLAGS_AVX512VL : 0;

    return flags;
}
#endif /*ARCH_X86_64*/

#ifdef ARCH_X86_64
#define SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512) \
    if (((uintptr_t)NULL != (uintptr_t)mmx) && (flags & CPU_FLAGS_MMX))                           \
        ptr = mmx;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)sse) && (flags & CPU_FLAGS_SSE))                           \
        ptr = sse;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)sse2) && (flags & CPU_FLAGS_SSE2))                         \
        ptr = sse2;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)sse3) && (flags & CPU_FLAGS_SSE3))                         \
        ptr = sse3;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)ssse3) && (flags & CPU_FLAGS_SSSE3))                       \
        ptr = ssse3;                                                                              \
    if (((uintptr_t)NULL != (uintptr_t)sse4_1) && (flags & CPU_FLAGS_SSE4_1))                     \
        ptr = sse4_1;                                                                             \
    if (((uintptr_t)NULL != (uintptr_t)sse4_2) && (flags & CPU_FLAGS_SSE4_2))                     \
        ptr = sse4_2;                                                                             \
    if (((uintptr_t)NULL != (uintptr_t)avx) && (flags & CPU_FLAGS_AVX))                           \
        ptr = avx;                                                                                \
    if (((uintptr_t)NULL != (uintptr_t)avx2) && (flags & CPU_FLAGS_AVX2))                         \
        ptr = avx2;                                                                               \
    if (((uintptr_t)NULL != (uintptr_t)avx512) && (flags & CPU_FLAGS_AVX512F))                    \
        ptr = avx512;
#else /* ARCH_X86_64 */
#define SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512)
#endif /* ARCH_X86_64 */

#define SET_FUNCTIONS(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512)     \
    do {                                                                                          \
        if (check_pointer_was_set && ptr != 0) {                                                  \
            printf("Error: %s:%i: Pointer \"%s\" is set before!\n", __FILE__, __LINE__, #ptr);    \
            assert(0);                                                                            \
        }                                                                                         \
        if ((uintptr_t)NULL == (uintptr_t)c) {                                                    \
            printf("Error: %s:%i: Pointer \"%s\" on C is NULL!\n", __FILE__, __LINE__, #ptr);     \
            assert(0);                                                                            \
        }                                                                                         \
        ptr = c;                                                                                  \
        SET_FUNCTIONS_X86(ptr, c, mmx, sse, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, avx512) \
    } while (0)

/* Macros SET_* use local variable CPU_FLAGS flags and Bool
 * check_pointer_was_set */
#define SET_ONLY_C(ptr, c)                                  SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define SET_SSE2(ptr, c, sse2)                              SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, 0, 0)
#define SET_SSE2_AVX2(ptr, c, sse2, avx2)                   SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, avx2, 0)
#define SET_SSE2_AVX512(ptr, c, sse2, avx512)               SET_FUNCTIONS(ptr, c, 0, 0, sse2, 0, 0, 0, 0, 0, 0, avx512)
#define SET_SSSE3(ptr, c, ssse3)                            SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, ssse3, 0, 0, 0, 0, 0)
#define SET_SSSE3_AVX2(ptr, c, ssse3, avx2)                 SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, ssse3, 0, 0, 0, avx2, 0)
#define SET_SSE41(ptr, c, sse4_1)                           SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, 0, 0)
#define SET_SSE41_AVX2(ptr, c, sse4_1, avx2)                SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, avx2, 0)
#define SET_SSE41_AVX2_AVX512(ptr, c, sse4_1, avx2, avx512) SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, sse4_1, 0, 0, avx2, avx512)
#define SET_AVX2(ptr, c, avx2)                              SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, avx2, 0)
#define SET_AVX2_AVX512(ptr, c, avx2, avx512)               SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, avx2, avx512)

void setup_common_rtcd_internal(CPU_FLAGS flags) {
    /* Avoid check that pointer is set double, after first  setup. */
    static uint8_t first_call_setup = 1;
    uint8_t check_pointer_was_set = first_call_setup;
    first_call_setup = 0;
#ifdef ARCH_X86_64
    /** Should be done during library initialization,
      but for safe limiting CPU flags again. */
    flags &= get_cpu_flags();
    // to use C: flags=0
#else
    (void)flags;
#endif

    SET_SSE2(svt_log2_32, log2_32_c, Log2_32_ASM);
}
