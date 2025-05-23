#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake_minimum_required(VERSION 3.10)

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(WARNING "Building in-source is highly not recommended\n"
                    "Please use the Build folder or create your own.")
endif()

project(svt-jpegxs VERSION 0.10.0
    LANGUAGES C CXX)

if(POLICY CMP0063)
  cmake_policy(SET CMP0063 NEW)
endif()

set(CMAKE_C_VISIBILITY_PRESET hidden)
# Default build type to release if the generator does not has its own set of build types
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "32-bit is not supported")
endif()

include(CheckCSourceCompiles)

find_program(YASM_EXE yasm)
option(ENABLE_NASM "Use nasm if available (Uses yasm by default if found)" OFF)
if(YASM_EXE AND NOT CMAKE_ASM_NASM_COMPILER MATCHES "yasm" AND NOT ENABLE_NASM)
    set(CMAKE_ASM_NASM_COMPILER "${YASM_EXE}" CACHE FILEPATH "Path to nasm compatible compiler" FORCE)
else()
    set(NASM_VERSION "0.0.0")
    include(CheckLanguage)
    check_language(ASM_NASM)
    execute_process(COMMAND ${CMAKE_ASM_NASM_COMPILER} -v
        OUTPUT_VARIABLE NASM_VERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "([.0-9])+" NASM_VERSION "${NASM_VERSION}")
    # NASM_VERSION should now contain something like 2.14.02
    # Needs to error out on a version less than 2.14
    if(NASM_VERSION VERSION_LESS "2.13" AND CMAKE_ASM_NASM_COMPILER MATCHES "nasm")
        message(FATAL_ERROR "Found nasm is too old (requires at least 2.13, found ${NASM_VERSION})!")
    endif()
endif()
enable_language(ASM_NASM)
add_definitions(-DARCH_X86_64=1)

include(GNUInstallDirs)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

set(CMAKE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/Bin/${CMAKE_BUILD_TYPE}/" CACHE PATH "Location to write the resulting binaries to")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY})

#Clang support, required to build static with LTO
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND UNIX AND NOT APPLE)
    find_program(LLVM_LD_EXE llvm-ld)
    find_program(LLVM_AR_EXE llvm-ar)
    find_program(LLVM_RANLIB_EXE llvm-ranlib)
    if(LLVM_LD_EXE)
        SET (CMAKE_LINKER  "llvm-ld")
    endif()
    if(LLVM_AR_EXE)
        SET (CMAKE_AR      "llvm-ar")
    endif()
    if(LLVM_RANLIB_EXE)
        SET (CMAKE_RANLIB  "llvm-ranlib")
    endif()
endif()

if(MSVC)
    if(DEFINED SANITIZER)
    message(WARNING "Sanitizers are not available with MSVC currently")
    endif()
    unset(SANITIZER)
    unset(SANITIZER CACHE)
else()
    set(SANITIZER "" CACHE STRING "Sanitizer to enable. Currently known values are Address, Memory, Thread, Undefined, and Integer")
    set_property(CACHE SANITIZER PROPERTY STRINGS Address Memory Thread Undefined Integer)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# BUILD_SHARED_LIBS is a standard CMake variable, but we declare it here to
# make it prominent in the GUI.
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." ON)
message(STATUS "BUILD_SHARED_LIBS: ${BUILD_SHARED_LIBS}")

#option(BUILD_TESTING "Build SvtLcevcUnitTests, SvtLcevcApiTests, and SvtLcevcE2ETests unit tests")
option(COVERAGE "Generate coverage report")
option(BUILD_APPS "Build Enc and Dec Apps" ON)

if(WIN32)
    set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -DWIN64")
else()
    set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -DUNIX64")
endif()

if(UNIX)
    if(APPLE)
        set(CMAKE_MACOSX_RPATH 1)
        set(CMAKE_C_ARCHIVE_CREATE   "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
        set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie -z noexecstack -z relro -z now")
    endif()
endif()

# Always build with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

function(check_flag lang flag)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" flag_var "${flag}")
    if(NOT DEFINED ${lang}_FLAG${flag_var})
        execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "-- Checking ${lang} flag support for: [${flag}] - ")
        if("${lang}" STREQUAL "CXX")
            check_cxx_compiler_flag("${flag}" "${lang}_FLAG${flag_var}")
        else()
            check_c_compiler_flag("${flag}" "${lang}_FLAG${flag_var}")
        endif()
        if(${lang}_FLAG${flag_var})
            execute_process(COMMAND ${CMAKE_COMMAND} -E echo "Yes")
        else()
            execute_process(COMMAND ${CMAKE_COMMAND} -E echo "No")
        endif()
    endif()
    set(check_flag ${${lang}_FLAG${flag_var}} PARENT_SCOPE)
