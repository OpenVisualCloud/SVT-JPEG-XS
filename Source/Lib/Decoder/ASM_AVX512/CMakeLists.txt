#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Decoder/ASM_AVX512 Directory CMakeLists.txt

# Include Decoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_SSE2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX2/
    )

check_both_flags_add(
    -mavx512f
    -mavx512bw
    -mavx512dq
    -mavx512vl)

if(MSVC)
    check_both_flags_add(/arch:AVX512)
endif()

file(GLOB all_files
    "*.h"
    "*.asm"
    "*.c")


add_library(DECODER_ASM_AVX512 OBJECT ${all_files})
