#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Exercises SvtJpegxsEncApp + SvtJpegxsDecApp across a small parameter matrix
# using whichever sanitizer the binaries were built with. Intended for the
# sanitizer CI jobs where the gtest harness can't run (e.g. MSan) or where an
# end-to-end pipeline check is wanted in addition to the unit tests.
#
# Exit status:
#   0  every encode+decode returned 0 (a halting sanitizer that finds an issue
#      aborts the app -> non-zero -> this script returns 1).
#   1  at least one encode or decode failed.
# Report-only sanitizers (e.g. integer with halt_on_error=0) do not abort, so
# findings are printed to the log but the script still returns 0.
#
# Usage: sanitizer_app_test.sh <bin_dir> [asm|auto]

set -u
BIN="${1:?usage: sanitizer_app_test.sh <bin_dir> [asm|auto]}"
ASM="${2:-auto}"
ENC="$BIN/SvtJpegxsEncApp"
DEC="$BIN/SvtJpegxsDecApp"
[ -x "$ENC" ] && [ -x "$DEC" ] || { echo "missing apps in $BIN"; exit 2; }
export LD_LIBRARY_PATH="$BIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

enc_ok=0; enc_fail=0; dec_ok=0; dec_fail=0
FAILS=""

asm_arg() { [ "$ASM" = "auto" ] && echo "" || echo "--asm $ASM"; }

size_bytes() {
  local w=$1 h=$2 depth=$3 fmt=$4 px=$((w * h)) bytes
  case $fmt in
    yuv420) bytes=$((px * 3 / 2)) ;;
    yuv422) bytes=$((px * 2)) ;;
    yuv444) bytes=$((px * 3)) ;;
    *)      bytes=$px ;;
  esac
  [ "$depth" -gt 8 ] && bytes=$((bytes * 2))
  echo "$bytes"
}

enc_dec() {
  local w=$1 h=$2 depth=$3 fmt=$4 bpp=$5; shift 5
  local extra="$*"
  local sz; sz=$(size_bytes "$w" "$h" "$depth" "$fmt")
  local tag="${w}x${h}_${depth}b_${fmt}_bpp${bpp}"
  [ -n "$extra" ] && tag="${tag}_$(echo "$extra" | tr -cd 'a-zA-Z0-9')"
  local in="$TMP/in_$tag.yuv" bs="$TMP/bs_$tag.jxs" out="$TMP/out_$tag.yuv"
  head -c "$sz" /dev/urandom > "$in"
  local desc="${w}x${h} ${fmt} ${depth}bit bpp=${bpp} ${extra}"
  echo "========== ENCODE: $desc =========="
  if "$ENC" -i "$in" -w "$w" -h "$h" --input-depth "$depth" \
        --colour-format "$fmt" -b "$bs" --bpp "$bpp" $(asm_arg) $extra; then
    enc_ok=$((enc_ok + 1))
    echo "========== DECODE: $desc =========="
    if "$DEC" -i "$bs" -o "$out" $(asm_arg); then
      dec_ok=$((dec_ok + 1))
    else
      local rc=$?; echo "DEC FAIL rc=$rc: $desc"; dec_fail=$((dec_fail + 1)); FAILS="$FAILS\n  DEC rc=$rc: $desc"
    fi
  else
    local rc=$?; echo "ENC FAIL rc=$rc: $desc"; enc_fail=$((enc_fail + 1)); FAILS="$FAILS\n  ENC rc=$rc: $desc"
  fi
}

# ---- Parameter matrix (resolutions, colour formats, bit depths, coding opts) ----
enc_dec 1920 1080  8 yuv422 3
enc_dec 1920 1080  8 yuv420 2
enc_dec 1280  720 10 yuv422 4
enc_dec  640  480 10 yuv444 5
enc_dec  176  144  8 yuv420 1
enc_dec 3840 2160  8 yuv420 2
enc_dec  512  512  8 yuv444 6
enc_dec 1280  720  8 yuv420 3 --decomp_v 2 --decomp_h 5
enc_dec 1280  720  8 yuv420 3 --quantization 1
enc_dec 1280  720  8 yuv420 3 --slice-height 16
enc_dec 1920 1080  8 yuv422 3 --lp 4

echo
echo "########## APP SANITIZER SUMMARY ##########"
echo "ENC ok=$enc_ok fail=$enc_fail | DEC ok=$dec_ok fail=$dec_fail"
if [ $((enc_fail + dec_fail)) -ne 0 ]; then
  echo -e "FAILURES:$FAILS"
  exit 1
fi
echo "No app-level failures."
exit 0