endfunction()

function(check_flags_add lang)
    cmake_parse_arguments(check_${lang}_flags_add
        "PREPEND"
        "TYPE"
        ""
        ${ARGN})

    if(check_${lang}_flags_add_TYPE)
        set(check_${lang}_flags_add_TYPE "_${check_${lang}_flags_add_TYPE}")
    endif()
    if(check_${lang}_flags_add_PREPEND)
        list(REVERSE check_${lang}_flags_add_UNPARSED_ARGUMENTS)
    endif()

    set(CMAKE_REQUIRED_QUIET true)

    foreach(flag IN LISTS check_${lang}_flags_add_UNPARSED_ARGUMENTS)
        check_flag("${lang}" "${flag}")
        if(NOT check_flag)
            continue()
        endif()

        if(check_${lang}_flags_add_PREPEND)
            set(CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE} "${flag} ${CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE}}")
        else()
            set(CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE} "${CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE}} ${flag}")
        endif()
    endforeach()
    if(CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]")
        message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} ${lang}FLAGS: ${CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE}}")
    endif()
    set(CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE} "${CMAKE_${lang}_FLAGS${check_${lang}_flags_add_TYPE}}" PARENT_SCOPE)
endfunction()

macro(check_both_flags_add)
    check_flags_add(C "${ARGV}")
    check_flags_add(CXX "${ARGV}")
endmacro()

check_both_flags_add(PREPEND -Wextra -Wformat -Wformat-security)

if(MSVC)
    check_both_flags_add(PREPEND /W3)
    check_both_flags_add(/MP)
    check_both_flags_add(TYPE DEBUG /Od)
else()
    check_both_flags_add(PREPEND -Wall)
    check_both_flags_add(-flto)
    option(NATIVE "Build for native performance (march=native)")
    if(NATIVE)
        check_both_flags_add(-march=native)
    endif()
    if(COVERAGE)
        check_both_flags_add(--coverage)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
    if(MINGW)
        check_both_flags_add(-mxsave -fno-asynchronous-unwind-tables)
    else()
        check_both_flags_add(-fstack-protector-strong)
    endif()
endif()

if(CMAKE_C_FLAGS MATCHES ".*-march=native.*" OR CMAKE_CXX_FLAGS MATCHES ".*-march=native.*")
    check_both_flags_add(-mno-avx)
endif()

if(CMAKE_C_FLAGS MATCHES "-O" AND NOT CMAKE_C_FLAGS MATCHES "-O0" AND NOT MINGW)
    add_definitions(-D_FORTIFY_SOURCE=2)
endif()

if(CMAKE_ASM_NASM_OBJECT_FORMAT MATCHES "win")
    set(CMAKE_ASM_NASM_FLAGS_DEBUG "${CMAKE_ASM_NASM_FLAGS_DEBUG} -gcv8")
elseif(CMAKE_ASM_NASM_COMPILER MATCHES "nasm")
    set(CMAKE_ASM_NASM_FLAGS_DEBUG "${CMAKE_ASM_NASM_FLAGS_DEBUG} -gdwarf")
elseif(CMAKE_ASM_NASM_COMPILER MATCHES "yasm")
    if(CMAKE_ASM_NASM_OBJECT_FORMAT MATCHES "macho")
        set(CMAKE_ASM_NASM_FLAGS_DEBUG "${CMAKE_ASM_NASM_FLAGS_DEBUG} -gnull")
    else()
        set(CMAKE_ASM_NASM_FLAGS_DEBUG "${CMAKE_ASM_NASM_FLAGS_DEBUG} -gdwarf2")
    endif()
endif()

include(CheckSymbolExists)

