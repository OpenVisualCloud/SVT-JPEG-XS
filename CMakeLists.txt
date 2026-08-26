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

set(CMAKE_C_VISIBILITY_PRESET hidden)
# Default build type to release if the generator does not has its own set of build types
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "32-bit is not supported")
endif()

include(CheckCSourceCompiles)

# Target architecture. ARCH_X86_64 and ARCH_AARCH64 select which SIMD trees are
# compiled and are exported to the sources as compile definitions of the same
# name. An unknown processor is not an error: the portable C paths build and run
# everywhere, only without the vectorized implementations.
if(CMAKE_OSX_ARCHITECTURES)
    set(SVT_JXS_TARGET_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(SVT_JXS_TARGET_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(SVT_JXS_TARGET_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(ARCH_X86_64 1)
elseif(SVT_JXS_TARGET_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(ARCH_AARCH64 1)
else()
    message(WARNING "Unrecognized target processor \"${SVT_JXS_TARGET_PROCESSOR}\": "
                    "building the portable C implementations only")
endif()
message(STATUS "Target processor: ${SVT_JXS_TARGET_PROCESSOR}")

# Architecture-specific object libraries, split the way the sample apps consume
# them: each links only its own half so that the linker still catches a stray
# reference from the decoder into the encoder and back. Empty on a processor
# with no SIMD tree of its own.
set(SVT_JXS_SIMD_TARGETS_COMMON "")
set(SVT_JXS_SIMD_TARGETS_ENCODER "")
set(SVT_JXS_SIMD_TARGETS_DECODER "")
if(ARCH_X86_64)
    set(SVT_JXS_SIMD_TARGETS_COMMON COMMON_ASM_SSE2 COMMON_ASM_AVX2)
    set(SVT_JXS_SIMD_TARGETS_ENCODER ENCODER_ASM_SSE2 ENCODER_ASM_SSE4_1 ENCODER_ASM_AVX2 ENCODER_ASM_AVX512)
    set(SVT_JXS_SIMD_TARGETS_DECODER DECODER_ASM_SSE4_1 DECODER_ASM_AVX2 DECODER_ASM_AVX512)
elseif(ARCH_AARCH64)
    set(SVT_JXS_SIMD_TARGETS_ENCODER ENCODER_ASM_NEON)
    set(SVT_JXS_SIMD_TARGETS_DECODER DECODER_ASM_NEON)
endif()

foreach(simd_part COMMON ENCODER DECODER)
    set(SVT_JXS_SIMD_OBJECTS_${simd_part} "")
    foreach(simd_target IN LISTS SVT_JXS_SIMD_TARGETS_${simd_part})
        list(APPEND SVT_JXS_SIMD_OBJECTS_${simd_part} "$<TARGET_OBJECTS:${simd_target}>")
    endforeach()
endforeach()

if(ARCH_X86_64)
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
        if(NASM_VERSION VERSION_LESS "2.14" AND CMAKE_ASM_NASM_COMPILER MATCHES "nasm")
            message(FATAL_ERROR "Found nasm is too old (requires at least 2.14, found ${NASM_VERSION})!")
        endif()
    endif()
    enable_language(ASM_NASM)
    add_definitions(-DARCH_X86_64=1)
elseif(ARCH_AARCH64)
    add_definitions(-DARCH_AARCH64=1)
endif()

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
        set(CMAKE_LINKER  "llvm-ld")
    endif()
    if(LLVM_AR_EXE)
        set(CMAKE_AR      "llvm-ar")
    endif()
    if(LLVM_RANLIB_EXE)
        set(CMAKE_RANLIB  "llvm-ranlib")
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

option(COVERAGE "Generate coverage report")
if(NOT MSVC)
    option(JPEGXS_PGO "Enable profile guided optimization. Creates the RunPGO and CompilePGO targets")
    # options for internal use
    option(JPEGXS_INTERNAL_PGO_GENERATE "Generate a profile guided optimization profile")
    option(JPEGXS_INTERNAL_PGO_USE "Use a profile for profile guided optimization")
    mark_as_advanced(JPEGXS_INTERNAL_PGO_GENERATE JPEGXS_INTERNAL_PGO_USE)
    set(JPEGXS_PGO_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Path to the directory passed to -fprofile-dir for use with profile guided optimization")
    set(JPEGXS_PGO_SAMPLES_DIR "" CACHE PATH "Path to the samples root (containing encoder_tests/, reference_decode/, test_bitsreams/) to run PGO training on instead of the default corpus")
else()
    set(JPEGXS_PGO OFF)
endif()
option(BUILD_APPS "Build Enc and Dec Apps" ON)
if(JPEGXS_PGO AND NOT BUILD_APPS)
    message(WARNING "PGO needs the apps to train on - disabling JPEGXS_PGO since BUILD_APPS is off.")
    set(JPEGXS_PGO OFF)
endif()

# PGO defaults on (see build.sh), so a missing llvm-profdata shouldn't be a hard configure
# failure - fall back through the versioned binary name every distro actually ships (matching
# the installed Clang's own major version, e.g. llvm-profdata-21 alongside clang-21) before
# giving up and just disabling PGO instead of failing the whole configure over a tool most
# people never asked for by name.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND JPEGXS_PGO)
    find_program(LLVM_PROFDATA llvm-profdata)
    if(NOT LLVM_PROFDATA)
        string(REGEX MATCH "^[0-9]+" clang_major_version "${CMAKE_C_COMPILER_VERSION}")
        find_program(LLVM_PROFDATA "llvm-profdata-${clang_major_version}")
    endif()
    if(NOT LLVM_PROFDATA)
        message(WARNING "Neither llvm-profdata nor llvm-profdata-${clang_major_version} found - disabling JPEGXS_PGO.")
        set(JPEGXS_PGO OFF)
    endif()
endif()

if(JPEGXS_INTERNAL_PGO_USE)
    set(PGO_COMPILER_FLAGS "-fprofile-use=${JPEGXS_PGO_DIR}")
    set(PGO_LINKER_FLAGS "-fprofile-use=${JPEGXS_PGO_DIR}")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # We want unprofiled code optimised for speed, not size, so we mark the profiling data as "partial".
        set(PGO_COMPILER_FLAGS "${PGO_COMPILER_FLAGS} -fprofile-correction -fprofile-partial-training")
        set(PGO_LINKER_FLAGS "${PGO_LINKER_FLAGS} -fprofile-correction -fprofile-partial-training")
        # -fprofile-use is applied via CMAKE_C_FLAGS/CMAKE_CXX_FLAGS below, i.e. to every target in
        # the project, but RunPGO only ever trains SvtJpegxsEncApp/SvtJpegxsDecApp - anything else
        # that gets built afterwards under this same configure (SampleEncoder/SampleDecoder, unit
        # tests) has no matching .gcda. GCC's -Wmissing-profile then turns that into a hard
        # -Werror failure instead of the intended fallback to non-profile-guided compilation for
        # that one translation unit - downgrade it back to a (harmless) warning.
        set(PGO_COMPILER_FLAGS "${PGO_COMPILER_FLAGS} -Wno-error=missing-profile")
    endif()
elseif(JPEGXS_INTERNAL_PGO_GENERATE)
    set(PGO_COMPILER_FLAGS "-fprofile-generate=${JPEGXS_PGO_DIR}")
    set(PGO_LINKER_FLAGS "-fprofile-generate=${JPEGXS_PGO_DIR}")
endif()

if(ARCH_X86_64)
    if(WIN32)
        set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -DWIN64")
    else()
        set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -DUNIX64")
    endif()
endif()

if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie -z noexecstack -z relro -z now")
endif()

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

if((CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "9.0") OR
   (CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "12.0") OR
   MSVC)
    option(JPEGXS_LTO "Attempt to enable Link Time Optimization if available" ON)
else()
    option(JPEGXS_LTO "Attempt to enable Link Time Optimization if available" OFF)
endif()
if(JPEGXS_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT jpegxs_ipo_supported OUTPUT jpegxs_ipo_error)
    if(jpegxs_ipo_supported AND NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "LTO/IPO: enabled (${CMAKE_C_COMPILER_ID})")
    elseif(jpegxs_ipo_supported)
        message(STATUS "LTO/IPO: supported, but CMAKE_INTERPROCEDURAL_OPTIMIZATION was already set to ${CMAKE_INTERPROCEDURAL_OPTIMIZATION} - leaving it alone")
    else()
        message(STATUS "LTO/IPO: not supported by this compiler/generator - ${jpegxs_ipo_error}")
    endif()
else()
    message(STATUS "LTO/IPO: disabled (JPEGXS_LTO=OFF)")
endif()

check_both_flags_add(PREPEND -Wextra -Wformat -Wformat-security)

if(MSVC)
    check_both_flags_add(PREPEND /W3)
    check_both_flags_add(/MP)
    check_both_flags_add(TYPE DEBUG /Od)
else()
    check_both_flags_add(PREPEND -Wall)
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
    # -fsanitize-recover=address: let halt_on_error=0 continue past each finding so
    # one non-halting pass enumerates every issue instead of aborting at the first.
    add_clike_and_ld_flags(-fsanitize=address -fsanitize-recover=address)
elseif(SANITIZER STREQUAL "memory")
    # -fsanitize-recover=memory: same, otherwise MSan aborts at the first uninit read.
    add_clike_and_ld_flags(-fsanitize=memory -fsanitize-memory-track-origins -fsanitize-recover=memory)
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
    # -g: without debug info the sanitizer reports carry no file:line, only symbol names,
    # which makes every finding in a large function look like it is at the function's
    # opening brace. Costs binary size only, not runtime.
    check_both_flags_add(-g -fno-omit-frame-pointer -fno-optimize-sibling-calls)
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

# PGO only ever instruments/trains SvtJpegxsLib + SvtJpegxsEncApp/SvtJpegxsDecApp (that's the
# scope RunPGO's PGOCompileGen/PGOCompileUse rebuild), so the -fprofile-generate/-fprofile-use
# flags are applied bracketing just those add_subdirectory() calls, not globally - otherwise every
# target added afterwards (SampleEncoder/SampleDecoder, unit tests, third_party) would compile
# under -fprofile-use with no matching profile at all, tripping a -Wmissing-profile warning on
# every single file for a profile they were never meant to have in the first place.
if(JPEGXS_PGO)
    set(JPEGXS_PRE_PGO_C_FLAGS "${CMAKE_C_FLAGS}")
    set(JPEGXS_PRE_PGO_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(JPEGXS_PRE_PGO_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    set(JPEGXS_PRE_PGO_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    set(JPEGXS_PRE_PGO_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PGO_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PGO_COMPILER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${PGO_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${PGO_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${PGO_LINKER_FLAGS}")
endif()

add_subdirectory(Source/Lib)

if(BUILD_APPS)
    add_subdirectory(Source/App/DecApp)
    add_subdirectory(Source/App/EncApp)

    if(JPEGXS_PGO)
        set(CMAKE_C_FLAGS "${JPEGXS_PRE_PGO_C_FLAGS}")
        set(CMAKE_CXX_FLAGS "${JPEGXS_PRE_PGO_CXX_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${JPEGXS_PRE_PGO_EXE_LINKER_FLAGS}")
        set(CMAKE_SHARED_LINKER_FLAGS "${JPEGXS_PRE_PGO_SHARED_LINKER_FLAGS}")
        set(CMAKE_MODULE_LINKER_FLAGS "${JPEGXS_PRE_PGO_MODULE_LINKER_FLAGS}")
    endif()

    add_subdirectory(Source/App/SampleEncoder)
    add_subdirectory(Source/App/SampleDecoder)
endif()
# JPEGXS_PGO AND NOT BUILD_APPS is already disabled earlier (PGO needs the apps to train on), so
# there's no case where the flags applied above for Source/Lib need restoring outside the
# BUILD_APPS branch.

if(BUILD_TESTING)
    include(CTest)
    target_compile_definitions(COMMON_CODEC PRIVATE BUILD_TESTING)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest-1.14.0)

    add_subdirectory(tests/UnitTests)
endif()

add_subdirectory(third_party/cpuinfo)

if(JPEGXS_PGO)
    set(JPEGXS_PGO_SAMPLES_DIR_RESOLVED "${JPEGXS_PGO_SAMPLES_DIR}")
    if(NOT JPEGXS_PGO_SAMPLES_DIR_RESOLVED)
        set(JPEGXS_PGO_SAMPLES_DIR_RESOLVED "/opt/samples")
    endif()

    add_custom_target(PGOCompileGen
        ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} -DJPEGXS_INTERNAL_PGO_GENERATE=ON -DJPEGXS_INTERNAL_PGO_USE=OFF
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
        --clean-first
        --target SvtJpegxsEncApp --target SvtJpegxsDecApp
        COMMENT "Compiling with PGO generate"
        VERBATIM
        USES_TERMINAL
    )

    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # We want unprofiled code optimised for speed, not size, so we mark the profiling data as "sparse".
        set(PROFILE_MERGE_CMD sh -c "${LLVM_PROFDATA} merge --sparse=true *.profraw -o default.profdata")
    else()
        # Do nothing. GCC has no profile merging step.
        set(PROFILE_MERGE_CMD)
    endif()

    add_custom_target(PGOCompileUse
        ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} -DJPEGXS_INTERNAL_PGO_GENERATE=OFF -DJPEGXS_INTERNAL_PGO_USE=ON
        COMMAND ${PROFILE_MERGE_CMD}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
        --clean-first
        --target SvtJpegxsEncApp --target SvtJpegxsDecApp
        COMMENT "Compiling PGO use"
        COMMAND_EXPAND_LISTS
        VERBATIM
        USES_TERMINAL
    )

    add_custom_target(PGOGenerateProfile ${CMAKE_COMMAND} -P
        ${PROJECT_SOURCE_DIR}/Build/linux/PGO/pgohelper.cmake
        ${CMAKE_BINARY_DIR}
        ${JPEGXS_PGO_SAMPLES_DIR_RESOLVED}
        $<TARGET_FILE:SvtJpegxsEncApp>
        $<TARGET_FILE:SvtJpegxsDecApp>
        COMMENT "Generating PGO profile"
        VERBATIM
        USES_TERMINAL
    )

    add_custom_target(RunPGO
        ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target PGOCompileGen
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target PGOGenerateProfile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target PGOCompileUse
        COMMENT "Compiling PGO Binary"
        VERBATIM
        USES_TERMINAL
    )
endif()

install(DIRECTORY ${PROJECT_SOURCE_DIR}/Source/API/ DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/svt-jpegxs" FILES_MATCHING PATTERN "*.h")
