#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


echo "Run Decoder Conformance Test"
source ./CommonLib.sh

path_bitstreams=$path_global
#Encoder binary next to $exec_dec, used to generate a temp MSB-aligned bitstream from real sample YUV
exec_enc="${exec_dec//SvtJpegxsDecApp/SvtJpegxsEncApp}"

echo "Decoder:         $exec_dec"
echo "Encoder:         $exec_enc"
echo "PATH BITSTERAMS: $path_bitstreams"

error=0
PARAM_LP_NUM=-1
PARAM_ASM="max"
PARAM_PACKETIZATION="0"

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        echo Exit $0 script with exit $error
        exit $error
    fi
}

function test_dec {
    name=$1
    format=$2
    yuv_name=$name"_"$format".yuv"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
     if [ $ignore -ne 0 ]; then
        return
    fi

    cmd="$valgrind$exec_dec --find-bitstream-header -i $path_bitstreams/test_bitsreams/$name.jxs -o ./$tmp_dir/$yuv_name --lp $PARAM_LP_NUM --asm ${SANITIZER_ASM:-$PARAM_ASM} --packetization-mode $PARAM_PACKETIZATION"
    echo "run command: $cmd"
    run_cmd "$cmd"

    cmd_cmp="diff $path_bitstreams/reference_decode/$yuv_name ./$tmp_dir/$yuv_name"
    ${cmd_cmp} >  /dev/null
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL comapare: $cmd_cmp"
        error=1
        end
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

#Prepare a temp bitstream from real sample YUV using EncApp, to test decoder output_bit_depth_msb_aligned
#without needing a pre-baked fixture file. Decoded twice below (msb-aligned 0 and 1) from this single bitstream.
bitstream_msb_prep="$tmp_dir/msb_prep.jxs"
cmd_prep="$exec_enc -i $path_bitstreams/encoder_tests/touchdown_1080p_yuv422p_10_bit_le_60_frames.yuv -w 1920 -h 1080 --input-depth 10 --colour-format yuv422 --bpp 3 --decomp_v 2 --decomp_h 5 --coding-sigf 1 --coding-vpred 0 --rc 0 -n 5 --asm max --lp 7 --profile latency --packetization-mode 0 -b $bitstream_msb_prep"
echo "run command: $cmd_prep"
run_cmd "$cmd_prep"

#RUN output_bit_depth_msb_aligned tests: decode the same self-generated bitstream twice (msb-aligned 0 and 1)
#Parameters (1:asm) (2:lp number) (3:packetization mode)
function test_msb_aligned_output {
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION=$3

    #(1:expect exit code) (2:expected md5, or IGNORE) (3:extra decoder params)
    function test_msb_dec {
        exit_code=$1
        md5=$2
        params=$3
        yuv_tmp="./$tmp_dir/msb_prep_out.yuv"

        common_lib_update_test_id_run_return_1_to_ignore
        ignore=$?
        if [ $ignore -ne 0 ]; then
            return
        fi

        cmd="$valgrind$exec_dec -i $bitstream_msb_prep -o $yuv_tmp --lp $PARAM_LP_NUM --asm ${SANITIZER_ASM:-$PARAM_ASM} --packetization-mode $PARAM_PACKETIZATION "$params
        echo "run command: $cmd"
        run_cmd "$cmd"

        ret=$?
        if [ $ret -ne $exit_code ]; then
            echo "FAIL Invalid error code: $ret expected: $exit_code"
            error=1
            end
        fi

        if [ $md5 = "IGNORE" ]; then
            echo "IGNORE TEST OUTPUT"
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

    #Default (LSB) and MSB-aligned outputs of the SAME bitstream, pinned md5 (verified: msb == lsb << (16-depth))
    test_msb_dec 0 f48a73066e7a78d0207d3419b37097f4 "--output-msb-aligned 0"
    test_msb_dec 0 984080ab17c11c747da832b3b7be6eab "--output-msb-aligned 1"

    #Reject any value other than 0/1
    test_msb_dec 2 d41d8cd98f00b204e9800998ecf8427e "--output-msb-aligned 2"
    test_msb_dec 2 d41d8cd98f00b204e9800998ecf8427e "--output-msb-aligned 255"
}

#No real 4-component (alpha) sample YUV exists in $path_bitstreams. Synthesize one on the fly (same
#technique as the MSB prep above): keep the real touchdown Y/Cb/Cr planes and append a duplicate of
#the Y plane as a synthetic full-resolution alpha plane, giving a valid yuva422 (4:2:2:4) raw layout.
yuva422_w=1920
yuva422_h=1080
yuva422_frames=2
yuva422_y_size=$((yuva422_w * yuva422_h))
yuva422_c_size=$((yuva422_w * yuva422_h / 2))
yuva422_frame_size=$((yuva422_y_size + 2 * yuva422_c_size))
yuva422_src="$path_bitstreams/encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
yuva422_synth="$tmp_dir/synth_yuva422.yuv"
rm -f "$yuva422_synth"
for ((yuva422_f = 0; yuva422_f < yuva422_frames; yuva422_f++)); do
    yuva422_off=$((yuva422_f * yuva422_frame_size))
    tail -c +$((yuva422_off + 1)) "$yuva422_src" | head -c $yuva422_frame_size >> "$yuva422_synth"
    tail -c +$((yuva422_off + 1)) "$yuva422_src" | head -c $yuva422_y_size >> "$yuva422_synth"
done

#Encode at a near-lossless bpp (24 = 8(Y)+4(Cb)+4(Cr)+8(A), i.e. uncompressed-equivalent budget) with
#deadzone quantization, to keep the decoded output close enough to the original input to compare directly.
bitstream_yuva422_prep="$tmp_dir/yuva422_prep.jxs"
cmd_prep_alpha="$exec_enc -i $yuva422_synth -w $yuva422_w -h $yuva422_h --input-depth 8 --colour-format yuva422 --bpp 24 --quantization 0 -n $yuva422_frames -b $bitstream_yuva422_prep --asm max"
echo "run command: $cmd_prep_alpha"
run_cmd "$cmd_prep_alpha"

#Same technique for 4:4:4:4 (rgba/yuva444): 4 full-resolution planes, no chroma subsampling, so just
#reuse the real Y plane 4 times per frame (no upsampling needed).
yuva444_w=1920
yuva444_h=1080
yuva444_frames=2
yuva444_plane_size=$((yuva444_w * yuva444_h))
yuva444_src="$path_bitstreams/encoder_tests/touchdown_1080p_yuv422p_8_bit_60_frames.yuv"
yuva444_synth="$tmp_dir/synth_yuva444.yuv"
rm -f "$yuva444_synth"
for ((yuva444_f = 0; yuva444_f < yuva444_frames; yuva444_f++)); do
    yuva444_off=$((yuva444_f * yuva422_frame_size))
    for ((yuva444_p = 0; yuva444_p < 4; yuva444_p++)); do
        tail -c +$((yuva444_off + 1)) "$yuva444_src" | head -c $yuva444_plane_size >> "$yuva444_synth"
    done
done

bitstream_yuva444_prep="$tmp_dir/yuva444_prep.jxs"
cmd_prep_444="$exec_enc -i $yuva444_synth -w $yuva444_w -h $yuva444_h --input-depth 8 --colour-format rgba --bpp 32 --quantization 0 -n $yuva444_frames -b $bitstream_yuva444_prep --asm max"
echo "run command: $cmd_prep_444"
run_cmd "$cmd_prep_444"

#Compares two same-size files sample-by-sample and fails if any absolute difference exceeds $3.
#(1:file A) (2:file B) (3:max allowed absolute per-byte difference)
function compare_yuv_tolerance {
    file_a=$1
    file_b=$2
    max_allowed=$3

    max_diff=`cmp -l "$file_a" "$file_b" | awk '{v1=strtonum("0"$2); v2=strtonum("0"$3); d=(v1>v2)?v1-v2:v2-v1; if(d>max)max=d} END{print max+0}'`
    echo -n "Max abs diff vs original input: $max_diff (allowed <= $max_allowed) "
    if [ "$max_diff" -gt "$max_allowed" ]; then
        echo "FAIL"
        error=1
        end
    else
        echo "OK"
    fi
}

#RUN yuva422 (4:2:2:4) decode test: decode the self-generated bitstream and compare against the
#original synthesized input (not just a pinned md5 of the decoder's own output), like MSB but going
#one step further and validating actual round-trip fidelity. Parameters (1:asm) (2:lp) (3:packetization-mode)
function test_four_component_alpha_decode {
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION=$3

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    out_yuv="$tmp_dir/yuva422_prep_out.yuv"
    cmd="$valgrind$exec_dec -i $bitstream_yuva422_prep -o $out_yuv --lp $PARAM_LP_NUM --asm $PARAM_ASM --packetization-mode $PARAM_PACKETIZATION"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Can not decode bitstream, error code: $ret"
        error=1
        end
    fi

    compare_yuv_tolerance "$yuva422_synth" "$out_yuv" 4
}

#RUN rgba/yuva444 (4:4:4:4) decode test - same round-trip-fidelity approach as yuva422 above.
#Parameters (1:asm) (2:lp) (3:packetization-mode)
function test_four_component_444_decode {
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION=$3

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    out_yuv="$tmp_dir/yuva444_prep_out.yuv"
    cmd="$valgrind$exec_dec -i $bitstream_yuva444_prep -o $out_yuv --lp $PARAM_LP_NUM --asm $PARAM_ASM --packetization-mode $PARAM_PACKETIZATION"
    echo "run command: $cmd"
    run_cmd "$cmd"
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Can not decode bitstream, error code: $ret"
        error=1
        end
    fi

    compare_yuv_tolerance "$yuva444_synth" "$out_yuv" 4
}

function test_all {
    PARAM_ASM=$1
    PARAM_LP_NUM=$2
    PARAM_PACKETIZATION=$3

    test_dec 001 1024x436_8bit_YUV444
    test_dec 002 4064x2704_8bit_YUV422
    test_dec 003 4064x2704_8bit_YUV422
    test_dec 004 4064x2704_8bit_YUV422
    test_dec 005 4064x2704_8bit_YUV422
    test_dec 006 4064x2704_8bit_YUV422
    test_dec 007 4064x2704_8bit_YUV422
    test_dec 008 4064x2704_8bit_YUV422
    test_dec 009 4064x2704_8bit_YUV422
    test_dec 010 11328x2704_11bit_YUV422
    test_dec 011 4064x2704_10bit_YUV422
    test_dec 012 4064x2704_10bit_YUV422
    test_dec 013 4096x1744_10bit_YUV444
    test_dec 014 4064x2704_10bit_YUV422
    test_dec 015 4096x1744_10bit_YUV444
    test_dec 016 4096x1744_10bit_YUV444
    test_dec 017 4096x1744_10bit_YUV444
    test_dec 018 4096x1744_10bit_YUV444
    test_dec 019 4096x1743_13bit_COMPONENTS_4
    test_dec 020 10496x2703_10bit_YUV422
    test_dec 021 12192x2703_10bit_YUV422
    test_dec 022 12287x1743_12bit_YUV444
    test_dec 023 12192x2703_10bit_YUV422
    test_dec 024 12287x1743_12bit_YUV444
    test_dec 025 12287x1743_12bit_YUV444
    test_dec 026 12191x2703_12bit_YUV444
    test_dec 027 12287x1743_12bit_YUV444
    test_dec 028 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 029 4095x1744_8bit_UNKNOWN_GRAY
    test_dec 030 4095x1744_8bit_UNKNOWN_GRAY
    test_dec 031 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 032 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 033 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 034 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 035 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 036 32x2703_10bit_YUV422
    test_dec 037 32x2703_10bit_YUV422
    test_dec 038 16x2703_10bit_YUV444
    test_dec 039 32x2703_10bit_YUV422
    test_dec 040 16x2703_10bit_YUV444
    test_dec 041 16x2703_10bit_YUV444
    test_dec 042 256x1199_8bit_COMPONENTS_4
    test_dec 043 256x1199_8bit_COMPONENTS_4
    test_dec 044 8192x1744_12bit_COMPONENTS_4
    test_dec 045 8192x1744_12bit_COMPONENTS_4
    test_dec 046 4095x1743_10bit_COMPONENTS_4
    test_dec 047 4095x1743_10bit_COMPONENTS_4
    test_dec 048 4064x2704_8bit_YUV422
    test_dec 049 4064x2704_8bit_YUV422
    test_dec 050 4073x1744_8bit_UNKNOWN_GRAY
    test_dec 051 4064x2704_10bit_YUV422
    test_dec 052 4064x2704_8bit_YUV422
    test_dec 053 12287x1743_12bit_YUV444
    test_dec 054 4064x2704_8bit_YUV422
    test_dec 055 4064x2704_8bit_YUV422
    test_dec 056 12287x1743_12bit_YUV444
    test_dec 057 4064x2704_8bit_YUV422
    test_dec 058 12191x2703_12bit_YUV444
    test_dec 059 4095x1743_10bit_COMPONENTS_4
    test_dec 060 4064x2704_8bit_YUV422
    test_dec 061 12287x1743_12bit_YUV444
    test_dec 062 4064x2704_8bit_YUV422
    test_dec 063 12287x1743_12bit_YUV444
    test_dec 064 4095x1743_10bit_COMPONENTS_4
    test_dec 065 11328x2704_11bit_YUV422
    test_dec 066 160x2704_10bit_YUV422
    test_dec 200 1520x1200_8bit_YUV420
    test_dec 201 1520x1200_8bit_YUV420
    test_dec 202 976x650_12bit_YUV420
    test_dec 203 976x650_12bit_YUV420
    test_dec 204 976x650_12bit_YUV444
    test_dec 205 976x650_12bit_YUV444
    test_dec 206 976x650_12bit_YUV422
    test_dec 207 976x650_12bit_YUV420
    test_dec 208 976x650_12bit_YUV420
    test_dec 209 976x650_12bit_COMPONENTS_4
    test_dec 210 488x325_12bit_COMPONENTS_4
    test_dec 211 488x325_12bit_COMPONENTS_4
    test_dec 212 488x325_12bit_COMPONENTS_4
    test_dec 213 488x325_12bit_COMPONENTS_4
    test_dec 214 488x325_12bit_COMPONENTS_4
    test_dec 215 488x325_12bit_COMPONENTS_4
    test_dec 216 488x325_14bit_COMPONENTS_4
    test_dec 217 976x650_12bit_UNKNOWN_GRAY
    test_dec 218 976x650_12bit_COMPONENTS_4
}

[[ $run_fast -eq 0 ]] && test_all c 10 0
                         test_all avx2 20 0
                         test_all avx2 20 1
[[ $run_fast -eq 0 ]] && test_all max 1 0
                         test_all max 1 1

echo Test output_bit_depth_msb_aligned
[[ $run_fast -eq 0 ]] && test_msb_aligned_output c 10 0
                         test_msb_aligned_output avx2 20 0
                         test_msb_aligned_output max 1 1

echo "Test yuva422 (4:2:2:4) decode round-trip"
[[ $run_fast -eq 0 ]] && test_four_component_alpha_decode c 10 0
                         test_four_component_alpha_decode avx2 20 0
                         test_four_component_alpha_decode max 1 1

echo "Test rgba/yuva444 (4:4:4:4) decode round-trip"
[[ $run_fast -eq 0 ]] && test_four_component_444_decode c 10 0
                         test_four_component_444_decode avx2 20 0
                         test_four_component_444_decode max 1 1

common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi

#return error code
end
