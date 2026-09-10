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
ASM_LEVEL="${2:-avx512}"
CSV_FILE="$SCRIPT_DIR/svt_app_results.csv"
# Node 1 is what the dual-socket CI runner is calibrated against; a single-node host (no node1
# under sysfs) has nothing to bind to there, so fall back to node 0.
if [ -d /sys/devices/system/node/node1 ]; then
    NUMA_NODE=1
else
    NUMA_NODE=0
fi
FRAMES=2000

case "$ASM_LEVEL" in
    avx512 | avx2 | sse | c) ;;
    *)
        echo "ERROR: unknown asm level '$ASM_LEVEL' - expected one of: avx512 avx2 sse c" >&2
        exit 1
        ;;
esac

# Baselines in MATRIX below are calibrated for avx512 only - the default and the only tier this
# script enforces a regression threshold against. avx2/sse/c have no calibrated baseline, so they
# are measured and reported (Status=INFO) but never fail the script.
ENFORCE=1
[ "$ASM_LEVEL" != "avx512" ] && ENFORCE=0
REGRESSION_THRESHOLD_PCT=5  # max % FPS drop vs. baseline before failing
SCRIPT_FAILED=0             # set by check_result() on any failure

# Use a dedicated tmpfs mount instead of a directory under the shared /dev/shm. /dev/shm is
# machine-wide on this self-hosted runner - something on the host has been deleting this
# script's files out from under it mid-run (confirmed externally: an inotifywait watch on
# /dev/shm during a CI run showed no delete/unmount event at all, ruling out this script and
# every cron/timer/systemd unit checked so far, yet the files were still gone). A mount this
# script creates itself, at a path nothing else on the host has any reason to know about, sidesteps
# whatever that is instead of chasing it further. mount/umount need root; 'gta' - the account
# Actions job steps actually run as - has confirmed passwordless sudo on this fleet.
RAMDISK_MOUNT="/mnt/svt_jpegxs_perf_ramdisk_$$_${RANDOM}${RANDOM}"

# Unmount+remove ramdisk mountpoints abandoned by a previously killed run (its own EXIT trap
# never got to fire). Age-gated on the mountpoint directory's mtime (untouched since creation),
# not unmounted unconditionally, so a live concurrent run's own mount is never touched.
while read -r stale_mount; do
    [ -d "$stale_mount" ] || continue
    if [ -n "$(find "$stale_mount" -maxdepth 0 -mmin +180)" ]; then
        sudo umount "$stale_mount" 2>/dev/null || true
        sudo rmdir "$stale_mount" 2>/dev/null || true
    fi
done < <(mount | awk '$3 ~ /^\/mnt\/svt_jpegxs_perf_ramdisk_/ {print $3}')

# 3G: RAMDISK_YUV/RAMDISK_JXS are fixed paths overwritten every iteration (not accumulated), and
# each iteration's own files are removed once no longer needed (see the loop below) - so the real
# peak is one row's worth: the largest MATRIX row (yuva444p8, bpp=5.0) needs ~2.4 GiB for the JXS
# bitstream (2000 frames * 1920*1080*5/8 bytes) plus the raw YUV input and synth files - this
# leaves only modest headroom, so don't shrink further than this.
sudo mkdir -p "$RAMDISK_MOUNT"
sudo mount -t tmpfs -o size=3G,mode=1777 tmpfs "$RAMDISK_MOUNT"
trap 'sudo umount "$RAMDISK_MOUNT" 2>/dev/null || sudo umount -l "$RAMDISK_MOUNT" 2>/dev/null; sudo rmdir "$RAMDISK_MOUNT" 2>/dev/null' EXIT

RUN_DIR="$RAMDISK_MOUNT"

RAMDISK_YUV=$(mktemp --suffix=.yuv "$RUN_DIR/test_stream_XXXXXX")
RAMDISK_JXS=$(mktemp --suffix=.jxs "$RUN_DIR/test_stream_XXXXXX")

