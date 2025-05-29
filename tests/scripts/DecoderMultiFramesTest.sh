#!/bin/bash
#param 'help' print script params

echo "Run Decoder Multiple Frames Test"
source ./CommonLib.sh
path_correct="$path_global/bitstream_multi_frames"
path_invalid="$path_global/bitstream_invalid"

echo "DECODER : $exec_dec"
echo "Path correct: $path_correct"
echo "Path change: $path_change"

cmd="$exec_dec"
#${cmd}

error=0
PARAM_LP_NUM=-1
PARAM_ASM="max"
PARAM_PACKETIZATION="0"


function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        echo Exit $0 script with exit $error
        exit $error
    fi
}

function test_dec {
    exit_code=$1
    name=$2
    md5=$3
    params=$4
    bin_name=$path_use"/"$name".jxs"
    yuv_name=$path_use"/dec_yuv/"$name".yuv"
    yuv_tmp="./$tmp_dir/"$name".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
     if [ $ignore -ne 0 ]; then
        return
    fi

    cmd="$valgrind$exec_dec -i $bin_name -o $yuv_tmp --lp $PARAM_LP_NUM --asm $PARAM_ASM --packetization-mode $PARAM_PACKETIZATION "$params
    echo "run command: $cmd"
    ${cmd}

    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ -z "$md5" ]; then
        echo -n "Test diff: "
        cmd_cmp="diff $yuv_name $yuv_tmp"
        ${cmd_cmp} >  /dev/null
        ret=$?
        if [ $ret -ne 0 ]; then
           echo "FAIL comapare: $cmd_cmp"
           error=1
           end
        else
            echo "OK"
        fi
    else
        echo -n "Test MD5 Expect: $md5 "
        md5_t=`md5sum ${yuv_tmp} | awk '{ print $1 }'`
        if [ $md5 = $md5_t ]; then
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
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION=$3

    path_use=$path_correct
    #Test R2R Decoder with MT per Slice small resolution
    test_dec 0 dec_r2r_mt-01                                          b00732fc18e6ca1ec02c9a4fb990a1ba
    test_dec 0 dec_r2r_mt-02                                          77ae505214f976fe30b55d1ac3594084
    test_dec 0 dec_r2r_mt-03                                          67f1ca191402372a227a5af65e47ed86
    test_dec 0 one_slice_1080                                         5cd468fb69609a8d6fbcecd2a9d3e57b

    #Test Coefficients minus-zero and plus-zero coding
    test_dec 0 test-zero-sign-1-minus-zero                            6ba9ba462d53490717983c171ef50e59
    test_dec 0 test-zero-sign-1-plus-zero                             6ba9ba462d53490717983c171ef50e59
    test_dec 0 test-zero-sign-2-minus-zero                            c57ad54b3a32a83b55a1cda3314c5a29
    test_dec 0 test-zero-sign-2-plus-zero                             c57ad54b3a32a83b55a1cda3314c5a29
    test_dec 0 test-zero-sign-3-minus-zer                             ad74cde2fc470f2c89b9060e7290f339
    test_dec 0 test-zero-sign-3-plus-zero                             ad74cde2fc470f2c89b9060e7290f339
    test_dec 0 test-zero-sign-4-minus-zero                            fde93442420ef3048a3481bf1449ba59
    test_dec 0 test-zero-sign-4-plus-zero                             fde93442420ef3048a3481bf1449ba59

    #Test Correct bitstreams
    #test  |  Expect error code | dir to file | file name            | MD5 sum if empty look compare file in dir dec_yuv
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v1_h5                    f65f42c074fa6fbdc3e9136ec86b9828
    test_dec 0 Cyclist_1920x1080_10b_422_20f_v2_h3                    39983e5e039da3917e5f7bf7ef9a87bf
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v1_h4                     281a046a4d0c9bcb554e828d2a8084ef
    test_dec 0 Cyclist_1920x1080_8b_422_20f_v2_h2                     e71c8400a4f3a636408b48450bae92d3
    test_dec 0 Daylight_1280x720_10b_422_20f_v1_h1                    db9a6c952daf5e9c7b108e61b6d0e056
    test_dec 0 Daylight_1280x720_10b_422_20f_v2_h5                    b543cacde0d9a3fdeb6a123875f2868d
    test_dec 0 Daylight_1280x720_8b_422_20f_v1_h1                     d0c8097ead80367ffb50c33553b51795
    test_dec 0 Daylight_1280x720_8b_422_20f_v1_h5                     f9b705a3ac20daeea8ce21af5f0bf908
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h4              e3e91a199bbd8096a8cfd2c0109829ff
    test_dec 0 RollerCoaster_3840x2160_10b_422_20f_v1_h5              01f13f55acf98b8b5fb19cfd3c6a54c1
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h2               550612cfe8797ea51fbc257158f319a3
    test_dec 0 RollerCoaster_3840x2160_8b_422_20f_v2_h3               2f9090271c68800014e15b88d08103aa
}

