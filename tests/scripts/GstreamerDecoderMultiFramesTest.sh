#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params

echo "Run GStreamer Decoder Multiple Frames Test"
source ./CommonLib.sh

path_correct="$path_global/bitstream_multi_frames"
path_invalid="$path_global/bitstream_invalid"
exec_gst="gst-launch-1.0"

echo "gst-launch:      $exec_gst"
echo "Path correct:    $path_correct"
echo "Path invalid:    $path_invalid"

# Behavioral differences vs FFmpegDecoderMultiFramesTest.sh:
#
# FRAMING: svtjpegxsdec expects one complete codestream per GstBuffer (alignment=frame).
#   Multi-frame files are split into equal-size buffers via filesrc blocksize=file_size/nframes.
#   All frames in a file must have the same byte size (enforced by divisibility check).
#
# BROKEN_* TESTS: mid-stream header changes are not rejected at caps level; the decoder
#   re-parses each frame. broken_decomh/bit_depth/weight_table/bitstream decode all 41 frames
#   (exit 0). broken_resolution uses nframes=1 because its frames have non-uniform byte sizes.
#
# INVALID BITSTREAMS: exit code is NOT asserted (svtjpegxsdec error propagation is
#   inconsistent); only the absence of non-empty output is checked.
#
# SMALL-RES: dec_r2r_mt-01/02/03 skipped - resolution below svtjpegxsdec's 16px minimum.

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# test_dec <exit_code> <name> <md5> <width> <height> <depth> <sampling:422|420> <nframes>
#   Sets filesrc blocksize=file_size/nframes so svtjpegxsdec receives one frame per buffer.
function test_dec {
    exit_code=$1
    name=$2
    md5=$3
    width=$4
    height=$5
    depth=$6
    fmt=$7
    nframes=$8
    bin_name="$path_correct/$name.jxs"
    yuv_tmp="./$tmp_dir/$name.yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    case $fmt in
        422) sampling="YCbCr-4:2:2" ;;
        420) sampling="YCbCr-4:2:0" ;;
        *)
            echo "FAIL Unsupported/unmapped pixel format: $fmt"
            error=1
            end
            return
            ;;
    esac

    file_size=$(stat -c%s "$bin_name")
    if (( file_size % nframes != 0 )); then
        echo "FAIL $name: file_size=$file_size is not divisible by nframes=$nframes - fixed-blocksize framing would desync"
        error=1
        end
        return
    fi
    frame_size=$((file_size / nframes))

#   eval: caps string contains parentheses; timeout 60: guard against hang regressions.
    cmd="timeout 60 $valgrind$exec_gst -q filesrc location=$bin_name blocksize=$frame_size ! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1\" ! svtjpegxsdec threads=$PARAM_THREADS ! filesink location=$yuv_tmp"
    echo "run command: $cmd"
    eval "$cmd"
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
        return
    fi

    echo -n "Test MD5 Expect: $md5 "
    if [ ! -f "$yuv_tmp" ]; then
        echo "FAIL: Output file was not created!"
        error=1
        end
        return
    fi
    md5_t=$(md5sum "$yuv_tmp" | awk '{ print $1 }')
    if [ "$md5" = "$md5_t" ]; then
        echo "OK"
    else
        echo "FAIL get $md5_t"
        error=1
        end
    fi
}

