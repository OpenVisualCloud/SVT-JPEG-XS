#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# ASM_SSE2 Directory CMakeLists.txt

# Include Common Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSE2/
    )

check_both_flags_add(-msse2)

file(GLOB all_files
    "*.h"
    "*.c")

set(asm_files
    PictureOperators_SSE2.asm)

add_library(COMMON_ASM_SSE2 OBJECT ${all_files})

asm_compile_to_target(COMMON_ASM_SSE2 ${asm_files})
