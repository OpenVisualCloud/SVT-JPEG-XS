#!/usr/bin/cmake -P
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# cmake -P Build/linux/PGO/pgohelper.cmake $PWD/Build /opt/samples $PWD/Bin/Release/SvtJpegxsEncApp $PWD/Bin/Release/SvtJpegxsDecApp

if(CMAKE_ARGC LESS 7)
    message(
        FATAL_ERROR
            "Usage: cmake -P ${CMAKE_ARGV2} build_dir /path/to/samples_root /path/to/SvtJpegxsEncApp /path/to/SvtJpegxsDecApp"
    )
endif()

set(BUILD_DIRECTORY "${CMAKE_ARGV3}")
set(SAMPLES_DIRECTORY "${CMAKE_ARGV4}")
set(ENC_APP "${CMAKE_ARGV5}")
set(DEC_APP "${CMAKE_ARGV6}")

# Single-frame seed stills, one per (resolution, bit depth, colour format) config, committed
# in-repo alongside this script (see .gitignore's Build/linux/PGO override) instead of pulled
# from the private /opt/samples corpus at build time: ~83MB total vs. the 1.2GB+ that corpus's
# full-length clips would cost, and it means RunPGO works from a plain `git clone` with no
# external samples at all for the encoder-training half below. The decode-only pass further
# down still reads the official conformance corpus from SAMPLES_DIRECTORY, since that one can't
# be approximated by a handful of committed stills without losing most of its point.
set(PGO_SEED_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT EXISTS ${ENC_APP})
    message(FATAL_ERROR "Can't run pgo if the encoder app doesn't exist. Looked at ${ENC_APP}")
endif()
if(NOT EXISTS ${DEC_APP})
    message(FATAL_ERROR "Can't run pgo if the decoder app doesn't exist. Looked at ${DEC_APP}")
endif()

# Delete any existing Clang profiling data.
file(GLOB OLD_FILES
    "${BUILD_DIRECTORY}/*.profraw"
    "${BUILD_DIRECTORY}/*.profdata"
)
if(OLD_FILES)
    file(REMOVE ${OLD_FILES})
endif()

# name::width::height::depth::colour-format::bpp::frames::seed-file-under-Build/linux/PGO
#
# Encoder side: run the *encoder* over enough distinct (resolution, bit depth, colour format)
# combinations that the profile reflects more than one code shape, then decode what came out so
# the decoder gets the same coverage on content it didn't fully control.
#
# Each entry's seed file is exactly one frame. JPEG XS has no cross-frame prediction to train
# around (checked: no motion/temporal state anywhere in RateControl.c/PreRcStageProcess.c, and
# this encoder never emits the one JPEG XS profile family - TDC - that would need a frame-buffer
# bandwidth bound for referencing a previous frame's buffer; see ProfileLevel.h), so rate-control
# convergence over clip_frames frames doesn't need clip_frames *distinct* frames of source video
# - only the input actually fed to EncApp needs to be that long. The loop below builds that
# explicitly (concatenate the one seed frame clip_frames times into a real multi-frame file)
# rather than leaning on EncApp's own short-input rewind behaviour to make up the difference.
#
# 1080p_422_8b/1080p_422_10b/1080p_420_10b/720p_420_8b match
# tests/scripts/PerformanceTestSampleApp.sh's own MATRIX. The 4k_*/12bit_420 entries add
# resolution/depth/format coverage (4K, 12-bit, 4:4:4) that those four don't exercise on their
# own; their seed stills come from the official JPEG XS conformance corpus's reference_decode/
# fixtures (single-frame already, no trimming needed).
#
# yuv400 (mono) and a deliberately too-narrow (32px) clip were tried here and dropped: EncApp's
# own image-reading path rejects yuv400 ("Unsupported format") and the 32-wide clip needs a
# --decomp_h below its default before the encoder will even attempt it, and still fails
# mid-frame - both are app/encoder limitations, not something a training corpus should paper over.
set(TRAINING_CLIPS
    "1080p_422_8b::1920::1080::8::yuv422::3.0::30::1080p_422_8b.yuv"
    "1080p_422_10b::1920::1080::10::yuv422::3.0::30::1080p_422_10b.yuv"
    "1080p_420_10b::1920::1080::10::yuv420::1.5::30::1080p_420_10b.yuv"
    "720p_420_8b::1280::720::8::yuv420::1.5::30::720p_420_8b.yuv"
    "4k_422_8b::4064::2704::8::yuv422::3.0::30::4k_422_8b.yuv"
    "4k_444_10b::4096::1744::10::yuv444::6.0::30::4k_444_10b.yuv"
    "12bit_420::976::650::12::yuv420::2.0::30::12bit_420.yuv"
)

