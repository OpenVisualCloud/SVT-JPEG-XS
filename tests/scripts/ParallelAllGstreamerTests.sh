#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Runs all GStreamer svtjpegxs plugin functional tests in parallel.
# Unlike ParallelAllTests.sh/ParallelAllFFmpegTests.sh, no decoder/encoder
# binary path is needed as a parameter: all Gstreamer*.sh scripts invoke
# "gst-launch-1.0" found via $PATH, so the caller only needs to have sourced
# gst-plugin-env.sh (produced by .github/scripts/build_gstreamer_plugin.sh)
# before running this script, exactly like the Gstreamer*.sh scripts
# themselves are run directly in gstreamer_plugin_build.yaml.


echo "Example: $0 parallel_number path_to_conformance_tests [...]"
nproc=$1
script_params=""

index=0
for ARG in "$@"; do
    if (($index >= 1)); then
        #echo $ARG
        script_params="$script_params $ARG"
    fi
    index=$((index+1))
done

[[ -z "$nproc" ]] && nproc=1
echo "Run Parallel All GStreamer NPROC: $nproc params: $script_params"
chmod +x ./CommonLib.sh
chmod +x ./ParallelScript.sh
chmod +x ./GstreamerDecoderConformanceTest.sh
chmod +x ./GstreamerDecoderMultiFramesTest.sh
chmod +x ./GstreamerEncodeTest.sh

error=0
if [[ $error -eq 0 ]]; then
    echo RUN GSTREAMER DECODER CONFORMANCE TEST
    ./ParallelScript.sh $nproc ./GstreamerDecoderConformanceTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN GSTREAMER DECODER MULTI FRAMES TEST
    ./ParallelScript.sh $nproc ./GstreamerDecoderMultiFramesTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN GSTREAMER ENCODER TEST
    ./ParallelScript.sh $nproc ./GstreamerEncodeTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi

if [ $error -eq 0 ]; then
    echo "DONE OK"
else
    echo "FAIL !!"
fi

echo Exit $0 script with exit $error
exit $error
