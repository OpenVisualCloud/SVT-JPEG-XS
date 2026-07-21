#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Runs all ffmpeg jpegxs plugin functional tests in parallel.


echo "Example: $0 parallel_number path_to_conformance_tests path_to_ffmpeg_binary [...]"
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
echo "Run Parallel All FFmpeg NPROC: $nproc params: $script_params"
chmod +x ./CommonLib.sh
chmod +x ./ParallelScript.sh
chmod +x ./FFmpegDecoderConformanceTest.sh
chmod +x ./FFmpegDecoderMultiFramesTest.sh
chmod +x ./FFmpegEncoderTest.sh

error=0
if [[ $error -eq 0 ]]; then
    echo RUN FFMPEG DECODER CONFORMANCE TEST
    ./ParallelScript.sh $nproc ./FFmpegDecoderConformanceTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN FFMPEG DECODER MULTI FRAMES TEST
    ./ParallelScript.sh $nproc ./FFmpegDecoderMultiFramesTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN FFMPEG ENCODER TEST
    ./ParallelScript.sh $nproc ./FFmpegEncoderTest.sh $script_params
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
