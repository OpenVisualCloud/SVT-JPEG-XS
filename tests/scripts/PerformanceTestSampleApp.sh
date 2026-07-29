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
# Regression check: each MATRIX row carries its own Baseline_Enc_FPS/
# Baseline_Dec_FPS. This script compares every measurement against
# REGRESSION_THRESHOLD_PCT% of that baseline itself (no separate Python
# checker or baseline CSV) - if any measurement falls more than that percent
# below baseline (or couldn't be measured at all), that row is marked FAIL
# and the script exits 1 at the end (failing the CI step), after still
# running/recording every other test case first.
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
# A measurement more than this percent below its MATRIX baseline fails the run.
REGRESSION_THRESHOLD_PCT=5
# Set to 1 by check_result() on any regression/failure; the script exits 1 at
# the very end (after every test case has run) if this is set.
SCRIPT_FAILED=0

# Cleanup on exit
trap 'rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"' EXIT

# Ensure CSV header exists. TestCase is the raw MATRIX line below (unique per
# test case, includes the baseline FPS targets) rather than separate
# StreamName/Resolution/BPP/Threads columns - a MATRIX entry can be copied
# straight into this script with no reformatting.
if [ ! -f "$CSV_FILE" ]; then
    echo "Operation,TestCase,Command,Result_FPS,Status" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|Framerate|BPP|Threads|SourceFile|Baseline_Enc_FPS|Baseline_Dec_FPS
# SourceFile is relative to SAMPLES_DIR. The 4k file is a single extracted
# frame (see Conformance-tests/4k/) - -n loops it up to FRAMES. Threads is
# passed to both apps as --lp, to compare FPS scaling across thread counts.
# Framerate isn't used by this script (SvtJpegxsEncApp/DecApp don't take a
# framerate argument) - it's kept only so both scripts share one matrix
# layout; PerformanceTestFfmpegPlugin.sh needs it for ffmpeg's -framerate.
# Baseline_Enc_FPS/Baseline_Dec_FPS are the reference median FPS measured on
# the self-hosted runner (PR #58 CI runs) - update them whenever a
# legitimate, reviewed performance change is merged.
MATRIX=(
    # 1080p 422p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|41.76|78.72"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|250.78|388.95"

    # 1080p 422p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|34.29|61.34"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|203.27|313.48"

    # 1080p 420p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|49.90|89.43"
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|284.62|446.03"

    # 1080p 420p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|37.88|66.09"
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|219.39|346.20"

    # 1080p 422p 8-bit - 1.5 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|42.88|81.43"
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|253.23|395.57"

    # 1080p 422p 8-bit - 3.0 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|34.61|62.04"
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|204.33|315.46"
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

# check_result  label  measured_fps  baseline_fps
#   Compares measured_fps against baseline_fps * (1 - REGRESSION_THRESHOLD_PCT/100).
#   Sets $STATUS to PASS/FAIL and SCRIPT_FAILED=1 on any failure (missing
#   measurement or regression), printing a SUCCESS/FAIL line to stdout. Called
#   directly (never as "$(check_result ...)"), so it runs in this shell, not
#   a subshell - SCRIPT_FAILED set here stays set for the rest of the script.
function check_result() {
    local label="$1" measured_fps="$2" baseline_fps="$3"

    # Edge-case safety: ensure baseline exists and is non-empty (e.g. a new
    # MATRIX row added without filling in its baseline target).
    if [ -z "$baseline_fps" ] || [ "$baseline_fps" = "N/A" ]; then
        STATUS="FAIL"
        SCRIPT_FAILED=1
        echo "FAILED: $label - missing baseline FPS target in MATRIX"
        return
    fi

    if [ -z "$measured_fps" ]; then
        STATUS="FAIL"
        SCRIPT_FAILED=1
        echo "FAILED: $label - no FPS measured"
        return
    fi

    local min_allowed
    min_allowed=$(awk -v b="$baseline_fps" -v t="$REGRESSION_THRESHOLD_PCT" 'BEGIN { printf "%.2f", b * (1 - t / 100) }')

    if awk -v f="$measured_fps" -v m="$min_allowed" 'BEGIN { exit !(f >= m) }'; then
        STATUS="PASS"
        echo "SUCCESS: $label -> $measured_fps FPS (baseline=$baseline_fps FPS)"
    else
        STATUS="FAIL"
        SCRIPT_FAILED=1
        echo "FAIL (REGRESSION): $label -> $measured_fps FPS, below allowed min $min_allowed FPS (baseline=$baseline_fps FPS, -${REGRESSION_THRESHOLD_PCT}% threshold)"
    fi
}

for test_case in "${MATRIX[@]}"; do
    IFS='|' read -r name w h depth fmt framerate bpp threads file baseline_enc_fps baseline_dec_fps <<< "$test_case"
    source_path="$SAMPLES_DIR/$file"

    if [ ! -f "$source_path" ]; then
        echo "ERROR: File $source_path not found!"
        SCRIPT_FAILED=1
        echo "ENCODE,$test_case,N/A,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,$test_case,N/A,N/A,FAIL" >> "$CSV_FILE"
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

    check_result "ENCODE $name (bpp=$bpp, threads=$threads)" "$enc_fps" "$baseline_enc_fps"
    echo "ENCODE,$test_case,\"$enc_cmd\",${enc_fps:-N/A},$STATUS" >> "$CSV_FILE"

    # --- Decode: only meaningful if the bitstream was actually produced ---
    if [ ! -s "$RAMDISK_JXS" ]; then
        echo "ERROR: No bitstream produced by encode step, skipping decode."
        SCRIPT_FAILED=1
        echo "DECODE,$test_case,N/A,N/A,FAIL" >> "$CSV_FILE"
        rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
        continue
    fi

    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$DEC_APP" -i "$RAMDISK_JXS" -o /dev/null -n "$FRAMES" --lp "$threads")
    dec_cmd=$(printf '%q ' "${dec_cmd_arr[@]}")
    dec_fps=$(run_measured "${dec_cmd_arr[@]}")

    check_result "DECODE $name (bpp=$bpp, threads=$threads)" "$dec_fps" "$baseline_dec_fps"
    echo "DECODE,$test_case,\"$dec_cmd\",${dec_fps:-N/A},$STATUS" >> "$CSV_FILE"

    rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
done

if [ "$SCRIPT_FAILED" -eq 1 ]; then
    echo ""
    echo "One or more performance tests regressed by more than ${REGRESSION_THRESHOLD_PCT}% vs. baseline (or failed to run) - see FAIL lines above."
    exit 1
fi

echo ""
echo "All performance tests passed within the ${REGRESSION_THRESHOLD_PCT}% regression threshold."
