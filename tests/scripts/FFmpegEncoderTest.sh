#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params
#
# Functional test for the ffmpeg jpegxs encoder plugin (ffmpeg-plugin/libsvtjpegxsenc.c).
# Modeled on EncoderTest.sh, but drives an `ffmpeg` binary built with --enable-libsvtjpegxs
# instead of SvtJpegxsEncApp directly.
#
# Notes on parity with EncoderTest.sh:
# - The ffmpeg plugin exposes a SMALLER option set than SvtJpegxsEncApp: bpp, decomp_v, decomp_h,
#   slice_height, quantization, coding-signs, coding-sigf, coding-vpred, coding-raw, cap-compat,
#   threads. There is NO --rc/--asm/--lp/--profile/--packetization-mode equivalent. The library's
#   default rate-control mode (used whenever the plugin doesn't set it) has been confirmed, by
#   actually running ffmpeg and diffing md5s, to match SvtJpegxsEncApp's "--rc 1" mode exactly.
#   So two test cases below reuse EncoderTest.sh's existing committed "--rc 1" md5s verbatim
#   (see comments at each test_enc call). All other cases use NEW md5s, recorded once by running
#   this exact ffmpeg+plugin build against the same encoder_tests/*.yuv assets (pinned regression
#   values, not reused from EncoderTest.sh, since those all use other --rc values).
# - The raw elementary bitstream is extracted with `-f image2pipe` (NOT `-f data`, which rejects
#   video streams entirely on the ffmpeg 9.0 build used to validate this script).
# - EncoderTest.sh's huge test count (~2000 test_enc invocations) is dominated by two things that
#   don't apply here: (1) the whole matrix repeated across --asm/--lp/--profile/--packetization-mode,
#   none of which the ffmpeg plugin exposes as separate options, and (2) an exhaustive
#   test_uncommon_resolution() sweep of ~200 off-by-one-pixel width/height edge cases, which
#   stresses the encoder library's own padding/precinct-rounding logic and is independent of
#   which CLI wraps it. Beyond that axis, this script aims for at least one case per distinct
#   AVOption value exposed by the plugin: coding-vpred (disable/no_residuals/no_coeffs),
#   coding-signs (disable/fast/full), quantization (deadzone/uniform), slice_height, bpp
#   (3/4/5/0.05), decomp_v (0/1/2), decomp_h (2/4/5), threads (ffmpeg's standard -threads N
#   pass-through), coding-raw (0/1), cap-compat (0/1, tested together with coding-raw=0 - see
#   comment at that test_enc call for why cap-compat is otherwise unobservable), and all 3
#   supported pixel format/bit-depth combos (yuv420p 8bit, yuv422p 8/10bit, yuv420p 10bit).

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
#   Explicitly named "-c:v libsvtjpegxs" (not the generic "-c:v jpegxs") so this always exercises
#   the SVT plugin even on an ffmpeg build that also ships a native JPEG XS codec under the same
#   codec ID - defensive/self-documenting, a no-op on the patched builds used by this CI.
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
    test_enc 0 3f268410cc008cbdcc73b3c2b863bb8b touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_residuals -coding-signs full"

#   Same input, coding-vpred/coding-signs disabled.
#   NOTE: matches EncoderTest.sh test_rate_control()'s other "--rc 1" row exactly.
    test_enc 0 f7e14efd78c00b7a379ff71e6fb79f80 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs disable"

#   8bit 720p yuv420, bpp 4, decomp_v 2, decomp_h 5. NEW md5, pinned from this ffmpeg build.
    test_enc 0 df6deb36e8f31186ecd416e0f8012217 touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p 5 "-bpp 4 -decomp_v 2 -decomp_h 5 -coding-sigf 0 -coding-vpred disable -coding-signs full"

