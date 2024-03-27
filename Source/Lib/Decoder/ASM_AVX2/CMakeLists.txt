#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Decoder/ASM_AVX2 Directory CMakeLists.txt

# Include Decoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_SSE2/
    )

check_both_flags_add(-mavx2)

if(MSVC)
    check_both_flags_add(/arch:AVX2)
endif()

file(GLOB all_files
    "*.h"
    "*.asm"
    "*.c")


add_library(DECODER_ASM_AVX2 OBJECT ${all_files})
