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
# Regression check: each MATRIX row carries its own Baseline_Enc_FPS/
# Baseline_Dec_FPS. This script compares every measurement against
# REGRESSION_THRESHOLD_PCT% of that baseline itself (no separate Python
# checker or baseline CSV) - if any measurement falls more than that percent
# below baseline (or couldn't be measured at all), that row is marked FAIL
# and the script exits 1 at the end (failing the CI step), after still
# running/recording every other test case first.
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
# Identical to PerformanceTestSampleApp.sh's matrix (same streams, same BPP
# values, same thread counts) so both harnesses' Result_FPS columns are
# directly comparable - the baseline FPS values differ though (ffmpeg has
# its own overhead vs. the sample apps). SourceFile is relative to
# SAMPLES_DIR. Framerate is passed to ffmpeg as -framerate (the rawvideo
# input's native rate). The 4k file is a single extracted frame (see
# Conformance-tests/4k/) - `-stream_loop -1` loops it up to FRAMES.
# Baseline_Enc_FPS/Baseline_Dec_FPS are the reference median FPS measured on
# the self-hosted runner (PR #58 CI runs) - update them whenever a
# legitimate, reviewed performance change is merged.
MATRIX=(
    # 1080p 422p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|74.37|128.65"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|390.40|589.45"

    # 1080p 422p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|58.49|101.72"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|312.70|482.39"

    # 1080p 420p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|83.00|145.09"
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|460.51|673.63"

    # 1080p 420p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|62.40|111.93"
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|347.89|544.22"

    # 1080p 422p 8-bit - 1.5 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|75.91|129.03"
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|405.43|600.78"

    # 1080p 422p 8-bit - 3.0 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|59.38|101.85"
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|319.64|492.25"
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
    pix_fmt=$(get_ffmpeg_pix_fmt "$fmt" "$depth")

    # --- Encode: -threads MUST come after -i so it applies to the output
    # encoder (libsvtjpegxs), not the rawvideo input reader. Placing it
    # before -i silently makes ffmpeg ignore it for the encoder and
    # auto-detect all CPU cores instead. ---
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -stream_loop -1 -f rawvideo -pix_fmt "$pix_fmt" -s:v "${w}x${h}" -framerate "$framerate" \
        -i "$RAMDISK_YUV" \
        -threads "$threads" -frames:v "$FRAMES" -c:v libsvtjpegxs -bpp "$bpp" \
        -f image2pipe "$RAMDISK_JXS")
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

    # --- Decode: -threads stays before -i since libsvtjpegxs is the input
    # decoder here; placed next to -c:v for readability. ---
    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -threads "$threads" -f jpegxs_pipe -c:v libsvtjpegxs -i "$RAMDISK_JXS" \
        -f rawvideo /dev/null)
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
