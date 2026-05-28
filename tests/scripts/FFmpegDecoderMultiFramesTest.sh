#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Decoder Multiple Frames Test"
source ./FFmpegCommonLib.sh

path_encoder_tests=$path_global"/encoder_tests"

echo "FFmpeg:       $exec_ffmpeg"
echo "Path encoder: $path_encoder_tests"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Test FFmpeg encode N frames then decode and verify frame count
# (1:name_yuv) (2:width) (3:height) (4:pix_fmt) (5:bpp) (6:frames_to_encode) (7:extra params)
function test_ffmpeg_multi_frame {
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
    bin_name=$test_id_print"_"$name_yuv"_${frames}f"
    bin_name="${bin_name:0:100}"
    mov_path="$tmp_dir/"$bin_name".mov"
    out_yuv_path="$tmp_dir/"$bin_name"_dec.yuv"

    # Encode N frames using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -f rawvideo -pix_fmt $pix_fmt -s ${width}x${height} -i $path_yuv -frames:v $frames -c:v libsvtjpegxs -bpp $bpp $extra_params $mov_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Encode failed with error code: $ret"
        error=1
        end
    fi

    # Decode all frames using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -i $mov_path -f rawvideo -pix_fmt $pix_fmt $out_yuv_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Decode failed with error code: $ret"
        error=1
        end
    fi

    # Verify output file exists
    if [ ! -f "$out_yuv_path" ]; then
        echo "FAIL: no decoded output file"
        error=1
        end
    fi

    # Calculate expected size
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
        echo "FAIL: size mismatch: expected=$expected_size ($frames frames) actual=$actual_size ($(( actual_size / frame_size )) frames)"
        error=1
        end
    fi

    echo "OK ($frames frames encoded+decoded, size=$actual_size)"
}

# Test encode with invalid params - expect failure
# (1:expected_code) (2:name_yuv) (3:width) (4:height) (5:pix_fmt) (6:bpp) (7:frames) (8:extra params)
function test_ffmpeg_enc_error {
    expected_code=$1
    name_yuv=$2
    width=$3
    height=$4
    pix_fmt=$5
    bpp=$6
    frames=$7
    extra_params=$8

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    path_yuv=$path_encoder_tests"/"$name_yuv".yuv"
    bin_name=$test_id_print"_error"
    mov_path="$tmp_dir/"$bin_name".mov"

    cmd="$valgrind$exec_ffmpeg -y -f rawvideo -pix_fmt $pix_fmt -s ${width}x${height} -i $path_yuv -frames:v $frames -c:v libsvtjpegxs -bpp $bpp $extra_params $mov_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?

    if [ $ret -eq 0 ]; then
        echo "FAIL: expected error code $expected_code but got success"
        error=1
        end
    fi

    echo "OK (expected failure, got code: $ret)"
}


rm -fr $tmp_dir
mkdir $tmp_dir


function test_multi_frame_counts {
    # Various frame counts - verify all frames survive encode-decode
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 1 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 2 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 10 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 20 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 60 ""
}

function test_multi_frame_formats {
    # Different formats with multiple frames
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 3 20 ""
    test_ffmpeg_multi_frame touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p 4 20 ""
    test_ffmpeg_multi_frame touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le 4 20 ""
}

function test_multi_frame_params {
    # Multi-frame with various encoder params
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 10 "-decomp_v 2 -decomp_h 5"
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 4 10 "-decomp_v 2 -decomp_h 2"
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 10 "-coding_signs 2"
    test_ffmpeg_multi_frame touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 10 "-slice_height 16"
}

function test_error_paths {
    # Invalid parameters - should fail
    test_ffmpeg_enc_error 1 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 0.1 5 ""
    test_ffmpeg_enc_error 1 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 3 5 "-decomp_v 5 -decomp_h 1"
}

echo "RUN MULTI-FRAME COUNT TESTS"
test_multi_frame_counts

echo "RUN MULTI-FRAME FORMAT TESTS"
[[ $run_fast -eq 0 ]] && test_multi_frame_formats

echo "RUN MULTI-FRAME PARAM TESTS"
[[ $run_fast -eq 0 ]] && test_multi_frame_params

echo "RUN ERROR PATH TESTS"
[[ $run_fast -eq 0 ]] && test_error_paths

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE FFmpeg Decoder Multi Frames OK"
fi

end

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE FFmpeg Decoder Multiple Frames OK"
fi

end
