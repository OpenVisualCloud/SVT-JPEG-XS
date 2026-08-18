#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# OOM fault-injection test using oomify (https://github.com/tavianator/oomify).
# Injects a malloc() failure at every allocation point inside the encoder and
# decoder and verifies that each failure results in a clean exit rather than
# a crash (signal / segfault).
#
# Usage: OomifyTest.sh <bin_dir> <oomify_dir>
#   bin_dir    : directory containing SvtJpegxsEncApp, SvtJpegxsDecApp, and
#                libSvtJpegxs.so
#   oomify_dir : directory containing the oomify binary and liboomify.so


set -u

BIN_DIR="${1?Usage: $0 <bin_dir> <oomify_dir>}"
OOMIFY_DIR="${2?Usage: $0 <bin_dir> <oomify_dir>}"

ENC_APP="$BIN_DIR/SvtJpegxsEncApp"
DEC_APP="$BIN_DIR/SvtJpegxsDecApp"
OOMIFY="$OOMIFY_DIR/oomify"

# libSvtJpegxs.so must be findable, as must oomify's liboomify.so.
if [ "$OOMIFY_DIR" = "$BIN_DIR" ]; then
    export LD_LIBRARY_PATH="${BIN_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
else
    export LD_LIBRARY_PATH="${BIN_DIR}:${OOMIFY_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

echo "=== OOMify fault-injection test ==="
echo "Encoder:         $ENC_APP"
echo "Decoder:         $DEC_APP"
echo "Oomify:          $OOMIFY"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo

error=0

# run_oomify_test <label> <cmd> [<args>...]
# 1. Dry-runs the command under oomify to count total allocations.
# 2. Loops over every allocation index (0..count-1), failing each one in turn.
# 3. Returns 1 immediately on crash, hang, or spawn error.
function run_oomify_test() {
    local label="$1"
    shift
    local cmd=("$@")
    local local_error=0

    echo "--- $label: dry run ---"
    local dry_out
    # 2>&1 >/dev/null: stderr (oomify messages + codec stderr) is captured;
    # stdout (codec stdout) is discarded.
    dry_out=$("$OOMIFY" -d -- "${cmd[@]}" 2>&1 >/dev/null)
    local dry_ret=$?
    echo "$dry_out"

    local count
    count=$(printf '%s\n' "$dry_out" \
            | grep -oE 'fallible allocations:[[:space:]]+[0-9]+' \
            | grep -oE '[0-9]+')
    if [ "$dry_ret" -ne 0 ] || [ -z "$count" ] || [ "$count" -eq 0 ]; then
        echo "ERROR: dry run failed (exit $dry_ret, count='${count:-?}') for $label"
        return 1
    fi
    echo "$label: $count allocations to test"
    echo

    echo "--- $label: fault injection (0..$((count - 1))) ---"
    local i run_out ret
    for i in $(seq 0 $(( count - 1 ))); do
        run_out=$(timeout 60 "$OOMIFY" -n "$i" -- "${cmd[@]}" 2>&1 >/dev/null)
        ret=$?

        if [ "$ret" -eq 124 ]; then
            echo "HANG at allocation $i: oomify killed after 60s"
            local_error=1
            break
        fi

        # Non-zero oomify exit below 128 means oomify_spawn() itself failed.
        if [ "$ret" -ne 0 ] && [ "$ret" -lt 128 ]; then
            echo "SPAWN ERROR at allocation $i: oomify exited with code $ret"
            local_error=1
            break
        fi

        # oomify reports child crashes on stderr as "terminated with signal N"
        if printf '%s\n' "$run_out" | grep -q "terminated with signal"; then
            echo "CRASH at allocation $i:"
            printf '%s\n' "$run_out" | grep "terminated with signal"
            local_error=1
            break
        fi
        # Defensive: guards against a future oomify version propagating child signal exit codes.
        if [ "$ret" -ge 128 ]; then
            echo "CRASH at allocation $i: oomify exited with code $ret (signal $((ret - 128)))"
            local_error=1
            break
        fi
    done

    if [ "$local_error" -eq 0 ]; then
        echo "$label: PASS ($count allocations tested)"
    else
        echo "$label: FAIL"
    fi
    echo
    return "$local_error"
}

