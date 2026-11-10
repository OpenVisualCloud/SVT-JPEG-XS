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
    test_enc 0 a0ca8165d5251dc3090d2b432b9180a2 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2c794bbdb723645a642ee54417ca409b touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64e6c53c76a4bbfc763ca6d2660dda79 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64e6c53c76a4bbfc763ca6d2660dda79 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9953e0ab2a3d05c8de48857131c81eac touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4b77a196ea3a2abc4e1e08be9a264806 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f9a6e136f5174dea48d2eb121006a3f7 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f9a6e136f5174dea48d2eb121006a3f7 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --coding-signs 0 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#Variable RC error path
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 0  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 1  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 2  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 0.1 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --coding-signs 2 --rc 3  -n 10 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   8bit tests RC
    test_enc 0 73e5417109b53ebf0a679ae1a03726a5 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e83d9b243d5ed0d3e5a78ec6a834ce4e touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4a36a98fa2f7bc7687ff807fc869ef85 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f59e094d7b37d4c56acdba513bca28c9 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 10ad29a3589b3c4d0e6c728b1c9c81b2 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fa512474dc6a9c00072bf775bc288e1d touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a6f2ab22d782cf05ccd2c0e54a30ab2 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9c41dbfe09d5e667351ad528a773ac7d touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit multiple frames
    test_enc 0 dccd4212705d1a68a8ec36bf4b289586 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8608e592744520f0349a28323c89fcc4 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit signal tests RC
    test_enc 0 a37a1bfb74e7a952d857557c9dc2193a signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 786d013013452bc99c459e181dd178c3 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4bf4195acecb8036ebaf4da274a879ec signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
