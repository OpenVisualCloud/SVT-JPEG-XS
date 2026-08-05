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

# NOTE on parity with FFmpegEncoderTest.sh/EncoderTest.sh:
# The upstream GStreamer "svtjpegxs" plugin (svtjpegxsenc/svtjpegxsdec, in
# gst-plugins-bad, see .github/scripts/build_gstreamer_plugin.sh) does NOT
# expose every AVOption the ffmpeg plugin has. Checked against the actual
# element properties (gst-inspect-1.0 svtjpegxsenc/svtjpegxsdec):
#   - "coding-vpred" (visual/coefficient prediction) and "coding-sigf"
#     (significance flag) have NO equivalent GStreamer property at all -
#     they are not settable from svtjpegxsenc, so no test below can
#     exercise them and no MD5 parity claim is made for those knobs.
#   - "-profile:v"/"-level:v" (Ppih/Plev override) likewise have no
#     svtjpegxsenc property - omitted.
#   - "-bpp" is the "bits-per-pixel" property (double), not "bpp".
#   - "-quantization deadzone|uniform" is the "quant-mode" enum property
#     (values: deadzone, uniform), not "quantization".
#   - "-coding-raw 0|1" is the "coding-raw" BOOLEAN property (true/false),
#     inverted sense is the same (true = raw-mode coding enabled). Only
#     available when built against SVT-JPEG-XS API >= 0.10 (conditionally
#     available property).
#   - "-cap-compat 1" is the "cap-compat" BOOLEAN property (true/false),
#     same conditional-availability caveat as coding-raw.
#   - "-decomp_v"/"-decomp_h" map to "decomp-v"/"decomp-h" (unchanged
#     semantics/range).
#   - "-slice_height" maps to "slice-height", but the GStreamer property's
#     valid range is only 1-16 (vs. ffmpeg's much larger range) - this is a
#     real difference in the plugin's property definition, not a script
#     bug, so no attempt is made to reuse ffmpeg's "-slice_height 120"
#     value here.
# All MD5s below were measured directly against this plugin build (not
# copied from ffmpeg_encode_test.sh/EncoderTest.sh) - they are the actual
# output of gst-launch-1.0 for each parameter combination. A couple of them
# happen to be byte-identical to specific EncoderTest.sh/FFmpegEncoderTest.sh
# rows (called out in comments below) which is expected: same library, same
# effective parameters, so same bitstream.

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

#   Translate the ffmpeg-style pix_fmt into a rawvideoparse format nick and
#   the exact byte size of one raw frame (rawvideoparse has no "encode only
#   N frames" property, so the exact byte count for N frames has to be fed
#   in via "head -c" instead).
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

#   Encode raw yuv to a raw jpegxs elementary stream and check expected error code.
#   NOTE: uses eval (not a plain ${cmd} word-split call like the ffmpeg/App
#   scripts) because this command line contains a shell pipe ("head -c ... |
#   gst-launch-1.0 ..."), which needs real shell parsing to work.
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
#       Check that the stream can be decoded back via svtjpegxsdec.
#       NOTE: filesrc needs an exact "blocksize" (the full codestream size)
#       and the capsfilter must be fully specified (including width/height/
#       framerate) or the pipeline will not preroll - see
#       tests/scripts/GstreamerPluginTest.sh / repo notes for details.
        if [ $ret -eq 0 ]; then
            jxsc_size=$(stat -c%s "$bin_path")
            cmd="$exec_gst -q filesrc location=$bin_path blocksize=$jxsc_size ! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1\" ! svtjpegxsdec ! filesink location=$out_yuv_path"
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