# run_config_test <colour_format> <bit_depth> <width> <height> [extra_enc_args...]
# Creates a synthetic YUV frame, runs encoder OOM test, encodes a clean
# reference bitstream, then runs decoder OOM test.
_cfg=0
function run_config_test() {
    local fmt="$1" depth="$2" w="$3" h="$4"
    shift 4
    _cfg=$(( _cfg + 1 ))
    local label="${fmt} ${depth}bit ${w}x${h}${*:+ ($*)}"

    # 10/12-bit samples are stored as 16-bit words; 8-bit uses 1 byte/sample.
    local bps=$(( depth > 8 ? 2 : 1 ))
    local samples
    case "$fmt" in
        yuv422) samples=$(( w * h * 2 )) ;;
        yuv444) samples=$(( w * h * 3 )) ;;
        *) echo "ERROR: unsupported colour format '$fmt'"; error=1; return 1 ;;
    esac

    # Numeric key avoids long filenames when extra_enc_args are present.
    local yuv_file="$TMP_DIR/cfg${_cfg}.yuv"
    local jxs_enc_out="$TMP_DIR/cfg${_cfg}_enc.jxs"
    local jxs_dec_in="$TMP_DIR/cfg${_cfg}_ref.jxs"
    local dec_out="$TMP_DIR/cfg${_cfg}_dec.yuv"

    dd if=/dev/zero of="$yuv_file" bs=$(( samples * bps )) count=1 status=none

    local enc_args=(-w "$w" -h "$h"
                    --colour-format "$fmt"
                    --input-depth "$depth"
                    --bpp 3
                    -n 1 --no-progress 1 -v 1
                    "$@")

    # ── Encoder OOM test ─────────────────────────────────────────────────────
    run_oomify_test "encoder[$label]" \
        "$ENC_APP" -i "$yuv_file" -b "$jxs_enc_out" "${enc_args[@]}" \
        || error=1

    # ── Build clean reference bitstream for the decoder test ─────────────────
    if "$ENC_APP" -i "$yuv_file" -b "$jxs_dec_in" "${enc_args[@]}" \
           > /dev/null 2>&1; then
        # ── Decoder OOM test ─────────────────────────────────────────────────
        run_oomify_test "decoder[$label]" \
            "$DEC_APP" -i "$jxs_dec_in" -o "$dec_out" -v 1 \
            || error=1
    else
        echo "ERROR: reference encode failed for $label; skipping decoder test"
        error=1
    fi
}

# Configs 1-4: format x depth matrix — covers 10-bit input parsing and full-chroma planes.
run_config_test yuv422  8  64  16 --decomp_h 1 --decomp_v 0
run_config_test yuv422 10  64  16 --decomp_h 1 --decomp_v 0
run_config_test yuv444  8  64  16 --decomp_h 1 --decomp_v 0
run_config_test yuv444 10  64  16 --decomp_h 1 --decomp_v 0
# Config 5: production decomposition — covers multi-level DWT subband allocations.
# Config 6: vertical prediction — covers per-column line buffer allocations.
# 128x64 is the minimum frame size accepted by the encoder at decomp_h5_v2.
run_config_test yuv422  8 128  64 --decomp_h 5 --decomp_v 2
run_config_test yuv422  8  64  16 --decomp_h 1 --decomp_v 0 --coding-vpred 2

if [ "$error" -ne 0 ]; then
    echo "OOMify test FAILED"
else
    echo "OOMify test PASSED"
fi
echo "Exit $0 script with exit $error"
exit $error
