#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent



set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
ENC_APP="$SCRIPT_DIR/../../Bin/Release/SvtJpegxsEncApp"
DEC_APP="$SCRIPT_DIR/../../Bin/Release/SvtJpegxsDecApp"
SAMPLES_DIR="${1:-$SCRIPT_DIR/../../Conformance-tests}"
CSV_FILE="$SCRIPT_DIR/svt_app_results.csv"
RAMDISK_YUV=$(mktemp --suffix=.yuv /dev/shm/test_stream_XXXXXX)
RAMDISK_JXS=$(mktemp --suffix=.jxs /dev/shm/test_stream_XXXXXX)
NUMA_NODE=1
FRAMES=2000
REGRESSION_THRESHOLD_PCT=5  # max % FPS drop vs. baseline before failing
SCRIPT_FAILED=0             # set by check_result() on any failure

# Cleanup on exit
trap 'rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"' EXIT

# Ensure CSV header exists. TestCase is the matching MATRIX line below.
if [ ! -f "$CSV_FILE" ]; then
    echo "Operation,TestCase,Command,Result_FPS,Target_FPS,Percent_Of_Target,Status" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|Framerate|BPP|Threads|SourceFile|Baseline_Enc_FPS|Baseline_Dec_FPS
MATRIX=(
    # 1080p 422p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|77|145"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|442|709"

    # 1080p 422p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|62|113"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|359|571"

    # 1080p 420p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|91|161"
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|512|810"

    # 1080p 420p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|69|123"
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|397|627"

    # 1080p 422p 8-bit - 1.5 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|77|147"
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|450|718"

    # 1080p 422p 8-bit - 3.0 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|62|114"
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|364|578"
)

# run_measured cmd...: single run, prints parsed FPS (empty if unparseable).
function run_measured() {
    local output fps
    output=$("$@" 2>&1) || true
    fps=$(echo "$output" | grep -oE 'average [0-9]+\.[0-9]+\[fps\]' | awk '{print $2}' | tr -d '[fps]' | tail -1) || true

    [ -n "$fps" ] && echo "$fps"
}

# check_result label fps baseline_fps: sets $STATUS/$PERCENT, sets SCRIPT_FAILED=1 on failure.
function check_result() {
    local label="$1" measured_fps="$2" baseline_fps="$3"

    if [ -z "$baseline_fps" ] || [ "$baseline_fps" = "N/A" ]; then
        STATUS="FAIL"
        PERCENT="N/A"
        SCRIPT_FAILED=1
        echo "FAILED: $label - missing baseline FPS target in MATRIX"
        return
    fi

    if [ -z "$measured_fps" ]; then
        STATUS="FAIL"
        PERCENT="N/A"
        SCRIPT_FAILED=1
        echo "FAILED: $label - no FPS measured"
        return
    fi

    local min_allowed
    min_allowed=$(awk -v b="$baseline_fps" -v t="$REGRESSION_THRESHOLD_PCT" 'BEGIN { printf "%.2f", b * (1 - t / 100) }')
    PERCENT=$(awk -v f="$measured_fps" -v b="$baseline_fps" 'BEGIN { printf "%.1f", (f / b) * 100 }')

    if awk -v f="$measured_fps" -v m="$min_allowed" 'BEGIN { exit !(f >= m) }'; then
        STATUS="PASS"
        echo "SUCCESS: $label -> $measured_fps FPS (baseline=$baseline_fps FPS, ${PERCENT}% of target)"
    else
        STATUS="FAIL"
        SCRIPT_FAILED=1
        echo "FAIL (REGRESSION): $label -> $measured_fps FPS, below allowed min $min_allowed FPS (baseline=$baseline_fps FPS, ${PERCENT}% of target, -${REGRESSION_THRESHOLD_PCT}% threshold)"
    fi
}

for test_case in "${MATRIX[@]}"; do
    IFS='|' read -r name w h depth fmt framerate bpp threads file baseline_enc_fps baseline_dec_fps <<< "$test_case"
    source_path="$SAMPLES_DIR/$file"

    if [ ! -f "$source_path" ]; then
        echo "ERROR: File $source_path not found!"
        SCRIPT_FAILED=1
        echo "ENCODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        continue
    fi
    cp -f "$source_path" "$RAMDISK_YUV"

    # --- Encode ---
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$ENC_APP" -i "$RAMDISK_YUV" -b "$RAMDISK_JXS" -w "$w" -h "$h" \
        --input-depth "$depth" --colour-format "$fmt" --bpp "$bpp" -n "$FRAMES" --lp "$threads")
    enc_cmd=$(printf '%q ' "${enc_cmd_arr[@]}")
    enc_fps=$(run_measured "${enc_cmd_arr[@]}")

    check_result "ENCODE $name (bpp=$bpp, threads=$threads)" "$enc_fps" "$baseline_enc_fps"
    echo "ENCODE,$test_case,\"$enc_cmd\",${enc_fps:-N/A},${baseline_enc_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    # --- Decode ---
    if [ ! -s "$RAMDISK_JXS" ]; then
        echo "ERROR: No bitstream produced by encode step, skipping decode."
        SCRIPT_FAILED=1
        echo "DECODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
        continue
    fi

    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$DEC_APP" -i "$RAMDISK_JXS" -o /dev/null -n "$FRAMES" --lp "$threads")
    dec_cmd=$(printf '%q ' "${dec_cmd_arr[@]}")
    dec_fps=$(run_measured "${dec_cmd_arr[@]}")

    check_result "DECODE $name (bpp=$bpp, threads=$threads)" "$dec_fps" "$baseline_dec_fps"
    echo "DECODE,$test_case,\"$dec_cmd\",${dec_fps:-N/A},${baseline_dec_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
done

if [ "$SCRIPT_FAILED" -eq 1 ]; then
    echo ""
    echo "One or more performance tests regressed by more than ${REGRESSION_THRESHOLD_PCT}% vs. baseline (or failed to run) - see FAIL lines above."
    exit 1
fi

echo ""
echo "All performance tests passed within the ${REGRESSION_THRESHOLD_PCT}% regression threshold."
