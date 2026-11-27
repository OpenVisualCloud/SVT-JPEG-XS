/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The dispatch of unpack_sign, in a file of its own: TestUnPack.cc has a typedef named unpack_data, as
 * decoder_dsp_rtcd.h names a function pointer. */

#include "gtest/gtest.h"
#include "common_dsp_rtcd.h"
#include "decoder_dsp_rtcd.h"
#include "Packing.h"
#include "SvtUtility.h"
#ifdef ARCH_X86_64
#include "UnPack_avx2.h"
#include "UnPack_avx512.h"
#endif

#ifdef ARCH_X86_64
/* The C tier, the tiers below AVX2 and a host without BMI2 and POPCNT take unpack_sign_c. The AVX2 tier
 * takes unpack_sign_avx2_bmi2, and the AVX-512 tier, which the host keeps only with AVX512BW, DQ, VL and
 * BMI2, takes unpack_sign_avx512. */
TEST(unpack_sign, dispatch) {
    const CPU_FLAGS host = get_cpu_flags();
    const CPU_FLAGS avx2_kernel = CPU_FLAGS_AVX2 | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    const CPU_FLAGS avx512_kernel = CPU_FLAGS_AVX512F | CPU_FLAGS_AVX512BW | CPU_FLAGS_AVX512DQ | CPU_FLAGS_AVX512VL |
        CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    const auto avx2_tier = (host & avx2_kernel) == avx2_kernel ? unpack_sign_avx2_bmi2 : unpack_sign_c;
    const auto avx512_tier = (host & avx512_kernel) == avx512_kernel ? unpack_sign_avx512 : avx2_tier;

    setup_decoder_rtcd_internal(CPU_FLAGS_C);
    EXPECT_EQ(unpack_sign, unpack_sign_c) << "C";

    /* Every tier below AVX2 */
    setup_decoder_rtcd_internal(CPU_FLAGS_AVX2 - 1);
    EXPECT_EQ(unpack_sign, unpack_sign_c) << "up to AVX";

    /* The AVX2 tier, without AVX-512 */
    setup_decoder_rtcd_internal(CPU_FLAGS_AVX512F - 1);
    EXPECT_EQ(unpack_sign, avx2_tier) << "up to AVX2";

    /* Every tier: AVX-512 on a host that has it */
    setup_decoder_rtcd_internal(CPU_FLAGS_ALL);
    EXPECT_EQ(unpack_sign, avx512_tier) << "all";
}
#endif /* ARCH_X86_64 */
