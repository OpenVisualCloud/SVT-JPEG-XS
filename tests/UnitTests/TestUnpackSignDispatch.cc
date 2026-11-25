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
#endif

#ifdef ARCH_X86_64
/* The AVX2 and AVX-512 tiers take unpack_sign_avx2_bmi2 on a host with BMI2 and POPCNT, and the C tier,
 * the tiers below AVX2 and a host without them take unpack_sign_c. */
TEST(unpack_sign, dispatch) {
    const CPU_FLAGS required = CPU_FLAGS_AVX2 | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    const bool host_has_kernel = (get_cpu_flags() & required) == required;
    const auto avx2_tier = host_has_kernel ? unpack_sign_avx2_bmi2 : unpack_sign_c;

    setup_decoder_rtcd_internal(CPU_FLAGS_C);
    EXPECT_EQ(unpack_sign, unpack_sign_c) << "C";

    /* Every tier below AVX2 */
    setup_decoder_rtcd_internal(CPU_FLAGS_AVX2 - 1);
    EXPECT_EQ(unpack_sign, unpack_sign_c) << "up to AVX";

    /* The AVX2 tier, without AVX-512 */
    setup_decoder_rtcd_internal(CPU_FLAGS_AVX512F - 1);
    EXPECT_EQ(unpack_sign, avx2_tier) << "up to AVX2";

    /* Every tier: AVX-512 on a host that has it, which takes the AVX2 kernel too */
    setup_decoder_rtcd_internal(CPU_FLAGS_ALL);
    EXPECT_EQ(unpack_sign, avx2_tier) << "all";
}
#endif /* ARCH_X86_64 */