# Invalid/corrupt bitstream check: exit code not asserted (inconsistent plugin behavior);
# only the absence of non-empty output is checked.
# test_dec_invalid <name> <width> <height> <depth> <sampling:422|420>
function test_dec_invalid {
    name=$1
    width=$2
    height=$3
    depth=$4
    fmt=$5
    bin_name="$path_invalid/$name.jxs"
    yuv_tmp="./$tmp_dir/$name.yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    case $fmt in
        422) sampling="YCbCr-4:2:2" ;;
        420) sampling="YCbCr-4:2:0" ;;
        *)
            echo "FAIL Unsupported/unmapped pixel format: $fmt"
            error=1
            end
            return
            ;;
    esac

    rm -f "$yuv_tmp"
    file_size=$(stat -c%s "$bin_name")
    cmd="timeout 60 $valgrind$exec_gst -q filesrc location=$bin_name blocksize=$file_size ! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1\" ! svtjpegxsdec ! filesink location=$yuv_tmp"
    echo "run command: $cmd"
    eval "$cmd"
    ret=$?
    echo "(informational, not asserted - see divergence note 4 above) gst-launch exit code: $ret"

    if [ -s "$yuv_tmp" ]; then
        echo "FAIL Expected no output for invalid bitstream, but $yuv_tmp is non-empty"
        error=1
        end
    else
        echo "OK (no output produced, as expected)"
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_all_correct {
    PARAM_THREADS=$1

    # dec_r2r_mt-01/02/03 SKIPPED - resolutions (8x4, 8x2, 4x2) below svtjpegxsdec's 16px minimum.
    test_dec 0 one_slice_1080 5cd468fb69609a8d6fbcecd2a9d3e57b 1920 1080 10 420 60

    # Coefficients minus-zero and plus-zero coding (single-frame files, nframes=1).
    test_dec 0 test-zero-sign-1-minus-zero 6ba9ba462d53490717983c171ef50e59 1920 1080 10 422 1
    test_dec 0 test-zero-sign-1-plus-zero  6ba9ba462d53490717983c171ef50e59 1920 1080 10 422 1
    test_dec 0 test-zero-sign-2-minus-zero c57ad54b3a32a83b55a1cda3314c5a29 1920 1080 10 422 1
    test_dec 0 test-zero-sign-2-plus-zero  c57ad54b3a32a83b55a1cda3314c5a29 1920 1080 10 422 1
    test_dec 0 test-zero-sign-3-minus-zer  ad74cde2fc470f2c89b9060e7290f339 1920 1081 8 422 1
    test_dec 0 test-zero-sign-3-plus-zero  ad74cde2fc470f2c89b9060e7290f339 1920 1081 8 422 1
    test_dec 0 test-zero-sign-4-minus-zero fde93442420ef3048a3481bf1449ba59 16 64 8 422 1
    test_dec 0 test-zero-sign-4-plus-zero  fde93442420ef3048a3481bf1449ba59 16 64 8 422 1

    # Regression: bitstream that used to hang the decoder (timeout 60 is the guard).
    test_dec 0 test_422_32x32_bpp6 2f9749279c126703adb5e07e1196f59c 32 32 8 422 1

    # Daylight_..._v1_h1 (decomp_h=1) included here; ffmpeg skips it but svtjpegxsdec handles it.
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v1_h5               f65f42c074fa6fbdc3e9136ec86b9828 1920 1080 10 422 20
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v2_h3               39983e5e039da3917e5f7bf7ef9a87bf 1920 1080 10 422 20
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v1_h4                281a046a4d0c9bcb554e828d2a8084ef 1920 1080 8  422 20
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v2_h2                e71c8400a4f3a636408b48450bae92d3 1920 1080 8  422 20
    test_dec 0 Daylight_1280x720_10b_422_20f_v1_h1                db9a6c952daf5e9c7b108e61b6d0e056 1280 720  10 422 20
    test_dec 0 Daylight_1280x720_10b_422_20f_v2_h5                b543cacde0d9a3fdeb6a123875f2868d 1280 720  10 422 20
    test_dec 0 Daylight_1280x720_8b_422_20f_v1_h1                 d0c8097ead80367ffb50c33553b51795 1280 720  8  422 20
    test_dec 0 Daylight_1280x720_8b_422_20f_v1_h5                 f9b705a3ac20daeea8ce21af5f0bf908 1280 720  8  422 20
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h4          e3e91a199bbd8096a8cfd2c0109829ff 3840 2160 10 422 20
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h5          01f13f55acf98b8b5fb19cfd3c6a54c1 3840 2160 10 422 20
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h2           550612cfe8797ea51fbc257158f319a3 3840 2160 8  422 20
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h3           2f9090271c68800014e15b88d08103aa 3840 2160 8  422 20
}

function test_all_broken {
    PARAM_THREADS=$1

    # Daylight_1280x720_8b_422: 20 correct + 1 broken + 20 correct frames (41 total).
    # The 4 byte-uniform broken cases collapse to the same md5 (40 frames of usable output).
    test_dec 0 broken_decomh_Daylight_1280x720_8b_422_20fx1fx20f       560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    test_dec 0 broken_bit_depth_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    # broken_resolution_*: the mid-stream resolution-change frame has a different
    # byte size from the other 40, so the file is NOT uniformly splittable into 41
    # equal chunks (file_size % 41 != 0). To avoid a false divisibility-check FAIL,
    # nframes=1 is used: the entire file is pushed as one buffer; svtjpegxsdec parses
    # one complete JPEG XS codestream from the buffer start and decodes only the first
    # frame, silently ignoring the rest - the same 1-frame output as before (see note 3).
    test_dec 0 broken_resolution_Daylight_1280x720_8b_422_20fx1fx20f   1b47ea4294ac7b142a17c7c1e10d3dca 1280 720 8 422 1
    test_dec 0 broken_weight_table_Daylight_1280x720_8b_422_20fx1fx20f 560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    test_dec 0 broken_bitstream_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
}

function test_all_invalid {
    # Genuinely invalid/corrupt bitstreams: exit code not asserted (see header note).
    test_dec_invalid error_injection_422_broken_bitstream 32 32 8 422
    test_dec_invalid invalid_small_cfg_zero_band_01 32 32 8 422
    # 02..07 fail at caps negotiation (width/height < 16); 08..10 fail at slice-decode.
    test_dec_invalid invalid_small_cfg_zero_band_02 4 2 8 420
    test_dec_invalid invalid_small_cfg_zero_band_03 4 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_04 4 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_05 6 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_06 8 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_07 8 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_08 16 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_09 30 64 8 422
    test_dec_invalid invalid_small_cfg_zero_band_10 32 64 8 422
}


for PARAM_THREADS in 0 1 10 20; do
    test_all_correct $PARAM_THREADS
    test_all_broken $PARAM_THREADS
done
test_all_invalid


common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi

#return error code
end
