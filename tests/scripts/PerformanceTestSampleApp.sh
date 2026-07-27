#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# PerformanceTestSampleApp.sh
#
# Phase 1 performance harness for SvtJpegxsEncApp/SvtJpegxsDecApp. For each
# stream/BPP combination: encodes the source .yuv to a .jxs bitstream on
# RAMDISK, then decodes that .jxs (to /dev/null, so disk I/O never throttles
# decode speed), logging both measurements to CSV. RAMDISK avoids disk I/O,
# numactl pins runs to a single NUMA node, and 1 warm-up + 3 measured runs
# (median FPS) smooth out turbo/cache noise. FPS is parsed from the apps'
# own "average X.XX[fps]" summary (same format for both), not shell
# wall-clock time, so process start/teardown overhead isn't counted.
#
# Prerequisites: numactl. Not checked here - see CI setup / documentation.
# Usage: ./PerformanceTestSampleApp.sh [samples_dir]
#   samples_dir: path to the conformance test resources (must contain encoder_tests/ and 4k/).
#                Defaults to ../../Conformance-tests (local dev symlink), so a bare invocation
#                still works when run manually on this machine. In CI, pass /opt/samples (or
#                wherever the pre-staged data lives on the runner) explicitly instead.
# Example: ./PerformanceTestSampleApp.sh
# Example: ./PerformanceTestSampleApp.sh /opt/samples

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
ENC_APP="$SCRIPT_DIR/../../Bin/Release/SvtJpegxsEncApp"
DEC_APP="$SCRIPT_DIR/../../Bin/Release/SvtJpegxsDecApp"
SAMPLES_DIR="${1:-$SCRIPT_DIR/../../Conformance-tests}"
CSV_FILE="$SCRIPT_DIR/svt_app_results.csv"
RAMDISK_YUV="/dev/shm/test_stream.yuv"
RAMDISK_JXS="/dev/shm/test_stream.jxs"
NUMA_NODE=1
FRAMES=2000

# Cleanup on exit
trap 'rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"' EXIT

# Ensure CSV header exists
if [ ! -f "$CSV_FILE" ]; then
    echo "Operation,Command,StreamName,Resolution,BPP,Threads,TargetFPS,Result_FPS,Status" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|TargetFPS|BPP|Threads|SourceFile
# SourceFile is relative to SAMPLES_DIR. The 4k file is a single extracted
# frame (see Conformance-tests/4k/) - -n loops it up to FRAMES. Threads is
# passed to both apps as --lp, to compare FPS scaling across thread counts.
MATRIX=(
    # 1080p - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|32|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|64|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|96|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"

    # 1080p - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|32|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|64|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|96|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"

    # 4K - 1.5 BPP Thread Scaling
   # "4K60_422p10|3840|2160|10|yuv422|60|1.5|1|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
   # "4K60_422p10|3840|2160|10|yuv422|60|1.5|4|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
   # "4K60_422p10|3840|2160|10|yuv422|60|1.5|8|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
   # "4K60_422p10|3840|2160|10|yuv422|60|1.5|16|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"

    # 4K - 3.0 BPP Thread Scaling
    #"4K60_422p10|3840|2160|10|yuv422|60|3.0|1|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
    #"4K60_422p10|3840|2160|10|yuv422|60|3.0|4|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
    #"4K60_422p10|3840|2160|10|yuv422|60|3.0|8|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
    #"4K60_422p10|3840|2160|10|yuv422|60|3.0|16|4k/merged_stream_3840x2160_2997fps_10bit_422.yuv"
)

# run_measured  cmd...
#   Runs 1 unmeasured warm-up + 3 measured passes of "$@", printing the
#   median FPS parsed from the app's "average X.XX[fps]" summary to stdout,
#   or nothing if every measured run failed to produce a parseable FPS.
function run_measured() {
    "$@" > /dev/null 2>&1 || true

    local fps_list=() run output fps
    for run in 1 2 3; do
        output=$("$@" 2>&1) || true
        fps=$(echo "$output" | grep -oE 'average [0-9]+\.[0-9]+\[fps\]' | awk '{print $2}' | tr -d '[fps]') || true
        if [ -n "$fps" ]; then
            fps_list+=("$fps")
        fi
    done

    if [ ${#fps_list[@]} -gt 0 ]; then
        printf '%s\n' "${fps_list[@]}" | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}'
    fi
}

for test_case in "${MATRIX[@]}"; do
    IFS='|' read -r name w h depth fmt target_fps bpp threads file <<< "$test_case"
    source_path="$SAMPLES_DIR/$file"

    if [ ! -f "$source_path" ]; then
        echo "ERROR: File $source_path not found!"
        echo "ENCODE,N/A,$name,${w}x${h},$bpp,$threads,$target_fps,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,N/A,$name,${w}x${h},$bpp,$threads,$target_fps,N/A,FAIL" >> "$CSV_FILE"
        continue
    fi
    cp -f "$source_path" "$RAMDISK_YUV"

    # --- Encode: produces the .jxs bitstream the decode step consumes ---
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$ENC_APP" -i "$RAMDISK_YUV" -b "$RAMDISK_JXS" -w "$w" -h "$h" \
        --input-depth "$depth" --colour-format "$fmt" --bpp "$bpp" -n "$FRAMES" --lp "$threads")
    enc_cmd=$(printf '%q ' "${enc_cmd_arr[@]}")
    enc_fps=$(run_measured "${enc_cmd_arr[@]}")

    if [ -n "$enc_fps" ]; then
        echo "ENCODE,\"$enc_cmd\",$name,${w}x${h},$bpp,$threads,$target_fps,$enc_fps,PASS" >> "$CSV_FILE"
        echo "SUCCESS: ENCODE $name (bpp=$bpp, threads=$threads) -> $enc_fps FPS"
    else
        echo "ENCODE,\"$enc_cmd\",$name,${w}x${h},$bpp,$threads,$target_fps,N/A,FAIL" >> "$CSV_FILE"
        echo "FAILED: ENCODE $name (bpp=$bpp, threads=$threads)"
    fi

    # --- Decode: only meaningful if the bitstream was actually produced ---
    if [ ! -s "$RAMDISK_JXS" ]; then
        echo "ERROR: No bitstream produced by encode step, skipping decode."
        echo "DECODE,N/A,$name,${w}x${h},$bpp,$threads,$target_fps,N/A,FAIL" >> "$CSV_FILE"
        rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
        continue
    fi

    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$DEC_APP" -i "$RAMDISK_JXS" -o /dev/null -n "$FRAMES" --lp "$threads")
    dec_cmd=$(printf '%q ' "${dec_cmd_arr[@]}")
    dec_fps=$(run_measured "${dec_cmd_arr[@]}")

    if [ -n "$dec_fps" ]; then
        echo "DECODE,\"$dec_cmd\",$name,${w}x${h},$bpp,$threads,$target_fps,$dec_fps,PASS" >> "$CSV_FILE"
        echo "SUCCESS: DECODE $name (bpp=$bpp, threads=$threads) -> $dec_fps FPS"
    else
        echo "DECODE,\"$dec_cmd\",$name,${w}x${h},$bpp,$threads,$target_fps,N/A,FAIL" >> "$CSV_FILE"
        echo "FAILED: DECODE $name (bpp=$bpp, threads=$threads)"
    fi

    rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
done
