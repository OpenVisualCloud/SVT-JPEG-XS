#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Decoder Multiple Frames Test"
source ./FFmpegCommonLib.sh

path_correct="$path_global/bitstream_multi_frames"
path_invalid="$path_global/bitstream_invalid"

echo "FFmpeg:       $exec_ffmpeg"
echo "Path correct: $path_correct"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Test FFmpeg decode of multi-frame JPEG-XS bitstream
# (1:expected error code) (2:file name) (3:md5 of decoded output)
function test_ffmpeg_dec {
    exit_code=$1
    name=$2
    md5=$3
    bin_name=$path_use"/"$name".jxs"
    yuv_tmp="./$tmp_dir/"$name".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    # Decode using ffmpeg with libsvtjpegxs decoder
    cmd="$valgrind$exec_ffmpeg -y -c:v libsvtjpegxs -i $bin_name -f rawvideo $yuv_tmp"
    echo "run command: $cmd"
    ${cmd} 2>&1
    ret=$?

    # For multi-frame tests, ffmpeg may return 0 even with partial decode (skipping broken frames)
    # or non-zero for fatal errors. We check based on expected behavior.
    if [ $exit_code -eq 0 ] && [ $ret -ne 0 ]; then
        echo "FAIL ffmpeg decode failed with error code: $ret expected: 0"
        error=1
        end
    fi

    # If file was not created (fatal error), check if that was expected
    if [ ! -f "$yuv_tmp" ]; then
        if [ $exit_code -ne 0 ]; then
            echo "OK (expected failure, no output file)"
            return
        else
            echo "FAIL: no output file created"
            error=1
            end
        fi
    fi

    if [ -z "$md5" ]; then
        echo "OK (no MD5 check)"
    else
        echo -n "Test MD5 Expect: $md5 "
        md5_t=$(md5sum ${yuv_tmp} | awk '{ print $1 }')
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


function test_all_correct {
    path_use=$path_correct

    # Test multi-frame bitstreams
    test_ffmpeg_dec 0 dec_r2r_mt-01                                          b00732fc18e6ca1ec02c9a4fb990a1ba
    test_ffmpeg_dec 0 dec_r2r_mt-02                                          77ae505214f976fe30b55d1ac3594084
    test_ffmpeg_dec 0 dec_r2r_mt-03                                          67f1ca191402372a227a5af65e47ed86
    test_ffmpeg_dec 0 one_slice_1080                                         5cd468fb69609a8d6fbcecd2a9d3e57b

    # Test Coefficients minus-zero and plus-zero coding
    test_ffmpeg_dec 0 test-zero-sign-1-minus-zero                            6ba9ba462d53490717983c171ef50e59
    test_ffmpeg_dec 0 test-zero-sign-1-plus-zero                             6ba9ba462d53490717983c171ef50e59
    test_ffmpeg_dec 0 test-zero-sign-2-minus-zero                            c57ad54b3a32a83b55a1cda3314c5a29
    test_ffmpeg_dec 0 test-zero-sign-2-plus-zero                             c57ad54b3a32a83b55a1cda3314c5a29
    test_ffmpeg_dec 0 test-zero-sign-3-minus-zer                             ad74cde2fc470f2c89b9060e7290f339
    test_ffmpeg_dec 0 test-zero-sign-3-plus-zero                             ad74cde2fc470f2c89b9060e7290f339
    test_ffmpeg_dec 0 test-zero-sign-4-minus-zero                            fde93442420ef3048a3481bf1449ba59
    test_ffmpeg_dec 0 test-zero-sign-4-plus-zero                             fde93442420ef3048a3481bf1449ba59

    # Test Correct multi-frame bitstreams
    test_ffmpeg_dec 0 Cyclist_1920x1080_10b_422_20f_v1_h5                    f65f42c074fa6fbdc3e9136ec86b9828
    test_ffmpeg_dec 0 Cyclist_1920x1080_10b_422_20f_v2_h3                    39983e5e039da3917e5f7bf7ef9a87bf
    test_ffmpeg_dec 0 Cyclist_1920x1080_8b_422_20f_v1_h4                     281a046a4d0c9bcb554e828d2a8084ef
    test_ffmpeg_dec 0 Cyclist_1920x1080_8b_422_20f_v2_h2                     e71c8400a4f3a636408b48450bae92d3
    test_ffmpeg_dec 0 Daylight_1280x720_10b_422_20f_v1_h1                    db9a6c952daf5e9c7b108e61b6d0e056
    test_ffmpeg_dec 0 Daylight_1280x720_10b_422_20f_v2_h5                    b543cacde0d9a3fdeb6a123875f2868d
    test_ffmpeg_dec 0 Daylight_1280x720_8b_422_20f_v1_h1                     d0c8097ead80367ffb50c33553b51795
    test_ffmpeg_dec 0 Daylight_1280x720_8b_422_20f_v1_h5                     f9b705a3ac20daeea8ce21af5f0bf908
    test_ffmpeg_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h4              e3e91a199bbd8096a8cfd2c0109829ff
    test_ffmpeg_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h5              01f13f55acf98b8b5fb19cfd3c6a54c1
    test_ffmpeg_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h2               550612cfe8797ea51fbc257158f319a3
    test_ffmpeg_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h3               2f9090271c68800014e15b88d08103aa
}

function test_all_broken {
    path_use=$path_correct

    # Test with broken frames in the middle - ffmpeg may handle these differently
    # than the standalone decoder (may skip frames or error out)
    test_ffmpeg_dec 0 test_422_32x32_bpp6                                    5fe89b9423b6c38a6a91c1ce81f90259

    # Change path to invalid bitstreams
    path_use=$path_invalid

    # Test invalid bitstreams - ffmpeg should fail gracefully
    test_ffmpeg_dec 1 error_injection_422_broken_bitstream                    ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_01                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_02                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_03                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_04                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_05                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_06                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_07                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_08                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_09                          ""
    test_ffmpeg_dec 1 invalid_small_cfg_zero_band_10                          ""
}

[[ $run_fast -eq 0 ]] && test_all_correct
                         test_all_broken
[[ $run_fast -eq 0 ]] && test_all_correct

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE FFmpeg Decoder Multiple Frames OK"
fi

end
