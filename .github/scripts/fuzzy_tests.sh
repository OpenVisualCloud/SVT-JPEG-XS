#!/bin/bash
# Copyright(c) 2024 Intel Corporation
# SPDX-License-Identifier: BSD-2-Clause-Patent

set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_DIR/Build/linux"
FUZZY_DIR="$REPO_DIR/tests/FuzzyTests"

BUILD=0
TEST_ENCODER=0
TEST_DECODER=0

usage() {
  echo "Usage: $0 [-b] [-te] [-td]"
  echo "  -b   Only build"
  echo "  -te  Only run encoder tests"
  echo "  -td  Only run decoder tests"
  exit 1
}

if [ $# -eq 0 ]; then
  BUILD=0
  TEST_ENCODER=0
  TEST_DECODER=0
else
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -b)
        BUILD=1
        ;;
      -te)
        TEST_ENCODER=1
        ;;
      -td)
        TEST_DECODER=1
        ;;
      *)
        usage
        ;;
    esac
    shift
  done
fi

DECODER_SAMPLES="${DECODER_SAMPLES:-$FUZZY_DIR/decoder_corpus}"
ENC_APP="${SVT_INSTALL_DIR:-/usr/local}/bin/SvtJpegxsEncApp"

# Encode small random frames with SvtJpegxsEncApp to seed the decoder corpus with streams using
# coding tools the conformance streams do not cover (lossless, RCT, alpha formats, raw mode,
# slice packetization).
generate_decoder_seeds() {
  local out_dir="$1"
  local w=64
  local h=32
  local tmp_dir
  tmp_dir="$(mktemp -d)"
  local idx=0
  # Each entry: <colour-format> <input-depth> <samples per luma pixel x2> <extra encoder options>
  local configs=(
    "yuv422 8 4 --lossless 1 --coding-raw 0"
    "yuv422 10 4 --lossless 1 --coding-raw 0"
    "yuv422 12 4 --lossless 1"
    "rgb 8 6 --lossless 1 --color-transform 1"
    "rgb 10 6 --bpp 3 --color-transform 1"
    "yuva422 10 6 --bpp 4"
    "rgba 8 8 --bpp 4"
    "rgba 12 8 --lossless 1"
    "yuv444 8 6 --bpp 16"
    "yuv422 10 4 --bpp 3 --packetization-mode 1"
    "yuv420 8 3 --bpp 3 --coding-raw 0"
  )
  for cfg in "${configs[@]}"; do
    read -r fmt depth samples2 opts <<< "$cfg"
    local pixel_size=1
    if [ "$depth" -gt 8 ]; then
      pixel_size=2
    fi
    local in_file="$tmp_dir/in_${idx}.yuv"
    head -c $((w * h * samples2 / 2 * pixel_size)) /dev/urandom > "$in_file"
    # shellcheck disable=SC2086
    if ! "$ENC_APP" -i "$in_file" -b "$out_dir/seed_${idx}_${fmt}_${depth}bit.jxs" -w $w -h $h \
        --colour-format "$fmt" --input-depth "$depth" $opts > /dev/null 2>&1; then
      echo "Warning: failed to generate decoder seed: $cfg"
    fi
    idx=$((idx + 1))
  done
  rm -rf "$tmp_dir"
}

export CPATH="${SVT_INSTALL_DIR:-/usr/local}/include/svt-jpegxs"
export LD_LIBRARY_PATH="${SVT_INSTALL_DIR:-/usr/local}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# 1. Build and install SVT-JPEG-XS
if [ $BUILD -eq 1 ]; then
  if find "${SVT_INSTALL_DIR:-/usr/local}" -name 'SvtJpegxs.pc' 2>/dev/null | grep -q .; then
    echo "svt-jpegxs already installed under ${SVT_INSTALL_DIR:-/usr/local} (reused build artifact), skipping rebuild."
  else
    echo "Building and installing SVT-JPEG-XS..."
    cd "$BUILD_DIR"
    if [ -n "$SVT_INSTALL_DIR" ]; then
      ./build.sh install --prefix "$SVT_INSTALL_DIR"
    else
      ./build.sh install
    fi
  fi
  # 3. Build Fuzzy Tests
  cd "$FUZZY_DIR"
  echo "Building encoder fuzzer..."
  clang -L"${SVT_INSTALL_DIR:-/usr/local}/lib" -lSvtJpegxs -fsanitize=fuzzer encoder.c -o SvtJxsEncFuzzer
  echo "Building decoder fuzzer..."
  clang -L"${SVT_INSTALL_DIR:-/usr/local}/lib" -lSvtJpegxs -fsanitize=fuzzer decoder.c -o SvtJxsDecFuzzer
  chmod +x SvtJxsEncFuzzer
  chmod +x SvtJxsDecFuzzer
fi

# 4. Run Encoder Fuzzer
if [ $TEST_ENCODER -eq 1 ]; then
  cd "$FUZZY_DIR"
  echo "Running encoder fuzzer..."
  ./SvtJxsEncFuzzer -max_len=96 -rss_limit_mb=15000 -max_total_time=${TIMEOUT_SECONDS} -jobs=${JOBS_NUM}
fi

# 5. Prepare corpus directory for decoder fuzzer
if [ $TEST_DECODER -eq 1 ]; then
  cd "$FUZZY_DIR"
  mkdir -p "$DECODER_SAMPLES"
  if [ -n "$INPUT_FILES_PATH" ]; then
    if [ -z "$(ls -A "$INPUT_FILES_PATH" 2>/dev/null)" ]; then
      echo "Error: INPUT_FILES_PATH=$INPUT_FILES_PATH does not exist or is empty, expected the conformance bitstreams"
      exit 1
    fi
    cp -r "$INPUT_FILES_PATH"/* "$DECODER_SAMPLES"/
  fi
  if [ -x "$ENC_APP" ]; then
    echo "Generating decoder seed streams..."
    generate_decoder_seeds "$DECODER_SAMPLES"
  else
    echo "Warning: $ENC_APP not found, decoder corpus not extended with generated streams"
  fi

  # 6. Run Decoder Fuzzer
  echo "Running decoder fuzzer..."
  ./SvtJxsDecFuzzer "$DECODER_SAMPLES" -rss_limit_mb=15000 -max_total_time=${TIMEOUT_SECONDS} -jobs=${JOBS_NUM}
fi