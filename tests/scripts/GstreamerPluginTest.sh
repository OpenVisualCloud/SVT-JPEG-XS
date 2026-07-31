#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Smoke-tests the GStreamer svtjpegxs plugin (svtjpegxsenc/svtjpegxsdec) by
# running gst-launch-1.0 encode+decode round-trip pipelines against real
# conformance test vectors.
#
# A round trip is considered a PASS when both pipelines complete without
# error and the decoded output size matches the original input size exactly
# (JPEG XS is lossy at typical bits-per-pixel settings, so byte-identical
# output is NOT expected/required - only the size must match).
#
# Usage: ./GstreamerPluginTest.sh <conformance-tests-path> [tmp_dir]
#   1: path to conformance test resources; expects a reference_decode/
#      subdir with raw planar YUV files named
#      NNN_WIDTHxHEIGHT_BITDEPTHbit_FORMAT.yuv
#   2: optional temp dir for intermediate .jxsc/.yuv files (default: tmp_gst)
#
# Requires gst-launch-1.0/gst-inspect-1.0 to already be on PATH with
# GST_PLUGIN_PATH/LD_LIBRARY_PATH set up to expose the svtjpegxs plugin
# (see build_gstreamer_plugin.sh, which writes a gst-plugin-env.sh to source).

set -u

INPUT_FILES_PATH=${1:?"Usage: $0 <conformance-tests-path> [tmp_dir]"}
TMP_DIR=${2:-tmp_gst}

REF_DIR="$INPUT_FILES_PATH/reference_decode"

error=0
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

# Use a fresh registry cache: self-hosted runners persist $HOME across CI
# runs, and a stale ~/.cache/gstreamer-1.0 registry (e.g. from a previous
# run's failed plugin load attempt) can wrongly hide/blacklist the plugin.
export GST_REGISTRY="$TMP_DIR/registry.bin"

echo "=== gst-inspect-1.0 svtjpegxs ==="
if ! gst-inspect-1.0 svtjpegxs; then
    echo "FAIL: svtjpegxs plugin not found/loadable"
    exit 1
fi

# name width height gst-format-nick jxsc-sampling depth bits-per-pixel
TEST_CASES=(
    "200_1520x1200_8bit_YUV420.yuv 1520 1200 i420 YCbCr-4:2:0 8 4"
    "002_4064x2704_8bit_YUV422.yuv 4064 2704 y42b YCbCr-4:2:2 8 4"
    "011_4064x2704_10bit_YUV422.yuv 4064 2704 i422-10le YCbCr-4:2:2 10 5"
    "013_4096x1744_10bit_YUV444.yuv 4096 1744 y444-10le YCbCr-4:4:4 10 6"
    "202_976x650_12bit_YUV420.yuv 976 650 i420-12le YCbCr-4:2:0 12 6"
    "206_976x650_12bit_YUV422.yuv 976 650 i422-12le YCbCr-4:2:2 12 6"
    "204_976x650_12bit_YUV444.yuv 976 650 y444-12le YCbCr-4:4:4 12 8"
)

function run_case {
    local name=$1 width=$2 height=$3 nick=$4 sampling=$5 depth=$6 bpp=$7
    local in="$REF_DIR/$name"
    local jxsc="$TMP_DIR/${name%.yuv}.jxsc"
    local dec="$TMP_DIR/${name%.yuv}.decoded.yuv"

    echo "--- Testing $name ($nick, ${width}x${height}, ${depth}-bit) ---"
    if [ ! -f "$in" ]; then
        echo "FAIL: input file not found: $in"
        error=1
        return
    fi

    local orig_size
    orig_size=$(stat -c%s "$in")

    echo "run command: gst-launch-1.0 filesrc location=$in ! rawvideoparse format=$nick width=$width height=$height framerate=25/1 ! svtjpegxsenc bits-per-pixel=$bpp ! filesink location=$jxsc"
    if ! gst-launch-1.0 \
        filesrc location="$in" ! \
        rawvideoparse format="$nick" width="$width" height="$height" framerate=25/1 ! \
        svtjpegxsenc bits-per-pixel="$bpp" ! \
        filesink location="$jxsc"; then
        echo "FAIL: encode pipeline for $name"
        error=1
        return
    fi

    local jxsc_size
    jxsc_size=$(stat -c%s "$jxsc")

    echo "run command: gst-launch-1.0 filesrc location=$jxsc blocksize=$jxsc_size ! image/x-jxsc,...,sampling=$sampling,depth=$depth,width=$width,height=$height ! svtjpegxsdec ! filesink location=$dec"
    if ! gst-launch-1.0 \
        filesrc location="$jxsc" blocksize="$jxsc_size" ! \
        "image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1" ! \
        svtjpegxsdec ! \
        filesink location="$dec"; then
        echo "FAIL: decode pipeline for $name"
        error=1
        return
    fi

    local dec_size
    dec_size=$(stat -c%s "$dec")
    if [ "$orig_size" -ne "$dec_size" ]; then
        echo "FAIL: $name decoded size $dec_size != original size $orig_size"
        error=1
        return
    fi
    echo "PASS: $name (codestream $jxsc_size bytes, decoded size matches original: $dec_size bytes)"
}

for case_line in "${TEST_CASES[@]}"; do
    run_case $case_line
done

rm -rf "$TMP_DIR"

if [ "$error" -ne 0 ]; then
    echo "Some GStreamer plugin pipeline tests FAILED"
    exit 1
fi

echo "All GStreamer plugin pipeline tests PASSED"
exit 0
