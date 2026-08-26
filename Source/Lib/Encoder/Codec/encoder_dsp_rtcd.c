/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#define ENCODER_RTCD_C
#include "encoder_dsp_rtcd.h"
#include "GcStageProcess.h"
#include "DwtStageProcess.h"
#include "GcStageProcess.h"
#include "Dwt.h"
#include "NltEnc.h"
#include "Quant.h"
#include "PackPrecinct.h"
#include "RateControl.h"

#ifdef ARCH_X86_64
#include "NltEnc_avx2.h"
#include "Enc_avx512.h"
#include "Dwt_AVX2.h"
#include "Quant_sse4_1.h"
#include "Quant_avx2.h"
#include "Quant_avx512.h"
#include "Pack_avx512.h"
#include "Pack_avx2.h"
#include "group_coding_sse4_1.h"
#include "RateControl_avx2.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
#include "Quant_neon.h"
#include "Pack_neon.h"
#include "Dwt_neon.h"
#include "GcStage_neon.h"
#include "NltEnc_neon.h"
#endif /* ARCH_AARCH64 */

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

/* Narrows a pointer already set by one of the macros above to its AArch64
 * implementation. Written as a separate line rather than as another column of
 * SET_FUNCTIONS because the two architectures are mutually exclusive: on x86
 * this expands to nothing, and on AArch64 the SET_* line above it has left the
 * pointer on the C implementation. */
#ifdef ARCH_AARCH64
#define SET_NEON(ptr, neon)                                               \
    if (((uintptr_t)NULL != (uintptr_t)neon) && (flags & CPU_FLAGS_NEON)) \
        ptr = neon;
#else /* ARCH_AARCH64 */
#define SET_NEON(ptr, neon)
#endif /* ARCH_AARCH64 */
#define SET_AVX512(ptr, c, avx512)                          SET_FUNCTIONS(ptr, c, 0, 0, 0, 0, 0, 0, 0, 0, 0, avx512)

void setup_encoder_rtcd_internal(CPU_FLAGS flags) {
    /* Avoid check that pointer is set double, after first  setup. */
    static uint8_t first_call_setup = 1;
    uint8_t check_pointer_was_set = first_call_setup;
    first_call_setup = 0;
#ifdef ARCH_X86_64
    /** Should be done during library initialization,
      but for safe limiting cpu flags again. */
    const CPU_FLAGS host_flags = get_cpu_flags();
    flags &= host_flags;
    /* The AVX-512 kernels are built with -mpopcnt and -mbmi2 for the whole directory, so
     * the compiler is free to emit those instructions anywhere in them, not
     * only where an intrinsic asks for them. Without a processor to back
     * them the whole tier has to stay unused.
     *
     * The test is on the host, not on the caller's request: a caller built
     * against an older header passes a mask with these bits clear, and it must
     * not lose AVX-512 on hardware that supports it. */
    if ((host_flags & (CPU_FLAGS_POPCNT | CPU_FLAGS_BMI2)) != (CPU_FLAGS_POPCNT | CPU_FLAGS_BMI2)) {
        flags &= ~(CPU_FLAGS)CPU_FLAGS_AVX512F;
    }
    /* The AVX2 directory is built with -mpopcnt as well, and RateControl_avx2.c
     * asks for POPCNT by intrinsic, so the same rule applies one tier down.
     * Every processor with AVX2 also has POPCNT, so this only matters where a
     * hypervisor or emulator masks the feature bits apart. */
    if (!(host_flags & CPU_FLAGS_POPCNT)) {
        flags &= ~(CPU_FLAGS)CPU_FLAGS_AVX2;
    }
    // to use C: flags=0
#else
    (void)flags;
#endif

    //SET_AVX2(get_sigflags_gc, get_sigflags_gc_c, get_sigflags_gc_avx2);
    SET_AVX2_AVX512(image_shift, image_shift_c, image_shift_avx2, image_shift_avx512);
    SET_AVX2_AVX512(dwt_horizontal_line, dwt_horizontal_line_c, dwt_horizontal_line_avx2, dwt_horizontal_line_avx512);
    SET_NEON(dwt_horizontal_line, dwt_horizontal_line_neon);

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
    SET_NEON(quantization, quantization_neon);
    SET_AVX2_AVX512(linear_input_scaling_line_8bit,
                    linear_input_scaling_line_8bit_c,
                    linear_input_scaling_line_8bit_avx2,
                    linear_input_scaling_line_8bit_avx512);
    SET_NEON(linear_input_scaling_line_8bit, linear_input_scaling_line_8bit_neon);
    SET_AVX2_AVX512(linear_input_scaling_line_16bit,
                    linear_input_scaling_line_16bit_c,
                    linear_input_scaling_line_16bit_avx2,
                    linear_input_scaling_line_16bit_avx512);
    SET_NEON(linear_input_scaling_line_16bit, linear_input_scaling_line_16bit_neon);
    SET_AVX2_AVX512(linear_input_scaling_line_16bit_msb,
                    linear_input_scaling_line_16bit_msb_c,
                    linear_input_scaling_line_16bit_msb_avx2,
                    linear_input_scaling_line_16bit_msb_avx512);
    SET_NEON(linear_input_scaling_line_16bit_msb, linear_input_scaling_line_16bit_msb_neon);

    SET_AVX2_AVX512(pack_data_single_group, pack_data_single_group_c, pack_data_single_group_avx2, pack_data_single_group_avx512);
    SET_SSE2(gc_precinct_stage_scalar_loop, gc_precinct_stage_scalar_loop_c, gc_precinct_stage_scalar_loop_ASM);
    SET_NEON(gc_precinct_stage_scalar_loop, gc_precinct_stage_scalar_loop_neon);
    SET_AVX2_AVX512(pack_data_groups, pack_data_groups_c, pack_data_groups_avx2, pack_data_groups_avx512);
    SET_NEON(pack_data_groups, pack_data_groups_neon);
    SET_AVX2_AVX512(gc_histogram_16, gc_histogram_16_c, gc_histogram_16_avx2, gc_histogram_16_avx512);
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
