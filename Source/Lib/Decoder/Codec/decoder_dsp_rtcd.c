/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#define DECODER_RTCD_C
#include "decoder_dsp_rtcd.h"
#include "Dwt53Decoder_AVX2.h"
#include "Dequant.h"
#include "Dequant_SSE4.h"
#include "Idwt.h"
#include "NltDec.h"
#include "NltDec_AVX2.h"
#include "Precinct.h"
#include "UnPack_avx2.h"
#include "Packing.h"
#include "idwt-avx512.h"
#include "NltDec_avx512.h"
#include "Dequant_avx512.h"

/**************************************
 * Instruction Set Support
 **************************************/

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

void setup_decoder_rtcd_internal(CPU_FLAGS flags) {
    /* Avoid check that pointer is set double, after first  setup. */
    static uint8_t first_call_setup = 1;
    uint8_t check_pointer_was_set = first_call_setup;
    first_call_setup = 0;
#ifdef ARCH_X86_64
    /** Should be done during library initialization,
      but for safe limiting cpu flags again. */
    flags &= get_cpu_flags();
    // to use C: flags=0
#else
    (void)flags;
#endif

    SET_SSE41_AVX2_AVX512(dequant, dequant_c, dequant_sse4_1, NULL, dequant_avx512);
    SET_AVX2(linear_output_scaling_8bit, linear_output_scaling_8bit_c, linear_output_scaling_8bit_avx2);
    SET_AVX2_AVX512(linear_output_scaling_8bit_line,
                    linear_output_scaling_8bit_line_c,
                    linear_output_scaling_8bit_line_avx2,
                    linear_output_scaling_8bit_line_avx512);
    SET_AVX2(linear_output_scaling_16bit, linear_output_scaling_16bit_c, linear_output_scaling_16bit_avx2);
    SET_AVX2_AVX512(linear_output_scaling_16bit_line,
                    linear_output_scaling_16bit_line_c,
                    linear_output_scaling_16bit_line_avx2,
                    linear_output_scaling_16bit_line_avx512);

    SET_AVX2(inv_sign, inv_sign_c, inv_sign_avx2);
    SET_AVX2(unpack_data, unpack_data_c, unpack_data_avx2);
    SET_AVX2_AVX512(idwt_horizontal_line_lf16_hf16,
                    idwt_horizontal_line_lf16_hf16_c,
                    idwt_horizontal_line_lf16_hf16_avx2,
                    idwt_horizontal_line_lf16_hf16_avx512);
    SET_AVX2_AVX512(idwt_horizontal_line_lf32_hf16,
                    idwt_horizontal_line_lf32_hf16_c,
                    idwt_horizontal_line_lf32_hf16_avx2,
                    idwt_horizontal_line_lf32_hf16_avx512);
    SET_AVX2_AVX512(idwt_vertical_line, idwt_vertical_line_c, idwt_vertical_line_avx2, idwt_vertical_line_avx512);
    SET_AVX2_AVX512(
        idwt_vertical_line_recalc, idwt_vertical_line_recalc_c, idwt_vertical_line_recalc_avx2, idwt_vertical_line_recalc_avx512);
}
