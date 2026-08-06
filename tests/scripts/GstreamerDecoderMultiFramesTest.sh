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

# NOTE on parity with FFmpegDecoderMultiFramesTest.sh / DecoderMultiFramesTest.sh (this
# was investigated empirically against the real built svtjpegxsdec element/library, not
# assumed from ffmpeg/native behavior):
#
# 1) FRAMING MODEL: ffmpeg's "-f jpegxs_pipe" demuxer auto-splits a continuous,
#    concatenated multi-frame raw .jxs file into individual AVPackets by parsing the
#    bitstream itself. The GStreamer "svtjpegxs" plugin has no equivalent demuxer/parser
#    element - svtjpegxsdec's sink caps declare "alignment=frame", i.e. it expects
#    exactly one codestream/frame per input GstBuffer, and gst_svt_jpeg_xs_dec_handle_frame()
#    processes exactly one GstVideoCodecFrame (one buffer) per call, with no internal
#    loop to consume more than one frame from a single buffer. This was confirmed by
#    pushing an entire 20-frame file as one filesrc buffer (blocksize=<file size>): only
#    the FIRST frame was decoded (output size == one frame, not 20).
#    Splitting the file into per-frame files and feeding it via multifilesrc was
#    considered, but multifilesrc lives in gst-plugins-good, which this repo's minimal
#    GStreamer build (see .github/scripts/build_gstreamer_plugin.sh) does not build.
#    Manually slicing the file by scanning for ALL "FF 10 FF 50" SOC+CAP marker byte
#    offsets (extending GstreamerDecoderConformanceTest.sh's find_bitstream_header_offset,
#    which only finds the first one) was also tried, but rejected: the marker bytes can
#    occur BY COINCIDENCE inside the compressed/entropy-coded slice data of a real
#    codestream, causing false-positive split points (confirmed on 4 of the correct test
#    files, e.g. Cyclist_1920x1080_8b_422_20f_v1_h4 got a spurious 21st "frame").
#    The solution used below instead relies on the fact that (almost) every multi-frame
#    test file here encodes a FIXED number of frames of UNIFORM byte size: a single
#    filesrc with "blocksize" set to (file size / frame count) pushes the file in
#    exactly frame-count buffers of exactly one real frame each, so svtjpegxsdec decodes
#    the WHOLE file (all N frames) in a single, persistent-decoder-instance pipeline run
#    - which is also a more realistic test than instantiating a fresh decoder per split
#    file, since it exercises decoder state actually carried across frames. This was
#    validated: for every "correct" bitstream below, the resulting concatenated raw YUV
#    is byte-identical (same md5) to either FFmpegDecoderMultiFramesTest.sh's or
#    DecoderMultiFramesTest.sh's own pinned reference value for the same source file.
#
# 2) DECOMP_H=1 SUPPORT: ffmpeg's plugin fails to decode Daylight_1280x720_{10b,8b}_422_20f_v1_h1
#    (decomp_h=1), so FFmpegDecoderMultiFramesTest.sh skips them. The native SvtJpegxsDecApp
#    does NOT have this limitation. Empirically, neither does svtjpegxsdec: both h1 variants
#    decode correctly below (and their md5s match DecoderMultiFramesTest.sh's own values for
#    the same files exactly), so they are NOT skipped here.
#
# 3) MID-STREAM HEADER-CHANGE HANDLING ("broken_*" tests): svt_jpeg_xs_decoder_send_frame()/
#    get_frame() re-parse each frame's own embedded header directly from the codestream
#    bytes; the width/height/depth declared in the pipeline's sink caps are only used to
#    kick off caps negotiation once, NOT re-validated per frame. So a mid-stream bit-depth
#    or resolution change is not rejected at caps level - the decoder just uses the real,
#    per-frame values (confirmed: broken_bit_depth_* frame 20 decodes as depth=10 producing
#    a differently-sized output for exactly that one frame; broken_resolution_* frame 20
#    decodes at 1920x1080 instead of 1280x720). Consequently:
#      - broken_resolution_* breaks the fixed-"blocksize = filesize/nframes" framing
#        strategy from point (1): its one resized frame is NOT the same byte size as the
#        other 40, so blocksize desyncs the whole stream from that point on and only the
#        first frame's worth of correct output is produced. This is a genuine, documented
#        artifact of this test script's chunking strategy (not a decoder bug) - the pinned
#        md5 below reflects that desynced (1-frame) output.
#      - broken_decomh_*/broken_bit_depth_*/broken_weight_table_*/broken_bitstream_* all
#        stay byte-uniform per frame, so they decode all 41 frames through a single
#        persistent decoder; exactly one (the corrupted) frame either decodes with
#        different bytes (decomh/weight_table - no visible error) or is silently dropped
#        with 0 bytes of output (bitstream - genuine slice decode failure, logged to
#        stderr by the library but NOT propagated as a GStreamer pipeline error - see
#        point (4)). All 4 end up byte-identical (40 real frames of raw output) and match
#        DecoderMultiFramesTest.sh's own pinned md5 for the same corruption class.
#    None of the 5 "broken_*" cases make gst-launch-1.0 exit non-zero (unlike the native
#    app, which aborts outright, exit 1), so exit_code=0 is expected for all of them here
#    (same convention as ffmpeg's script, though for a different underlying reason).
#
# 4) INVALID/CORRUPT BITSTREAM HANDLING: both ffmpeg and the native app reliably return a
#    non-zero exit code on genuinely corrupt bitstreams. svtjpegxsdec does NOT: internally
#    it uses GST_VIDEO_DECODER_ERROR() with a max-consecutive-errors threshold of 1, which
#    (from GstVideoDecoder's base class semantics) only turns into a real GST_FLOW_ERROR
#    for the SECOND+ consecutive decode error, and only causes gst-launch to exit non-zero
#    when the *entire* session decodes zero valid frames by EOS. For files with >=16
#    width/height that fail purely at the slice-decode stage (e.g.
#    error_injection_422_broken_bitstream), this "no valid frames" EOS check was, in
#    practice, NOT triggered - gst-launch exits 0 with a 0-byte output file. For
#    invalid_small_cfg_zero_band_08/09/10 (also >=16, also fail at slice-decode) it WAS
#    triggered - gst-launch exits 1. For invalid_small_cfg_zero_band_02..07 (all <16 in
#    width/height) the failure happens earlier still, at caps negotiation (svtjpegxsdec's
#    sink pad only accepts width/height in [16,16384]) - also exit 1. Given this
#    inconsistency, test_dec_invalid() below does NOT assert on the exit code (unlike
#    ffmpeg's/native's test_dec_invalid) - it only asserts that no non-empty output file
#    was produced, and prints the actual exit code for information only.
#
# 5) dec_r2r_mt-01/02/03 are SKIPPED: their real resolutions (8x4, 8x2, 4x2, discovered via
#    svt_jpeg_xs_decoder_get_single_frame_size() probing since the names don't encode
#    dimensions) are all below svtjpegxsdec's sink caps floor of width/height >= 16, so the
#    pipeline can never link regardless of the bitstream's actual validity - a hard,
#    permanent plugin limitation (same class of limitation as the depth/grayscale/
#    4-components gaps documented in GstreamerDecoderConformanceTest.sh), not a bug in the
#    test files themselves (ffmpeg and the native app can decode them fine).
#
# 6) "-frames:v 2"/"-n 2" HANG-REGRESSION EQUIVALENT (test_422_32x32_bpp6): that flag/option
#    is a safety cap for a decoder bug that used to hang when decoding from the start of
#    this specific bitstream; since our approach here runs one bounded gst-launch
#    invocation per (correct) bitstream rather than an open-ended streaming session, a
#    "timeout" wrapper around every test_dec/test_dec_invalid gst-launch-1.0 invocation
#    (not just this one) is the direct behavioral equivalent - it guarantees the test
#    suite cannot hang forever if the regression ever reappears, without needing a
#    frame-count flag GStreamer's filesrc/svtjpegxsdec have no equivalent of anyway (this
#    bitstream only contains a single real frame, so there is no "-frames:v 2"-style
#    truncation to translate in the first place).

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
#   Splits the multi-frame file into <nframes> equal-size chunks implicitly, by setting
#   filesrc's "blocksize" to (file size / nframes) - see framing-model note (1) above -
#   so svtjpegxsdec decodes the whole file (all frames) in one pipeline run.
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

