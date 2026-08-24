#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent


set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
FFMPEG_BIN="ffmpeg"
SAMPLES_DIR="${1:-$SCRIPT_DIR/../../Conformance-tests}"
CSV_FILE="$SCRIPT_DIR/ffmpeg_results.csv"
NUMA_NODE=1
FRAMES=2000
REGRESSION_THRESHOLD_PCT=5  # max % FPS drop vs. baseline before failing
SCRIPT_FAILED=0             # set by check_result() on any failure

# Remove stale /dev/shm files left by any previously killed run BEFORE creating new ones; if they
# accumulate they can fill the ramdisk and silently cause the synthesis below to produce empty files
# (ENOSPC masked by || true in the original code, or a failed dd here).
rm -f /dev/shm/test_stream_ffmpeg_*.yuv /dev/shm/test_stream_ffmpeg_*.jxs \
      /dev/shm/synth_yuva422_ffmpeg_*.yuv /dev/shm/synth_yuva422_ffmpeg_*.yuv.y \
      /dev/shm/synth_yuva422_ffmpeg_*.yuv.cb /dev/shm/synth_yuva422_ffmpeg_*.yuv.cr \
      /dev/shm/synth_yuva444_ffmpeg_*.yuv 2>/dev/null || true

RAMDISK_YUV=$(mktemp --suffix=.yuv /dev/shm/test_stream_ffmpeg_XXXXXX)
RAMDISK_JXS=$(mktemp --suffix=.jxs /dev/shm/test_stream_ffmpeg_XXXXXX)
SYNTH_YUVA422=$(mktemp --suffix=.yuv /dev/shm/synth_yuva422_ffmpeg_XXXXXX)
SYNTH_YUVA444=$(mktemp --suffix=.yuv /dev/shm/synth_yuva444_ffmpeg_XXXXXX)

# Cleanup on exit (includes intermediate .y/.cb/.cr temp files used during synthesis)
trap 'rm -f "$RAMDISK_YUV" "$RAMDISK_JXS" "$SYNTH_YUVA422" "$SYNTH_YUVA444" \
           "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.cb" "${SYNTH_YUVA422}.cr"' EXIT

# No real 4-component (alpha) sample YUV exists in $SAMPLES_DIR. Synthesize a single-frame fixture
# (ffmpeg's -stream_loop -1 below repeats it to reach $FRAMES) from the existing 1080p 8bit yuv422
# fixture: real Y/Cb/Cr planes kept as-is, alpha/extra-chroma planes are Y duplicates.
#
# Use dd with iflag=skip_bytes,count_bytes instead of tail|head: dd seeks directly without a pipe so
# there is no SIGPIPE, and any write failure (e.g. ENOSPC) propagates as a non-zero exit code that
# is not masked by set -o pipefail.
SYNTH_SRC="$SAMPLES_DIR/encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
if [ -f "$SYNTH_SRC" ]; then
    SYNTH_Y_SIZE=$((1920 * 1080))
    SYNTH_C_SIZE=$((1920 * 1080 / 2))
    head -c $SYNTH_Y_SIZE "$SYNTH_SRC" > "${SYNTH_YUVA422}.y"
    dd if="$SYNTH_SRC" iflag=skip_bytes,count_bytes skip="$SYNTH_Y_SIZE" count="$SYNTH_C_SIZE" status=none > "${SYNTH_YUVA422}.cb"
    dd if="$SYNTH_SRC" iflag=skip_bytes,count_bytes skip="$((SYNTH_Y_SIZE + SYNTH_C_SIZE))" count="$SYNTH_C_SIZE" status=none > "${SYNTH_YUVA422}.cr"
    cat "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.cb" "${SYNTH_YUVA422}.cr" "${SYNTH_YUVA422}.y" > "$SYNTH_YUVA422"
    cat "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.y" > "$SYNTH_YUVA444"
    rm -f "${SYNTH_YUVA422}.y" "${SYNTH_YUVA422}.cb" "${SYNTH_YUVA422}.cr"
    if [ ! -s "$SYNTH_YUVA422" ] || [ ! -s "$SYNTH_YUVA444" ]; then
        echo "ERROR: synthesis of YUVA input files failed — check /dev/shm space: $(df -h /dev/shm | awk 'NR==2')"
        exit 1
    fi
fi

