#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Decoder Conformance Test"
source ./FFmpegCommonLib.sh

path_encoder_tests=$path_global"/encoder_tests"
echo "FFmpeg:          $exec_ffmpeg"
echo "PATH ENCODER:    $path_encoder_tests"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Test FFmpeg encode-decode roundtrip: encode YUV → .mov, decode .mov → YUV,
# verify decoded output has correct size (frame_count * frame_size).
# (1:name_yuv) (2:width) (3:height) (4:pix_fmt) (5:bpp) (6:frames) (7:extra params)
function test_ffmpeg_enc_dec {
    name_yuv=$1
    width=$2
    height=$3
    pix_fmt=$4
    bpp=$5
    frames=$6
    extra_params=$7

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    path_yuv=$path_encoder_tests"/"$name_yuv".yuv"
    bin_name=$test_id_print"_"$name_yuv
    bin_name="${bin_name:0:100}"
    mov_path="$tmp_dir/"$bin_name".mov"
    out_yuv_path="$tmp_dir/"$bin_name"_dec.yuv"

    # Encode using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -f rawvideo -pix_fmt $pix_fmt -s ${width}x${height} -i $path_yuv -frames:v $frames -c:v libsvtjpegxs -bpp $bpp $extra_params $mov_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Encode failed with error code: $ret"
        error=1
        end
    fi

    # Decode using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -i $mov_path -f rawvideo -pix_fmt $pix_fmt $out_yuv_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Decode failed with error code: $ret"
        error=1
        end
    fi

    # Verify output file exists and has expected size
    if [ ! -f "$out_yuv_path" ]; then
        echo "FAIL: no decoded output file"
        error=1
        end
    fi

    # Calculate expected frame size based on pix_fmt
    case "$pix_fmt" in
        yuv420p)     frame_size=$(( width * height * 3 / 2 )) ;;
        yuv420p10le) frame_size=$(( width * height * 3 / 2 * 2 )) ;;
        yuv422p)     frame_size=$(( width * height * 2 )) ;;
        yuv422p10le) frame_size=$(( width * height * 2 * 2 )) ;;
        yuv444p)     frame_size=$(( width * height * 3 )) ;;
        yuv444p10le) frame_size=$(( width * height * 3 * 2 )) ;;
        *)           frame_size=0 ;;
    esac

    expected_size=$(( frame_size * frames ))
    actual_size=$(stat -c%s "$out_yuv_path")

    if [ "$actual_size" -ne "$expected_size" ]; then
        echo "FAIL: decoded size mismatch: expected=$expected_size actual=$actual_size"
        error=1
        end
    fi

    echo "OK ($frames frames, size=$actual_size)"
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_basic_formats {
    # 8bit YUV422 - various bpp
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 10 ""
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 4 10 ""
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 5 ""

    # 10bit YUV422
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 3 10 ""
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 4 5 ""

    # 8bit YUV420
    test_ffmpeg_enc_dec touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p 4 10 ""

    # 10bit YUV420
    test_ffmpeg_enc_dec touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le 4 10 ""
}

function test_encoder_params {
    # Various encoder params - verify decode still works
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-decomp_v 2 -decomp_h 5"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-decomp_v 2 -decomp_h 2"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-decomp_v 0 -decomp_h 2"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-coding_signs 0"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-coding_signs 1"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-coding_signs 2"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-slice_height 16"
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-quant 1"
}

function test_uncommon_resolutions {
    # Uncommon resolutions - 8bit 422
    test_ffmpeg_enc_dec uncommon_resolution_8bit_422_1920x1081 1920 1081 yuv422p 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_422_1920x1082 1920 1082 yuv422p 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_422_1920x1083 1920 1083 yuv422p 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_422_1922x1080 1922 1080 yuv422p 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_422_1924x1080 1924 1080 yuv422p 3 1 ""

    # Uncommon resolutions - 10bit 422
    test_ffmpeg_enc_dec uncommon_resolution_10bit_422_1920x1081 1920 1081 yuv422p10le 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_10bit_422_1920x1082 1920 1082 yuv422p10le 3 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_10bit_422_1922x1080 1922 1080 yuv422p10le 3 1 ""

    # Uncommon resolutions - 8bit 420
    test_ffmpeg_enc_dec uncommon_resolution_8bit_420_1920x1088 1920 1088 yuv420p 4 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_420_1922x1082 1922 1082 yuv420p 4 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_8bit_420_1928x1080 1928 1080 yuv420p 4 1 ""

    # Uncommon resolutions - 10bit 420
    test_ffmpeg_enc_dec uncommon_resolution_10bit_420_1920x1088 1920 1088 yuv420p10le 4 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_10bit_420_1922x1082 1922 1082 yuv420p10le 4 1 ""
    test_ffmpeg_enc_dec uncommon_resolution_10bit_420_1928x1080 1928 1080 yuv420p10le 4 1 ""
}

echo "RUN BASIC FORMAT TESTS"
test_basic_formats

echo "RUN ENCODER PARAM DECODE TESTS"
[[ $run_fast -eq 0 ]] && test_encoder_params

echo "RUN UNCOMMON RESOLUTION TESTS"
[[ $run_fast -eq 0 ]] && test_uncommon_resolutions

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE FFmpeg Decoder Conformance OK"
fi

end
