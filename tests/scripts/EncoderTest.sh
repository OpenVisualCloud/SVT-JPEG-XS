#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run Encoder Conformance Test"
source ./CommonLib.sh

path_correct="$path_global/encoder_tests"
exec_enc_rel="${exec_dec//SvtJpegxsDecApp/SvtJpegxsEncApp}"
#current CI not support delivery Debug
exec_enc_dbg="${exec_enc_rel//RelWithDebInfo/Debug}"
exec_enc_dbg="${exec_enc_dbg//Release/Debug}"

echo "Run Encoder Test"
echo "Decode:          $decode_flag"
echo "Path correct:    $path_correct"
echo "Decoder:         $exec_dec"
echo "Encoder Release: $exec_enc_rel"
echo "Encoder Debug:   $exec_enc_dbg"

error=0

function end {
#   CI output not required
    if [ $decode_flag -eq 0 ]; then
       rm -fr $tmp_dir
    fi

    if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# (1:expected error code) (2:md5 or test.jxs file if '.' in name) (3:name input yuv) (4: "all other parameters to encoder")
function test_enc {
    exit_code=$1
    name_yuv=$3
    path_yuv=$path_correct"/"$name_yuv".yuv"
    encoder_parameters=$4

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
     if [ $ignore -ne 0 ]; then
        return
    fi

    name_postfix=$encoder_parameters
#    Rename and space and - in filename to _ and remove double __
    name_postfix="${name_postfix// /_}"
    name_postfix="${name_postfix//-/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"
    name_postfix="${name_postfix//__/_}"


    if [[ "$2" == *"."* ]]; then
        yuv_compare_name=$path_correct"/enc_bin/"$2$name_postfix
    else
        md5=$2
    fi

    bin_name=$test_id_print"_"$name_yuv"_"$name_postfix
#   Reduce file name to 100 chars, too long filename can not be opened.
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".jxs"
    out_yuv_path="$tmp_dir/"$bin_name".yuv"

#   Encode and check expected error code
    cmd="$valgrind$exec_enc -i $path_yuv -b $bin_path $encoder_parameters"
    echo "run command: $cmd"
    run_cmd "$cmd"

    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $decode_flag -ne 0 ]; then
#       Check that stream is possible to decode
        if [ $ret -eq 0 ]; then
            cmd="$exec_dec -i $bin_path"
#           CI not required output YUV
            cmd="$cmd -o $out_yuv_path"
            echo "run command: $cmd"
            run_cmd "$cmd"
            ret=$?
            if [ $ret -ne 0 ]; then
                echo "FAIL Can not decode bitstream, error code: $ret"
                error=1
                end
            fi
#
#            TODO: A place to compare the source YUV and the decoded YUV and see if the quality doesn't drop too much
#
        fi
    fi

#   Check bitstream are as expected
    if [ -z "$md5" ]; then
        echo -n "Test diff: "
        cmd_cmp="diff $yuv_compare_name $yuv_tmp"
        ${cmd_cmp} >  /dev/null
        ret=$?
        if [ $ret -ne 0 ]; then
           echo "FAIL compare: $cmd_cmp"
           error=1
           end
        else
            echo "OK"
        fi
    elif [ $md5 = "IGNORE" ]; then
        echo "IGNORE TEST OUTPUT"
    else
        echo -n "Test MD5 Expect: $md5 "
        md5_t=`md5sum ${bin_path} | awk '{ print $1 }'`
        if [ $md5 = $md5_t ]; then
            echo "OK"
        else
            echo "FAIL get $md5_t"
#           Comment below 2 lines to generate md5 differences
            error=1
            end
        fi
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

#No real 4-component (alpha) sample YUV exists in $path_correct. Synthesize one on the fly (like the
#MSB-aligned test synthesizes a temp bitstream) from the real touchdown 8bit 422 sample: keep its
#real Y/Cb/Cr planes as-is and append a duplicate of the Y plane as a synthetic full-resolution alpha
#plane, giving a valid yuva422 (4:2:2:4) raw layout without adding any new external fixture file.
yuva422_w=1920
yuva422_h=1080
yuva422_frames=3
yuva422_y_size=$((yuva422_w * yuva422_h))
yuva422_c_size=$((yuva422_w * yuva422_h / 2))
yuva422_frame_size=$((yuva422_y_size + 2 * yuva422_c_size))
yuva422_src="$path_correct/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
yuva422_synth="$tmp_dir/synth_touchdown_1080p_yuva422p_8_bit_3_frames.yuv"
rm -f "$yuva422_synth"
for ((yuva422_f = 0; yuva422_f < yuva422_frames; yuva422_f++)); do
    yuva422_off=$((yuva422_f * yuva422_frame_size))
    tail -c +$((yuva422_off + 1)) "$yuva422_src" | head -c $yuva422_frame_size >> "$yuva422_synth"
    tail -c +$((yuva422_off + 1)) "$yuva422_src" | head -c $yuva422_y_size >> "$yuva422_synth"
done

#No real 4:4:4:4 (rgba/yuva444) sample YUV exists in $path_correct either. Synthesize one the same
#way: take the real Y (luma) plane of each frame and use 4 copies of it as the 4 full-resolution
#planes (4:4:4:4 has no chroma subsampling, so all 4 planes are simply w*h bytes each).
yuva444_w=1920
yuva444_h=1080
yuva444_frames=3
yuva444_plane_size=$((yuva444_w * yuva444_h))
yuva444_src="$path_correct/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
yuva444_synth="$tmp_dir/synth_touchdown_1080p_yuva444p_8_bit_3_frames.yuv"
rm -f "$yuva444_synth"
for ((yuva444_f = 0; yuva444_f < yuva444_frames; yuva444_f++)); do
    yuva444_off=$((yuva444_f * yuva422_frame_size))
    for ((yuva444_p = 0; yuva444_p < 4; yuva444_p++)); do
        tail -c +$((yuva444_off + 1)) "$yuva444_src" | head -c $yuva444_plane_size >> "$yuva444_synth"
    done
done

#Pre-existing "Experimental" formats (yuv444/rgb planar, rgbp packed) also have zero script coverage.
#Reuse the same real Y plane (3 copies instead of 4 - no alpha) for the planar 3-component case; the
#packed case only differs in how EncApp structures its in-memory buffer (one interleaved buffer vs 3
#separate arrays), not in total per-frame byte count, so the identical flat byte layout works as input
#for both - content correctness isn't the point here, exercising the code path deterministically is.
rgb_w=1920
rgb_h=1080
rgb_frames=3
rgb_plane_size=$((rgb_w * rgb_h))
rgb_src="$path_correct/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
rgb_synth="$tmp_dir/synth_touchdown_1080p_rgb_8bit_3_frames.yuv"
rm -f "$rgb_synth"
for ((rgb_f = 0; rgb_f < rgb_frames; rgb_f++)); do
    rgb_off=$((rgb_f * yuva422_frame_size))
    for ((rgb_p = 0; rgb_p < 3; rgb_p++)); do
        tail -c +$((rgb_off + 1)) "$rgb_src" | head -c $rgb_plane_size >> "$rgb_synth"
    done
done

#RUN different RC parameters Release/Debug/ ASM_C/ASM_MAX compare Parameters (1:asm) (2:lp number)
function test_rate_control {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#Variable RC
    test_enc 0 989fff01fadeed13fa40039e2a631f57 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 70b374d7bc085b0f75590be48b187b92 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b3dfe4cc034a65d2050b3437ed0fdfcc touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b3dfe4cc034a65d2050b3437ed0fdfcc touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b3f0e7495d30816487ae52fa932d5ce0 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c4cbd6aa6e1e74802c1f5986c211c57f touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4640850c328f3d0bc0ae466a9489d9e8 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4640850c328f3d0bc0ae466a9489d9e8 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#Variable RC error path
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   8bit tests RC
    test_enc 0 ce4e7b3760076f1ec144bd9b358460ca touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3ba07efb3c0eb7285b625d3d1aa4b6a7 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1930cc847631284d1714f5e61bfa0727 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a6bbf2e3c7532ae37f6ef43a4a4d595b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 560b050d9a71238a900588755091b0b4 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 79943d440cecefb2f4fc4a0e1f8ffec3 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6b3adced79add374c51ade9ed5378538 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3f6a913d57a901fd285d81c487159972 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit multiple frames
    test_enc 0 c8a2ca94dbd4396fb41f87680fa8c0b3 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 59669af4bb5fa2b634a36a6623a90b6a touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit signal tests RC
    test_enc 0 93347047a5fee214400d60767f0157f2 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6a5b8e333b449350eaffcf61f7a15503 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 96db90c25b4b3c6768090000049fea36 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit signal tests RC multiple V H
    test_enc 0 948d5a32b59e775b36e079a9edf3eedf signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f64a76b05f33931c86577ce1546cd92c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b614b4954589cab9871df94d9f9edb00 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e3ed040389980c0c9e5881e6c704ae80 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 17452b1ad366ca8bb6b4e4490c64bc39 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e2508ab6871ce16a2b2d162c9c109377 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0954a574fe96d57c38b21294c952dd7c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0b8d30231a4f5b8929b9654fca6a021f signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6160446481631ebe3c4009c6b5edaf50 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 890a2942b6a783ef0b9f5ec7d7ce0688 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7f6f74f4c778132b27899ac7af217575 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 43eabf118c3fef8ce4e952e71d0e31c8 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 98a09eb813b74aa32c67ad7c7583a4e5 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dacfe13597ac68b299ab5d03e44109c8 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   YUV420 RC 420 not support V:0
    test_enc 0 e2c0b0a79d7f1f612a6f4d801c3b588d touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5cc4cc74efd17ea61e4531b24ffafc02 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d73041e68c1c03e58d6c18be6c7cf8d1 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d42b0f177ff892842f56886842aa3c54 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 55635a3160a3bd96afcc91030ed516a4 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 979676a187416bdd85c18f56ec9bc572 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 73a4a95e5d3fbb6f962ceabb47c28598 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ecb3fdcdb4bfd99610614f75689a84f4 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5d31153a05acdce380c98f4ffb1a58fb touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 aa0ea4880679f4c3e3f4dfa571fa0b1f touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0743f50786758c277558c0df49e7043e touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1a3f0630784a3304fddac3d64a6212d6 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cfe4e90bcba0d09f13ca600fbdf5ff91 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 97bfd27d24ef37d34fa967c82b98913d touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 68f85e83b1caaa8a40594e0d59c74f9f touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4cde4238e544f958f74280bbd7b36f37 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e9a94cef69e6b46dfdcb87a5398a54ec touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c68f47e8b7e062bffcfd2ec9627eb9ad touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5c56785b4100e1ca8012c94930d4ea6e touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fe8f86862052a566e9b2c02958f27ced touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Vertical Prediction significance all modes
    test_enc 0 d57e11961fbbbbfc9afa5b954013776b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 48703c912508d02e6d317c97852db547 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 10cd3757667c8b48f56173bb53b60499 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0d8d85d42f3eb870c68f824cf53b1707 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c899ffc2a099a679caa1860bf240c4e8 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cfa1736d1b4fafd81e097cbcb3394ea3 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3187d12561f51d14152c66ca7cb318f3 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 e329a5d317a7ef295e42fc2d8fafbafa touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6d435ddaf8d6b9d59ca2e152aab5f0b7 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7e743192546dde0abb4bd38f004b95e9 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 be5bcee9bbe11858135a69687ae6d2ab touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a4eb668d7bbdb4553d1eab49dc18d454 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9c257e4ddff1e147a66ebb68c62cd697 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ef3a1d65e1caf43709741401eee9f096 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 a19323cb2b87a0a277074eb7ba0897ac touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 501abc3148402b87172351e8fb7568c6 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6438dd378d6345f9251e4ec92b9efc55 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 42ec61e7f18c66e8065f49185dcade17 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f63320d81fe0d5e04af2e8f00c6e2105 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ae8da2e9ae7992814d0a561e7575eb93 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 769258d30eb95ff5d2c4d7be9a2f78ae touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b220506e143c14551158f4ad93fd6fd5 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2b160821732c31fb94fcba36f756d5f5 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 aba9a2593e42593ec418a41888c07a10 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2f2262c2474126940a56def728091f37 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 91bbc34f298f1eee8ff4ecfb6fbeb8bd touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 480e4f692535f3002e930b08ff4d437d touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 755887460090dba21cb9e622352c186d touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 934a36fc41bb8ca990ebd5974f4ae845 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a5754f60b9baafd5e06cfa8c6e4f2afd touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 78805bde7bfad9e8d5f9ab6caf10f72a touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fa37d8e98f90c4a9908aeeb00272de79 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED FULL Mode 1
    test_enc 0 65b59e8a2e50477646f3be00c05d05bf touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 36d04c3b5cb4d3bc6be4e356e9b61a6f touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 58154b098e589d90144e3c9a976b70bb touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f0ab48e0b81ed03faf5945fa257ff5ae signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 16f9b3969d9d69c50b5211648f6fedd4 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4ade9d10b98cf1c37ffade5861c68da1 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7d36eb288ec94cc10466d3f8ba10df74 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 16f9b3969d9d69c50b5211648f6fedd4 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4c40cedec62a7cfeb52a32af157cc2ce signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 592119cff40d8d7f3278d7e203c16b4d signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c3ae67f0b8eddff62d807f258834ad10 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c72016ece86fd57d407112d079404610 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 88d64cff090133d69d9bee070ab9d87f touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED FULL Mode 2
    test_enc 0 fa7d3f6c773cfe5aaec31a39676a1b9b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a66101e991e715f325da5b2624753378 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 efd2a568be5f99e00916a3bb2bd38c87 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3b3a5e80038a4bc9b838058df967fd09 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ca69fc7f8431491a656c77ed1698724 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4d1a6c6f9cb2fc2baf88dea673b38aa3 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e95641a604ab0e696ff0a7aee0253f66 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ca69fc7f8431491a656c77ed1698724 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1706956aba7293db5e115c052555675c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d6920fe05dfbb17f8088657abaabdd09 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 911bcd1d403e18261441d8ea7d6e3efa signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a2e9b8adb043366568af1f6e60a1c853 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 95fcdb04fd0e40a071e425bbe907aa49 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#Qunatization uniform
    test_enc 0 46405bcc87dbda7cb9b0681c90f868ba touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6595f9c094b1832347e78ad5ea427135 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 57fd14ccb7afdb5fcbffca22035c48a8 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --quantization 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 428f5997ec6a6112840dc9e6124aeb0e signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1fa125f7b1a61b061051a28658feb244 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c14ab18433531ce1c2b2069be4fdcbf7 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dfc111b135c513db0ef175a9f6854bff touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3e8c2d29335e397a6eb28a374c892e20 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#slice height
    test_enc 0 185a3dd03e81c0a8a367a0982814127e touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --slice-height 7  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3bf903f73f12aa4b470431f2f85bd212 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --slice-height 12 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2244caf8eb311f8f885679b711d0784e touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --slice-height 18 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d361620a3ea19f3fa217d623a7d1d081 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --slice-height 20 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 862e88b0282b4d91ff1f64138ed4089f signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 24 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 83b6f67838c85b73679f6fde8f580ac3 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --slice-height 22 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a0a13283485cfd782890beb49e7e5d7 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 28 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f12b2d9e88df7c850eab834638967db9 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 32 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d9d201a740e03e473cb7cbf4397d226f touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 1080 --rc 0    --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}


#RUN different RC parameters Release/Debug/ ASM_C/ASM_MAX compare Parameters (1:asm) (2:lp number)
function test_rate_control_signs {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

    #Qunatization deadzone
    test_enc 0 150f8bcf2fac2b8107e4ad469c04084e touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 55dde06b2d88c1326c18f98a86aa2fa0 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7b1af53dc4e2ed47c103dcb4734645ed signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #Qunatization uniform
    test_enc 0 7dc8e45d12fad716813a6fc970657909 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1d7c729d65cf4edc714842b5cdee4282 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd372856adbad85c7dbae8eec34fc6f3 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Qunatization deadzone
    test_enc 0 8274ff5e03e5e5cb992fc1da7c141c1d touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ef1ada9260d3c376c743978786d8a591 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e0711eb7fd003f16ea588baee07da74c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #Qunatization uniform
    test_enc 0 ed08480a72930288906ea5fa21857e9b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9278a6233da1a2620057ed815c0c1591 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f3a6c394d02b0a5d27710e0ca9b4275a signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#RUN input-msb-aligned Parameters (1:asm) (2:lp number)
function test_msb_aligned {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   Explicit msb-aligned=0 must be byte-identical to the default (untouched) path
    test_enc 0 c8a2ca94dbd4396fb41f87680fa8c0b3 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5 --input-msb-aligned 0   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   msb-aligned=1 must produce a valid, decodable, deterministic bitstream
    test_enc 0 72b7163ce7fd8b8909f805c0b2c96a94 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5 --input-msb-aligned 1   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e49e0e78627a87165337ed915065c9c4 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --input-msb-aligned 1   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Reject any value other than 0/1
    test_enc 5 IGNORE                           signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --input-msb-aligned 2     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --input-msb-aligned 255   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

# (1:expected error code) (2:name input yuv) (3:frame size in bytes) (4:number of frames actually
# encoded, matches any "-n" in param 5) (5:"all other parameters to encoder")
# Lossless must reproduce the source exactly - unlike test_enc's bitstream-MD5 pinning (which only
# catches "output changed vs a snapshot"), this compares MD5 of the source bytes that were encoded
# against MD5 of the decoded output, directly verifying the actual bit-exact-lossless claim.
function test_enc_lossless {
    exit_code=$1
    name_yuv=$2
    frame_size_bytes=$3
    num_frames=$4
    encoder_parameters=$5
    path_yuv=$path_correct"/"$name_yuv".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    bin_name=$test_id_print"_"$name_yuv"_lossless"
    bin_name="${bin_name:0:100}"
    bin_path="$tmp_dir/"$bin_name".jxs"
    out_yuv_path="$tmp_dir/"$bin_name".yuv"

    cmd="$exec_enc -i $path_yuv -b $bin_path $encoder_parameters"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $ret -eq 0 ]; then
        cmd="$exec_dec -i $bin_path -o $out_yuv_path"
        echo "run command: $cmd"
        run_cmd "$cmd"
        ret=$?
        if [ $ret -ne 0 ]; then
            echo "FAIL Can not decode bitstream, error code: $ret"
            error=1
            end
        else
            bytes_to_compare=$((frame_size_bytes * num_frames))
            md5_src=`head -c $bytes_to_compare "$path_yuv" | md5sum | awk '{ print $1 }'`
            md5_dec=`md5sum "$out_yuv_path" | awk '{ print $1 }'`
            echo -n "Test lossless round-trip MD5 src=$md5_src dec=$md5_dec "
            if [ "$md5_src" = "$md5_dec" ]; then
                echo "OK"
            else
                echo "FAIL decoded output MD5 != source YUV MD5 (not bit-exact)"
                error=1
                end
            fi
        fi
    fi
}

#RUN --lossless (true lossless coding, Fq=0) smoke test Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_lossless {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   lossless is incompatible with slice_packetization_mode (hdr_Lcod must be known before the header
#   packet is released, but lossless's true final size is only known after every slice is packed) -
#   always encode with --packetization-mode 0 here regardless of what the driver passed in; the
#   rejection itself is covered explicitly below.
    test_enc_lossless 0 signal_1080p_yuv422p_10bit_le_1_frame       8294400 1 "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 --lossless 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 0"
    test_enc_lossless 0 touchdown_1080p_yuv422p_8_bit_60_frames     4147200 5 "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 --lossless 1 -n 5 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 0"
    test_enc_lossless 0 touchdown_720p_yuv420p_8_bit_60_frames      1382400 3 "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 --lossless 1 -n 3 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 0"
#   Compressible/flat content: exercises the compaction pass actually shrinking output below the CBR-equivalent size.
    test_enc_lossless 0 small_422_8bit_32x32-small-blank-small      2048    3 "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 --lossless 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 0"

#   Rejected combinations
    test_enc 5 IGNORE                           signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --lossless 1 --input-msb-aligned 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 0"
    test_enc 5 IGNORE                           signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --lossless 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode 1"
}

#(1:expected error code) (2:expected md5 or IGNORE) (3:"all other parameters to encoder")
#Independent of test_enc/$path_correct - always encodes the self-generated $yuva422_synth file.
function test_enc_yuva422_synth {
    exit_code=$1
    md5=$2
    encoder_parameters=$3

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    bin_name="${test_id_print}_yuva422_synth"
    bin_path="$tmp_dir/$bin_name.jxs"
    out_yuv_path="$tmp_dir/$bin_name.yuv"

    cmd="$exec_enc -i $yuva422_synth -b $bin_path $encoder_parameters"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $ret -eq 0 ]; then
        cmd="$exec_dec -i $bin_path -o $out_yuv_path"
        echo "run command: $cmd"
        run_cmd "$cmd"
        ret=$?
        if [ $ret -ne 0 ]; then
            echo "FAIL Can not decode bitstream, error code: $ret"
            error=1
            end
        fi
    fi

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

#RUN yuva422 (4:2:2:4, YUV422 + alpha) new-format smoke test Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_four_component_alpha {
    asm=$1
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

    test_enc_yuva422_synth 0 8c0aee24484e5caf4e54d2d1e1819db5 "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 4 --decomp_v 2 --decomp_h 5 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva422_synth 0 1edd6fa138be71d56bde15179a8034c2 "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva422_synth 0 78c30d9c784133e6673bd6b9bdd71b6f "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 4 --decomp_v 0 --decomp_h 1 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   yuva422's Y/Cb/Cr planes have 2:1 horizontal chroma subsampling like plain yuv422 - odd width must be rejected
    test_enc_yuva422_synth 5 IGNORE "-w $((yuva422_w + 1)) -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 4 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#(1:expected error code) (2:expected md5 or IGNORE) (3:"all other parameters to encoder")
#Independent of test_enc/$path_correct - always encodes the self-generated $yuva444_synth file.
function test_enc_yuva444_synth {
    exit_code=$1
    md5=$2
    encoder_parameters=$3

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    bin_name="${test_id_print}_yuva444_synth"
    bin_path="$tmp_dir/$bin_name.jxs"
    out_yuv_path="$tmp_dir/$bin_name.yuv"

    cmd="$exec_enc -i $yuva444_synth -b $bin_path $encoder_parameters"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $ret -eq 0 ]; then
        cmd="$exec_dec -i $bin_path -o $out_yuv_path"
        echo "run command: $cmd"
        run_cmd "$cmd"
        ret=$?
        if [ $ret -ne 0 ]; then
            echo "FAIL Can not decode bitstream, error code: $ret"
            error=1
            end
        fi
    fi

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

#RUN rgba/yuva444 (4:4:4:4) new-format smoke test Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_four_component_444 {
    asm=$1
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

    test_enc_yuva444_synth 0 ea11414c92b23483f4f22da0d6e953c2 "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --decomp_v 2 --decomp_h 5 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva444_synth 0 c3f7b45bd7a079911d0919e169c235de "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format yuva444 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva444_synth 0 bdf99dd1999816bcea14e279cd4add21 "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --decomp_v 0 --decomp_h 1 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   4:4:4:4 has no chroma subsampling - odd width must be ACCEPTED (contrast case vs yuva422 above)
    test_enc_yuva444_synth 0 94dc70e86b8f27bbad843171b3e8c453 "-w $((yuva444_w + 1)) -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#(1:expected error code) (2:expected md5 or IGNORE) (3:"all other parameters to encoder")
#Independent of test_enc/$path_correct - always encodes the self-generated $rgb_synth file.
function test_enc_rgb_synth {
    exit_code=$1
    md5=$2
    encoder_parameters=$3

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    bin_name="${test_id_print}_rgb_synth"
    bin_path="$tmp_dir/$bin_name.jxs"
    out_yuv_path="$tmp_dir/$bin_name.yuv"

    cmd="$exec_enc -i $rgb_synth -b $bin_path $encoder_parameters"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne $exit_code ]; then
        echo "FAIL Invalid error code: $ret expected: $exit_code"
        error=1
        end
    fi

    if [ $ret -eq 0 ]; then
        cmd="$exec_dec -i $bin_path -o $out_yuv_path"
        echo "run command: $cmd"
        run_cmd "$cmd"
        ret=$?
        if [ $ret -ne 0 ]; then
            echo "FAIL Can not decode bitstream, error code: $ret"
            error=1
            end
        fi
    fi

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

#RUN yuv444/rgb (planar, 3-comp, no alpha) and rgbp (packed, 3-comp) pre-existing "Experimental"
#formats - had zero script-level coverage before. Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_yuv444_rgb_and_rgbp {
    asm=$1
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   yuv444 (COLOUR_FORMAT_PLANAR_YUV444) and rgb (COLOUR_FORMAT_PLANAR_RGB) are coded the same way
#   without --color-transform - both must byte-match
    test_enc_rgb_synth 0 7d00da4db86a5386fc0de6d25f4551af "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format yuv444 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_rgb_synth 0 7d00da4db86a5386fc0de6d25f4551af "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb    --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_rgb_synth 0 60fac1bda955191ed05b90efd4ccc74f "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb    --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   rgbp (packed) - single interleaved buffer, distinct enum/code path from planar rgb/yuv444.
#   Known pre-existing TODO limitation: packed only works in Low latency threading model, not CPU_PROFILE_CPU.
    if [ "$cpu_profile" = "cpu" ]; then
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    else
        test_enc_rgb_synth 0 801921bbd24d8a33a5404bae7b56edb0 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 0 9bd4bd72735115e80d9ae2b032f8cf05 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    fi

#   No chroma subsampling on either format - odd width must be ACCEPTED
    test_enc_rgb_synth 0 e9b15767af0f5be07f969d7a66629cf2 "-w $((rgb_w + 1)) -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#RUN encoder-side forward RCT (--color-transform, Cpih=1) smoke test Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_color_transform {
    asm=$1
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   --color-transform 0 (explicit) must be byte-identical to the default (untouched) rgb path
    test_enc_rgb_synth 0 7d00da4db86a5386fc0de6d25f4551af "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 0 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    if [ "$cpu_profile" = "cpu" ]; then
#       enable_color_transform requires cpu_profile Low latency - rejected under cpu
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#       Regression: decomp_v 0 internally force-downgrades cpu_profile to Low latency (never makes
#       sense to run CPU threading with no vertical decomposition) - the enable_color_transform
#       validation must still reject this using the caller's originally-requested cpu profile, not
#       the already-downgraded one, or an explicit --profile cpu request would silently bypass it.
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 0 --decomp_h 2 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    else
#       --color-transform 1 must produce a valid, decodable, deterministic (cross-asm bit-exact) bitstream
        test_enc_rgb_synth 0 d2125daae126fb0e3ff81ba4c7beb52e "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 0 260713ec69374ceb7397f0b754de353f "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#       --color-transform needs input declared as RGB (Cpih=1 is decoded as R, G, B) - yuv444 is rejected
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format yuv444 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    fi

#   --color-transform requires planar RGB input (COLOUR_FORMAT_PLANAR_RGB, --colour-format rgb) -
#   rejected for subsampled planar (yuv422) and for packed (rgbp)
    test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

function test_uncommon_resolution {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   TEST SLICE HEIGHT EQUAL HEIGHT
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f5eb132c4002fc4661dc0f4b14d5cce7 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a4daaa52acb73567ea8138570498ea8a uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8dc6831377a485a06527e52ef94e2b3f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 216ba415a8be015fd45511211330e92f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 59cf7e86b1ba3adbbd2330657645fb38 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9b227e7896d490005eb523dcc00374d9 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3369b8c55fcebb67e8707e3c7d0c16bd uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 59c169c0127b9912f5394812beb7bc81 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c8f6fb165fff446f9bca186d6867fe6d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bff96e1cad563a76c1481680cef81f26 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 238b918e3b3d5d9f61a1b7ab81fda056 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cb15d5eeb88a2ab8e8dd0814f0678b4b uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 181d2bc64bb5e4ba7142fbfbe389f767 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8593f3fd81bb1a8333ab54f849512f3d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2bb8b0d01dc9e2ecbff060bed6809a0d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 82f564ec6117d250564d06b21356b011 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 821ee9cd3c632e8b775c20948c19a63f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ebd7cd0a1966509fd80f3a379efcfb9f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 70543bd97252c200d094732d95ed448f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 37199dc65d5a830df4b3e4a69eaf9c9f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4b1c71e0ba8cef495579dc0b3566349b uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9aac3d5dcbc2fa451cf4ce8f55fefd0d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3920d4c6b172564164674991db2cefad uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1a151a14e5da839a6dfc10ab833e4f63 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7d909e9ac337473a102c34f91956add9 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 497773f8fefa7d8fa5e9ac8be62c107f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b049c9f8d772244ee6291b935e5fac81 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e979c73b3a9a76ae40227ea2548d0553 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f2133a9da5e0f8c5ff3a00b6576d8711 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ce068ec0b9787802968751da01ff66dd uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e49a6e732f7c4a6ab0f6e36bc0aeaf88 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 269ab22796795ee03d94ee3a9d58eb6e uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 deb84db31d9d78d0735de189414a3d45 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6c0536af8a9a60380b4d71b5147ec9d5 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ca9b4eada32239b1508a496eb86f487 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2cb744500bf9da3b80baab32c7abe779 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 61f763c0e7dd71d14a1d21c653c3124c uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bd8baf8884faf8e38ded16e29ff02cb7 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 54f83e8a42e67e13efffc45e03d16a69 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 00f70f9d7f12079b6dace4413aa142b2 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 90ebae19544d4b74bd6489757298f652 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c4d2db5c0c5d204dc216d07c424d762e uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 2    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 3    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 92513162b77c8a89ce1acadd9ad087af uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 5    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 635b22b42a1c05f5d9b5f2be212783b0 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 6    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 7    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 74354f36dae1be34cc155899603b7d69 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1000 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1001 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9c4808ae6a98858fffb1639d358d032a uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1002 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1003 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f36aed6034088a301ea10878f600fa04 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1004 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1005 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5334dc8b7810a3477855c27993dd2563 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1006 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1007 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 682a2eddfa63e6cdd45cef683917ce7e uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 2    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 3    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dafd5f73e866365acec7dc663fa622e4 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 5    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 202bad8031d0d30494d180a63e958542 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 6    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 7    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2aaacfb8efa70d0ac7e83e2835853926 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1000 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1001 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 52e98cba3186d497d2e476237b9de8de uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1002 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1003 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0ac18683c2cbf995d28d99c830ed9cce uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1004 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1005 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d433c06118cbd853fc15bc509464ffd6 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1006 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1007 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1080    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #1 precincts 2 slices
    test_enc 0 d774345eb6a0c65eeb86574a7f1556c1 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 8   --slice-height 4 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 26bdca34315fd55590e5cdd0f16381f1 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4   --slice-height 2 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 53630a734666e1cd3beb7484c313943f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2   --slice-height 1 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 93f820c27988dc0bc15b5b9501861ed5 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 8   --slice-height 4 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 34b79ca3580edb17ce68058cdfc3ece3 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4   --slice-height 2 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #2 precincts 2 slices
    test_enc 0 ef3cf56f7a447e523baaeb6be4ba1602 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 16  --slice-height 8 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b34deaa0979b75e26b95d8059217c19f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 8   --slice-height 4 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f0dcf23e8257dbb988c2fd219cbf4c58 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4   --slice-height 2 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ffc2178a471e154e5b1369e7b8d88c44 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 16  --slice-height 8 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e8a092788d06ba6f6bdefa0cccabb21f uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 8   --slice-height 4 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #3 precincts 2 slices
    test_enc 0 3172992c8defd68a3511e9e2b90aefa3 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 24  --slice-height 12 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cc1aba96f184398683cfa72da093b9e8 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 12  --slice-height 6 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f1a5148c5a1bbd7211946a16933014b1 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6   --slice-height 3 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a06f4e3393a53ffaa9b5c5501ea07fd uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 24  --slice-height 12 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 51a252536a7dec73d7986d8b0b288937 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 12  --slice-height 6 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #5 precincts 2 slices
    test_enc 0 eb2d213ebed14abd38ce2d66578d3dac uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 40  --slice-height 20 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 32bb19088272db6aacea7aeef35ccfcb uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 20  --slice-height 10 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 342491a09442b2de88e218804dfa5bb7 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 10  --slice-height 5 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c406eae4380be5700673bcf90c56685b uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 40  --slice-height 20 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 11aa927cc9dd51c0ae4da081e956017b uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 20  --slice-height 10 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   TEST HEIGHT 422
    test_enc 0 14aeb6f828be61a6577825c9f0c5a988 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 038a928accedf04d3efb9bc0de5372bf uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 639c87f32a38298c191683a47b232ca6 uncommon_resolution_8bit_422_1920x1082      "-w 1920 -h 1082 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 948f5430763c4d58a193608cbedceac6 uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 55908fdd215cb14915a839140fbffcaf uncommon_resolution_8bit_422_1920x1084      "-w 1920 -h 1084 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2f3612a7ba35b61fe2ebc932dd938cb9 uncommon_resolution_8bit_422_1920x1085      "-w 1920 -h 1085 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 847493916501fe84805cca169dbe4992 uncommon_resolution_8bit_422_1920x1086      "-w 1920 -h 1086 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7f8783074d4c6b23b22072441e18ede7 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 7a06b2c46b5e866027413a8aff7ed59b uncommon_resolution_10bit_422_1920x1080     "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a05c2d0edb514f15b4c70f3771f9b4c8 uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9684edd2dc15d075a35a916ddc3056a9 uncommon_resolution_10bit_422_1920x1082     "-w 1920 -h 1082 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 965f6b49587d1ed6938d980b5b42a9e9 uncommon_resolution_10bit_422_1920x1083     "-w 1920 -h 1083 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dd18adbf9d8a3a93a117472e0a7274ee uncommon_resolution_10bit_422_1920x1084     "-w 1920 -h 1084 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f33916118b6824765d1ad9bb6e8bd5c6 uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5fa22a784192f7ae6f6c81b996dd7dac uncommon_resolution_10bit_422_1920x1086     "-w 1920 -h 1086 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a83de99eab249d6dfa07e77d4d36078f uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Different transformations height
    test_enc 0 985a8e54a37abfdb34dafdff5dfdce80 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3d507a60351b11982d910c1517101e62 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f45bfc8d5c9315227b127c5f155fc16a uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 069787e3ec097f1cd34400053551d405 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 317d25df825d3ae81ce08b8b26a21b3e uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 87c3218ae11c9e46a9d4116705af79e3 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 599290837e3f04a13e90e80bd0d5f6a6 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6fff67c3e731c248fd78edecbb99e52e uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a181143add8bd1499d154e6e0899e656 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64d75e594af1ee460573583f0ae1fcaf uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bd97da440c7c0a0a42258af3c346c8ab uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6152d67112d28fcbdfcb78bdcfb15b82 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fe46e4da75e6172e9d33ab77df16063c uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fc6445c2a3b3d14a492532bab371d8a8 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 e49305bf1fe82dc59c906461968308d5 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 795647ed3ecbae122ed67b65b1227e63 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 113be30dd9c77d6a4f12ed8b1447f393 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d086db69cc132f00bad3a7c1f6c7f707 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5bf3adf7b0381717ba1b17a2acb1f0f8 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 05efbeb23a2609ac0e29214e6044da9d uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 38ca087d7129b633a7647c576236be13 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a892d60a78ec313299f50d30bad3ed29 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 508192742f85b9bdf460a349fd374017 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f77b83df0bc16a31fd76e5fdcb366f54 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2b18292cf02f465f42a36a07b545d713 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f498fc535a879cc63f54c0c23d4e5c6d uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e7f73f74adc2e897deec25b24b694af5 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8b5cdcc02bc2fa12935cb3daa31c4eac uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   TEST WIDTH 422
    test_enc 0 09a83eaf8a4a151ff9fd45c78271ad7f uncommon_resolution_8bit_422_1922x1080        "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5d061107213470c501423b1a2b8d3018 uncommon_resolution_8bit_422_1924x1080        "-w 1924 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 12b2ae59010b12059a455a54da4cf4da uncommon_resolution_8bit_422_1926x1080        "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9989fe8ab4611738d7ed0c71b1af4bce uncommon_resolution_10bit_422_1922x1080       "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6254630ab10ce5732d2405e9a93f46a2 uncommon_resolution_10bit_422_1924x1080       "-w 1924 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 92854546a6f452c53ffbf6b7db0a5e95 uncommon_resolution_10bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Different transformations width
    test_enc 0 865e70027c8b72816596cb730a2c1692 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a49822d6d22a00dc86aa06cbd93b55be uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 779a4528377d836ec1066319c886f7a4 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 caaf777f9b06a92f863d2b74731fc1e9 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5030854e8ba0ca0f80211bb9498f5170 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ca9e41a325b22aba9c55dc6f0019d9ff uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1e1f7ac45a684b61427782e2c2a919a2 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6ccd85f996b37813eb81032bc2129f67 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1b5816deee4754b08701903da2a2c716 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 39085fcaa1f6c67528f4deec6708fd04 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0c1028acf28e57bd61bc37369e0f6e62 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a5f6d08ec5932db4469b32f99e47cb60 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd9dc3e92a79db6ba860df35ca0bcb36 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ac6f9c7ffa939db9a7a408ca0153763 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a23a9d7a622f5fba6f7b4cd8cad6f079 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ee70811fbb97f4b0e7fd9d24123242f7 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 56b1f4c5ec3680dc26d1f9f33e95b905 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e1efa410af9e325f94ee8203e9d15ce8 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 611cfc3ef80887883d425c0cc1c1b1cf uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4d512c5b3f454481de49cf711efe8f3d uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e823e3521a94de998dc6e47b20efd1c9 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 57bbe4ea37f0bd570f7ccddeb8ff6e0b uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8c3557dd34ea8c2383da3faaf1ca580c uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8fd14c9b2dba2d60feaf41c4e97a01fd uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c5e3d870acc72f112dd482335ec02a38 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fb900a2d6e1abe61cb54d92c95ccaa66 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fb59467c25a9344e24e7c72d9cf5ea56 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1eead0150c39333a93efb64511e4e124 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#  ERROR config handle
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1921 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1923 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1925 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1921 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1923 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1925 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   YUV420
    test_enc 0 48bbf2c30dfe87ab8bdf5d643ad5821d uncommon_resolution_8bit_420_1922x1082       "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b3c40c25f8b17e7709d59de5e5135804 uncommon_resolution_8bit_420_1924x1084       "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f4be4cd30f23a810251476a9e5e47a5c uncommon_resolution_8bit_420_1926x1086       "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 181e21d6b660a2b4e90bce946c8a820b uncommon_resolution_8bit_420_1928x1088       "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0faa9d465daf2c9fba5b9853f5c11f76 uncommon_resolution_8bit_420_1920x1088       "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7831fb2141fc4e96cf38d192dc51ad08 uncommon_resolution_8bit_420_1928x1080       "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2c0b19ab90ea37c287cfd6455f149d34 uncommon_resolution_10bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64cd6292fbd264f56d40967e7f88af79 uncommon_resolution_10bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 849c631a89d011f86e02de934cdd7fc3 uncommon_resolution_10bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 13c76e733da63171a8dd31c07d9d982d uncommon_resolution_10bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 115de213a172913c96ddb125a4c5a0c9 uncommon_resolution_10bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 45b504237c1d1aab95cfc3684edd0e9d uncommon_resolution_10bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1921 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1920 -h 1081 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1923 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1920 -h 1083 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1921 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1920 -h 1081 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1923 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1920 -h 1083 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED MODE 1
    test_enc 0 34c6bf4155d902a47397cbd43d0fcea2 uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1882b0d1435a303a419f4cb88bf688d7 uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7aec60dfc70b3e9821d5369a40499db4 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7f913827ff660c3b1c287779179d2149 uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ab25360ccb2c272fc539942f4459db15 uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 aa0677fe11ebd96972952aa79a766615 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bb2aca3e4fcc67c7e07d26a5cc2a17b7 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c133bf08da6110c9a1567db67b557a67 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 29758a0b8ca71f7911247df53fc8c2e9 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b7be9daca25788e5f43e44707af5da0f uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dd685c77a71320bb81dc19482d79d153 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 06e33dc1b526ec58464556943fdbc13b uncommon_resolution_8bit_422_1922x1080      "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a2abeb663a469a19d37a7d9b3fb16ec8 uncommon_resolution_10bit_422_1922x1080     "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3b0f8b7e9c2cc8461f1f65ce69ee7423 uncommon_resolution_8bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 12bf29672c2ae76dd0f0819f0b64357a uncommon_resolution_8bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8bc6b2b4d237a6ab0e1894053d8d207c uncommon_resolution_8bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1c742879478aa08dce563c024240b6eb uncommon_resolution_8bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 742f58350127fb05301d8b8343fdb454 uncommon_resolution_8bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd0246c35b9a11725e00c763daa94208 uncommon_resolution_8bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3461702a3f2fa0ce9757fdb84f3f3729 uncommon_resolution_10bit_420_1922x1082     "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2582f9a2711d9ab9aa89158e39a5de73 uncommon_resolution_10bit_420_1924x1084     "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5dd1821e90f006b6643f5756a0eee30b uncommon_resolution_10bit_420_1926x1086     "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1412dedd24f4314d23dce9181328f059 uncommon_resolution_10bit_420_1928x1088     "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 aa76dafea19695ce343d08b73cc5f143 uncommon_resolution_10bit_420_1920x1088     "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9e6e409cefb8ae1480b4e5fb6beebc88 uncommon_resolution_10bit_420_1928x1080     "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED MODE 2
    test_enc 0 77e82018b30956630770851dbc21547a uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c3a8f2504f9b43057e651a601bcc12cf uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 21bf65f8f20833f783bec6e30bca11e1 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6560f0b88c804be0cb2c9ce6bc730d98 uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8299ffee4466f44ff769f17d09c7b28d uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 14101b0c564409a46ab9f2e7b931a36a uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f8c94bbd71287451f94a8fd7ca2a5d09 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ee79a5c13514ac8bff1b026d45b4c4bb uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e944b1434ec594fcbe921cbdcfadc0a2 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9da631ba2fcb563e1e8448280d93ab68 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 387c0ef95ad79db16b6e279e939413b8 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e5cfc754174a1572415769567ced63f0 uncommon_resolution_8bit_422_1922x1080      "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8c759d785ba72cd95d5352568347afde uncommon_resolution_10bit_422_1922x1080     "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cbbefad5d82287c91692c4bc474200b9 uncommon_resolution_8bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2a79edf27296d399a00ecd53d534c25a uncommon_resolution_8bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 374b23e8f8e498d096796288cd70e8a6 uncommon_resolution_8bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dc5f0da23f27c1092f024a70eeb5820e uncommon_resolution_8bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c98f6b3bceac3b5167c15164d1eb27b9 uncommon_resolution_8bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 795f26adb3eb4be9be9ad7b81480f1e8 uncommon_resolution_8bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 26cf2aea57c29f9b809b5041124dba90 uncommon_resolution_10bit_420_1922x1082     "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3bd539ebbabe288810f45164ae99a71d uncommon_resolution_10bit_420_1924x1084     "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3e06dec3b0015db714ed37d893abe6cf uncommon_resolution_10bit_420_1926x1086     "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a02c7110842eba5f36d06d9fbe1edeed uncommon_resolution_10bit_420_1928x1088     "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 246c153d6d05927b393faf6c4006e27a uncommon_resolution_10bit_420_1920x1088     "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7c1a9f77a7d2aef0511f3d3d85200a1e uncommon_resolution_10bit_420_1928x1080     "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

function test_handle_errors {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4
    #Some images are only random yuv to tests

    #Test error handling 1000 times RC should return error
    test_enc 11 IGNORE                           small_422_8bit_32x32-small-blank-small       "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 2 -n 1000 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Unsuported Decomposition
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 40 --decomp_v 2 --decomp_h 6 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 40 --decomp_v 3 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 40 --decomp_v 2 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 40 --decomp_v 0 --decomp_h 0 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 10 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Invalid sizes
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 1 -h 1 --input-depth 8  --colour-format yuv422 --bpp 40   --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 2 -h 1 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 989db12949148841274b4f2deb972f30 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 1 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 1 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 282beaadfc85fba10b22950d7de7e512 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b616dc4fc33013bfd6917e425009acbb touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4adbdca94d52791341b23dc1d54bd72d touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 4 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 1 -h 1 --input-depth 10 --colour-format yuv422 --bpp 40   --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 2 -h 1 --input-depth 10 --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a1942aae1934879ceecb661a517e590 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 1 --input-depth 10 --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 1 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6085582f16dc1686ed5e16c570ec35d2 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cb7fcc83b3f695ff5ab804c7abecf889 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3f638373f036bdd61455577c7be99c4a touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 4 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Too big decomposition for small sizes
    test_enc 0 5ece0abef9bd3395aba01aab2c700d07 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 54118d5200b91fadb87a5de4d49a4af0 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 6 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5a27ad0ac1474fcc3567703fc1939887 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 6 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 6 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 bf3be324c0d8118bfe22c7c352af0dc0 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bc0a516a4ba0182a83a656b2fd235811 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 e3c5101cde8beda8d8eb4171c0c3557e touchdown_1080p_yuv422p_8_bit_60_frames      "-w 16 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1b4d38ce646f0c6476cdd9789f0369ec touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 16 -h 64 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 16 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 a9e14bcf2ce74ebc148af23e4662f6ed touchdown_1080p_yuv422p_8_bit_60_frames      "-w 30 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ad6ae01f25c7f732dd4be1e97a890a5c touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 30 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 30 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 4a729f51650e65d8144448fc547244de touchdown_1080p_yuv422p_8_bit_60_frames      "-w 32 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd87045d42034b2ba50932cbb4ddddb4 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 32 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 32 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 81f95efb62aebdd5928f168238350967 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a3d558049f672f50e48a6ed52c282d4 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bf31c190167426c21ce4188bf0234738 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 2 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 2c515bba6f7aacfa632b375e58194216 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d2eb8f8aff8a0d43237a01f6542a54ff touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 34efeeb8c1310027ba83fd218247f274 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5  IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 0 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b994fda1ecf287fbc049fb9d99779450 small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 6 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5  IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 0 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 1 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 2 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 5.5 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b994fda1ecf287fbc049fb9d99779450 small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 6 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}


#RUN different RC parameters Release/Debug/ ASM_C/ASM_MAX compare Parameters (1:asm) (2:lp number)
function test_invalid_yuv {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   Use 8bit YUV as unpacked 10 bit YUV, values in YUV out of a range
    test_enc 0 c9a46d659873415e4f770eb7d9bc0a8b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9da65b148cd97bc5cd4f53aa8f4bf006 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 53fa86b9d1a6742beabb61cd6a8a1d71 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 beed5ff4cd66dc016048c80fb8343786 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9e5ada1875691a59e68b66b50bc38d87 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4edc3376265a425f9fc95ab08da586e6 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1a42270754072036f2a14ed68c237b9c touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2ac2f0e101e76f4c64477359d12e68b2 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ea188adf88b8d5c3ef06afadcdd602de touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 919eec8b489e605ae20da5ab429ca28f touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f25ec067d775095f5d49a83afe24ad74 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e44525f0815e4b37f8c2ec2d395f806e touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 12b771e54f382c4a2aeb51863a2deb69 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5006074f203f691baaee304f1db47697 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c37470b966c5a1c19d3a7e66c16f8359 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

echo RUN SAMPLE ENCODER TEST
exec_enc=$exec_enc_rel
#test_enc 0 a0648fc4fe92b33ffa5edfe75b370fbd touchdown_1080p_yuv422p_8_bit_60_frames "-w 1920 -h 1080 --input-depth 8 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --asm max --lp 2"

echo RUN RELEASE TEST AVX
exec_enc=$exec_enc_rel
[[ $run_fast -eq 0 ]] && test_rate_control c 10 latency 0
[[ $run_fast -eq 0 ]] && test_rate_control avx2 5 cpu 0
                         test_rate_control max 7 latency 0
                         test_rate_control max 7 latency 1

[[ $run_fast -eq 0 ]] && test_rate_control_signs c 10 latency 0
[[ $run_fast -eq 0 ]] && test_rate_control_signs avx2 5 cpu 0
                         test_rate_control_signs max 7 latency 0
                         test_rate_control_signs max 7 latency 1

echo Test MSB-aligned input
[[ $run_fast -eq 0 ]] && test_msb_aligned c 10 latency 0
[[ $run_fast -eq 0 ]] && test_msb_aligned avx2 5 cpu 0
                         test_msb_aligned max 7 latency 0
                         test_msb_aligned max 7 latency 1

echo "Test --lossless (true lossless coding, Fq=0)"
[[ $run_fast -eq 0 ]] && test_lossless c 10 latency 0
[[ $run_fast -eq 0 ]] && test_lossless avx2 5 cpu 0
                         test_lossless max 7 latency 0
                         test_lossless max 7 latency 1

echo "Test yuva422 (4:2:2:4, YUV422+alpha)"
[[ $run_fast -eq 0 ]] && test_four_component_alpha c 10 latency 0
[[ $run_fast -eq 0 ]] && test_four_component_alpha avx2 5 cpu 0
                         test_four_component_alpha max 7 latency 0
                         test_four_component_alpha max 7 latency 1

echo "Test rgba/yuva444 (4:4:4:4)"
[[ $run_fast -eq 0 ]] && test_four_component_444 c 10 latency 0
[[ $run_fast -eq 0 ]] && test_four_component_444 avx2 5 cpu 0
                         test_four_component_444 max 7 latency 0
                         test_four_component_444 max 7 latency 1

echo "Test yuv444/rgb (planar) and rgbp (packed)"
[[ $run_fast -eq 0 ]] && test_yuv444_rgb_and_rgbp c 10 latency 0
[[ $run_fast -eq 0 ]] && test_yuv444_rgb_and_rgbp avx2 5 cpu 0
                         test_yuv444_rgb_and_rgbp max 7 latency 0
                         test_yuv444_rgb_and_rgbp max 7 latency 1

echo "Test --color-transform (encoder-side forward RCT, Cpih=1)"
[[ $run_fast -eq 0 ]] && test_color_transform c 10 latency 0
[[ $run_fast -eq 0 ]] && test_color_transform avx2 5 cpu 0
                         test_color_transform max 7 latency 0
                         test_color_transform max 7 latency 1

#echo RUN DEBUG TEST C
#exec_enc=$exec_enc_dbg
#test_rate_control C 1

echo Test Uncommon resolution
[[ $run_fast -eq 0 ]] && test_uncommon_resolution c 10 latency 0
                         test_uncommon_resolution avx2 10 latency 0
[[ $run_fast -eq 0 ]] && test_uncommon_resolution avx2 1 cpu 0
[[ $run_fast -eq 0 ]] && test_uncommon_resolution max 10 latency 0
[[ $run_fast -eq 0 ]] && test_uncommon_resolution avx2 1 cpu 1
[[ $run_fast -eq 0 ]] && test_uncommon_resolution max 10 latency 1

echo Test Handle errors
[[ $run_fast -eq 0 ]] && test_handle_errors c 2 cpu 0
                         test_handle_errors avx2 2 latency 0
                         test_handle_errors avx2 2 latency 1

echo Test Handle Invalid Input YUV
[[ $run_fast -eq 0 ]] && test_invalid_yuv c 10 latency 0
[[ $run_fast -eq 0 ]] && test_invalid_yuv c 10 cpu 0
                         test_invalid_yuv avx2 10 latency 0
[[ $run_fast -eq 0 ]] && test_invalid_yuv avx2 1 cpu 0
[[ $run_fast -eq 0 ]] && test_invalid_yuv max 10 latency 0
[[ $run_fast -eq 0 ]] && test_invalid_yuv max 10 cpu 0
[[ $run_fast -eq 0 ]] && test_invalid_yuv max 10 latency 1
[[ $run_fast -eq 0 ]] && test_invalid_yuv max 10 cpu 1

common_lib_end_summary

echo "DONE Encoder OK"
error=0
end
