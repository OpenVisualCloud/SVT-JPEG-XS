#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Runs all ffmpeg jpegxs plugin functional tests in parallel.


echo "Example: $0 parallel_number path_to_conformance_tests path_to_ffmpeg_binary [...]"
nproc=$1
path_global=$2
exec_ffmpeg=$3
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

# The `jpegxs_pipe` raw demuxer only exists natively in ffmpeg >= 8.1 (shipped upstream). Our own
# patches for 6.1/7.0/7.1/8.0 only add the codec type + mp4/mkv/mpegts container support, NOT the
# img2 pipe registration, and there is no working fallback for genuinely multi-frame elementary
# streams on those versions (see FFmpegDecoderMultiFramesTest.sh header comment). So detect support
# once here and skip what can't work instead of letting it fail every run on those versions:
#   - FFmpegDecoderMultiFramesTest.sh is skipped entirely (its whole test scenario needs
#     jpegxs_pipe to feed multi-frame raw codestreams to ffmpeg).
#   - FFmpegEncoderTest.sh's optional decode-verification step (triggered by a trailing "dec"
#     argument) also needs jpegxs_pipe, so "dec" is dropped from its params when unsupported;
#     the encode-side md5 checks still run either way.
supports_jpegxs_pipe=1
if ! $exec_ffmpeg -formats 2>/dev/null | grep -q "jpegxs_pipe"; then
    supports_jpegxs_pipe=0
    echo "ffmpeg build has no jpegxs_pipe demuxer (expected for ffmpeg < 8.1): skipping FFmpegDecoderMultiFramesTest.sh and the FFmpegEncoderTest.sh decode-verification step"
fi

script_params_no_dec="$path_global $exec_ffmpeg"

error=0
if [[ $error -eq 0 ]]; then
    echo RUN FFMPEG DECODER CONFORMANCE TEST
    ./ParallelScript.sh $nproc ./FFmpegDecoderConformanceTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 && $supports_jpegxs_pipe -eq 1 ]]; then
    echo RUN FFMPEG DECODER MULTI FRAMES TEST
    ./ParallelScript.sh $nproc ./FFmpegDecoderMultiFramesTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
elif [[ $error -eq 0 ]]; then
    echo "SKIP FFMPEG DECODER MULTI FRAMES TEST (jpegxs_pipe not supported by this ffmpeg build)"
fi
if [[ $error -eq 0 ]]; then
    echo RUN FFMPEG ENCODER TEST
    if [[ $supports_jpegxs_pipe -eq 1 ]]; then
        ./ParallelScript.sh $nproc ./FFmpegEncoderTest.sh $script_params
    else
        ./ParallelScript.sh $nproc ./FFmpegEncoderTest.sh $script_params_no_dec
    fi
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
