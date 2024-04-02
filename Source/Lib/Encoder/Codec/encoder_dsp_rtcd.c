/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#define ENCODER_RTCD_C
#include "encoder_dsp_rtcd.h"
#include "GcStageProcess.h"
#include "NltEnc_avx2.h"
#include "DwtStageProcess.h"
#include "Enc_avx512.h"
#include "GcStageProcess.h"
#include "Dwt.h"
#include "Dwt_AVX2.h"
#include "NltEnc.h"
#include "Quant_sse4_1.h"
#include "Quant_avx2.h"
#include "Quant_avx512.h"
#include "Quant.h"
#include "PackPrecinct.h"
#include "Pack_avx512.h"
#include "group_coding_sse4_1.h"
#include "RateControl.h"
#include "RateControl_avx2.h"

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
#define SET_AVX512(ptr, c, avx512)                          SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, 0, avx512)

void setup_encoder_rtcd_internal(CPU_FLAGS flags) {
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

    //SET_AVX2(get_sigflags_gc, get_sigflags_gc_c, get_sigflags_gc_avx2);
    SET_AVX2_AVX512(image_shift, image_shift_c, image_shift_avx2, image_shift_avx512);
    SET_AVX2_AVX512(dwt_horizontal_line, dwt_horizontal_line_c, dwt_horizontal_line_avx2, dwt_horizontal_line_avx512);

    SET_AVX2_AVX512(transform_V1_Hx_precinct_recalc_HF_prev,
                    transform_V1_Hx_precinct_recalc_HF_prev_c,
                    transform_V1_Hx_precinct_recalc_HF_prev_avx2,
                    transform_V1_Hx_precinct_recalc_HF_prev_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_hf_line_0,
                    transform_vertical_loop_hf_line_0_c,
                    transform_vertical_loop_hf_line_0_avx2,
                    transform_vertical_loop_hf_line_0_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_lf_line_0,
                    transform_vertical_loop_lf_line_0_c,
                    transform_vertical_loop_lf_line_0_avx2,
                    transform_vertical_loop_lf_line_0_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_lf_hf_line_0,
                    transform_vertical_loop_lf_hf_line_0_c,
                    transform_vertical_loop_lf_hf_line_0_avx2,
                    transform_vertical_loop_lf_hf_line_0_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_lf_hf_line_x_prev,
                    transform_vertical_loop_lf_hf_line_x_prev_c,
                    transform_vertical_loop_lf_hf_line_x_prev_avx2,
                    transform_vertical_loop_lf_hf_line_x_prev_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_lf_hf_hf_line_x,
                    transform_vertical_loop_lf_hf_hf_line_x_c,
                    transform_vertical_loop_lf_hf_hf_line_x_avx2,
                    transform_vertical_loop_lf_hf_hf_line_x_avx512);
    SET_AVX2_AVX512(transform_vertical_loop_lf_hf_hf_line_last_even,
                    transform_vertical_loop_lf_hf_hf_line_last_even_c,
                    transform_vertical_loop_lf_hf_hf_line_last_even_avx2,
                    transform_vertical_loop_lf_hf_hf_line_last_even_avx512);

    SET_AVX2_AVX512(
        gc_precinct_stage_scalar, gc_precinct_stage_scalar_c, gc_precinct_stage_scalar_avx2, gc_precinct_stage_scalar_avx512);
    SET_SSE41_AVX2_AVX512(quantization, quantization_c, quantization_sse4_1, quantization_avx2, quantization_avx512);
    SET_AVX2_AVX512(linear_input_scaling_line_8bit,
                    linear_input_scaling_line_8bit_c,
                    linear_input_scaling_line_8bit_avx2,
                    linear_input_scaling_line_8bit_avx512);
    SET_AVX2_AVX512(linear_input_scaling_line_16bit,
                    linear_input_scaling_line_16bit_c,
                    linear_input_scaling_line_16bit_avx2,
                    linear_input_scaling_line_16bit_avx512);

    SET_AVX2_AVX512(pack_data_single_group, pack_data_single_group_c, NULL, pack_data_single_group_avx512);
    SET_SSE2(gc_precinct_stage_scalar_loop, gc_precinct_stage_scalar_loop_c, gc_precinct_stage_scalar_loop_ASM);
    SET_SSE41(gc_precinct_sigflags_max, gc_precinct_sigflags_max_c, gc_precinct_sigflags_max_sse4_1);
    SET_AVX2_AVX512(rate_control_calc_vpred_cost_nosigf,
                    rate_control_calc_vpred_cost_nosigf_c,
                    rate_control_calc_vpred_cost_nosigf_avx2,
                    rate_control_calc_vpred_cost_nosigf_avx512);
    SET_AVX512(rate_control_calc_vpred_cost_sigf_nosigf,
               rate_control_calc_vpred_cost_sigf_nosigf_c,
               rate_control_calc_vpred_cost_sigf_nosigf_avx512);

    SET_AVX2_AVX512(convert_packed_to_planar_rgb_8bit,
                    convert_packed_to_planar_rgb_8bit_c,
                    convert_packed_to_planar_rgb_8bit_avx2,
                    convert_packed_to_planar_rgb_8bit_avx512);
    SET_AVX2_AVX512(convert_packed_to_planar_rgb_16bit,
                    convert_packed_to_planar_rgb_16bit_c,
                    convert_packed_to_planar_rgb_16bit_avx2,
                    convert_packed_to_planar_rgb_16bit_avx512);
}
