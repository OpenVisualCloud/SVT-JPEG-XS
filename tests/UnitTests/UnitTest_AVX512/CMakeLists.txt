#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake_minimum_required(VERSION 3.10)

include_directories(
    ${PROJECT_SOURCE_DIR}/tests/UnitTests
    ${PROJECT_SOURCE_DIR}/Source/API
)

#UT TESTS SOURCES
file(GLOB all_files
    "*.h"
    "*.cc"
    "*.cpp")

#Support AVX2 and AVX512
check_both_flags_add(
    -mavx2
    -mavx512f
    -mavx512bw
    -mavx512dq
    -mavx512vl)

if(MSVC)
    check_both_flags_add(/arch:AVX2)
    check_both_flags_add(/arch:AVX512)
endif()

add_library(SvtJpegxsUnitTests_AVX512 OBJECT ${all_files})
