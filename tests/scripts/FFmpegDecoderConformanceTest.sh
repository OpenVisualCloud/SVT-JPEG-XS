#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params

echo "Run FFmpeg Decoder Conformance Test"
source ./CommonLib.sh

path_bitstreams=$path_global
exec_ffmpeg="$exec_dec"
echo "ffmpeg:           $exec_ffmpeg"
echo "PATH BITSTERAMS:  $path_bitstreams"

if $exec_ffmpeg -formats 2>/dev/null | grep -q "jpegxs_pipe"; then
    demuxer="-f jpegxs_pipe"
else
    # Fallback for FFmpeg 6.0, 7.0, and 8.0 patched with SVT-JPEG-XS
    demuxer="-f image2"
fi

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

    offset=$(find_bitstream_header_offset "$src_jxs")
    if [ -z "$offset" ]; then
        echo "FAIL Could not find bitstream header (FF 10 FF 50) in $src_jxs"
        error=1
        end
        return
    fi
    tail -c +$((offset+1)) "$src_jxs" > "$stripped_jxs"

    cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error $demuxer -c:v libsvtjpegxs -i $stripped_jxs -f rawvideo $out_yuv"
    echo "run command: $cmd"
    ${cmd}
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
    test_dec 010
    test_dec 011
    test_dec 012
    test_dec 013
    test_dec 014
    test_dec 015
    test_dec 016
    test_dec 017
    test_dec 018
    # 019 4096x1743_13bit_COMPONENTS_4 (4:4:4:4, SDBQ-3776): verified byte-identical to
    # reference_decode/019_*.yuv now that the ffmpeg plugin maps COLOUR_FORMAT_PLANAR_4_COMPONENTS
    # to gbrap14le for bit depths above 12 (no upstream 13/14-bit yuva444p pix_fmt exists).
    test_dec 019
    test_dec 020
    test_dec 021
    test_dec 022
    test_dec 023
    test_dec 024
    test_dec 025
    test_dec 026
    test_dec 027
    test_dec 028
    test_dec 029
    test_dec 030
    test_dec 031
    test_dec 032
    test_dec 033
    test_dec 034
    test_dec 035
    test_dec 036
    test_dec 037
    test_dec 038
    test_dec 039
    test_dec 040
    test_dec 041
    # 042-047 *_COMPONENTS_4 (4:4:4:4, SDBQ-3776): verified byte-identical to their
    # reference_decode/*.yuv now that the ffmpeg plugin supports COLOUR_FORMAT_PLANAR_4_COMPONENTS.
    test_dec 042
    test_dec 043
    test_dec 044
    test_dec 045
    test_dec 046
    test_dec 047
    test_dec 048
    test_dec 049
    test_dec 050
    test_dec 051
    test_dec 052
    test_dec 053
    test_dec 054
    test_dec 055
    test_dec 056
    test_dec 057
    test_dec 058
    # 059 4095x1743_10bit_COMPONENTS_4 (4:4:4:4, SDBQ-3776): verified byte-identical to
    # reference_decode/059_*.yuv now that the ffmpeg plugin supports COLOUR_FORMAT_PLANAR_4_COMPONENTS.
    test_dec 059
    test_dec 060
    test_dec 061
    test_dec 062
    test_dec 063
    # 064 4095x1743_10bit_COMPONENTS_4 (4:4:4:4, SDBQ-3776): verified byte-identical to
    # reference_decode/064_*.yuv now that the ffmpeg plugin supports COLOUR_FORMAT_PLANAR_4_COMPONENTS.
    test_dec 064
    test_dec 065
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
    # 209-216, 218 *_COMPONENTS_4 (4:4:4:4, SDBQ-3776): verified byte-identical to their
    # reference_decode/*.yuv now that the ffmpeg plugin supports COLOUR_FORMAT_PLANAR_4_COMPONENTS.
    test_dec 209
    test_dec 210
    test_dec 211
    test_dec 212
    test_dec 213
    test_dec 214
    test_dec 215
    test_dec 216
    test_dec 217
    test_dec 218
}

#RUN output_bit_depth_msb_aligned tests: decode the same stripped conformance bitstream twice
#(msb-aligned 0 and 1). NOTE: the decoder AVOption must be placed BEFORE -i, not passed as an
#extra_args-style option after -i, or ffmpeg silently ignores it (only a warning, no error) -
#a real pitfall hit while writing this test.
#(1:bitstream name, without .jxs)
function test_msb_aligned {
    name=$1
    src_jxs=$path_bitstreams/test_bitsreams/$name.jxs
    stripped_jxs=./$tmp_dir/$name"_stripped.jxs"

    #(1:expect exit code) (2:expected md5) (3:msb_aligned value)
    function test_msb_dec {
        exit_code=$1
        md5=$2
        msb_aligned=$3
        out_yuv=./$tmp_dir/$name"_msb$msb_aligned.yuv"

        common_lib_update_test_id_run_return_1_to_ignore
        ignore=$?
        if [ $ignore -ne 0 ]; then
            return
        fi

        cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error $demuxer -c:v libsvtjpegxs -msb_aligned $msb_aligned -i $stripped_jxs -f rawvideo $out_yuv"
        echo "run command: $cmd"
        ${cmd}
        ret=$?
        if [ $ret -ne $exit_code ]; then
            echo "FAIL Invalid error code: $ret expected: $exit_code"
            error=1
            end
        fi

        echo -n "Test MD5 Expect: $md5 "
        md5_t=$(md5sum "${out_yuv}" | awk '{ print $1 }')
        if [ "$md5" = "$md5_t" ]; then
            echo "OK"
        else
            echo "FAIL get $md5_t"
            error=1
            end
        fi
    }

    offset=$(find_bitstream_header_offset "$src_jxs")
    tail -c +$((offset+1)) "$src_jxs" > "$stripped_jxs"

    #msb_aligned=0 explicit must be byte-identical to the existing reference_decode/<name>_*.yuv
    #(the untouched default path).
    test_msb_dec 0 "$2" 0
    #msb_aligned=1: pinned md5, verified (via a matching plain decode, right-shifted by the
    #(16 - bit_depth) msb-shift amount) to be the exact MSB-aligned re-packing of the
    #msb_aligned=0/default output above.
    test_msb_dec 0 "$3" 1
}

#(name, without .jxs) (msb_aligned=0 expected md5) (msb_aligned=1 expected md5)
test_all
test_msb_aligned 011 e379a376d704e3e0100f748fd4dd1bdd 58df668b531f53cbf4666c9a116d553e
#4:4:4:4 (12bit COMPONENTS_4, SDBQ-3776): confirms msb_aligned composes correctly with the new
#4-component colour formats, following the exact same "same bitstream, decoded 2 ways" pattern
#as the 011 (3-component) case above.
test_msb_aligned 209 1952fd773841c005c35d904d0891094e d3b4fb93cecc5562209ebc2b47ca4d21


common_lib_end_summary

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi

#return error code
end