SYNTH_YUVA422="$(mktemp --suffix=.yuv "$RUN_DIR/synth_yuva422_XXXXXX")"
SYNTH_YUVA444="$(mktemp --suffix=.yuv "$RUN_DIR/synth_yuva444_XXXXXX")"

# No real 4-component (alpha) sample YUV exists in $SAMPLES_DIR. Synthesize small multi-frame
# yuva422/yuva444 raw files on the fly from the real touchdown 8bit 422 sample (same technique as
# tests/scripts/EncoderTest.sh) - the app rewinds/loops the input file to satisfy -n $FRAMES, so a
# short synthesized file is enough. Real Y/Cb/Cr planes kept as-is, alpha/extra planes are Y duplicates.
#
# Use dd with iflag=skip_bytes,count_bytes instead of tail|head: dd seeks directly without a pipe so
# there is no SIGPIPE, and any write failure (e.g. ENOSPC) propagates as a non-zero exit code that
# is not masked by set -o pipefail.
SYNTH_SRC="$SAMPLES_DIR/encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
if [ -f "$SYNTH_SRC" ]; then
    SYNTH_FRAMES=5
    SYNTH_Y_SIZE=$((1920 * 1080))
    SYNTH_C_SIZE=$((1920 * 1080 / 2))
    SYNTH_422_FRAME_SIZE=$((SYNTH_Y_SIZE + 2 * SYNTH_C_SIZE))
    for ((f = 0; f < SYNTH_FRAMES; f++)); do
        off=$((f * SYNTH_422_FRAME_SIZE))
        dd if="$SYNTH_SRC" iflag=skip_bytes,count_bytes skip="$off" count="$SYNTH_422_FRAME_SIZE" status=none >> "$SYNTH_YUVA422"
        dd if="$SYNTH_SRC" iflag=skip_bytes,count_bytes skip="$off" count="$SYNTH_Y_SIZE" status=none >> "$SYNTH_YUVA422"
        for ((p = 0; p < 4; p++)); do
            dd if="$SYNTH_SRC" iflag=skip_bytes,count_bytes skip="$off" count="$SYNTH_Y_SIZE" status=none >> "$SYNTH_YUVA444"
        done
    done
    if [ ! -s "$SYNTH_YUVA422" ] || [ ! -s "$SYNTH_YUVA444" ]; then
        echo "ERROR: synthesis of YUVA input files failed — check ramdisk space: $(df -h "$RUN_DIR" | awk 'NR==2')"
        exit 1
    fi
fi

# Ensure the CSV header matches the current schema. TestCase is the matching MATRIX line below.
# A stale header left over from before AsmLevel existed would otherwise get new rows appended
# under it with a mismatched column count, so rewrite the file whenever the header differs too.
CSV_HEADER="Operation,TestCase,AsmLevel,Command,Result_FPS,Target_FPS,Percent_Of_Target,Status"
if [ ! -f "$CSV_FILE" ] || [ "$(head -n 1 "$CSV_FILE")" != "$CSV_HEADER" ]; then
    echo "$CSV_HEADER" > "$CSV_FILE"
fi

