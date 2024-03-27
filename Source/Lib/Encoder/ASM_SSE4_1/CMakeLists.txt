#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# ASM_SSE4_1 Directory CMakeLists.txt
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Codec/
    )

check_both_flags_add(-msse4.1)

file(GLOB all_files
    "*.h"
    "*.c")

add_library(ENCODER_ASM_SSE4_1 OBJECT ${all_files})
