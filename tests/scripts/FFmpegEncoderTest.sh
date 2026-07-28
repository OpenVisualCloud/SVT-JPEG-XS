#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Encoder Test"
source ./CommonLib.sh

path_correct="$path_global/encoder_tests"
exec_ffmpeg="$exec_dec"

echo "Decode:          $decode_flag"
echo "Path correct:    $path_correct"
echo "ffmpeg:          $exec_ffmpeg"

error=0

function end {
    if [ $decode_flag -eq 0 ]; then
        rm -fr $tmp_dir
    fi

    if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# (1:expected error code, or "NONZERO" to accept any non-zero code)
# (2:expected md5 of raw bitstream, or "IGNORE" to skip the check)
# (3:name of input yuv, without .yuv extension, from encoder_tests/)
# (4:width) (5:height) (6:ffmpeg pix_fmt) (7:number of frames to encode)
# (8: all other jpegxs encoder AVOptions, e.g. "-bpp 3 -decomp_v 2 -decomp_h 5")
function test_enc {
    exit_code=$1
    md5=$2
    name_yuv=$3
    width=$4
    height=$5
    pix_fmt=$6
    frames=$7
    encoder_parameters=$8
    path_yuv=$path_correct"/"$name_yuv".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    name_postfix=$encoder_parameters
#   Rename space and - in filename to _ and remove double __
    name_postfix="${name_postfix// /_}"
    name_postfix="${name_postfix//-/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"

    bin_name=$test_id_print"_"$name_yuv"_"$name_postfix
#   Reduce file name to 100 chars, too long filename can not be opened.
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".jxs"
    out_yuv_path="$tmp_dir/"$bin_name".yuv"

#   Encode raw yuv to a raw jpegxs elementary stream and check expected error code.
    cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error -f rawvideo -pix_fmt $pix_fmt -s:v ${width}x${height} -r 25 -i $path_yuv -frames:v $frames -c:v libsvtjpegxs $encoder_parameters -f image2pipe $bin_path"
    echo "run command: $cmd"
    ${cmd}

    ret=$?
    if [ "$exit_code" = "NONZERO" ]; then
        if [ $ret -eq 0 ]; then
            echo "FAIL Expected non-zero error code, got: $ret"
            error=1
            end
        fi
        return
    fi
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $decode_flag -ne 0 ]; then
#       Check that the stream can be decoded back via the ffmpeg jpegxs_pipe demuxer/decoder
        if [ $ret -eq 0 ]; then
            cmd="$exec_ffmpeg -y -hide_banner -loglevel error -f jpegxs_pipe -c:v libsvtjpegxs -i $bin_path -f rawvideo $out_yuv_path"
            echo "run command: $cmd"
            ${cmd}
            ret=$?
            if [ $ret -ne 0 ]; then
                echo "FAIL Can not decode bitstream, error code: $ret"
                error=1
                end
            fi
        fi
    fi

#   Check bitstream is as expected
    if [ "$md5" = "IGNORE" ]; then
        echo "IGNORE TEST OUTPUT"
    else
        echo -n "Test MD5 Expect: $md5 "
        md5_t=`md5sum ${bin_path} | awk '{ print $1 }'`
        if [ "$md5" = "$md5_t" ]; then
            echo "OK"
        else
            echo "FAIL get $md5_t"
            error=1
            end
        fi
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_all {
#   8bit 1080p yuv422, bpp 3, decomp_v 2, decomp_h 5, coding-sigf 1, coding-vpred no_residuals,
#   coding-signs full, 10 frames.
#   NOTE: matches EncoderTest.sh test_rate_control()'s "--rc 1" row exactly (same expected md5),
#   confirming ffmpeg's default rate-control mode == SvtJpegxsEncApp's --rc 1.
    test_enc 0 3f24dcf3bdfd1184caacac7fa9989a78 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_residuals -coding-signs full"

#   Same input, coding-vpred/coding-signs disabled.
#   NOTE: matches EncoderTest.sh test_rate_control()'s other "--rc 1" row exactly.
    test_enc 0 820a3890a0b7748802672f37e0f90565 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs disable"

#   8bit 720p yuv420, bpp 4, decomp_v 2, decomp_h 5. NEW md5, pinned from this ffmpeg build.
    test_enc 0 84cd5feebcc0651157afafa92cc2724e touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p 5 "-bpp 4 -decomp_v 2 -decomp_h 5 -coding-sigf 0 -coding-vpred disable -coding-signs full"

#   10bit(le) 1080p yuv422, bpp 3, decomp_v 2, decomp_h 5. NEW md5, pinned from this ffmpeg build.
    test_enc 0 1e5989607bd7412547d16e29fd2064e5 touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   10bit(le) 1080p yuv422 single frame. NEW md5, pinned from this ffmpeg build.
    test_enc 0 94806b522fbe0409f15b29847b95728e signal_1080p_yuv422p_10bit_le_1_frame 1920 1080 yuv422p10le 1 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   Uncommon (odd) resolution 8bit yuv422 1920x1081. NEW md5, pinned from this ffmpeg build.
    test_enc 0 599c9b2c9b6f95d15a203e7a8f8e7c2f uncommon_resolution_8bit_422_1920x1081 1920 1081 yuv422p 1 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   Uncommon (odd) resolution 10bit(le) yuv420 1922x1082. NEW md5, pinned from this ffmpeg build.
    test_enc 0 e582583486adc5cbd6aa80adebfae9c9 uncommon_resolution_10bit_420_1922x1082 1922 1082 yuv420p10le 1 "-bpp 3 -decomp_v 1 -decomp_h 4 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   coding-vpred=no_coeffs (mode 2, "VPRED FULL Mode 2" in EncoderTest.sh). NEW md5, pinned from this ffmpeg build.
#   Previously untested value of the coding-vpred AVOption (only disable/no_residuals were covered above).
    test_enc 0 38f36f8115333adecb4a3367a4ef46b4 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_coeffs -coding-signs full"

#   coding-signs=fast (mode 1). NEW md5, pinned from this ffmpeg build.
#   Previously untested value of the coding-signs AVOption (only disable/full were covered above).
    test_enc 0 dab9a55934b4c46bb87cecf5d3efcc4b touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_residuals -coding-signs fast"

#   threads=4: ffmpeg's standard thread pass-through (-threads N), previously untested here.
#   NEW md5, pinned from this ffmpeg build - verified (by actually running both) to be byte-
#   identical to the same params without -threads (and with -threads 1), confirming thread count
#   only affects parallelism, not the encoded bitstream.
    test_enc 0 91780e4e9d32683b0a11583d3c0accb5 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-threads 4 -bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   coding-raw=0 (disable packet-based raw-mode coding, for legacy-decoder compatibility).
#   NEW md5, pinned from this ffmpeg build - previously untested value of the coding-raw AVOption
#   (default/unset is coding-raw=1/enabled, exercised implicitly by every other test above).
    test_enc 0 9138e221e7e8d138ca56dd20bea64b3e touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full -coding-raw 0"

#   cap-compat=1 (emit an empty CAP marker when no capability bit is required), combined with
#   coding-raw=0 so that no capability bit (raw-mode or 4:2:0) is actually set - this is the only
#   way to make cap-compat observable (verified: with the default coding-raw=1, the raw-mode
#   capability bit is always set, so cap-compat=1 produces byte-identical output to cap-compat=0
#   and the test would not actually exercise anything). NEW md5, pinned from this ffmpeg build.
    test_enc 0 e34aa650919f8c0c5cf380a836f6b736 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full -coding-raw 0 -cap-compat 1"

#   quantization=uniform. NEW md5, pinned from this ffmpeg build.
#   The quantization AVOption (deadzone/uniform) had zero coverage before.
    test_enc 0 69986efe852479b112898689b17e2784 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -quantization uniform"

#   slice_height=120 (10bit yuv422). NEW md5, pinned from this ffmpeg build.
#   The slice_height AVOption had zero coverage before.
    test_enc 0 ce3a0a35dec2548897753e5e196ec653 touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -slice_height 120"

#   decomp_v=0 path (only decomp_v 1/2 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 ebbd8c89dbbf4faa14cbaea356003e99 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 4 -decomp_v 0 -decomp_h 2 -coding-sigf 1 -coding-vpred disable"

#   bpp=5 (only bpp 3/4/0.05 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 2a5b347a944e05f343b203573f881d4e touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 5 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable"

#   10bit(le) yuv420 (only 8bit yuv420 and 10bit yuv422 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 2e6eeef88bc70abe6f64db08babab398 touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le 5 "-bpp 4 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable"

#   Error path: invalid (too low) bpp must be rejected by the plugin (non-zero ffmpeg exit code).
    test_enc NONZERO IGNORE touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 2 "-bpp 0.05 -decomp_v 2 -decomp_h 5"
}

test_all


common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi

#return error code
end
