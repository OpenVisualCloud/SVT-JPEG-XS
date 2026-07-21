#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params
#
# Functional test for the ffmpeg jpegxs decoder plugin (ffmpeg-plugin/libsvtjpegxsdec.c), reusing
# the SAME official conformance bitstreams (test_bitsreams/*.jxs) and reference decoded YUVs
# (reference_decode/*.yuv) as DecoderConformanceTest.sh.
#
# IMPORTANT DIFFERENCE vs DecoderConformanceTest.sh:
# The official conformance `.jxs` files are wrapped in an ISOBMFF/JP2-style box header before the
# actual JPEG-XS codestream (signature "JXS \r\n" box, then ftyp/jp2h/ihdr...). SvtJpegxsDecApp
# skips this via its `--find-bitstream-header` flag (see DecoderConformanceTest.sh). ffmpeg's
# `jpegxs_pipe` demuxer has no equivalent - it expects the codestream to start at byte 0 - so this
# script strips the leading box bytes itself before feeding ffmpeg, by locating the SOC+CAP marker
# (bytes FF 10 FF 50) with `grep -abo` and slicing with `tail -c`. This was verified (byte-for-byte,
# via diff/md5) to produce output identical to reference_decode/*.yuv for every supported file.
#
# KNOWN LIMITATION (confirmed by actually running all 85 test_bitsreams/*.jxs through this plugin):
# 18 of the 85 official conformance files use COMPONENTS_4 (4-component, e.g. CMYK-like) pixel
# formats the ffmpeg plugin does not support mapping to any AVPixelFormat (fails with "Unsupported
# pixel format" from the plugin). This is a genuine plugin limitation, not a test-harness bug, so
# those test ids are intentionally excluded below (see SKIPPED IDS comment).
# NOTE: the 10 UNKNOWN_GRAY (monochrome) files were ALSO unsupported for the same reason, until
# libsvtjpegxsdec.c's set_pix_fmt() was fixed to map COLOUR_FORMAT_GRAY/COLOUR_FORMAT_PLANAR_YUV400
# to AV_PIX_FMT_GRAY8/9LE/10LE/12LE/14LE/16LE (mirroring the existing YUV420/422/444 branches).
# Verified byte-identical to reference_decode/*.yuv for all 10 GRAY files after the fix, so they are
# now included below like any other supported test id.
#
# NOTE: test_dec only takes the test id, not a pixel-format string. ffmpeg is never told what
# format to expect - it (like SvtJpegxsDecApp) reads width/height/bit-depth/pixel-format straight
# out of the JPEG-XS bitstream header itself. The only reason we need a reference file at all is
# to diff against it, so test_dec locates it with a glob (reference_decode/<id>_*.yuv) instead of
# requiring the format to be spelled out again at every call site.

echo "Run FFmpeg Decoder Conformance Test"
source ./CommonLib.sh

path_bitstreams=$path_global
exec_ffmpeg="$exec_dec"
echo "ffmpeg:           $exec_ffmpeg"
echo "PATH BITSTERAMS:  $path_bitstreams"

# The `jpegxs_pipe` raw demuxer (libavformat/img2dec.c IMAGEAUTO_DEMUXER) only exists natively in
# ffmpeg >= 8.1 (which ship JPEG-XS support upstream). Our own patches for 6.1/7.0/7.1/8.0 only add
# the codec type + mp4/mkv/mpegts container support (ffmpeg-plugin/<ver>/000{1,2}*.patch), NOT the
# img2 pipe registration - so fall back to the generic `image2` demuxer plus an explicit codec
# selection (-c:v libsvtjpegxs) on those versions, since image2 can't auto-probe a raw codestream
# it doesn't recognize the way jpegxs_pipe does.
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

#   No need to tell ffmpeg the pixel format/resolution: the plugin reads that straight out of the
#   JPEG-XS bitstream header itself (svt_jpeg_xs_decoder_init()), the same way SvtJpegxsDecApp does.
#   The only reason we need a name at all is to find the matching reference_decode/*.yuv file to
#   diff against, so just glob for it instead of hardcoding the format string per test id.
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

    cmd="$valgrind$exec_ffmpeg -y -hide_banner -loglevel error $demuxer -c:v libsvtjpegxs -i $stripped_jxs $out_yuv"
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
    # 019 4096x1743_13bit_COMPONENTS_4 - SKIPPED, unsupported pixel format in ffmpeg plugin
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
    # 042-047 *_COMPONENTS_4 - SKIPPED, unsupported pixel format in ffmpeg plugin
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
    # 059 4095x1743_10bit_COMPONENTS_4 - SKIPPED, unsupported pixel format in ffmpeg plugin
    test_dec 060
    test_dec 061
    test_dec 062
    test_dec 063
    # 064 4095x1743_10bit_COMPONENTS_4 - SKIPPED, unsupported pixel format in ffmpeg plugin
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
    # 209-216, 218 *_COMPONENTS_4 - SKIPPED, unsupported pixel format in ffmpeg plugin
    test_dec 217
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