# Matrix: Name|Width|Height|BitDepth|Format|Framerate|BPP|Threads|SourceFile|Baseline_Enc_FPS|Baseline_Dec_FPS|ExtraEncArgs(optional)|ExtraDecArgs(optional)
# SourceFile "SYNTH:yuva422"/"SYNTH:yuva444" is a sentinel meaning "use the on-the-fly synthesized file above",
# since no real 4-component sample fixture exists in $SAMPLES_DIR.
MATRIX=(
    # 1080p yuva422 (4:2:2:4) 8-bit - 4.0 BPP Thread Scaling. Baselines calibrated from the
    # CI runner's own measurements (dev-sandbox numbers were faster/slower than this host).
    "1080p60_yuva422p8|1920|1080|8|yuva422|60|4.0|1|SYNTH:yuva422|39|40"
    "1080p60_yuva422p8|1920|1080|8|yuva422|60|4.0|8|SYNTH:yuva422|235|146"

    # 1080p rgba/yuva444 (4:4:4:4) 8-bit - 5.0 BPP Thread Scaling.
    "1080p60_yuva444p8|1920|1080|8|rgba|60|5.0|1|SYNTH:yuva444|30|33"
    "1080p60_yuva444p8|1920|1080|8|rgba|60|5.0|8|SYNTH:yuva444|180|210"

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

    # 1080p 422p 10-bit - 3.0 BPP - MSB-aligned input/output: same baseline as the equivalent
    # LSB row above (msb-aligned kernels have same perf as LSB, verified separately).
    "1080p60_422p10_msb|1920|1080|10|yuv422|60|3.0|8|encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv|359|571|--input-msb-aligned 1|--output-msb-aligned 1"
)

# run_measured cmd...: single run, prints parsed FPS (empty if unparseable or if the command
# exited non-zero - EncApp/DecApp now abort on a write/close error, sometimes *after* already
# printing a valid "average fps" line, so a non-zero exit must blank FPS even then).
# This function must never itself return non-zero: every assignment/statement below is guarded
# (&&/|| or unconditional) because the script runs under `set -eo pipefail`, and a bare failing
# command in `enc_fps=$(run_measured ...)` position would abort the whole matrix loop instead of
# letting check_result report a normal FAIL and continue to the next test case.
function run_measured() {
    local output fps exit_code
    output=$("$@" 2>&1) && exit_code=0 || exit_code=$?
    fps=$(echo "$output" | grep -oE 'average [0-9]+\.[0-9]+\[fps\]' | awk '{print $2}' | tr -d '[fps]' | tail -1) || true

    if [ "$exit_code" -ne 0 ]; then
        # Surface the app's own diagnostics (now including strerror(errno) on I/O failures)
        # that would otherwise be captured into $output above and silently discarded.
        echo "$output" >&2
        fps=""
    fi

    echo "$fps"
}