#   10bit signal tests RC multiple V H
    test_enc 0 7ba0bf174ca41469e97e64f16ad400d6 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 071563c78bdb9c653c3ec84768ac1b9b signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5ad33b0cdd25d4685fa654bebfdebc55 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f30d71e6838c92b7533a6912b8802248 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6d27958da4bd82c8d93c3089c85762f1 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 727b28d3d48929303df7cbffb62cd083 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cddd07c60e1bc5cde6ce5f8a7ebe522c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3c0dd8928e7fe8d72424ddd2f0acb78e signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d3b3b1d201757327470dac2f0f68716c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3e7a28ba6dfcd2b1e733ba369d6cb0b2 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6b33ab5be44fc0a55b514ff512314d7b signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 eb7921bd1fedea23e726b385486d96ea signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 875846e850cd07a0948f456589ffa092 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e81f2aee152c3523691560c46a698c69 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   YUV420 RC 420 not support V:0
    test_enc 0 700cc806f61aede6fb8c8ab0e54a0cff touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a9f95d5b0892b41edb45f7f78216b7a9 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fee53d0d0050e9eafaace0b6c8729d16 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 27b091a658e7a4ff8675d38691dfb123 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 be8cd5655b9c98c70cfd67b889da029a touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1e91c96d76584df67e8ae321f4c568f0 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a8e1d7c49d1fe2ad2a975d9d0fad0768 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6d8805b358a9c1cfbe7b514897ba7bc4 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bf46a14d6f8b42ebea35db66c9190e76 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f121ef1cdae87b64c072a46d6e368c86 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c438740f9863257038c6292af06ce2fb touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d58d820fd5e783960760b3746b7c2971 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cfb3cf5d2e690c36a8865e18dd584584 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e5013bc5de3f26ebb769336aebc6128b touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 2be411bf02a8fc30b5ec8fc1e667fca3 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 76b2825c22a80846775f804ed9b2b172 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3e629c2dacd8693da68a728d0ca03a55 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3a92f8d63592d10c5cbacbec31b3a43f touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0e529efffb8f0b9f6ed32876babf5a95 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 20d04b34dc46efe13e9cd2a6ac08d079 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Vertical Prediction significance all modes
    test_enc 0 b2f403497aee8406a06e10039cab6df6 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 427d9978ec34a72728b793c64c842809 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f050bb1573b4d7ad75a175635b091f28 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0fbf3b0deb57c4e8f2bd5f08afae151d touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 15b2cc0f07f0f58e03843d256694b7d8 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c11c97729bbbd4ae21f1839974ec0f35 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d81bf9ba4f63ddb92f6282c6d98eab88 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 2c3043d75f335973582605eac0a383cb touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 686fcb19cd1348d4f263380de9a905c0 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8a02630a685e74abc3a4132ad7002ef7 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 eba0955338e36b5e495b8007589668c6 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a80ce493e81dda5e7eca521a9e4bf0aa touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a0d43fe614e70518907892180e9eebaf touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 db7eff68f823a89438843bcd8e47add1 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 65a1e1db3cc6f861f95b0ab18d6d9b99 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a4319b7722d03a7db2ff0d22e02fbc29 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4501301cc9a5cec48d1376027494e1b4 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c5bb5f959f0bbac2415580eaedbff913 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1e8208936e32df55688297e4368f1b0e touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ce70f149d04bb1f28e6e5217e4a668e touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d876fe18fef52bf811f4e30bafbe38e5 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd39165cb32b958aeea60658fa938e06 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 12dafb41bd5fd59d3add8e4245365950 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 7e334ff6eda9799c72f96cef54b79cc7 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 0 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9a4cb121843d747824143ad95d9bb8e9 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 31d9d69278a44ade0269f9bfb1005e5a touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a320856a4a2a8632f43151a7fa8a4b3d touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 459e5503a1ecc44347884693c2e8ba5b touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f6344bb00e95b09af749c995410e4cc9 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 976887a329bd2f418d7607aa9aafc6d4 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 67a4ebe6d26eb4d8890c9c3d5b0712bc touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 604bcbd2d96d42d980884ee0d13415ae touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED FULL Mode 1
    test_enc 0 36a70bf01c594ef2c3c433a375b4e104 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 865848f0b1226a3b41d41a4dfd25ea63 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f26fbfd38774ac8c9ee2c0b9dba52092 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ad396c74f5497046a92ebe3819e55383 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a3d7403f1b39075c116df84be1857e37 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f69516791e1461475b17658363f78739 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 392bea1ed7efc5405c34826d7f49a893 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a3d7403f1b39075c116df84be1857e37 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c98b07a50de1255daaa2daef9d9060b6 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e11e35745bee029b7e74e62b0992e2cd signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3cb8dbd474ef4dddb57f42e7c008712c signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ebfcb1ae11c918d4de5131b10f7653ea touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a8195b8d1533c9eba6e921addd7397f3 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED FULL Mode 2
    test_enc 0 8d1246bf8b3a428990f2dc88ec814a58 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7d7151aa98fdb6263bf6110809091fde touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ee4f2fd752043156ec04b608a6c2a97c touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5fda9ca4274d145ff2e65d735181626b signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f7f4993f859b1ae17395cce4572b7082 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9b28486fa95b1132b3138144e1d3ce81 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b59bb47599f517044b603ed2035e6f0e signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f7f4993f859b1ae17395cce4572b7082 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a7b1dc6f5e4a6ebbb63dd2a1394ea79a signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f08cb7ccdbf8be3c5dfbaef681e62024 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cba72e0c3bfa4f0b468635c119e52039 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 67485dad6a6fe31951a79e9280ee2096 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e8849af6cbf0ea17a53c715ab845eb9a touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#Qunatization uniform
    test_enc 0 32843b6754f6eca3db420150f946e5d3 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 18da38688e40bbcf1fa4081d3b51c365 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c3f26eca65d8c3938f406b7746d1b2fc touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --quantization 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1977badeb08da01b67eae3dbfe4ad8fd signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6a5ba0debde5985be5956c7598736611 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9fde5208fca94a38225a65bc3f3f2737 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 93d18c39a98c9e36949f60119fcfe619 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3134f80eeafd45abc5db149d295c1518 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --quantization 1 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#slice height
    test_enc 0 0f7ac4f30b7b5d8e3624bcc1e2698583 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --slice-height 7  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fa0623d20117ea91ae6b74f5852e2b3a touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --slice-height 12 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d151b1c4e8797b4053128a3c6686ed19 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --slice-height 18 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0cae1874e61d68c0c85065b9d55be0b0 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --slice-height 20 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dd4d8c0923e8c846508d3bd14ae4dbdd signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 24 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 87934002d7eec181d356eab327cb0352 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --slice-height 22 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 aac7f7c57974e1da5771280ef82c6390 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 28 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5d85c828d01b727372909724b74db984 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 32 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 de75c229882b66375d3047a73ff9c476 touchdown_1080p_yuv420p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --slice-height 1080 --rc 0    --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}


