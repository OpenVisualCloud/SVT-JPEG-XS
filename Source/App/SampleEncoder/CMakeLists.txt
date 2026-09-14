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
    ${SVT_JXS_SIMD_OBJECTS_COMMON}
    $<TARGET_OBJECTS:ENCODER_CODEC>
    ${SVT_JXS_SIMD_OBJECTS_ENCODER}
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
    ${SVT_JXS_SIMD_TARGETS_COMMON}
    ENCODER_CODEC
    ${SVT_JXS_SIMD_TARGETS_ENCODER}
    cpuinfo_public)

install(TARGETS SvtJpegxsSampleEncoder RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
