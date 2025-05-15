#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake_minimum_required(VERSION 3.10)

add_subdirectory(UnitTest_AVX512)

include_directories(
    ${gtest_SOURCE_DIR}/include
    ${gtest_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests/UnitTests
    ${PROJECT_SOURCE_DIR}/tests/UnitTests/UnitTest_AVX512
    ${PROJECT_SOURCE_DIR}/Source/API
    ${PROJECT_SOURCE_DIR}/Source/App/EncApp
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec
    ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX2
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Codec
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_SSE4_1
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX2
    ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX512
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/Codec/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX512/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_AVX2/
    ${PROJECT_SOURCE_DIR}/Source/Lib/Decoder/ASM_SSE4_1/
)

#ALL LIBS
set(lib_list_all
    $<TARGET_OBJECTS:COMMON_CODEC>
    $<TARGET_OBJECTS:COMMON_ASM_SSE2>
    $<TARGET_OBJECTS:COMMON_ASM_AVX2>
    $<TARGET_OBJECTS:ENCODER_CODEC>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE2>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE4_1>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX2>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX512>
    $<TARGET_OBJECTS:DECODER_CODEC>
    $<TARGET_OBJECTS:DECODER_ASM_AVX512>
    $<TARGET_OBJECTS:DECODER_ASM_AVX2>
    $<TARGET_OBJECTS:DECODER_ASM_SSE4_1>
    $<TARGET_OBJECTS:SvtJpegxsUnitTests_AVX512>
    cpuinfo_public
    )

#UT TESTS SOURCES
file(GLOB all_files
    "*.h"
    "*.cc"
    "*.cpp")

check_both_flags_add(-mavx2)

if(MSVC)
    check_both_flags_add(/arch:AVX2)
endif()

#UT MAIN
add_executable(SvtJpegxsUnitTests ${all_files})

target_link_libraries(SvtJpegxsUnitTests PUBLIC
    ${lib_list_all}
    gtest
    gtest_main)

add_dependencies(SvtJpegxsUnitTests SvtJpegxsLib SvtJpegxsUnitTests_AVX512)

add_test(SvtJpegxsUnitTests ${CMAKE_OUTPUT_DIRECTORY}/SvtJpegxsUnitTests)