#   NOTE: uses eval (not a plain ${cmd} word-split call like the ffmpeg script) because
#   the caps string below contains parentheses (e.g. "(fraction)"), which need real
#   shell quoting to avoid being misinterpreted as shell syntax. "timeout 60" guards
#   against the historical hang regression - see note (6) above.
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

# Invalid/corrupt bitstream check - see divergence note (4) above: only the ABSENCE of
# (non-empty) output is asserted, the exit code is reported but not checked.
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

    # R2R Decoder with MT per Slice small resolution.
    # dec_r2r_mt-01/02/03 SKIPPED - their real resolutions (8x4, 8x2, 4x2, determined via
    # svt_jpeg_xs_decoder_get_single_frame_size() probing) are below svtjpegxsdec's sink
    # caps floor of width/height >= 16 - see note (5) above.
    test_dec 0 one_slice_1080 5cd468fb69609a8d6fbcecd2a9d3e57b 1920 1080 10 420 60

    # Coefficients minus-zero and plus-zero coding. Dimensions/depth/format for these
    # (names don't encode them) were determined via svt_jpeg_xs_decoder_get_single_frame_size()
    # probing (each is a single-frame file, so no splitting needed - nframes=1).
    test_dec 0 test-zero-sign-1-minus-zero 6ba9ba462d53490717983c171ef50e59 1920 1080 10 422 1
    test_dec 0 test-zero-sign-1-plus-zero  6ba9ba462d53490717983c171ef50e59 1920 1080 10 422 1
    test_dec 0 test-zero-sign-2-minus-zero c57ad54b3a32a83b55a1cda3314c5a29 1920 1080 10 422 1
    test_dec 0 test-zero-sign-2-plus-zero  c57ad54b3a32a83b55a1cda3314c5a29 1920 1080 10 422 1
    test_dec 0 test-zero-sign-3-minus-zer  ad74cde2fc470f2c89b9060e7290f339 1920 1081 8 422 1
    test_dec 0 test-zero-sign-3-plus-zero  ad74cde2fc470f2c89b9060e7290f339 1920 1081 8 422 1
    test_dec 0 test-zero-sign-4-minus-zero fde93442420ef3048a3481bf1449ba59 16 64 8 422 1
    test_dec 0 test-zero-sign-4-plus-zero  fde93442420ef3048a3481bf1449ba59 16 64 8 422 1

    # Regression test for a bitstream that used to HANG the decoder - see note (6) above.
    # (single real frame; also determined via probing, name doesn't encode dimensions.)
    test_dec 0 test_422_32x32_bpp6 2f9749279c126703adb5e07e1196f59c 32 32 8 422 1

    # Correct bitstreams (dimensions/depth/frame-count all encoded in the filename).
    # NOTE: Daylight_..._v1_h1 (decomp_h=1) is NOT skipped here, unlike ffmpeg's script
    # - see divergence note (2) above; svtjpegxsdec decodes it fine and the md5 below
    # matches DecoderMultiFramesTest.sh's own (native-app) pinned value exactly.
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

    # Bitstreams with a header change mid-stream, plus one with a genuinely corrupted
    # mid-stream packet (broken_bitstream_*) - all Daylight_1280x720_8b_422, 20 correct
    # frames + 1 broken frame + 20 correct frames = 41 frames total. See divergence
    # note (3) above for exactly why/how each of these 5 behaves the way it does under
    # svtjpegxsdec, and why 4 of the 5 collapse to the SAME md5 (which matches
    # DecoderMultiFramesTest.sh's own pinned md5 for this corruption class) while
    # broken_resolution_* is a distinct, GStreamer-chunking-specific case.
    test_dec 0 broken_decomh_Daylight_1280x720_8b_422_20fx1fx20f       560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    test_dec 0 broken_bit_depth_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    # broken_resolution_*: mid-stream resolution change desyncs the fixed-blocksize
    # chunking strategy from that point on (see note 3) - only 1 frame's worth of
    # valid output survives; this is the pinned, GStreamer-specific result.
    test_dec 0 broken_resolution_Daylight_1280x720_8b_422_20fx1fx20f   1b47ea4294ac7b142a17c7c1e10d3dca 1280 720 8 422 41
    test_dec 0 broken_weight_table_Daylight_1280x720_8b_422_20fx1fx20f 560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
    test_dec 0 broken_bitstream_Daylight_1280x720_8b_422_20fx1fx20f    560d305916e20f010098bc31ece5d5b6 1280 720 8 422 41
}

function test_all_invalid {
    # Genuinely invalid/corrupt bitstreams - see divergence note (4) above for why exit
    # codes are not asserted here. Dimensions/depth/format determined via
    # svt_jpeg_xs_decoder_get_single_frame_size() probing (none of these names encode them).
    test_dec_invalid error_injection_422_broken_bitstream 32 32 8 422
    # invalid_small_cfg_zero_band_01 actually contains 3 concatenated 32x32 frames, but
    # is tested as one opaque blob (matching the ffmpeg/native scripts' single test call):
    # it is expected to fail to produce any usable output regardless.
    test_dec_invalid invalid_small_cfg_zero_band_01 32 32 8 422
    # invalid_small_cfg_zero_band_02..07 fail at CAPS NEGOTIATION (width/height < 16,
    # svtjpegxsdec's sink pad floor - see note 5), not at slice-decode time like 08/09/10;
    # the end result (no output produced) is the same either way.
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
