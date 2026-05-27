#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Common library for FFmpeg plugin tests
# Reuses CommonLib.sh and adds FFmpeg-specific helpers.
#
# Usage: source ./FFmpegCommonLib.sh
# Arguments are the same as CommonLib.sh:
#   $1: path to test resources
#   $2: path to ffmpeg binary (compiled with --enable-libsvtjpegxs)


# Source the base common library (provides: path_global, exec_dec, valgrind,
# range_min, range_max, tmp_dir, run_fast, test_id, common_lib_update_test_id_run_return_1_to_ignore,
# common_lib_end_summary, etc.)
source ./CommonLib.sh

# FFmpeg-specific: alias exec_dec as exec_ffmpeg for readability
exec_ffmpeg=$exec_dec

# Helper: get pixel format string for ffmpeg from resolution description
# Usage: get_ffmpeg_pix_fmt "8" "YUV422" -> "yuv422p"
#        get_ffmpeg_pix_fmt "10" "YUV420" -> "yuv420p10le"
function get_ffmpeg_pix_fmt() {
    local bit_depth=$1
    local colour_format=$2

    case "${colour_format}" in
        YUV420)
            case "${bit_depth}" in
                8*) echo "yuv420p" ;;
                10*) echo "yuv420p10le" ;;
                12*) echo "yuv420p12le" ;;
                14*) echo "yuv420p14le" ;;
                *) echo "yuv420p" ;;
            esac
            ;;
        YUV422)
            case "${bit_depth}" in
                8*) echo "yuv422p" ;;
                10*) echo "yuv422p10le" ;;
                12*) echo "yuv422p12le" ;;
                14*) echo "yuv422p14le" ;;
                *) echo "yuv422p" ;;
            esac
            ;;
        YUV444)
            case "${bit_depth}" in
                8*) echo "yuv444p" ;;
                10*) echo "yuv444p10le" ;;
                12*) echo "yuv444p12le" ;;
                14*) echo "yuv444p14le" ;;
                *) echo "yuv444p" ;;
            esac
            ;;
        *)
            echo "yuv422p"
            ;;
    esac
}
