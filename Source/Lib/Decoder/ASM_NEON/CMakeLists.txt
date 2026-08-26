#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Decoder/ASM_NEON Directory CMakeLists.txt

# Include Decoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    )

# Advanced SIMD is part of the AArch64 baseline, so no -m flag is needed here.

file(GLOB all_files
    "*.h"
    "*.c")

add_library(DECODER_ASM_NEON OBJECT ${all_files})
