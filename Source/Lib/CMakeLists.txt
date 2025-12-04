#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

# Decoder Library directory CMakeLists.txt

# Shared Decoder Version, please also change SVT_JPEGXS_API_VER_MAJOR/SVT_JPEGXS_API_VER_MINOR
set(SVT_LIB_VERSION_MAJOR 0)
set(SVT_LIB_VERSION_MINOR 10)
set(SVT_LIB_VERSION_PATCH 0)
set(SVT_LIB_VERSION ${SVT_LIB_VERSION_MAJOR}.${SVT_LIB_VERSION_MINOR}.${SVT_LIB_VERSION_PATCH})

add_subdirectory(Common/Codec)
add_subdirectory(Common/ASM_SSE2)
add_subdirectory(Common/ASM_AVX2)
add_subdirectory(Encoder/Codec)
add_subdirectory(Encoder/ASM_SSE2)
add_subdirectory(Encoder/ASM_SSE4_1)
add_subdirectory(Encoder/ASM_AVX2)
add_subdirectory(Encoder/ASM_AVX512)
add_subdirectory(Decoder/Codec)
add_subdirectory(Decoder/ASM_SSE4_1)
add_subdirectory(Decoder/ASM_AVX2)
add_subdirectory(Decoder/ASM_AVX512)

if(UNIX)
    if(NOT APPLE)
        find_library(M_LIB NAMES m)
        if(M_LIB)
            list(APPEND PLATFORM_LIBS m)
        endif()
        list(APPEND PLATFORM_LIBS Threads::Threads)
    endif()
    set(LIBS_PRIVATE "-lpthread -lm")
endif()

if(COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        set(LIBS_PRIVATE "-lgcov ${LIBS_PRIVATE}")
    else(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(LIBS_PRIVATE "--coverage ${LIBS_PRIVATE}")
    endif()
endif()

if (BUILD_SHARED_LIBS)
    target_compile_definitions(COMMON_CODEC
        PUBLIC DEF_DLL
        PRIVATE DEF_BUILDING_SHARED_LIBS)

    target_compile_definitions(ENCODER_CODEC
        PUBLIC DEF_DLL
        PRIVATE DEF_BUILDING_SHARED_LIBS)

    target_compile_definitions(DECODER_CODEC
        PUBLIC DEF_DLL
        PRIVATE DEF_BUILDING_SHARED_LIBS)
endif()

# Decoder Lib Source Files
add_library(SvtJpegxsLib
    $<TARGET_OBJECTS:COMMON_CODEC>
    $<TARGET_OBJECTS:COMMON_ASM_AVX2>
    $<TARGET_OBJECTS:COMMON_ASM_SSE2>
    $<TARGET_OBJECTS:ENCODER_CODEC>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE2>
    $<TARGET_OBJECTS:ENCODER_ASM_SSE4_1>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX2>
    $<TARGET_OBJECTS:ENCODER_ASM_AVX512>
    $<TARGET_OBJECTS:DECODER_CODEC>
    $<TARGET_OBJECTS:DECODER_ASM_AVX512>
    $<TARGET_OBJECTS:DECODER_ASM_AVX2>
    $<TARGET_OBJECTS:DECODER_ASM_SSE4_1>
    )

set_target_properties(SvtJpegxsLib PROPERTIES OUTPUT_NAME "SvtJpegxs")
set_target_properties(SvtJpegxsLib PROPERTIES VERSION ${SVT_LIB_VERSION})
set_target_properties(SvtJpegxsLib PROPERTIES SOVERSION ${SVT_LIB_VERSION_MAJOR})
set_target_properties(SvtJpegxsLib PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(SvtJpegxsLib PUBLIC ${PLATFORM_LIBS})
target_link_libraries(SvtJpegxsLib PRIVATE cpuinfo_public)

set(LIB_PKG_CONFIG_EXTRA_CFLAGS "")
if (BUILD_SHARED_LIBS)
    target_compile_definitions(SvtJpegxsLib
        PUBLIC DEF_DLL
        PRIVATE DEF_BUILDING_SHARED_LIBS)
    set(LIB_PKG_CONFIG_EXTRA_CFLAGS " -DDEF_DLL")
endif()

install(TARGETS SvtJpegxsLib
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

configure_file(pkg-config.pc.in ${CMAKE_BINARY_DIR}/SvtJpegxs.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/SvtJpegxs.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
