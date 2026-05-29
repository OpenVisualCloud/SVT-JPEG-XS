#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Encoder Test"
source ./FFmpegCommonLib.sh

path_correct="$path_global/encoder_tests"

echo "FFmpeg Encoder Test"
echo "FFmpeg:          $exec_ffmpeg"
echo "Path correct:    $path_correct"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Test FFmpeg encode from raw YUV to JPEG-XS bitstream
# (1:expected error code) (2:md5 of output jxs) (3:name input yuv) (4:width) (5:height) (6:pix_fmt) (7:extra ffmpeg encoder params) (8:frames)
function test_ffmpeg_enc {
    exit_code=$1
    md5=$2
    name_yuv=$3
    width=$4
    height=$5
    pix_fmt=$6
    enc_params=$7
    frames=${8:-10}

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    path_yuv=$path_correct"/"$name_yuv".yuv"
    bin_name=$test_id_print"_"$name_yuv"_ffmpeg"
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".mov"

    # Encode using ffmpeg with libsvtjpegxs encoder
    cmd="$valgrind$exec_ffmpeg -y -f rawvideo -pix_fmt $pix_fmt -s ${width}x${height} -i $path_yuv -frames:v $frames -c:v libsvtjpegxs $enc_params $bin_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?

    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    # Check MD5 if encoding succeeded and MD5 is not IGNORE
    if [ $ret -eq 0 ] && [ "$md5" != "IGNORE" ]; then
        echo -n "Test MD5 Expect: $md5 "
        md5_t=$(md5sum ${bin_path} | awk '{ print $1 }')
        if [ "$md5" = "$md5_t" ]; then
            echo "OK"
        else
            echo "FAIL get $md5_t"
            error=1
            end
        fi
    elif [ "$md5" = "IGNORE" ]; then
        echo "IGNORE TEST OUTPUT"
    fi
}

# Test FFmpeg encode and then decode back, compare decoded output
# (1:name input yuv) (2:width) (3:height) (4:pix_fmt) (5:extra ffmpeg encoder params) (6:frames)
function test_ffmpeg_enc_dec {
    name_yuv=$1
    width=$2
    height=$3
    pix_fmt=$4
    enc_params=$5
    frames=${6:-10}

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    path_yuv=$path_correct"/"$name_yuv".yuv"
    bin_name=$test_id_print"_"$name_yuv"_ffmpeg_encdec"
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".mov"
    out_yuv_path="$tmp_dir/"$bin_name".yuv"

    # Encode using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -f rawvideo -pix_fmt $pix_fmt -s ${width}x${height} -i $path_yuv -frames:v $frames -c:v libsvtjpegxs $enc_params $bin_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Encode failed with error code: $ret"
        error=1
        end
    fi

    # Decode using ffmpeg
    cmd="$valgrind$exec_ffmpeg -y -c:v libsvtjpegxs -i $bin_path -f rawvideo -pix_fmt $pix_fmt $out_yuv_path"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Decode failed with error code: $ret"
        error=1
        end
    fi

    echo "OK encode-decode cycle completed"
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_basic_encode {
    # Basic encode tests - verify ffmpeg encoder produces valid output
    # 8bit YUV422
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3" 10
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 4" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 5" 5

    # 10bit YUV422
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le "-bpp 3" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le "-bpp 4" 5

    # 8bit YUV420
    test_ffmpeg_enc 0 IGNORE touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p "-bpp 4" 5

    # 10bit YUV420
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le "-bpp 4" 5
}

function test_encode_params {
    # Test various encoder parameters through ffmpeg
    # decomp_v and decomp_h
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -decomp_v 2 -decomp_h 5" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -decomp_v 2 -decomp_h 2" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -decomp_v 0 -decomp_h 2" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 4 -decomp_v 0 -decomp_h 1" 5

    # coding_signs
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -coding_signs 0" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -coding_signs 1" 5
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -coding_signs 2" 5

    # slice_height
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -slice_height 16" 5

    # quantization
    test_ffmpeg_enc 0 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3 -quant 1" 5
}

function test_encode_decode_cycle {
    # Test full encode-decode cycle - verify roundtrip works
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 3" 5
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 4" 5
    test_ffmpeg_enc_dec touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le "-bpp 3" 5
    test_ffmpeg_enc_dec touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p "-bpp 4" 5
    test_ffmpeg_enc_dec touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le "-bpp 4" 5
}

function test_encode_error_paths {
    # Test invalid parameters - expect failure
    test_ffmpeg_enc 1 IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p "-bpp 0.1" 5
}

echo "RUN BASIC ENCODE TESTS"
test_basic_encode

echo "RUN ENCODE PARAMETER TESTS"
[[ $run_fast -eq 0 ]] && test_encode_params

echo "RUN ENCODE-DECODE CYCLE TESTS"
test_encode_decode_cycle

echo "RUN ENCODE ERROR PATH TESTS"
[[ $run_fast -eq 0 ]] && test_encode_error_paths

common_lib_end_summary

echo "DONE FFmpeg Encoder OK"
error=0
end
