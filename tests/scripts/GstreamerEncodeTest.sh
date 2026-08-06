#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params

echo "Run GStreamer Encoder Test"
source ./CommonLib.sh

path_correct="$path_global/encoder_tests"
exec_gst="gst-launch-1.0"

echo "Decode:          $decode_flag"
echo "Path correct:    $path_correct"
echo "gst-launch:      $exec_gst"

# svtjpegxsenc property mapping vs ffmpeg (not exposed in GStreamer: coding-vpred,
# coding-sigf, -profile:v, -level:v). Name differences: bpp→bits-per-pixel,
# quantization→quant-mode, slice_height→slice-height (range 1-16 only),
# coding-raw 0/1→coding-raw false/true, cap-compat 1→cap-compat true.
# All MD5s are measured from this plugin build.

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
# (4:width) (5:height) (6:ffmpeg-style pix_fmt) (7:number of frames to encode)
# (8: svtjpegxsenc property=value string, e.g. "bits-per-pixel=3 decomp-v=2 decomp-h=5")
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
#   Rename space and - and = in filename to _ and remove double __
    name_postfix="${name_postfix// /_}"
    name_postfix="${name_postfix//-/_}"
    name_postfix="${name_postfix//=/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"

    bin_name=$test_id_print"_"$name_yuv"_"$name_postfix
#   Reduce file name to 100 chars, too long filename can not be opened.
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".jxs"
    out_yuv_path="$tmp_dir/"$bin_name".yuv"

#   Map ffmpeg pix_fmt to GStreamer format nick; compute total bytes for N frames
#   (rawvideoparse has no frame-count limit, so frame count is enforced via head -c).
    local gst_fmt sampling depth bytes_per_frame
    case $pix_fmt in
        yuv422p)
            gst_fmt="y42b"
            sampling="YCbCr-4:2:2"
            depth=8
            bytes_per_frame=$((width * height * 2))
            ;;
        yuv420p)
            gst_fmt="i420"
            sampling="YCbCr-4:2:0"
            depth=8
            bytes_per_frame=$((width * height * 3 / 2))
            ;;
        yuv422p10le)
            gst_fmt="i422-10le"
            sampling="YCbCr-4:2:2"
            depth=10
            bytes_per_frame=$((width * height * 4))
            ;;
        yuv420p10le)
            gst_fmt="i420-10le"
            sampling="YCbCr-4:2:0"
            depth=10
            bytes_per_frame=$((width * height * 3))
            ;;
        *)
            echo "FAIL Unknown pix_fmt: $pix_fmt"
            error=1
            end
            return
            ;;
    esac
    local total_bytes=$((bytes_per_frame * frames))

#   Encode to raw jpegxs; eval is used because the command contains a shell pipe.
    cmd="head -c $total_bytes $path_yuv | $valgrind$exec_gst -q fdsrc ! rawvideoparse format=$gst_fmt width=$width height=$height framerate=25/1 ! svtjpegxsenc $encoder_parameters ! filesink location=$bin_path"
    echo "run command: $cmd"
    eval "$cmd"

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
#       Decode back via svtjpegxsdec: blocksize=total/frames (alignment=frame;
#       full file would decode only the first frame). Capsfilter must be fully specified.
        if [ $ret -eq 0 ]; then
            jxsc_size=$(stat -c%s "$bin_path")
            frame_jxsc_size=$((jxsc_size / frames))
            cmd="$exec_gst -q filesrc location=$bin_path blocksize=$frame_jxsc_size ! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1\" ! svtjpegxsdec ! filesink location=$out_yuv_path"
            echo "run command: $cmd"
            eval "$cmd"
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
#   8bit 1080p yuv422, bpp 3, decomp-v 2, decomp-h 5, coding-signs=full, 10 frames.
    test_enc 0 6cc8ed5b3e12fc937a9d93b1083a9d55 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-signs=full"

#   Same input, coding-signs=disable (default). Byte-identical to
#   EncoderTest.sh test_rate_control()'s "--coding-vpred 0 --coding-signs 0
#   --rc 0" row (same library defaults for the knobs GStreamer doesn't expose).
    test_enc 0 d5646d86e31816150472519976661acc touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 10 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-signs=disable"

#   8bit 720p yuv420, bpp 4, decomp-v 2, decomp-h 5.
    test_enc 0 74ff7770166e6c2b92815bd0006ce93c touchdown_720p_yuv420p_8_bit_60_frames 1280 720 yuv420p 5 "bits-per-pixel=4 decomp-v=2 decomp-h=5 coding-signs=full"

#   10bit(le) 1080p yuv422, bpp 3, decomp-v 2, decomp-h 5.
    test_enc 0 8d5aa27db12701843818b50886c28ab2 touchdown_1080p_yuv422p_10_bit_le_60_frames 1920 1080 yuv422p10le 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-signs=full"

#   10bit(le) 1080p yuv422 single frame.
    test_enc 0 17471b74eb34ab79628db43b7067c259 signal_1080p_yuv422p_10bit_le_1_frame 1920 1080 yuv422p10le 1 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-signs=full"

#   coding-signs=fast (only disable/full were covered above).
    test_enc 0 70431b494fa6db6f9738b9d4e95d3331 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-signs=fast"

#   quant-mode=uniform (only the default "deadzone" was covered above).
    test_enc 0 1dba4e6b034371cbdf960e8292a1c1fc touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 quant-mode=uniform"

#   rate-control-mode=cbr-slice (only the default "cbr-precinct" was covered above).
    test_enc 0 19563e0f7018cf70a4d41baa2cba6446 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 rate-control-mode=cbr-slice"

#   slice-height=8 (property range is only 1-16, unlike ffmpeg's slice_height AVOption).
    test_enc 0 f70b85f6ef928dba679ea8276068ba55 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 slice-height=8"

#   threads=4: byte-identical to EncoderTest.sh test_rate_control()'s
#   "--coding-sigf 1 --coding-vpred 0 --rc 0 -n 5" row - confirms thread
#   count only affects parallelism, not the encoded bitstream.
    test_enc 0 e65e13545fba3dfd64deb040b175ae58 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 threads=4"

#   coding-raw=false (disable packet-based raw-mode coding, for legacy-decoder
#   compatibility). Requires SVT-JPEG-XS API >= 0.10 (conditionally available property).
    test_enc 0 6b6624043be790343f5f60fb5474cc1e touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-raw=false"

#   cap-compat=true, combined with coding-raw=false so that no capability bit
#   is actually set - this is the only way to make cap-compat observable
#   (see FFmpegEncoderTest.sh for the same reasoning).
    test_enc 0 3a62981aa39562ee3dcc86701bf98908 touchdown_1080p_yuv422p_8_bit_60_frames 1920 1080 yuv422p 5 "bits-per-pixel=3 decomp-v=2 decomp-h=5 coding-raw=false cap-compat=true"

#   10bit(le) yuv420 (only 8bit yuv420 and 10bit yuv422 were covered above).
    test_enc 0 25d2c0af4843165b3351e677cf288b43 touchdown_1080p_yuv420p_10_bit_le_60_frames 1920 1080 yuv420p10le 5 "bits-per-pixel=4 decomp-v=2 decomp-h=5"
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
