#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# DECODER_CODEC Directory CMakeLists.txt

# Include Decoder Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSE2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX512/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_SSE4_1/
    )

file(GLOB all_files
    "*.h"
    "*.c")

add_library(DECODER_CODEC OBJECT ${all_files})
