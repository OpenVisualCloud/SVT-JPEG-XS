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

export CPATH="/usr/local/include/svt-jpegxs"
export LD_LIBRARY_PATH="/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# 1. Build and install SVT-JPEG-XS
if [ $BUILD -eq 1 ]; then
  echo "Building and installing SVT-JPEG-XS..."
  cd "$BUILD_DIR"
  ./build.sh install
  # 3. Build Fuzzy Tests
  cd "$FUZZY_DIR"
  echo "Building encoder fuzzer..."
  clang -lSvtJpegxs -fsanitize=fuzzer encoder.c -o SvtJxsEncFuzzer
  echo "Building decoder fuzzer..."
  clang -lSvtJpegxs -fsanitize=fuzzer decoder.c -o SvtJxsDecFuzzer
  chmod +x SvtJxsEncFuzzer
  chmod +x SvtJxsDecFuzzer
fi

# 4. Run Encoder Fuzzer
if [ $TEST_ENCODER -eq 1 ]; then
  cd "$FUZZY_DIR"
  echo "Running encoder fuzzer..."
  ./SvtJxsEncFuzzer -max_len=70 -rss_limit_mb=15000 -max_total_time=${TIMEOUT_SECONDS} -jobs=${JOBS_NUM}
fi

# 5. Prepare corpus directory for decoder fuzzer
if [ $TEST_DECODER -eq 1 ]; then
  cd "$FUZZY_DIR"
  mkdir -p ${DECODER_SAMPLES} && cp -r ${INPUT_FILES_PATH}/* ${DECODER_SAMPLES}/

  # 6. Run Decoder Fuzzer
  echo "Running decoder fuzzer..."
  ./SvtJxsDecFuzzer "$DECODER_SAMPLES" -rss_limit_mb=15000 -max_total_time=${TIMEOUT_SECONDS} -jobs=${JOBS_NUM}
fi