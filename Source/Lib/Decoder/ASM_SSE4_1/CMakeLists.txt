#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Decoder/ASM_SSE4_1 Directory CMakeLists.txt

# Include Decoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    )

check_both_flags_add(-msse4.1)

file(GLOB all_files
    "*.h"
    "*.asm"
    "*.c")

add_library(DECODER_ASM_SSE4_1 OBJECT ${all_files})
