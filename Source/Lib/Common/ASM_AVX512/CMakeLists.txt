#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Common/ASM_AVX512 Directory CMakeLists.txt

include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSE2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX512/
    )

check_both_flags_add(
    -mavx512f
    -mavx512bw
    -mavx512dq
    -mavx512vl
    -mbmi2)

if(MSVC)
    check_both_flags_add(/arch:AVX512)
endif()

file(GLOB all_files
    "*.h"
    "*.asm"
    "*.c")

add_library(COMMON_ASM_AVX512 OBJECT ${all_files})
