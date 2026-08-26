#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake_minimum_required(VERSION 3.10)

if(ARCH_X86_64)
    add_subdirectory(UnitTest_AVX512)
endif()

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
    $<TARGET_OBJECTS:ENCODER_CODEC>
    $<TARGET_OBJECTS:DECODER_CODEC>
    ${SVT_JXS_SIMD_OBJECTS_COMMON}
    ${SVT_JXS_SIMD_OBJECTS_ENCODER}
    ${SVT_JXS_SIMD_OBJECTS_DECODER}
    cpuinfo_public
    )
if(ARCH_X86_64)
    list(APPEND lib_list_all $<TARGET_OBJECTS:SvtJpegxsUnitTests_AVX512>)
endif()

#UT TESTS SOURCES
file(GLOB all_files
    "*.h"
    "*.cc"
    "*.cpp")

if(ARCH_X86_64)
    check_both_flags_add(-mavx2)

    if(MSVC)
        check_both_flags_add(/arch:AVX2)
    endif()
endif()

#UT MAIN
add_executable(SvtJpegxsUnitTests ${all_files} ${PROJECT_SOURCE_DIR}/Source/App/EncApp/EncAppConfig.c)
target_compile_definitions(SvtJpegxsUnitTests PRIVATE BUILD_TESTING)

target_link_libraries(SvtJpegxsUnitTests PUBLIC
    ${lib_list_all}
    gtest
    gtest_main)

add_dependencies(SvtJpegxsUnitTests SvtJpegxsLib)
if(ARCH_X86_64)
    add_dependencies(SvtJpegxsUnitTests SvtJpegxsUnitTests_AVX512)
endif()

add_test(SvtJpegxsUnitTests ${CMAKE_OUTPUT_DIRECTORY}/SvtJpegxsUnitTests)
