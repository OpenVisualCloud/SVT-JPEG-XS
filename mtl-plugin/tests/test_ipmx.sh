#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# IPMX config files (mtl-plugin/ipmx_1080p.json, ipmx_2160p.json) through the MTL ST22 pipeline
# samples, TX encoding GBRPLANAR10LE, and GBRPLANAR8 (the IPMX RGB 4:4:4 8-bit of TR-10-15
# Part 1 §8) with the file:
# - 1080p: RX receives the codestreams without decoding (JPEGXS_CODESTREAM output) and dumps
#   them, and every picture header must carry the VSF TR-08 §8.1.1 / ISO/IEC 21122-2 High444.12
#   values of the file, its component table the bit depth of the format
# - 2160p: RX decodes to the TX format, so the file is accepted and its stream decodes. The
#   picture header is not checked: in codestream output the RX sample takes frames up to 1 MiB
#   only (MTL's derive mode default), 2160p at 3 bpp is 3.1 MB
# The samples run in QUALITY mode only; SPEED gives the same codec parameters, since the files
# set every field either preset changes (decomp_v, quantization, slice_height; test_config.sh
# checks that they are in the files).

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

init_test ipmx "IPMX config files, picture headers of the codestreams" "$(loopback_minutes 8)"
require_plugin
require_mtl
require_ports
build_frame_tool
write_kahawai
write_env_info

# picture header fields every IPMX codestream of a W x H RGB stream of bit depth B must have,
# in the order frame_tool pih prints them:
# - Ppih 0x4a40 High444.12, Cpih 1 RCT, NL,x 5, NL,y 2, Qpih 1 uniform (TR-08 §8.1.1)
# - Plev: the level of the file, Sublev3bpp at 3 bpp, Sublev4bpp at 4 bpp (TR-08 §8.1.1)
# - Cw 0 (one full-width column), Bw 20, Fq 8, Br 4, Lh 0, Rl 0, Hsl 4 precincts = 16 lines at
#   NL,y 2 (High444.12, ISO/IEC 21122-2 Table A.3)
# - Fslc, Ppoc, Fs, Rm: encoder defaults the files leave alone, not constrained by IPMX
# - component table: B 8, 10 or 12 for all three components (TR-08 §8.1.1), sx = sy = 1 for
#   every component, 4:4:4 (ISO/IEC 21122-1 Table A.15)
expected_pih() {
    echo "ppih=0x4a40 plev=$1 wf=$2 hf=$3 cw=0 hsl=4 nc=3 bw=20 fq=8 br=4 fslc=0 ppoc=0 cpih=1" \
        "nlx=5 nly=2 lh=0 rl=0 qpih=1 fs=0 rm=0 b=$4,$4,$4 sx=1,1,1 sy=1,1,1"
}

# bpp4_config <name> <ipmx file> <plev>: the IPMX file for 4 bpp, as README.md "IPMX" says:
# encoder.bpp 4 and the Sublev4bpp stream_level, written to $WORK_DIR/<name>.json
bpp4_config() {
    sed -e "s/\"stream_level\": \"0x[0-9A-Fa-f]*\"/\"stream_level\": \"$3\", \"bpp\": 4/" "$2" \
        >"$WORK_DIR/$1.json"
    grep -q "\"bpp\": 4" "$WORK_DIR/$1.json" || die "no stream_level in $2"
}

# bpp_pkts <width> <height> <bpp>: packets of a frame, the codestream plus the ST22 box header
bpp_pkts() {
    echo $((($1 * $2 * $3 / 8 + ST22_BOX_HDR + ST22_PKT_PAYLOAD - 1) / ST22_PKT_PAYLOAD))
}

