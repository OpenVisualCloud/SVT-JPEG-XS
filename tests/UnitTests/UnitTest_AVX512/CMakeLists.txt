#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake_minimum_required(VERSION 3.10)

#gtest needs C++14; the main test target gets it by linking gtest, an object
#library has to ask for it itself.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${gtest_SOURCE_DIR}/include
    ${gtest_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/UnitTests
    ${PROJECT_SOURCE_DIR}/Source/API
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Codec
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX2
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX512
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX2
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX512
)

#UT TESTS SOURCES
file(GLOB all_files
    "*.h"
    "*.cc"
    "*.cpp")

#Support AVX2 and AVX512
#BMI2 and POPCNT match the flags of the production AVX-512 directories: the
#helpers tested here use PDEP and PEXT.
check_both_flags_add(
    -mavx2
    -mavx512f
    -mavx512bw
    -mavx512dq
    -mavx512vl
    -mpopcnt
    -mbmi2)

if(MSVC)
    check_both_flags_add(/arch:AVX2)
    check_both_flags_add(/arch:AVX512)
endif()

add_library(SvtJpegxsUnitTests_AVX512 OBJECT ${all_files})
