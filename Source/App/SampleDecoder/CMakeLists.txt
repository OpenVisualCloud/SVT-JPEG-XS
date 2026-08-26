#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# SampleDecoder Directory CMakeLists.txt

# Include Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    )

file(GLOB all_files
    "../../API/*.h"
    "main.c"
)

# App Source Files
add_executable(SvtJpegxsSampleDecoder
    ${all_files})

#Link only with required libraries to detect in linker time references between decoder_lib an encoder_lib
set(lib_list_decoder
    $<TARGET_OBJECTS:COMMON_CODEC>
    ${SVT_JXS_SIMD_OBJECTS_COMMON}
    $<TARGET_OBJECTS:DECODER_CODEC>
    ${SVT_JXS_SIMD_OBJECTS_DECODER}
    cpuinfo_public
    )

#********** SET COMPILE FLAGS************
# Link the Decoder Sample App
target_link_libraries(SvtJpegxsSampleDecoder
    ${lib_list_decoder})

if(UNIX)
    target_link_libraries(SvtJpegxsSampleDecoder
        pthread
        m)
endif()

add_dependencies(SvtJpegxsSampleDecoder
    COMMON_CODEC
    ${SVT_JXS_SIMD_TARGETS_COMMON}
    DECODER_CODEC
    ${SVT_JXS_SIMD_TARGETS_DECODER}
    cpuinfo_public)

install(TARGETS SvtJpegxsSampleDecoder RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