# ipmx_case <case> <config file> <width> <height> <fps> <bpp> <plev|""> <fmt>: frames, logs and
# packets per frame of a <bpp> codestream of a <fmt> source (more than a frame at <bpp> - 1
# needs, so a 4 bpp config can't fall back to the samples' 3 bpp); with a plev RX dumps the
# codestreams and their picture headers are checked, without one RX decodes to <fmt>
ipmx_case() {
    local case_name="$1" cfg="$2" bpp="$6" plev="$7" fmt="$8" problem headers want got
    [ -s "$cfg" ] || die "$case_name: config file $cfg missing"
    local rx_fmt=JPEGXS_CODESTREAM
    # generated content: a SOURCE_YUV of the caller has the caller's TEST_WIDTH x TEST_HEIGHT
    local SOURCE_YUV=
    TEST_WIDTH="$3" TEST_HEIGHT="$4" TEST_FPS="$5"
    local source="$WORK_DIR/$case_name.gbr.yuv"
    [ -n "$plev" ] || rx_fmt="$fmt"
    gen_picture "$fmt" "$source"
    run_samples "$case_name" "$fmt" "$rx_fmt" "$source" "$cfg"
    problem="$(log_errors "$TX_LOG" "$RX_LOG" | head -3)"
    local min_pkts max_pkts
    min_pkts="$(bpp_pkts "$3" "$4" $((bpp - 1)))"
    max_pkts="$(bpp_pkts "$3" "$4" "$bpp")"
    if ! frames_ok; then
        fail "$case_name" "tx $TX_FRAMES / rx $RX_FRAMES frames"
        return
    elif [ -n "$problem" ]; then
        fail "$case_name" "log errors: $(tr '\n' ' ' <<<"$problem")"
        return
    elif ! awk -v p="$PKTS_PER_FRAME" -v lo="$min_pkts" -v hi="$max_pkts" \
        'BEGIN { exit !(p > lo && p <= hi) }'; then
        fail "$case_name" \
            "$PKTS_PER_FRAME packets per frame, expected above $min_pkts, at most $max_pkts"
        return
    elif [ -z "$plev" ]; then
        pass "$case_name" "decoded $fmt, $3x$4 $5 $bpp bpp, $PKTS_PER_FRAME packets per frame"
        return
    fi
    headers="$CASE_DIR/$case_name.pih.txt"
    if ! "$FRAME_TOOL" pih "$RX_DUMP" >"$headers" 2>&1; then
        fail "$case_name" "$(head -1 "$headers")"
        return
    fi
    local depth=10
    [ "$fmt" = GBRPLANAR8 ] && depth=8
    want="$(expected_pih "$plev" "$3" "$4" "$depth")"
    # the dump holds the last 3 codestreams RX received
    got="$(sort -u "$headers")"
    if [ "$(wc -l <"$headers")" -lt 3 ]; then
        fail "$case_name" "$(wc -l <"$headers") picture headers in the dump, expected 3"
    elif [ "$got" != "$want" ]; then
        fail "$case_name" "picture header $(head -1 <<<"$got"), expected $want"
    else
        pass "$case_name" "$fmt plev $plev, $3x$4 $5 $bpp bpp, $PKTS_PER_FRAME packets per frame"
    fi
}

# 1080p59.94 at 2k-1, the lowest TR-08 level for it, at the samples' 3 bpp and at 4 bpp (the
# TR-08 Appendix B RGB interoperability rate); a 4 bpp 1080p frame (1036800 bytes) still fits
# the 1 MiB codestream output of the RX sample
ipmx_case ipmx_1080p59 "$PLUGIN_DIR/ipmx_1080p.json" 1920 1080 p59 3 0x1004 GBRPLANAR10LE
bpp4_config ipmx_1080p_4bpp "$PLUGIN_DIR/ipmx_1080p.json" 0x1006
ipmx_case ipmx_1080p59_4bpp "$WORK_DIR/ipmx_1080p_4bpp.json" 1920 1080 p59 4 0x1006 GBRPLANAR10LE
# 2160p at 30 fps: at 60 fps RX drops about 5 frames at start while the decoder warms up, with
# any encoder config; the file's level 4k-2 is the lowest TR-08 level for 2160p at any rate
ipmx_case ipmx_2160p30 "$PLUGIN_DIR/ipmx_2160p.json" 3840 2160 p30 3 "" GBRPLANAR10LE
bpp4_config ipmx_2160p_4bpp "$PLUGIN_DIR/ipmx_2160p.json" 0x2406
ipmx_case ipmx_2160p30_4bpp "$WORK_DIR/ipmx_2160p_4bpp.json" 3840 2160 p30 4 "" GBRPLANAR10LE

# RGB 4:4:4 8-bit, the IPMX JPEG XS format of TR-10-15 Part 1 §8, with the same files: the
# same picture header as 10-bit, B 8 in the component table. Skipped when the plugin doesn't
# offer GBRPLANAR8 (built against an MTL without it): bit 39 of the encoder caps MTL logs
CASES_8BIT=(ipmx_1080p59_8bit ipmx_1080p59_8bit_4bpp ipmx_2160p30_8bit ipmx_2160p30_8bit_4bpp)
if [ -n "$ENC_IN_CAPS" ] && [ $(((ENC_IN_CAPS >> 39) & 1)) = 1 ]; then
    ipmx_case ipmx_1080p59_8bit "$PLUGIN_DIR/ipmx_1080p.json" 1920 1080 p59 3 0x1004 GBRPLANAR8
    ipmx_case ipmx_1080p59_8bit_4bpp "$WORK_DIR/ipmx_1080p_4bpp.json" 1920 1080 p59 4 0x1006 \
        GBRPLANAR8
    # decoded back to GBRPLANAR8, as the 10-bit 2160p cases
    ipmx_case ipmx_2160p30_8bit "$PLUGIN_DIR/ipmx_2160p.json" 3840 2160 p30 3 "" GBRPLANAR8
    ipmx_case ipmx_2160p30_8bit_4bpp "$WORK_DIR/ipmx_2160p_4bpp.json" 3840 2160 p30 4 "" \
        GBRPLANAR8
elif [ -z "$ENC_IN_CAPS" ]; then
    # no caps line: a changed MTL log format must not hide the 8-bit cases as skipped
    for c in "${CASES_8BIT[@]}"; do
        fail "$c" "encoder caps not found in the TX log, can't tell whether the plugin offers GBRPLANAR8"
    done
else
    for c in "${CASES_8BIT[@]}"; do
        skip "$c" "plugin does not offer GBRPLANAR8 (encoder caps $ENC_IN_CAPS)"
    done
fi

finish