#RUN different RC parameters Release/Debug/ ASM_C/ASM_MAX compare Parameters (1:asm) (2:lp number)
function test_rate_control_signs {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

    #Qunatization deadzone
    test_enc 0 519e7aca0d5beb7d0572f873ea390277 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dd0a52c5f2acd72e5481a4b5e2f4991a touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e645629222fccc15d5a326a84f100842 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #Qunatization uniform
    test_enc 0 7c67f3d42ef00837fe0fa82b85f2209e touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9af175968dfb0febbc84c3e6cd3bdf47 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 42fdf95e21c42dea34edfeaea93d8144 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1  --rc 0  --coding-signs 2 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Qunatization deadzone
    test_enc 0 20d12e3eb6f22591a9e258e07d815b92 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8eb03238d5cdd9385297eab5eeca486d touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ddd54f31518387bbe897ef105ccc470 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 0 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #Qunatization uniform
    test_enc 0 b1f8f2a16b337395ab93b535f89c7673 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4fd3424ef89bcce33578c556c4195d01 touchdown_720p_yuv420p_8_bit_60_frames      "-w 1280 -h 720  --input-depth 8  --colour-format yuv420 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b5b5ecbe42b70497dfcc2e6cae5922fa signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1  --quantization 1 --rc 0   --coding-signs 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#RUN input-msb-aligned Parameters (1:asm) (2:lp number)
function test_msb_aligned {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   Explicit msb-aligned=0 must be byte-identical to the default (untouched) path
    test_enc 0 dccd4212705d1a68a8ec36bf4b289586 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5 --input-msb-aligned 0   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   msb-aligned=1 must produce a valid, decodable, deterministic bitstream
    test_enc 0 260510d7e19bfacfb154b2ed088026d4 touchdown_1080p_yuv422p_10_bit_le_60_frames "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5 --input-msb-aligned 1   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a1ccff7b4da2ed1999aa9d0ee0cb5990 signal_1080p_yuv422p_10bit_le_1_frame       "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --input-msb-aligned 1   --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

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

    test_enc_yuva422_synth 0 202b16aae47d74a320028e22bfb0497e "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 4 --decomp_v 2 --decomp_h 5 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva422_synth 0 744b3b34771792490d0f2573b7798a69 "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva422_synth 0 4b17fdf043b99c39fb64f7be82291595 "-w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 4 --decomp_v 0 --decomp_h 1 --rc 0 -n $yuva422_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

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

    test_enc_yuva444_synth 0 4f8a6f1601ba2272a26fe0e595e0851b "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --decomp_v 2 --decomp_h 5 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva444_synth 0 0f901f0f8630b5ec5d3d6b23357b0611 "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format yuva444 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_yuva444_synth 0 ebe34e1f523dd8109047bc91bf165303 "-w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --decomp_v 0 --decomp_h 1 --rc 0 -n $yuva444_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   4:4:4:4 has no chroma subsampling - odd width must be ACCEPTED (contrast case vs yuva422 above)
    test_enc_yuva444_synth 0 998c258d0fef85ad92eed7d4cca11471 "-w $((yuva444_w + 1)) -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 4 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
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
    test_enc_rgb_synth 0 4ba473eb4495cff9a648032740167858 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format yuv444 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_rgb_synth 0 4ba473eb4495cff9a648032740167858 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb    --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc_rgb_synth 0 649a297a6f7c117564e8fe43e4b68b7f "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb    --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   rgbp (packed) - single interleaved buffer, distinct enum/code path from planar rgb/yuv444.
#   Known pre-existing TODO limitation: packed only works in Low latency threading model, not CPU_PROFILE_CPU.
    if [ "$cpu_profile" = "cpu" ]; then
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 5 IGNORE "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    else
        test_enc_rgb_synth 0 1ea2328c196dd4efe33f3065d11ef4b7 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 0 4cbcd9da42c677a98aa48c31f9992d49 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgbp   --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    fi

#   No chroma subsampling on either format - odd width must be ACCEPTED
    test_enc_rgb_synth 0 e7cc03eb50af42000e8df00ec3e1a991 "-w $((rgb_w + 1)) -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}

#RUN encoder-side forward RCT (--color-transform, Cpih=1) smoke test Parameters (1:asm) (2:lp) (3:profile) (4:packetization-mode)
function test_color_transform {
    asm=$1
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   --color-transform 0 (explicit) must be byte-identical to the default (untouched) rgb path
    test_enc_rgb_synth 0 4ba473eb4495cff9a648032740167858 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 0 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

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
        test_enc_rgb_synth 0 79133d033770682b0e7b8cd627d8adb0 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
        test_enc_rgb_synth 0 b896cc808ef0acefdb1dcee5795fe453 "-w $rgb_w -h $rgb_h --input-depth 8 --colour-format rgb --bpp 4 --decomp_v 1 --decomp_h 4 --rc 0 -n $rgb_frames --color-transform 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

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
    test_enc 0 b8ece8ca0f4c22bbb94a1dc3d8448599 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bb88d5ec7f59685a00ded7abd4e56766 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b6e925ace353372baf2d5c743dc645ca uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d2872623a18c8bd7d86e0d557b73f651 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 60f5ac3d8c1a8b255e075e20d04bd841 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 54081740c5823008e69f17a17dc4469d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3c4f691dcb3c7b9bb4b68e88600c9421 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a7edbb8983aac586e6b516050b6d931c uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0876d4e1f33d7cb248ebea5c7e8d86bd uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1fabecb22b6dca8ea146bbae5e41854f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e6b1ca5493f2866242fe33fa16323b42 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d8d7a94e6a6a9b7eb77987cb697ed9c1 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ef683619d4fc158f65198953b99aa1e0 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c2508cd4a26fdc67f93530fd7f1d562f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c1e67ed193aee14b2bc575d2de5355dc uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 312ecb901a715c2082627d485d3ee245 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 156dadf7ed2a113d0a87484c8adf9aec uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f72d997fd223317d47eeabf1b11a3473 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1db3ca25d4ee21e13668137f4362a967 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 33d0c2a429c8c649f8559a8e194499a6 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dcf832beee9f9947f6d4fb8c03ee4f50 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d86f61dc44a17aebfb11d792fed363e2 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 efc8463815868193d5a51ddc9ab76446 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7e849ed4c16d875bd1816e661a2ac67e uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 db5f951e6762c673e330b310bfe40d5f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cd1a7c6860b909991881004b64ca2a15 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 627f6f0d9da4490ab99bd4a3105dfd1b uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dd051e34398f513b4e01a9b94c729919 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8ec50e9bbc2c5c8a84850273f716cdf1 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b997b8e4a1866578c5d167164b28a00c uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 3    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 32649d29b34b4b4079ddf3105e4ab794 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1ed183a9f2a09bc5ea209c4b35814765 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 5    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1c03fa3a9b49a6f6b87c2e4b1ca326b7 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 eb9644fbb77a37b2c3e0136dff83ce6f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 7    --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b4bd9b4d0408949dbce45babcf935c3e uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1000 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 20fb9f622c4ed8d4344fe21ade4fc58d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1001 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4b80b3ebb55e27a0c728c1ce21512d89 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1002 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6bf30ae3302471ab0d46b846a81f2f2b uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1003 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 175c621c81364e97ef926dab265f29c9 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1004 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ff835ec7b8ce18f0890b0eae940efc46 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1005 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1b82b58168a06c0baa7ec7246ecac7c6 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1006 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5d2b0f4c4264f8fa233adb165e50f55c uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1007 --slice-height 5000 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 2    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 3    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ecfa64db4d8d7c0d3375bc39dbcc5e6 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 5    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 581410de67f4a6224062dc47b64805a9 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 6    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 7    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6a501fb2a4d1959c39dd6f141d65418a uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1000 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1001 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a238afc0f9bd29466b6905d93f9b798e uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1002 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1003 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b06b42140f81fc434763e2f5b4ae1976 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1004 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1005 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6854e1d027648e6069b53811f4952436 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1006 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1007 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 16cd0577b18df597e56c5abec87d88bf uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 2    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 3    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 be326fb3546475726d9b6e99faba5408 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 5    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 33c11e5c19f3b07cbc75712bf33c98be uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 6    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 7    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 462a3e587fc5eab032385ecb9dc34d94 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1000 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1001 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 bfe7e14b42e91ba1cbb50bff7c80343e uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1002 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1003 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ca99db039f0801f287363f3409daeec2 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1004 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1005 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 31095c627cb5049914984374785c2bf3 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1006 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1007 --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 1080    --slice-height 5000 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #1 precincts 2 slices
    test_enc 0 1b1e409d1f08ae8034af46309ee26831 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 8   --slice-height 4 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b6ec31e4206e8a9c468b71fa7f2d9a75 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4   --slice-height 2 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5b946f7b38b7a7f988eeb5e04c5c4564 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 2   --slice-height 1 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b0ec2a8824142c5db375bd3ba9c353c7 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 8   --slice-height 4 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5f08d9a6f5de66b086017c3074c24d26 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 4   --slice-height 2 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #2 precincts 2 slices
    test_enc 0 a2ebcd051d3027e00b9cde7fa59a0212 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 16  --slice-height 8 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 abb87f3d1c26ff3047620ded4a2b0110 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 8   --slice-height 4 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4f3b0c0fd5e59dba48899d703563fc0f uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 4   --slice-height 2 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 14608846a591f8b606e4c5bfbc3d4bf5 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 16  --slice-height 8 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e20889640c503277cc552601838da270 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 8   --slice-height 4 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #3 precincts 2 slices
    test_enc 0 ebd0974644f50261f91dca11bf8e54be uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 24  --slice-height 12 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e593aca45e5341118ce1455bdee63884 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 12  --slice-height 6 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e079ce4d19c3b837b084f778f082870a uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 6   --slice-height 3 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7b60d0e8a8bad86306cecbe3e41ec799 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 24  --slice-height 12 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0049ac86f8e728662c141b6b9bb330b6 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 12  --slice-height 6 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    #5 precincts 2 slices
    test_enc 0 842eea224011cf9a305b7f7437adf3da uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 40  --slice-height 20 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f0f90385fc2c4e269628686254e6f71d uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 20  --slice-height 10 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 df795849960c14c765874ed9567aa522 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 10  --slice-height 5 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f6826ad5415ca4f3fbaca9b0befa457f uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 40  --slice-height 20 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 efb6224ed648b05e3bfefacfbd19e345 uncommon_resolution_10bit_420_1928x1080     "-w 1920 -h 20  --slice-height 10 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   TEST HEIGHT 422
    test_enc 0 f47404850cdab5851db4972bea624cd2 uncommon_resolution_8bit_422_1920x1080      "-w 1920 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 814f39e7849d5d679414422559e25372 uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ee4e953210fcac329d4d70518acb79be uncommon_resolution_8bit_422_1920x1082      "-w 1920 -h 1082 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e76a7e3979313ede03d913c5589d178e uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3abc3a94dccc31c49a0924ebb9b1ddea uncommon_resolution_8bit_422_1920x1084      "-w 1920 -h 1084 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a2ba8bb8d5058059debe96e56e922fb0 uncommon_resolution_8bit_422_1920x1085      "-w 1920 -h 1085 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9f13b34323106db9c0f9e2c4d67f7d70 uncommon_resolution_8bit_422_1920x1086      "-w 1920 -h 1086 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 89842b208d3a09772cce8094c5e0450a uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 caf76ef084e1609cb68f61255833e923 uncommon_resolution_10bit_422_1920x1080     "-w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64516883f3895cab0bd1187e5e805dc2 uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 082ded7e94636e36cbb7f35eb81ba4fc uncommon_resolution_10bit_422_1920x1082     "-w 1920 -h 1082 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e1b21d7776ececbd846336fdf6705578 uncommon_resolution_10bit_422_1920x1083     "-w 1920 -h 1083 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6b987badb30c45c59cc2da27165a2b6a uncommon_resolution_10bit_422_1920x1084     "-w 1920 -h 1084 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cbda2b01f50823eb929505262a835376 uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fbbb13575642749aaa5728dcbd4532d6 uncommon_resolution_10bit_422_1920x1086     "-w 1920 -h 1086 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7c320f2eb4a9955f0718f6ee0f022bd6 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Different transformations height
    test_enc 0 ea44bef816148f7d2741d08ecb6cca59 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5c3ed20bab6683a99cd98aa691c9b134 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 085138d57680b5e679be65f46a8c5570 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8ecb9750190fffd9ae65f493f95fab60 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c32a0c2836b9dab4799cbe0e37347b6e uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e8afde951ef2717d8bb3d211b217e054 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 faac068911f17da38344c4d8f7bdcd6c uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 173af96bf5abff738175f38e22d1960f uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1ceb5413edb8d046ea0c8cf213657d27 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 705818b90e1268aa74079cde0e585dae uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 41e85782d73941570bcaab1c1106c733 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e5616485c260812ed694236f41930139 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9de17b7ad62e661635dca4471067614e uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3fb7f8234fa30885749297bb69839bc5 uncommon_resolution_8bit_422_1920x1087       "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 56b6cb9f4929097150a704cd719d3c41 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 dda1761db2ddc6f90f7e90f7cb8c09e7 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 be7c72084622fbf144923b5b220c1fd3 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 70d56bb248e035a6a440ffbf997ff7f8 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e54ddc8f6f3026c880713b60a1f21689 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5319719e3ecefd6103dc06ee1ac259d8 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 16788dd0c511e6a3028f079501029e08 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 339ef2aeb159b2c40e0089b35572ddb1 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8c96f84c84244bc4be4f527c5031f647 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6c29496cf6e7a66426db4bc16cf821a4 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7175cbc2f48432fca4c8a69c73582590 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8812ce0223d78d0e295430c02496ecfc uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6510f4419be7d6889dbc40d40afc99ba uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8f24c59d413760f90ae1b7581a7a9009 uncommon_resolution_10bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   TEST WIDTH 422
    test_enc 0 236adf089ed3c95b22c8859fd8968319 uncommon_resolution_8bit_422_1922x1080        "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8146d5a57e4a8ca3b65cb86527af2371 uncommon_resolution_8bit_422_1924x1080        "-w 1924 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 83f075eec45f34ab618d94b4309c99d5 uncommon_resolution_8bit_422_1926x1080        "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3cb9b3de32d840867ee36f7cdd007965 uncommon_resolution_10bit_422_1922x1080       "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1a275430a570d26799237610643001bd uncommon_resolution_10bit_422_1924x1080       "-w 1924 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6928a07d191065021a50083520a3247a uncommon_resolution_10bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   Different transformations width
    test_enc 0 c396581078774920f680132289cced81 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1a3fad8b49081ce21eb59a2f4e2094cf uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5f8cfa3b9be7c3070e1513aa3eddf970 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6732ab20c1cc686c87bddebacc5fbc7c uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 359a176a9c1fcaea970697ebbbdcf32e uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 337e8718f874c2a49f88b748c8ca4336 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8568f9d670cfefc64d76488a1b55389d uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b2d527b93e0f99d9b44f452b187e10bb uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f4b1d8c56985c6a53af20a9738f54b71 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 29c1b2ceacf39049467fa9a5ae61f098 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0a9c17e3efc1489ab570a1fb5ff4af09 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ea9201bc3d029d502718d240a81af095 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0fd91bf87b37f972c96b9b3987c62e70 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8655471e13ac12da3100d651e0eb30a2 uncommon_resolution_8bit_422_1926x1080       "-w 1926 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e3b5d0ab23516834bc145705c87ec54c uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 79c9c39e293543fcbc72442b9cfd05c2 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8cce1f7dbda100c9157883832de3239b uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b224f261bf51cd1289d814298174bbdb uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8444a6b892e2e4b68227deb50a0d868a uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d993ea7acc9e46cca842897f86a7711f uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 993a4123ffff6c2dfb5bb8a8f9f5e774 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9ade559ba2c797ad7f05c5fa7e5f0ffe uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a5731022f53ed15f7957c22a0ced8c05 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a6761c04dde87488c80b0dc971d14713 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d6942ee349f4a25da339e481871d3dc7 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 107db6c6f38afe80dc928f9d49d7e135 uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 3 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 27e3c7c97d36d2b7bcf493f45da20aec uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9b23026dc1459bc356f69e831d2f942a uncommon_resolution_10bit_422_1926x1080      "-w 1926 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 1 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#  ERROR config handle
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1921 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1923 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_422_1926x1080       "-w 1925 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1921 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1923 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_422_1926x1080      "-w 1925 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#   YUV420
    test_enc 0 e04cae3cefc30dd23daae8276cfd4a7e uncommon_resolution_8bit_420_1922x1082       "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7eab62066d96eb73ee50a5454553fbe8 uncommon_resolution_8bit_420_1924x1084       "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 deceb9a393187aee9f0e128373654720 uncommon_resolution_8bit_420_1926x1086       "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ee8307472e41b79ff9a69c250ed11b26 uncommon_resolution_8bit_420_1928x1088       "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ec0b248720bdfe1a929c51f30aed458e uncommon_resolution_8bit_420_1920x1088       "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e0908079b8980ecbcfd52e0715bc1de2 uncommon_resolution_8bit_420_1928x1080       "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 80be79f5660b0e021c72aa68a42fbf57 uncommon_resolution_10bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 79784a1c5b65167166637c31cdec8962 uncommon_resolution_10bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 afb9d89005d608e29d673dede0e81c4a uncommon_resolution_10bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cb8b1144e801a72959cec11ffac05841 uncommon_resolution_10bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5f66995fa39abc83b5c42c6fc882210e uncommon_resolution_10bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d91cbc4f6c9ed184dd346df0b112447b uncommon_resolution_10bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1921 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1920 -h 1081 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1923 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_8bit_420_1928x1088       "-w 1920 -h 1083 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1921 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1920 -h 1081 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1923 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           uncommon_resolution_10bit_420_1928x1088      "-w 1920 -h 1083 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED MODE 1
    test_enc 0 685d9522f57cd10e13010ef57e89e9cc uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 223e14f95c09b2436f5164ca65419aa7 uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c5254885be62630238cea65be28cd05d uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4380f266da9d1c735fced573cb500227 uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 248270c0729d85cb1b92f5e79d96c27b uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8b4ab10c7f374a78966484530e4fdedf uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8f11f425c74188dd564995b6ba43cfcc uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 16e757b093f10d961f862358c01917df uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4e1e5bffdd1c814c62d3bbf486eb2a76 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 96065c269db9500e75da850019e413f3 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8bbc3654ea56f05b0518deecc173607b uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 cdadd7f0312309cd157ad3f87b356e9d uncommon_resolution_8bit_422_1922x1080      "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 77915faa3f8cd3de7fe99b244a7f696a uncommon_resolution_10bit_422_1922x1080     "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 60a2a79247c6f7f3fa3c0a2f89a2b051 uncommon_resolution_8bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6f7ba574bd973b6447ec22c070673e67 uncommon_resolution_8bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b0ae7728d83910b70e163c16457d74c6 uncommon_resolution_8bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 41014c5c3be6e11b7a585fbe6258c2db uncommon_resolution_8bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0ba390f92e991ab53a17b6dccb81c9d9 uncommon_resolution_8bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d52bf6b4b415b56c2057b25c5f2a538f uncommon_resolution_8bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4d858f6d89e15f02f5a4324d4f889316 uncommon_resolution_10bit_420_1922x1082     "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b4faf25815dfc4d2cf96cc5b5bcdcf84 uncommon_resolution_10bit_420_1924x1084     "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7b90d38346b30601aaa693a879145749 uncommon_resolution_10bit_420_1926x1086     "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 65791c0a37978b590efb2c9f61ec835d uncommon_resolution_10bit_420_1928x1088     "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 4998546079648f978a001ba405105798 uncommon_resolution_10bit_420_1920x1088     "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 1f35f4a7a82b314487cc56bdab804e4b uncommon_resolution_10bit_420_1928x1080     "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 1 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

#VPRED MODE 2
    test_enc 0 ce286e64ce208434d6bec261b2646b89 uncommon_resolution_8bit_422_1920x1081      "-w 1920 -h 1081 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c39fc130f330de9f6e17e51aec540ead uncommon_resolution_8bit_422_1920x1083      "-w 1920 -h 1083 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b153e83a0100b5104d99c1184840d8ce uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f3cd91b7940c20deb87d332dc6f36d10 uncommon_resolution_10bit_422_1920x1085     "-w 1920 -h 1085 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 62c9dc0d27bf0d01eb261b4f803d777e uncommon_resolution_10bit_422_1920x1081     "-w 1920 -h 1081 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ad114d108cba72233e2962f1f4e64cd6 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d8b6f47e89614baeeabc28788f443edf uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 1 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fddc7007bb1b3d7a6a73fd48bd081680 uncommon_resolution_8bit_422_1920x1087      "-w 1920 -h 1087 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5ae04d210b749da48e323b68fac37a1e uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f09950310cc75995765c07828f09e5f0 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 1 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ca4443cacba92e84fe1856649a5a2363 uncommon_resolution_10bit_422_1920x1087     "-w 1920 -h 1087 --input-depth 10 --colour-format yuv422 --bpp 4 --decomp_v 0 --decomp_h 2 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fa2f1107a2212d658087c551ca061c24 uncommon_resolution_8bit_422_1922x1080      "-w 1922 -h 1080 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 52a84703e1e0567b7515e79df3efa1ea uncommon_resolution_10bit_422_1922x1080     "-w 1922 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fef050ea5b3e7ef11aefcac54468e621 uncommon_resolution_8bit_420_1922x1082      "-w 1922 -h 1082 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0ab7f11309deb1811e5d619cac694f2f uncommon_resolution_8bit_420_1924x1084      "-w 1924 -h 1084 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7065abed57ae1b474c6452df27f6e426 uncommon_resolution_8bit_420_1926x1086      "-w 1926 -h 1086 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 68d5a58166dd738b9ce6bb4efa989c0c uncommon_resolution_8bit_420_1928x1088      "-w 1928 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7bf08921cc45779ccdaa1578dd711713 uncommon_resolution_8bit_420_1920x1088      "-w 1920 -h 1088 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 6902705de89c248efa8d83a4807cdf4f uncommon_resolution_8bit_420_1928x1080      "-w 1928 -h 1080 --input-depth 8  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 8d29aab9cb921a2b7f39f727e786403f uncommon_resolution_10bit_420_1922x1082     "-w 1922 -h 1082 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 296e76f7220af72ecf673951f4e1bb68 uncommon_resolution_10bit_420_1924x1084     "-w 1924 -h 1084 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0e44560d237be30c28ec0873a58d0036 uncommon_resolution_10bit_420_1926x1086     "-w 1926 -h 1086 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 933dd21235973cfcd88bafeeda3d8198 uncommon_resolution_10bit_420_1928x1088     "-w 1928 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 148c38ca05bc8087fe903dd0690e49a2 uncommon_resolution_10bit_420_1920x1088     "-w 1920 -h 1088 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ef2bad6a16f0c04a21bb8b07ecbe276a uncommon_resolution_10bit_420_1928x1080     "-w 1928 -h 1080 --input-depth 10 --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 2 --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
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
    test_enc 0 14557ab6508374d28518d7c14da131b4 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 1 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 1 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 24b13637154cd6d7624da8db6226e2fc touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 26bb0a5f2367d76de3307844d7b1c193 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 2 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9316f9d86a797619781019cc88acfac3 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 4 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 1 -h 1 --input-depth 10 --colour-format yuv422 --bpp 40   --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 2 -h 1 --input-depth 10 --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 c35737146ca1c4e2fb332146b26cddab touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 1 --input-depth 10 --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 1 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 186fed966b7117c6895c0624daab957a touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 4 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 fd50cdfcae3083e358e250490864cfed touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 2 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 f52f1acfbf3f4885210997ba1070dd3c touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 4 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    #Too big decomposition for small sizes
    test_enc 0 78be5e3261eb4d8324bf7170cba4a5c7 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 1 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 0 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 4 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 6628836fa0b3cc9b0481075e1168cd2d touchdown_1080p_yuv422p_8_bit_60_frames      "-w 6 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 66a348b44cef0ebc0500445af659080a touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 6 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 6 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 a7ecf521b1e1918a9921f3db336aa5f4 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 91b0da675cbe7b9f5f1fb914595bdedc touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 8 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 2 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 8 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 3544c5a1100e77956853c54a39b77aba touchdown_1080p_yuv422p_8_bit_60_frames      "-w 16 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 7e82c1acab57136d77ca4cac1ff14aa6 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 16 -h 64 --input-depth 8  --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 3 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 16 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 d9925df29176c03205d5db40774b41bf touchdown_1080p_yuv422p_8_bit_60_frames      "-w 30 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 198fbf9ed5f2356ae9ac4cc308f1e4f6 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 30 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 30 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 ea6f368628fbcec59e0726aafdd9361f touchdown_1080p_yuv422p_8_bit_60_frames      "-w 32 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 860289641926f6f8c2d88b2722c062f6 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 32 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 5 IGNORE                           touchdown_1080p_yuv422p_8_bit_60_frames      "-w 32 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 4a448e597d68ec5e5da6a4360a4f25d5 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 41b0b4d81edaab0206322737564cb41d touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e270fc6f9e3dfb74a98e8978a27516b4 touchdown_1080p_yuv422p_8_bit_60_frames      "-w 34 -h 64 --input-depth 8  --colour-format yuv422 --bpp 1000 --decomp_v 2 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 0 77aed5cf2f6a65ef61511934648047a9 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 4 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 b8a8f05e2850aad3023d1a441063a667 touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 1 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 107e50b64d87f3985bf70fe6b9eed38f touchdown_1080p_yuv420p_10_bit_le_60_frames  "-w 34 -h 64 --input-depth 10 --colour-format yuv420 --bpp 1000 --decomp_v 2 --decomp_h 5 --coding-vpred 2 --rc 0 -n 1 --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5  IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 0 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ae6e0cb64b9104e161d54ff656fc05c2 small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 6 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0  --rc 0      --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"

    test_enc 5  IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 0 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 1 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 2 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 4 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 5 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 11 IGNORE                          small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 5.5 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0 --rc 0       --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 ae6e0cb64b9104e161d54ff656fc05c2 small_422_8bit_32x32-small-blank-small      "-w 32 -h 32 --input-depth 8  --colour-format yuv422 --bpp 6 --decomp_v 2 --decomp_h 4 --coding-sigf 1 --coding-vpred 0   --rc 0     --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
}


#RUN different RC parameters Release/Debug/ ASM_C/ASM_MAX compare Parameters (1:asm) (2:lp number)
function test_invalid_yuv {
    asm=${SANITIZER_ASM:-$1}
    lp=$2
    cpu_profile=$3
    packetization_mode=$4

#   Use 8bit YUV as unpacked 10 bit YUV, values in YUV out of a range
    test_enc 0 f421bcdadf7796b7b4e0b65f19891f74 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 541fa066fce672c0448e1073b23f1ad5 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 64c9fc982be3d221689c93c4bbcbd53f touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 a6f62675478972bc38205e49d62fd963 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 3a377e477da851516d0381716c38a091 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 0b6d8644f20e1aced986f7e282cb979d touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 76f59d32071990d4940aef14a556430f touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 d34b99e9414617602930ecd949a97a14 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 74f5c9a025915ec1c65aebc12dfed603 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 1 --decomp_h 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 74e75b570a993b62351f3edb0f2dc663 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 e0d0be2a8fdacd4fecc8f1fcce590922 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 4 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 9c7677c6c844347468aac725de260c90 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 3 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 20169605553259f20f5de4c0fa48f579 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 2 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 efc4064fd26f2e22d8c7009e48f9b397 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv422 --bpp 3 --decomp_v 0 --decomp_h 1 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
    test_enc 0 5ee53184a9b526cdef508e892358b927 touchdown_1080p_yuv422p_8_bit_60_frames     "-w 1920 -h 1080 --input-depth 10  --colour-format yuv420 --bpp 3 --decomp_v 2 --decomp_h 5 --rc 0 -n 5  --asm $asm --lp $lp --profile $cpu_profile --packetization-mode $packetization_mode"
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
