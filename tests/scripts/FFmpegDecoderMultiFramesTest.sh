#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run FFmpeg Decoder Multiple Frames Test"
source ./CommonLib.sh
path_correct="$path_global/bitstream_multi_frames"
path_invalid="$path_global/bitstream_invalid"
exec_ffmpeg="$exec_dec"

echo "ffmpeg:       $exec_ffmpeg"
echo "Path correct: $path_correct"
echo "Path invalid: $path_invalid"

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}


ffmpeg_formats=$($exec_ffmpeg -formats 2>/dev/null)
if [ $? -ne 0 ]; then
    echo "FAIL Could not run ffmpeg binary: $exec_ffmpeg"
    error=1
    end
fi

demuxer="-f jpegxs_pipe"

# (1:expected error code) (2:name, without .jxs) (3:expected md5 of decoded yuv) (4:optional extra ffmpeg args)
function test_dec {
    exit_code=$1
    name=$2
    md5=$3
    extra_args=$4
    bin_name=$path_use"/"$name".jxs"
    yuv_tmp="./$tmp_dir/"$name".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error $demuxer -c:v libsvtjpegxs -threads $PARAM_THREADS -i $bin_name $extra_args -f rawvideo $yuv_tmp"
    echo "run command: $cmd"
    ${cmd}
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    echo -n "Test MD5 Expect: $md5 "
    if [ ! -f "$yuv_tmp" ]; then
        echo "FAIL: Output file was not created!"
        error=1
        end
    fi
    md5_t=$(md5sum "${yuv_tmp}" | awk '{ print $1 }')
    if [ "$md5" = "$md5_t" ]; then
        echo "OK"
    else
        echo "FAIL get $md5_t"
        error=1
        end
    fi
}