foreach(clip IN LISTS TRAINING_CLIPS)
    string(REPLACE "::" ";" fields "${clip}")
    list(GET fields 0 clip_name)
    list(GET fields 1 clip_w)
    list(GET fields 2 clip_h)
    list(GET fields 3 clip_depth)
    list(GET fields 4 clip_fmt)
    list(GET fields 5 clip_bpp)
    list(GET fields 6 clip_frames)
    list(GET fields 7 clip_file)

    set(clip_seed_frame "${PGO_SEED_DIR}/${clip_file}")
    if(NOT EXISTS ${clip_seed_frame})
        message(WARNING "Skipping ${clip_name}: ${clip_seed_frame} not found")
        continue()
    endif()

    set(clip_input "${BUILD_DIRECTORY}/pgo_${clip_name}_input.yuv")
    set(seed_frame_repeated "")
    foreach(unused RANGE 1 ${clip_frames})
        list(APPEND seed_frame_repeated ${clip_seed_frame})
    endforeach()
    execute_process(
        COMMAND cat ${seed_frame_repeated}
        OUTPUT_FILE ${clip_input}
        COMMAND_ERROR_IS_FATAL ANY
    )

    set(clip_bitstream "${BUILD_DIRECTORY}/pgo_${clip_name}.jxs")

    set(ENCODING_COMMAND ${ENC_APP} -i ${clip_input} -b ${clip_bitstream}
        -w ${clip_w} -h ${clip_h} --input-depth ${clip_depth} --colour-format ${clip_fmt}
        --bpp ${clip_bpp} -n ${clip_frames} --lp 8 --asm max --no-progress 1)
    list(JOIN ENCODING_COMMAND " " ENCODING_COMMAND_STR)
    message(STATUS "Running ${ENCODING_COMMAND_STR}")
    execute_process(COMMAND ${ENCODING_COMMAND})

    set(DECODING_COMMAND ${DEC_APP} -i ${clip_bitstream} -o /dev/null -n ${clip_frames} --lp 8 --asm max)
    list(JOIN DECODING_COMMAND " " DECODING_COMMAND_STR)
    message(STATUS "Running ${DECODING_COMMAND_STR}")
    execute_process(COMMAND ${DECODING_COMMAND})

    file(REMOVE ${clip_input} ${clip_bitstream})
endforeach()

# Decoder side, independent of the above: decode every bitstream in the official conformance
# corpus (tests/scripts/DecoderConformanceTest.sh's own fixtures). This is the one part of the
# profile that needs no guessing about encode parameters at all - the bitstreams are
# self-describing - and between them they cover close to the full matrix of profiles, levels,
# colour formats, bit depths and resolutions (including 4K+) that DecApp has to handle, none of
# which the encoder-produced bitstreams above are guaranteed to touch. Unlike the encoder-side
# seeds, this pass still needs SAMPLES_DIRECTORY - it isn't practical to approximate 85 official
# conformance bitstreams with a handful of committed stills without losing most of its point.
file(GLOB CONFORMANCE_BITSTREAMS "${SAMPLES_DIRECTORY}/test_bitsreams/*.jxs")
if(CONFORMANCE_BITSTREAMS)
    foreach(bitstream IN LISTS CONFORMANCE_BITSTREAMS)
        set(DECODING_COMMAND ${DEC_APP} --find-bitstream-header -i ${bitstream} -o /dev/null
            --lp 8 --asm max --packetization-mode 0)
        list(JOIN DECODING_COMMAND " " DECODING_COMMAND_STR)
        message(STATUS "Running ${DECODING_COMMAND_STR}")
        execute_process(COMMAND ${DECODING_COMMAND})
    endforeach()
else()
    message(WARNING "Skipping conformance decode pass: no *.jxs found under ${SAMPLES_DIRECTORY}/test_bitsreams")
endif()