# check_result label fps baseline_fps [enforce=1]: sets $STATUS/$PERCENT; sets SCRIPT_FAILED=1
# on failure only when enforce=1 - otherwise STATUS is always INFO and SCRIPT_FAILED is untouched.
function check_result() {
    local label="$1" measured_fps="$2" baseline_fps="$3" enforce="${4:-1}"

    if [ -z "$baseline_fps" ] || [ "$baseline_fps" = "N/A" ]; then
        PERCENT="N/A"
        if [ "$enforce" = "1" ]; then
            STATUS="FAIL"
            SCRIPT_FAILED=1
        else
            STATUS="INFO"
        fi
        echo "${STATUS}: $label - missing baseline FPS target in MATRIX"
        return
    fi

    if [ -z "$measured_fps" ]; then
        PERCENT="N/A"
        if [ "$enforce" = "1" ]; then
            STATUS="FAIL"
            SCRIPT_FAILED=1
        else
            STATUS="INFO"
        fi
        echo "${STATUS}: $label - no FPS measured"
        return
    fi

    local min_allowed
    min_allowed=$(awk -v b="$baseline_fps" -v t="$REGRESSION_THRESHOLD_PCT" 'BEGIN { printf "%.2f", b * (1 - t / 100) }')
    PERCENT=$(awk -v f="$measured_fps" -v b="$baseline_fps" 'BEGIN { printf "%.1f", (f / b) * 100 }')

    # Not the enforced tier: always INFO, regardless of whether the measurement happens to
    # clear the avx512-calibrated threshold - there is no real pass/fail criterion for this
    # tier, only a number to compare by eye. Checked first so nothing below can ever produce
    # PASS/FAIL for a non-enforced tier, matching the closing summary's "results above are
    # informational only" claim in every case, not just some.
    if [ "$enforce" != "1" ]; then
        STATUS="INFO"
        echo "INFO: $label -> $measured_fps FPS (baseline=$baseline_fps FPS, ${PERCENT}% of target, not enforced for this tier)"
        return
    fi

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
        SYNTH:yuva422) source_path="$SYNTH_YUVA422" ;;
        SYNTH:yuva444) source_path="$SYNTH_YUVA444" ;;
        *) source_path="$SAMPLES_DIR/$file" ;;
    esac

    if [ ! -s "$source_path" ]; then
        echo "ERROR: File $source_path not found or empty!"
        SCRIPT_FAILED=1
        echo "ENCODE,$test_case,$ASM_LEVEL,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        echo "DECODE,$test_case,$ASM_LEVEL,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        continue
    fi
    cp -f "$source_path" "$RAMDISK_YUV"

    # --- Encode ---
    echo "Encoding: $name (bpp=$bpp, threads=$threads, asm=$ASM_LEVEL)"
    enc_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$ENC_APP" -i "$RAMDISK_YUV" -b "$RAMDISK_JXS" -w "$w" -h "$h" \
        --input-depth "$depth" --colour-format "$fmt" --bpp "$bpp" -n "$FRAMES" --lp "$threads" --asm "$ASM_LEVEL" $extra_enc_args)
    enc_cmd=$(printf '%q ' "${enc_cmd_arr[@]}")
    enc_fps=$(run_measured "${enc_cmd_arr[@]}")

    check_result "ENCODE $name (bpp=$bpp, threads=$threads, asm=$ASM_LEVEL)" "$enc_fps" "$baseline_enc_fps" "$ENFORCE"
    echo "ENCODE,$test_case,$ASM_LEVEL,\"$enc_cmd\",${enc_fps:-N/A},${baseline_enc_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    # --- Decode ---
    if [ ! -s "$RAMDISK_JXS" ]; then
        echo "ERROR: No bitstream produced by encode step, skipping decode. ramdisk: $(df -h "$RUN_DIR" | awk 'NR==2')"
        [ "$ENFORCE" = "1" ] && SCRIPT_FAILED=1
        echo "DECODE,$test_case,$ASM_LEVEL,N/A,N/A,N/A,N/A,FAIL" >> "$CSV_FILE"
        rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
        continue
    fi

    echo "Decoding: $name (bpp=$bpp, threads=$threads, asm=$ASM_LEVEL)"
    dec_cmd_arr=(numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE \
        "$DEC_APP" -i "$RAMDISK_JXS" -o /dev/null -n "$FRAMES" --lp "$threads" --asm "$ASM_LEVEL" $extra_dec_args)
    dec_cmd=$(printf '%q ' "${dec_cmd_arr[@]}")
    dec_fps=$(run_measured "${dec_cmd_arr[@]}")

    check_result "DECODE $name (bpp=$bpp, threads=$threads, asm=$ASM_LEVEL)" "$dec_fps" "$baseline_dec_fps" "$ENFORCE"
    echo "DECODE,$test_case,$ASM_LEVEL,\"$dec_cmd\",${dec_fps:-N/A},${baseline_dec_fps:-N/A},$PERCENT,$STATUS" >> "$CSV_FILE"

    rm -f "$RAMDISK_YUV" "$RAMDISK_JXS"
done

if [ "$SCRIPT_FAILED" -eq 1 ]; then
    echo ""
    echo "One or more performance tests regressed by more than ${REGRESSION_THRESHOLD_PCT}% vs. baseline (or failed to run) - see FAIL lines above."
    exit 1
fi

echo ""
if [ "$ENFORCE" = "1" ]; then
    echo "All performance tests passed within the ${REGRESSION_THRESHOLD_PCT}% regression threshold."
else
    echo "asm=$ASM_LEVEL has no calibrated baseline - results above are informational only (Status=INFO), not a pass/fail gate."
fi