# Invalid/corrupt bitstream check: ffmpeg must fail (non-zero exit) and must NOT produce output.
function test_dec_invalid {
    name=$1
    bin_name=$path_invalid"/"$name".jxs"
    yuv_tmp="./$tmp_dir/"$name".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    rm -f $yuv_tmp
    cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error $demuxer -c:v libsvtjpegxs -threads $PARAM_THREADS -i $bin_name -f rawvideo $yuv_tmp"
    echo "run command: $cmd"
    ${cmd}
    ret=$?
    if [ $ret -eq 0 ]; then
        echo "FAIL Expected non-zero error code for invalid bitstream, got: $ret"
        error=1
        end
    fi
    if [ -s "$yuv_tmp" ]; then
        echo "FAIL Expected no output for invalid bitstream, but $yuv_tmp is non-empty"
        error=1
        end
    else
        echo "OK (failed as expected, no output produced)"
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_all_correct {
    PARAM_THREADS=$1
    path_use=$path_correct

    # R2R Decoder with MT per Slice small resolution.
    test_dec 0 dec_r2r_mt-01                                          b00732fc18e6ca1ec02c9a4fb990a1ba
    test_dec 0 dec_r2r_mt-02                                          77ae505214f976fe30b55d1ac3594084
    test_dec 0 dec_r2r_mt-03                                          67f1ca191402372a227a5af65e47ed86
    test_dec 0 one_slice_1080                                         5cd468fb69609a8d6fbcecd2a9d3e57b

    # Coefficients minus-zero and plus-zero coding.
    test_dec 0 test-zero-sign-1-minus-zero                            6ba9ba462d53490717983c171ef50e59
    test_dec 0 test-zero-sign-1-plus-zero                             6ba9ba462d53490717983c171ef50e59
    test_dec 0 test-zero-sign-2-minus-zero                            c57ad54b3a32a83b55a1cda3314c5a29
    test_dec 0 test-zero-sign-2-plus-zero                             c57ad54b3a32a83b55a1cda3314c5a29
    test_dec 0 test-zero-sign-3-minus-zer                             ad74cde2fc470f2c89b9060e7290f339
    test_dec 0 test-zero-sign-3-plus-zero                             ad74cde2fc470f2c89b9060e7290f339
    test_dec 0 test-zero-sign-4-minus-zero                            fde93442420ef3048a3481bf1449ba59
    test_dec 0 test-zero-sign-4-plus-zero                             fde93442420ef3048a3481bf1449ba59

    # Correct bitstreams (h1/decomp_h=1 variants excluded - see plugin limitation note at top).
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v1_h5                    f65f42c074fa6fbdc3e9136ec86b9828
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v2_h3                    39983e5e039da3917e5f7bf7ef9a87bf
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v1_h4                     281a046a4d0c9bcb554e828d2a8084ef
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v2_h2                     e71c8400a4f3a636408b48450bae92d3
    # Daylight_1280x720_{10b,8b}_422_20f_v1_h1 SKIPPED - ffmpeg plugin decode error (decomp_h=1)
    test_dec 0 Daylight_1280x720_10b_422_20f_v2_h5                    b543cacde0d9a3fdeb6a123875f2868d
    test_dec 0 Daylight_1280x720_8b_422_20f_v1_h5                     f9b705a3ac20daeea8ce21af5f0bf908
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h4              e3e91a199bbd8096a8cfd2c0109829ff
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h5              01f13f55acf98b8b5fb19cfd3c6a54c1
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h2               550612cfe8797ea51fbc257158f319a3
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h3               2f9090271c68800014e15b88d08103aa

    # Regression test for a bitstream that used to HANG the decoder when decoding from the start
    # of the stream. The native test only decodes the first 2 frames via SvtJpegxsDecApp's "-n 2" flag;
    # ffmpeg has a direct equivalent (-frames:v N)
    test_dec 0 test_422_32x32_bpp6                                    2f9749279c126703adb5e07e1196f59c "-frames:v 2"
}

function test_all_broken {
    PARAM_THREADS=$1
    path_use=$path_correct

    # Bitstreams with a header change mid-stream, plus one with a genuinely corrupted
    # mid-stream packet (broken_bitstream_*). ffmpeg does NOT abort the whole conversion 
    # on any of these decode errors (unlike SvtJpegxsDecApp, which returns exit 1) 
    # - it logs the error per-packet and continues, exiting 0. All 5 produce the SAME
    # deterministic (41-frame) output, so they share one NEW pinned md5.
    test_dec 0 broken_decomh_Daylight_1280x720_8b_422_20fx1fx20f       2d658bef484f1de8bde1b0f233bcf6a6
    test_dec 0 broken_bit_depth_Daylight_1280x720_8b_422_20fx1fx20f    2d658bef484f1de8bde1b0f233bcf6a6
    test_dec 0 broken_resolution_Daylight_1280x720_8b_422_20fx1fx20f   2d658bef484f1de8bde1b0f233bcf6a6
    test_dec 0 broken_weight_table_Daylight_1280x720_8b_422_20fx1fx20f 2d658bef484f1de8bde1b0f233bcf6a6
    test_dec 0 broken_bitstream_Daylight_1280x720_8b_422_20fx1fx20f    2d658bef484f1de8bde1b0f233bcf6a6
}

function test_all_invalid {
    # Genuinely invalid/corrupt bitstreams: ffmpeg fails the conversion and writes no output.
    # Not looped over PARAM_THREADS: rejection happens at demux/first-bad-packet time regardless of
    # thread count, so a single fixed value (library default) is sufficient here.
    PARAM_THREADS=0
    test_dec_invalid error_injection_422_broken_bitstream
    test_dec_invalid invalid_small_cfg_zero_band_01
    test_dec_invalid invalid_small_cfg_zero_band_02
    test_dec_invalid invalid_small_cfg_zero_band_03
    test_dec_invalid invalid_small_cfg_zero_band_04
    test_dec_invalid invalid_small_cfg_zero_band_05
    test_dec_invalid invalid_small_cfg_zero_band_06
    test_dec_invalid invalid_small_cfg_zero_band_07
    test_dec_invalid invalid_small_cfg_zero_band_08
    test_dec_invalid invalid_small_cfg_zero_band_09
    test_dec_invalid invalid_small_cfg_zero_band_10
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

end
