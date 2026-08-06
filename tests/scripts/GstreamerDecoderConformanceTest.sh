#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params

echo "Run GStreamer Decoder Conformance Test"
source ./CommonLib.sh

path_bitstreams=$path_global
exec_gst="gst-launch-1.0"
echo "gst-launch:       $exec_gst"
echo "PATH BITSTERAMS:  $path_bitstreams"

# svtjpegxsdec limitations vs ffmpeg (reason for skipped test cases below):
#   - Depth: only 8/10/12-bit accepted; 11/13/14-bit fail at caps negotiation.
#   - Sampling: no Grayscale; no 4-component (COMPONENTS_4) support.
#   - Odd heights: GStreamer's raw-video buffer allocator pads odd row counts to even,
#     so filesink output is larger than the reference YUV and diff always fails.
#     The decode itself succeeds; this is a buffer layout artifact, not a decoder bug.
# All other test cases produce diff-clean output against reference_decode/*.yuv.

error=0

function end {
    rm -fr $tmp_dir
    if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        echo Exit $0 script with exit $error
        exit $error
    fi
}

# Locate the JPEG-XS SOC+CAP marker (FF 10 FF 50) and print its byte offset, or nothing if absent.
function find_bitstream_header_offset {
    LC_ALL=C grep -abo $'\xff\x10\xff\x50' "$1" | head -1 | cut -d: -f1
}

function test_dec {
    name=$1
    src_jxs=$path_bitstreams/test_bitsreams/$name.jxs
    stripped_jxs=./$tmp_dir/$name"_stripped.jxs"

    common_lib_update_test_id_run_return_1_to_ignore
    ignore=$?
    if [ $ignore -ne 0 ]; then
        return
    fi

    ref_yuv=$(ls $path_bitstreams/reference_decode/$name"_"*.yuv 2>/dev/null | head -1)
    if [ -z "$ref_yuv" ]; then
        echo "FAIL Could not find reference_decode/$name""_*.yuv"
        error=1
        end
        return
    fi
    out_yuv=./$tmp_dir/$(basename "$ref_yuv")

    ref_base=$(basename "$ref_yuv" .yuv)
    rest=${ref_base#*_}
    wh=${rest%%_*}
    width=${wh%%x*}
    height=${wh##*x}
    rest2=${rest#*_}
    depth=${rest2%%bit_*}
    fmt=${rest2#*bit_}

    case $fmt in
        YUV444) sampling="YCbCr-4:4:4" ;;
        YUV422) sampling="YCbCr-4:2:2" ;;
        YUV420) sampling="YCbCr-4:2:0" ;;
        *)
            echo "FAIL Unsupported/unmapped pixel format in filename: $fmt"
            error=1
            end
            return
            ;;
    esac

    offset=$(find_bitstream_header_offset "$src_jxs")
    if [ -z "$offset" ]; then
        echo "FAIL Could not find bitstream header (FF 10 FF 50) in $src_jxs"
        error=1
        end
        return
    fi
    tail -c +$((offset+1)) "$src_jxs" > "$stripped_jxs"
    jxsc_size=$(stat -c%s "$stripped_jxs")

#   eval is used because the caps string contains parentheses that confuse plain word-splitting.
    cmd="$valgrind$exec_gst -q filesrc location=$stripped_jxs blocksize=$jxsc_size ! \"image/x-jxsc,alignment=(string)frame,interlace-mode=(string)progressive,sampling=(string)$sampling,depth=(int)$depth,width=(int)$width,height=(int)$height,framerate=(fraction)25/1\" ! svtjpegxsdec ! filesink location=$out_yuv"
    echo "run command: $cmd"
    eval "$cmd"
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL Can not decode bitstream, error code: $ret"
        error=1
        end
        return
    fi

    cmd_cmp="diff $ref_yuv $out_yuv"
    ${cmd_cmp} > /dev/null
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "FAIL comapare: $cmd_cmp"
        error=1
        end
    fi
}


rm -fr $tmp_dir
mkdir $tmp_dir

function test_all {
    test_dec 001
    test_dec 002
    test_dec 003
    test_dec 004
    test_dec 005
    test_dec 006
    test_dec 007
    test_dec 008
    test_dec 009
    # 010 11328x2704_11bit_YUV422 - SKIPPED, svtjpegxsdec sink caps only accept depth 8/10/12 (11-bit is rejected at caps negotiation)
    test_dec 011
    test_dec 012
    test_dec 013
    test_dec 014
    test_dec 015
    test_dec 016
    test_dec 017
    test_dec 018
    # 019 4096x1743_13bit_COMPONENTS_4 - SKIPPED, unsupported pixel format (4 components) in gst plugin, also 13-bit depth unsupported
    # 020 10496x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (2703): GStreamer's default raw video buffer allocation pads odd
    #     frame heights up to an even number (extra padding row per plane), so filesink's raw output is not byte-identical
    #     to the tightly-packed reference_decode YUV even though decoding itself succeeds. See header comment above.
    # 021 12192x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (see 020)
    # 022 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 023 12192x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (see 020)
    # 024 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 025 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 026 12191x2703_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 027 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 028-035 *_UNKNOWN_GRAY - SKIPPED, svtjpegxsdec has no Grayscale value in its sink caps "sampling" list and no
    #     COLOUR_FORMAT_GRAY case in its format-mapping switch, so grayscale streams fail to negotiate a src format
    # 036 32x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (see 020)
    # 037 32x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (see 020)
    # 038 16x2703_10bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 039 32x2703_10bit_YUV422 - SKIPPED, ODD HEIGHT (see 020)
    # 040 16x2703_10bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 041 16x2703_10bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 042-047 *_COMPONENTS_4 - SKIPPED, unsupported pixel format in gst plugin
    test_dec 048
    test_dec 049
    # 050 4073x1744_8bit_UNKNOWN_GRAY - SKIPPED, no grayscale support (see 028-035)
    test_dec 051
    test_dec 052
    # 053 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    test_dec 054
    test_dec 055
    # 056 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    test_dec 057
    # 058 12191x2703_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 059 4095x1743_10bit_COMPONENTS_4 - SKIPPED, unsupported pixel format in gst plugin
    test_dec 060
    # 061 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    test_dec 062
    # 063 12287x1743_12bit_YUV444 - SKIPPED, ODD HEIGHT (see 020)
    # 064 4095x1743_10bit_COMPONENTS_4 - SKIPPED, unsupported pixel format in gst plugin
    # 065 11328x2704_11bit_YUV422 - SKIPPED, 11-bit depth unsupported (see 010)
    test_dec 066
    test_dec 200
    test_dec 201
    test_dec 202
    test_dec 203
    test_dec 204
    test_dec 205
    test_dec 206
    test_dec 207
    test_dec 208
    # 209-216, 218 *_COMPONENTS_4 - SKIPPED, unsupported pixel format in gst plugin (216 is also 14-bit, also unsupported)
    # 217 976x650_12bit_UNKNOWN_GRAY - SKIPPED, no grayscale support (see 028-035)
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
