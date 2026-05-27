#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Decoder Conformance Test"
source ./FFmpegCommonLib.sh

path_bitstreams=$path_global
echo "FFmpeg:          $exec_ffmpeg"
echo "PATH BITSTREAMS: $path_bitstreams"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Test FFmpeg decode of JPEG-XS bitstream and compare against reference
# (1:bitstream name) (2:format description WxH_bits_format)
function test_ffmpeg_dec {
    name=$1
    format=$2
    yuv_name=$name"_"$format".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    # Decode using ffmpeg with libsvtjpegxs decoder
    cmd="$valgrind$exec_ffmpeg -y -c:v libsvtjpegxs -i $path_bitstreams/test_bitsreams/$name.jxs -f rawvideo ./$tmp_dir/$yuv_name"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL ffmpeg decode failed with error code: $ret"
        error=1
        end
    fi

    cmd_cmp="diff $path_bitstreams/reference_decode/$yuv_name ./$tmp_dir/$yuv_name"
    ${cmd_cmp} > /dev/null
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL compare: $cmd_cmp"
        error=1
        end
    fi

    echo "OK"
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_all {
    test_ffmpeg_dec 001 1024x436_8bit_YUV444
    test_ffmpeg_dec 002 4064x2704_8bit_YUV422
    test_ffmpeg_dec 003 4064x2704_8bit_YUV422
    test_ffmpeg_dec 004 4064x2704_8bit_YUV422
    test_ffmpeg_dec 005 4064x2704_8bit_YUV422
    test_ffmpeg_dec 006 4064x2704_8bit_YUV422
    test_ffmpeg_dec 007 4064x2704_8bit_YUV422
    test_ffmpeg_dec 008 4064x2704_8bit_YUV422
    test_ffmpeg_dec 009 4064x2704_8bit_YUV422
    test_ffmpeg_dec 010 11328x2704_11bit_YUV422
    test_ffmpeg_dec 011 4064x2704_10bit_YUV422
    test_ffmpeg_dec 012 4064x2704_10bit_YUV422
    test_ffmpeg_dec 013 4096x1744_10bit_YUV444
    test_ffmpeg_dec 014 4064x2704_10bit_YUV422
    test_ffmpeg_dec 015 4096x1744_10bit_YUV444
    test_ffmpeg_dec 016 4096x1744_10bit_YUV444
    test_ffmpeg_dec 017 4096x1744_10bit_YUV444
    test_ffmpeg_dec 018 4096x1744_10bit_YUV444
    test_ffmpeg_dec 019 4096x1743_13bit_COMPONENTS_4
    test_ffmpeg_dec 020 10496x2703_10bit_YUV422
    test_ffmpeg_dec 021 12192x2703_10bit_YUV422
    test_ffmpeg_dec 022 12287x1743_12bit_YUV444
    test_ffmpeg_dec 023 12192x2703_10bit_YUV422
    test_ffmpeg_dec 024 12287x1743_12bit_YUV444
    test_ffmpeg_dec 025 12287x1743_12bit_YUV444
    test_ffmpeg_dec 026 12191x2703_12bit_YUV444
    test_ffmpeg_dec 027 12287x1743_12bit_YUV444
    test_ffmpeg_dec 028 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 029 4095x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 030 4095x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 031 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 032 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 033 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 034 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 035 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 036 32x2703_10bit_YUV422
    test_ffmpeg_dec 037 32x2703_10bit_YUV422
    test_ffmpeg_dec 038 16x2703_10bit_YUV444
    test_ffmpeg_dec 039 32x2703_10bit_YUV422
    test_ffmpeg_dec 040 16x2703_10bit_YUV444
    test_ffmpeg_dec 041 16x2703_10bit_YUV444
    test_ffmpeg_dec 042 256x1199_8bit_COMPONENTS_4
    test_ffmpeg_dec 043 256x1199_8bit_COMPONENTS_4
    test_ffmpeg_dec 044 8192x1744_12bit_COMPONENTS_4
    test_ffmpeg_dec 045 8192x1744_12bit_COMPONENTS_4
    test_ffmpeg_dec 046 4095x1743_10bit_COMPONENTS_4
    test_ffmpeg_dec 047 4095x1743_10bit_COMPONENTS_4
    test_ffmpeg_dec 048 4064x2704_8bit_YUV422
    test_ffmpeg_dec 049 4064x2704_8bit_YUV422
    test_ffmpeg_dec 050 4073x1744_8bit_UNKNOWN_GRAY
    test_ffmpeg_dec 051 4064x2704_10bit_YUV422
    test_ffmpeg_dec 052 4064x2704_8bit_YUV422
    test_ffmpeg_dec 053 12287x1743_12bit_YUV444
    test_ffmpeg_dec 054 4064x2704_8bit_YUV422
    test_ffmpeg_dec 055 4064x2704_8bit_YUV422
    test_ffmpeg_dec 056 12287x1743_12bit_YUV444
    test_ffmpeg_dec 057 4064x2704_8bit_YUV422
    test_ffmpeg_dec 058 12191x2703_12bit_YUV444
    test_ffmpeg_dec 059 4095x1743_10bit_COMPONENTS_4
    test_ffmpeg_dec 060 4064x2704_8bit_YUV422
    test_ffmpeg_dec 061 12287x1743_12bit_YUV444
    test_ffmpeg_dec 062 4064x2704_8bit_YUV422
    test_ffmpeg_dec 063 12287x1743_12bit_YUV444
    test_ffmpeg_dec 064 4095x1743_10bit_COMPONENTS_4
    test_ffmpeg_dec 065 11328x2704_11bit_YUV422
    test_ffmpeg_dec 066 160x2704_10bit_YUV422
    test_ffmpeg_dec 200 1520x1200_8bit_YUV420
    test_ffmpeg_dec 201 1520x1200_8bit_YUV420
    test_ffmpeg_dec 202 976x650_12bit_YUV420
    test_ffmpeg_dec 203 976x650_12bit_YUV420
    test_ffmpeg_dec 204 976x650_12bit_YUV444
    test_ffmpeg_dec 205 976x650_12bit_YUV444
    test_ffmpeg_dec 206 976x650_12bit_YUV422
    test_ffmpeg_dec 207 976x650_12bit_YUV420
    test_ffmpeg_dec 208 976x650_12bit_YUV420
    test_ffmpeg_dec 209 976x650_12bit_COMPONENTS_4
    test_ffmpeg_dec 210 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 211 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 212 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 213 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 214 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 215 488x325_12bit_COMPONENTS_4
    test_ffmpeg_dec 216 488x325_14bit_COMPONENTS_4
    test_ffmpeg_dec 217 976x650_12bit_UNKNOWN_GRAY
    test_ffmpeg_dec 218 976x650_12bit_COMPONENTS_4
}

[[ $run_fast -eq 0 ]] && test_all
[[ $run_fast -eq 1 ]] && test_all

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE FFmpeg Decoder Conformance OK"
fi

end