function test_all_broken {
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION="0"

    # TEST WITH IGNORE SOME FRAMES
    #a. broken_decomh_Daylight_1280x720_8b_422_20fx1fx20f.jxs
    #1. 20frames 8bit yuv422 1280x720 v1 h5
    #2. 1frame   8bit yuv422 1280x720 v1 h3
    #3. 20frames 8bit yuv422 1280x720 v1 h5
    test_dec 1 broken_decomh_Daylight_1280x720_8b_422_20fx1fx20f       560d305916e20f010098bc31ece5d5b6

    #b.broken_bit_depth_Daylight_1280x720_8b_422_20fx1fx20f.jxs
    #1. 20frames 8bit  yuv422 1280x720 v1 h5
    #2. 1frames  10bit yuv422 1280x720 v1 h5
    #3. 20frames 8bit  yuv422 1280x720 v1 h5
    test_dec 1 broken_bit_depth_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6

    #c. broken_resolution_Daylight_1280x720_8b_422_20fx1fx20f.jxs
    #1. 20frames 8bit yuv422 1280x720  v1 h5
    #2. 1frames  8bit yuv422 1920x1080 v1 h5
    #3. 20frames 8bit yuv422 1280x720  v1 h5
    test_dec 1 broken_resolution_Daylight_1280x720_8b_422_20fx1fx20f   560d305916e20f010098bc31ece5d5b6

    #d. broken_bitstream_Daylight_1280x720_8b_422_20fx1fx20f.jxs
    #1. 20frames 8bit yuv422 1280x720 v1 h5
    #2. 1frame   8bit yuv422 1280x720 v1 h5 BROKEN BITSTREAM (wrong D[p,b] Bitplane count coding mode of band b)
    #3. 20frames 8bit yuv422 1280x720 v1 h5
    test_dec 1 broken_bitstream_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6

    #e. broken_weight_table_Daylight_1280x720_8b_422_20fx1fx20f.jxs
    #1. 20frames 8bit yuv422 1280x720 v1 h5 REF weight_table
    #2. 1frames   8bit yuv422 1280x720 v1 h5 MOD weight_table
    #3. 20frames 8bit yuv422 1280x720 v1 h5 REF weight_table
    test_dec 1 broken_weight_table_Daylight_1280x720_8b_422_20fx1fx20f 560d305916e20f010098bc31ece5d5b6


    #Test hang decoder tests (when try decode bitstream from begin)
    test_dec 0 test_422_32x32_bpp6                                     5fe89b9423b6c38a6a91c1ce81f90259 "-n 2"

    #Change PATH to bitstreams with error injection
    path_use=$path_invalid

    #Test invalid bitstreams
    test_dec 1 error_injection_422_broken_bitstream                    d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_01                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_02                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_03                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_04                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_05                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_06                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_07                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_08                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_09                          d41d8cd98f00b204e9800998ecf8427e
    test_dec 2 invalid_small_cfg_zero_band_10                          d41d8cd98f00b204e9800998ecf8427e
}

[[ $run_fast -eq 0 ]] && test_all_correct C 10 0
                         test_all_broken C 10
                         test_all_correct avx2 20 0
                         test_all_broken avx2 20
                         test_all_correct avx2 20 1
[[ $run_fast -eq 0 ]] && test_all_correct max 1 0
                         test_all_broken max 1
                         test_all_correct max 1 1


common_lib_end_summary

echo "DONE Multiple Frames OK"
error=0
end
