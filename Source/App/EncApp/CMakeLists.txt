#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# APP Directory CMakeLists.txt

# Include Subdirectories
include_directories(
    ${PROJECT_SOURCE_DIR}/Source/API/
    ${PROJECT_SOURCE_DIR}/Source/App/Common/
    )

file(GLOB all_files
    "../../API/*.h"
    "../Common/*.h"
    "../Common/*.c"
    "*.h"
    "*.c"
)

# App Source Files
add_executable(SvtJpegxsEncApp
    ${all_files})

#********** SET COMPILE FLAGS************
# Link the Encoder App
target_link_libraries(SvtJpegxsEncApp
    SvtJpegxsLib)

if(UNIX)
    target_link_libraries(SvtJpegxsEncApp
        pthread
        m)
endif()


install(TARGETS SvtJpegxsEncApp RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