# Ensure CSV header exists. TestCase is the matching MATRIX line below.
if [ ! -f "$CSV_FILE" ]; then
    echo "Operation,TestCase,Command,Result_FPS,Target_FPS,Percent_Of_Target,Status" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|Framerate|BPP|Threads|SourceFile|Baseline_Enc_FPS|Baseline_Dec_FPS|ExtraEncArgs(optional)|ExtraDecArgs(optional)
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

    # 1080p 422p 10-bit - 3.0 BPP - MSB-aligned input/output: same baseline as the equivalent
    # LSB row above (msb-aligned kernels have same perf as LSB, verified separately).
    "1080p60_422p10_msb|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|313|480|-msb_aligned 1|-msb_aligned 1"

    # 1080p yuva422 (4:2:2:4) 8-bit - 4.0 BPP Thread Scaling (SDBQ-3776). SourceFile "SYNTH" is a
    # sentinel meaning "use the on-the-fly synthesized $SYNTH_YUVA422 file above". Baselines
    # measured locally on this sandbox - re-calibrate on the real target CI hardware.
    "1080p60_yuva422p8|1920|1080|8|yuva422|60|4.0|1|SYNTH|40|37"
    "1080p60_yuva422p8|1920|1080|8|yuva422|60|4.0|8|SYNTH|185|124"

    # 1080p rgba/yuva444 (4:4:4:4) 8-bit - 5.0 BPP Thread Scaling (SDBQ-3776). SourceFile "SYNTH" is
    # a sentinel meaning "use the on-the-fly synthesized $SYNTH_YUVA444 file above". Baselines
    # measured locally on this sandbox - re-calibrate on the real target CI hardware.
    "1080p60_yuva444p8|1920|1080|8|rgba|60|5.0|1|SYNTH|32|38"
    "1080p60_yuva444p8|1920|1080|8|rgba|60|5.0|8|SYNTH|152|110"
)

# get_ffmpeg_pix_fmt colour_format bit_depth: maps to the ffmpeg pix_fmt name.
function get_ffmpeg_pix_fmt() {
    local colour_format="$1" bit_depth="$2"
    local suffix=""
    [ "$bit_depth" -gt 8 ] && suffix="${bit_depth}le"
    case "$colour_format" in
        yuv420) echo "yuv420p${suffix}" ;;
        yuv444) echo "yuv444p${suffix}" ;;
        yuva422) echo "yuva422p${suffix}" ;;
        rgba) echo "gbrap${suffix}" ;;
        yuv422|*) echo "yuv422p${suffix}" ;;
    esac
}

# run_measured cmd...: single run, prints FPS computed from ffmpeg's rtime= (empty if unparseable).
function run_measured() {
    local output rtime fps
    output=$("$@" 2>&1) || true
    rtime=$(echo "$output" | grep -oE 'rtime=[0-9.]+' | cut -d= -f2 | tail -1) || true

    if [ -n "$rtime" ]; then
        fps=$(awk -v f="$FRAMES" -v r="$rtime" 'BEGIN { if (r > 0) printf "%.2f", f / r }')
        [ -n "$fps" ] && echo "$fps"
    fi
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
    IFS='|' read -r name w h depth fmt framerate bpp threads file baseline_enc_fps baseline_dec_fps extra_enc_args extra_dec_args <<< "$test_case"
    case "$file" in
        SYNTH)
            case "$fmt" in
                yuva422) source_path="$SYNTH_YUVA422" ;;
                rgba) source_path="$SYNTH_YUVA444" ;;
            esac
            ;;
        *) source_path="$SAMPLES_DIR/$file" ;;
    esac

    if [ ! -f "$source_path" ]; then
        echo "ERROR: File $source_path not found!"
        SCRIPT_FAILED=1
        echo "ENCODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,$test_case,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        continue
    fi
    cp -f "$source_path" "$RAMDISK_YUV"
    pix_fmt=$(get_ffmpeg_pix_fmt "$fmt" "$depth")

    # Encode: -threads must come after -i (applies to the encoder, not the input reader).
    echo "Encoding: $name (bpp=$bpp, threads=$threads)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -stream_loop -1 -f rawvideo -pix_fmt "$pix_fmt" -s:v "${w}x${h}" -framerate "$framerate" \
        -i "$RAMDISK_YUV" \
        -threads "$threads" -frames:v "$FRAMES" -c:v libsvtjpegxs -bpp "$bpp" $extra_enc_args \
        -f image2pipe "$RAMDISK_JXS")
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

    # Decode: -threads applies to libsvtjpegxs as the input decoder here.
    echo "Decoding: $name (bpp=$bpp, threads=$threads)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$FFMPEG_BIN" -y -hide_banner -loglevel info -nostats -benchmark \
        -threads "$threads" -f jpegxs_pipe -c:v libsvtjpegxs $extra_dec_args -i "$RAMDISK_JXS" \
        -f rawvideo /dev/null)
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
