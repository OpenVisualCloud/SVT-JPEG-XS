#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# SampleEncoder Directory CMakeLists.txt

# Include Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    )

file(GLOB all_files
    "../../API/*.h"
    "main.c"
)

# App Source Files
add_executable(SvtJpegxsSampleEncoder
    ${all_files})

#Link only with required libraries to detect in linker time references between decoder_lib an encoder_lib
set(lib_list_encoder
    $<TARGET_OBJECTS:COMMON_CODEC>
    $<TARGET_OBJECTS:COMMON_ASM_SSE2>
    $<TARGET_OBJECTS:COMMON_ASM_AVX2>
    $<TARGET_OBJECTS:ENCODER_CODEC>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE2>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE4_1>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX2>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX512>
    cpuinfo_public
    )

#********** SET COMPILE FLAGS************
# Link the Encoder Sample App
target_link_libraries(SvtJpegxsSampleEncoder
    ${lib_list_encoder})

if(UNIX)
    target_link_libraries(SvtJpegxsSampleEncoder
        pthread
        m)
endif()

add_dependencies(SvtJpegxsSampleEncoder
    COMMON_CODEC
    COMMON_ASM_SSE2
    COMMON_ASM_AVX2
    ENCODER_CODEC
    ENCODER_ASM_SSE2
    ENCODER_ASM_SSE4_1
    ENCODER_ASM_AVX2
    ENCODER_ASM_AVX512
    cpuinfo_public)

install(TARGETS SvtJpegxsSampleEncoder RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