# ASM compiler macro
macro(ASM_COMPILE_TO_TARGET target)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        if(CMAKE_BUILD_TYPE MATCHES Release)
            set(ASM_FLAGS "${CMAKE_ASM_NASM_FLAGS_RELEASE}")
        elseif(CMAKE_BUILD_TYPE MATCHES Debug)
            set(ASM_FLAGS "${CMAKE_ASM_NASM_FLAGS_DEBUG}")
        elseif(CMAKE_BUILD_TYPE MATCHES MinSizeRel)
            set(ASM_FLAGS "${CMAKE_ASM_NASM_FLAGS_MINSIZEREL}")
        elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
            set(ASM_FLAGS "${CMAKE_ASM_NASM_FLAGS_RELWITHDEBINFO}")
        endif()

        get_property(inc_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
        foreach(inc_dir ${inc_dirs})
            set(ASM_FLAGS "${ASM_FLAGS} -I${inc_dir}")
        endforeach()

        string(REGEX REPLACE " " ";" ASM_FLAGS "${ASM_FLAGS} ${CMAKE_ASM_NASM_FLAGS}")

        foreach(asm_file ${ARGN})
            get_filename_component(filename "${asm_file}" NAME_WE)
            set(SIMD_OBJ "${CMAKE_CURRENT_BINARY_DIR}/${filename}${CMAKE_C_OUTPUT_EXTENSION}")
            message(STATUS "Pregenerating ASM_NASM file ${filename}${CMAKE_C_OUTPUT_EXTENSION}")
            execute_process(COMMAND ${CMAKE_ASM_NASM_COMPILER}
                    -f${CMAKE_ASM_NASM_OBJECT_FORMAT}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -o${SIMD_OBJ}
                    ${ASM_FLAGS}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${asm_file}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
            list(APPEND SIMD_OBJS "${SIMD_OBJ}")
        endforeach()
        #target_sources(${target} PUBLIC ${SIMD_OBJS})
        target_sources(common_lib INTERFACE ${SIMD_OBJS})
    else()
        # For every other generator
        foreach(asm_file ${ARGN})
            get_filename_component(filename "${asm_file}" ABSOLUTE)
            target_sources(${target} PUBLIC ${filename})
        endforeach()
    endif()
endmacro()

# Find out if we have threading available
set(CMAKE_THREAD_PREFER_PTHREADS ON)
find_package(Threads)

macro(add_clike_and_ld_flags)
    string(REPLACE ";" " " add_clike_and_ld_flags "${ARGV}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${add_clike_and_ld_flags}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${add_clike_and_ld_flags}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${add_clike_and_ld_flags}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${add_clike_and_ld_flags}")
endmacro()

string(TOLOWER "${SANITIZER}" SANITIZER)

# Address Memory Thread Undefined
if(SANITIZER STREQUAL "address")
    add_clike_and_ld_flags(-fsanitize=address)
elseif(SANITIZER STREQUAL "memory")
    add_clike_and_ld_flags(-fsanitize=memory -fsanitize-memory-track-origins)
elseif(SANITIZER STREQUAL "thread")
    add_clike_and_ld_flags(-fsanitize=thread)
elseif(SANITIZER STREQUAL "undefined")
    add_clike_and_ld_flags(-fsanitize=undefined)
elseif(SANITIZER STREQUAL "integer")
    add_clike_and_ld_flags(-fsanitize=integer)
elseif(SANITIZER)
    message(FATAL_ERROR "Unknown sanitizer: ${SANITIZER}")
endif()

if(SANITIZER)
    check_both_flags_add(-fno-omit-frame-pointer -fno-optimize-sibling-calls)
endif()

if(MSVC OR MINGW)
#"-Wall" too big restrict for MSVC
else()
check_both_flags_add(-Wall -Werror)
endif()

set(SVT_JPEGXS_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(SVT_JPEGXS_LIBDIR "${CMAKE_INSTALL_LIBDIR}")

if(NOT IS_ABSOLUTE "${SVT_JPEGXS_INCLUDEDIR}")
    string(PREPEND SVT_JPEGXS_INCLUDEDIR "\${prefix}/")
endif()
if(NOT IS_ABSOLUTE "${SVT_JPEGXS_LIBDIR}")
    string(PREPEND SVT_JPEGXS_LIBDIR "\${exec_prefix}/")
endif()

# Add Subdirectories
add_subdirectory(Source/Lib)

if(BUILD_APPS)
    add_subdirectory(Source/App/DecApp)
    add_subdirectory(Source/App/EncApp)
    add_subdirectory(Source/App/SampleEncoder)
    add_subdirectory(Source/App/SampleDecoder)
endif()

if(BUILD_TESTING)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest-1.14.0)

    add_subdirectory(tests/UnitTests)
    #enable_testing()
endif()

add_subdirectory(third_party/cpuinfo)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/Source/API/ DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/svt-jpegxs" FILES_MATCHING PATTERN "*.h")