#   10bit(le) 1080p yuv422, bpp 3, decomp_v 2, decomp_h 5. NEW md5, pinned from this ffmpeg build.
    test_enc 0 3cee778d91ca4483441a76b4b8d3c6dd touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   10bit(le) 1080p yuv422 single frame. NEW md5, pinned from this ffmpeg build.
    test_enc 0 5ad84e608319f54d8fbded9fa76edff1 signal_1080p_yuv422p_10bit_le_1_frame 1920 1080 yuv422p10le 1 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   Uncommon (odd) resolution 8bit yuv422 1920x1081. NEW md5, pinned from this ffmpeg build.
    test_enc 0 dfeaa6b80837a161b3309ef227108007 uncommon_resolution_8bit_422_1920x1081 1920 1081 yuv422p 1 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   Uncommon (odd) resolution 10bit(le) yuv420 1922x1082. NEW md5, pinned from this ffmpeg build.
    test_enc 0 1dbe68ba9111d444469c2ed548143bf5 uncommon_resolution_10bit_420_1922x1082 1922 1082 yuv420p10le 1 "-bpp 3 -decomp_v 1 -decomp_h 4 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   coding-vpred=no_coeffs (mode 2, "VPRED FULL Mode 2" in EncoderTest.sh). NEW md5, pinned from this ffmpeg build.
#   Previously untested value of the coding-vpred AVOption (only disable/no_residuals were covered above).
    test_enc 0 7c033976b7550f9554e36615b6ef517a touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_coeffs -coding-signs full"

#   coding-signs=fast (mode 1). NEW md5, pinned from this ffmpeg build.
#   Previously untested value of the coding-signs AVOption (only disable/full were covered above).
    test_enc 0 f0ca8a697076790fdce0d85c057e187c touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred no_residuals -coding-signs fast"

#   threads=4: ffmpeg's standard thread pass-through (-threads N), previously untested here.
#   NEW md5, pinned from this ffmpeg build - verified (by actually running both) to be byte-
#   identical to the same params without -threads (and with -threads 1), confirming thread count
#   only affects parallelism, not the encoded bitstream.
    test_enc 0 ce4a5f06daaf0e30dfaec28be84d8c1f touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-threads 4 -bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full"

#   coding-raw=0 (disable packet-based raw-mode coding, for legacy-decoder compatibility).
#   NEW md5, pinned from this ffmpeg build - previously untested value of the coding-raw AVOption
#   (default/unset is coding-raw=1/enabled, exercised implicitly by every other test above).
    test_enc 0 b781b2b652e8e3efc2c8842ba48165d3 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full -coding-raw 0"

#   cap-compat=1 (emit an empty CAP marker when no capability bit is required), combined with
#   coding-raw=0 so that no capability bit (raw-mode or 4:2:0) is actually set - this is the only
#   way to make cap-compat observable (verified: with the default coding-raw=1, the raw-mode
#   capability bit is always set, so cap-compat=1 produces byte-identical output to cap-compat=0
#   and the test would not actually exercise anything). NEW md5, pinned from this ffmpeg build.
    test_enc 0 32ca9913c1d3cd8312938d854b2e4f41 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable -coding-signs full -coding-raw 0 -cap-compat 1"

#   quantization=uniform. NEW md5, pinned from this ffmpeg build.
#   The quantization AVOption (deadzone/uniform) had zero coverage before.
    test_enc 0 e60442f420cc09d3e6aecbf4cdceeb3f touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -quantization uniform"

#   slice_height=120 (10bit yuv422). NEW md5, pinned from this ffmpeg build.
#   The slice_height AVOption had zero coverage before.
    test_enc 0 93b6a3bd74307ec5c33c5beb731e25f3 touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 5 "-bpp 3 -decomp_v 2 -decomp_h 5 -slice_height 120"

#   decomp_v=0 path (only decomp_v 1/2 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 5569fb8b93f3e89554002d7b612ae244 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 4 -decomp_v 0 -decomp_h 2 -coding-sigf 1 -coding-vpred disable"

#   bpp=5 (only bpp 3/4/0.05 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 48f81ec2f73f358f512d86a43fe54d28 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "-bpp 5 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable"

#   10bit(le) yuv420 (only 8bit yuv420 and 10bit yuv422 were covered above). NEW md5, pinned from this ffmpeg build.
    test_enc 0 2ed262645c8a7331c85c1d80b56626ad touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le 5 "-bpp 4 -decomp_v 2 -decomp_h 5 -coding-sigf 1 -coding-vpred disable"

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
