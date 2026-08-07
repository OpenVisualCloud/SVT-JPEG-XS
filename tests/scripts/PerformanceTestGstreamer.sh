#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# GStreamer svtjpegxs plugin performance test.
# Requires gst-launch-1.0 with svtjpegxsenc/svtjpegxsdec in GST_PLUGIN_PATH
# and libSvtJpegxs in LD_LIBRARY_PATH (source gst-plugin-env.sh before running).


set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
SAMPLES_DIR="${1:-$SCRIPT_DIR/../../Conformance-tests}"
CSV_FILE="$SCRIPT_DIR/gstreamer_results.csv"
RAMDISK_YUV=$(mktemp --suffix=.yuv /dev/shm/test_stream_gst_XXXXXX)
RAMDISK_JXS=$(mktemp --suffix=.jxs /dev/shm/test_stream_gst_XXXXXX)
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
# Baselines mirror the FFmpeg plugin values; both run on the same jpeg-perf runner
# using the same libSvtJpegxs under the hood.
MATRIX=(
    # 1080p 422p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|74|128"
    "1080p60_422p10|1920|1080|10|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|392|589"

    # 1080p 422p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|58|101"
    "1080p60_422p10|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|313|480"

    # 1080p 420p 10-bit - 1.5 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|83|145"
    "1080p60_420p10|1920|1080|10|yuv420|60|1.5|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|465|676"

    # 1080p 420p 10-bit - 3.0 BPP Thread Scaling
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|1|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|62|112"
    "1080p60_420p10|1920|1080|10|yuv420|60|3.0|8|encoder_tests/touchdown_1080p_yuv420p_10_bit_le_60_frames.yuv|348|548"

    # 1080p 422p 8-bit - 1.5 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|76|129"
    "1080p60_422p8|1920|1080|8|yuv422|60|1.5|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|406|595"

    # 1080p 422p 8-bit - 3.0 BPP Thread Scaling
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|1|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|59|102"
    "1080p60_422p8|1920|1080|8|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv|319|486"
)

# get_gst_params colour_format bit_depth width height:
#   Sets globals: gst_fmt  (rawvideoparse format= value)
#                 sampling (image/x-jxsc sampling= value for decoder caps)
#                 bytes_per_frame (raw YUV bytes per frame)
function get_gst_params() {
    local colour_format="$1" bit_depth="$2" width="$3" height="$4"
    case "${colour_format}_${bit_depth}" in
        yuv422_8)
            gst_fmt="y42b"
            sampling="YCbCr-4:2:2"
            bytes_per_frame=$(( width * height * 2 ))
            ;;
        yuv420_8)
            gst_fmt="i420"
            sampling="YCbCr-4:2:0"
            bytes_per_frame=$(( width * height * 3 / 2 ))
            ;;
        yuv422_10)
            gst_fmt="i422-10le"
            sampling="YCbCr-4:2:2"
            bytes_per_frame=$(( width * height * 4 ))
            ;;
        yuv420_10)
            gst_fmt="i420-10le"
            sampling="YCbCr-4:2:0"
            bytes_per_frame=$(( width * height * 3 ))
            ;;
        *)
            echo "ERROR: Unknown format/depth combination: $colour_format / $bit_depth"
            return 1
            ;;
    esac
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

# measure_fps start_ns end_ns: prints FPS = FRAMES / elapsed_seconds.
function measure_fps() {
    local start_ns="$1" end_ns="$2"
    awk -v f="$FRAMES" -v s="$start_ns" -v e="$end_ns" \
        'BEGIN { elapsed = (e - s) / 1e9; if (elapsed > 0) printf "%.2f", f / elapsed }'
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

    # Resolve GStreamer format strings and per-frame byte count
    if ! get_gst_params "$fmt" "$depth" "$w" "$h"; then
        SCRIPT_FAILED=1
        echo "ENCODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        continue
    fi

    total_bytes=$(( bytes_per_frame * FRAMES ))
    src_size=$(stat -c%s "$RAMDISK_YUV")

    # --- Encode ---
    # Stream exactly FRAMES frames of raw YUV (looping the source file as needed) into
    # fdsrc, encode with svtjpegxsenc, and write the JPEG-XS bitstream to RAMDISK_JXS.
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd="numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
gst-launch-1.0 -q \
fdsrc \
! rawvideoparse format=$gst_fmt width=$w height=$h framerate=$framerate/1 \
! svtjpegxsenc bits-per-pixel=$bpp threads=$threads \
! filesink location=$RAMDISK_JXS"

    enc_start=$(date +%s%N)
    {
        sent=0
        while (( sent + src_size <= total_bytes )); do
            cat "$RAMDISK_YUV"
            sent=$(( sent + src_size ))
        done
        remaining=$(( total_bytes - sent ))
        (( remaining > 0 )) && head -c "$remaining" "$RAMDISK_YUV"
    } | eval "$enc_cmd" 2>/dev/null || true
    enc_end=$(date +%s%N)

    enc_fps=$(measure_fps "$enc_start" "$enc_end")

    check_result "ENCODE $name (bpp=$bpp, threads=$threads)" "$enc_fps" "$baseline_enc_fps"
    echo "ENCODE,$test_case,\"$enc_cmd\",${enc_fps:-N/A},${baseline_enc_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    # --- Decode ---
    if [ ! -s "$RAMDISK_JXS" ]; then
        echo "ERROR: No bitstream produced by encode step, skipping decode."
        SCRIPT_FAILED=1
        echo "DECODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        rm -f "$RAMDISK_JXS"
        RAMDISK_JXS=$(mktemp --suffix=.jxs /dev/shm/test_stream_gst_XXXXXX)
        continue
    fi

    # Compute per-frame JXS size. JPEG-XS is fixed-rate so all frames are equal size.
    jxs_total=$(stat -c%s "$RAMDISK_JXS")
    frame_jxsc_size=$(( jxs_total / FRAMES ))

    # Decode from RAMDISK_JXS using filesrc with fixed blocksize=one frame, send output
    # to fakesink with sync=false to measure raw decode throughput (no clock throttling).
    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd="numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
gst-launch-1.0 -q \
filesrc location=$RAMDISK_JXS blocksize=$frame_jxsc_size \
! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$w,height=(int)$h,framerate=(fraction)$framerate/1\" \
! svtjpegxsdec threads=$threads \
! fakesink sync=false"

    dec_start=$(date +%s%N)
    eval "$dec_cmd" 2>/dev/null || true
    dec_end=$(date +%s%N)

    dec_fps=$(measure_fps "$dec_start" "$dec_end")

    check_result "DECODE $name (bpp=$bpp, threads=$threads)" "$dec_fps" "$baseline_dec_fps"
    echo "DECODE,$test_case,\"$dec_cmd\",${dec_fps:-N/A},${baseline_dec_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    rm -f "$RAMDISK_JXS"
    RAMDISK_JXS=$(mktemp --suffix=.jxs /dev/shm/test_stream_gst_XXXXXX)
done

if [ "$SCRIPT_FAILED" -eq 1 ]; then
    echo ""
    echo "One or more performance tests regressed by more than ${REGRESSION_THRESHOLD_PCT}% vs. baseline (or failed to run) - see FAIL lines above."
    exit 1
fi

echo ""
echo "All performance tests passed within the ${REGRESSION_THRESHOLD_PCT}% regression threshold."
