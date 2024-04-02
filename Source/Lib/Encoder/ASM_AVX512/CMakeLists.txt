#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Encoder/ASM_AVX512 Directory CMakeLists.txt

# Include Encoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_SSE2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX512/
   )

check_both_flags_add(
    -mavx512f
    -mavx512bw
    -mavx512cd
    -mavx512dq
    -mavx512vl)

if(MSVC)
    check_both_flags_add(/arch:AVX512)
endif()

file(GLOB all_files
    "*.h"
    "*.asm"
    "*.c")


add_library(ENCODER_ASM_AVX512 OBJECT ${all_files})
