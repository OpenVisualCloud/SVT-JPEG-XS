#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# PerformanceTestFfmpegPlugin.sh
#
# Phase 2 performance harness for the ffmpeg jpegxs plugin (encoder:
# ffmpeg-plugin/libsvtjpegxsenc.c, decoder: libsvtjpegxsdec.c), sibling of
# PerformanceTestSampleApp.sh (Phase 1, which drives SvtJpegxsEncApp/
# SvtJpegxsDecApp directly). Same matrix, same technique, same CSV shape -
# just swaps the app binaries for `ffmpeg -c:v libsvtjpegxs` commands, so
# the two harnesses' Result_FPS columns are directly comparable.
#
# For each stream/BPP combination: encodes the source .yuv to a .jxs
# bitstream on RAMDISK via ffmpeg (`-stream_loop -1` loops the source up to
# FRAMES frames), then decodes that .jxs (to /dev/null, so disk I/O never
# throttles decode speed) via ffmpeg's jpegxs_pipe demuxer, logging both
# measurements to CSV. RAMDISK avoids disk I/O, numactl pins runs to a
# single NUMA node, and 1 warm-up + 3 measured runs (median FPS) smooth out
# turbo/cache noise. FPS is computed from ffmpeg's own "-benchmark"
# rtime=... summary (frames / rtime), not shell wall-clock time.
#
# Prerequisites: numactl, and an `ffmpeg` on PATH built with
# --enable-libsvtjpegxs (see ffmpeg-plugin/readme.md). Not checked here -
# see CI setup / documentation.
# Usage: ./PerformanceTestFfmpegPlugin.sh [samples_dir]
#   samples_dir: path to the conformance test resources (must contain encoder_tests/ and 4k/).
#                Defaults to ../../Conformance-tests (local dev symlink), so a bare invocation
#                still works when run manually on this machine. In CI, pass /opt/samples (or
#                wherever the pre-staged data lives on the runner) explicitly instead.
# Example: ./PerformanceTestFfmpegPlugin.sh
# Example: ./PerformanceTestFfmpegPlugin.sh /opt/samples

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
FFMPEG_BIN="ffmpeg"
SAMPLES_DIR="${1:-$SCRIPT_DIR/../../Conformance-tests}"
CSV_FILE="$SCRIPT_DIR/ffmpeg_results.csv"
RAMDISK_YUV="/dev/shm/test_stream_ffmpeg.yuv"
RAMDISK_JXS="/dev/shm/test_stream_ffmpeg.jxs"
NUMA_NODE=1
FRAMES=2000

# Cleanup on exit
trap 'rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"' EXIT

# Ensure CSV header exists
if [ ! -f "$CSV_FILE" ]; then
    echo "Operation,Command,StreamName,Resolution,BPP,Threads,TargetFPS,Result_FPS,Status" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|TargetFPS|BPP|Threads|SourceFile
# Identical to PerformanceTestSampleApp.sh's matrix (same streams, same BPP
# values, same thread counts) so both harnesses' Result_FPS columns are
# directly comparable. SourceFile is relative to SAMPLES_DIR. The 4k file is
# a single extracted frame (see Conformance-tests/4k/) - `-stream_loop -1`
# loops it up to FRAMES.
MATRIX=(
    # 1080p 422p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"

    # 1080p 422p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv"

    # 1080p 420p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv"
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv"

    # 1080p 420p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv"
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv"

    # 1080p 422p 8-bit - 1.5 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"

    # 1080p 422p 8-bit - 3.0 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
)

# get_ffmpeg_pix_fmt  colour_format bit_depth
#   Maps this script's colour_format/bit_depth (same convention as Phase
#   1's --colour-format/--input-depth) to the ffmpeg pix_fmt name the
#   plugin supports (see `ffmpeg -h encoder=libsvtjpegxs`).
function get_ffmpeg_pix_fmt() {
    local colour_format="$1" bit_depth="$2"
    local suffix=""
    [ "$bit_depth" -gt 8 ] && suffix="${bit_depth}le"
    case "$colour_format" in
        yuv420) echo "yuv420p${suffix}" ;;
        yuv444) echo "yuv444p${suffix}" ;;
        yuv422|*) echo "yuv422p${suffix}" ;;
    esac
}

# run_measured  cmd...
#   Runs 1 unmeasured warm-up + 3 measured passes of "$@", printing the
#   median FPS computed from ffmpeg's own "-benchmark" summary line
#   (frames / rtime) to stdout, or nothing if every measured run failed to
#   produce a parseable rtime.
function run_measured() {
    "$@" > /dev/null 2>&1 || true

    local fps_list=() run output rtime fps
    for run in 1 2 3; do
        output=$("$@" 2>&1) || true
        rtime=$(echo "$output" | grep -oE 'rtime=[0-9.]+' | cut -d= -f2) || true
        if [ -n "$rtime" ]; then
            fps=$(awk -v f="$FRAMES" -v r="$rtime" 'BEGIN { if (r > 0) printf "%.2f", f / r }')
            [ -n "$fps" ] && fps_list+=("$fps")
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
    pix_fmt=$(get_ffmpeg_pix_fmt "$fmt" "$depth")

    # --- Encode: -threads MUST come after -i so it applies to the output
    # encoder (libsvtjpegxs), not the rawvideo input reader. Placing it
    # before -i silently makes ffmpeg ignore it for the encoder and
    # auto-detect all CPU cores instead. ---
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -stream_loop -1 -f rawvideo -pix_fmt "$pix_fmt" -s:v "${w}x${h}" -framerate "$target_fps" \
        -i "$RAMDISK_YUV" \
        -threads "$threads" -frames:v "$FRAMES" -c:v libsvtjpegxs -bpp "$bpp" \
        -f image2pipe "$RAMDISK_JXS")
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

    # --- Decode: -threads stays before -i since libsvtjpegxs is the input
    # decoder here; placed next to -c:v for readability. ---
    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -threads "$threads" -f jpegxs_pipe -c:v libsvtjpegxs -i "$RAMDISK_JXS" \
        -f rawvideo /dev/null)
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
