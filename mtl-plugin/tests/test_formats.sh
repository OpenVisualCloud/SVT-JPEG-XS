#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Progressive loopback of every plugin format through the MTL ST22 pipeline samples:
# - lossless: every received frame bit-exact with the source
# - GBR decoded as YUV444: planes R, G, B (codec component order)
# - lossy: all frames received, clean logs, packets per frame within the codestream budget
# - YUV422PLANAR16LE: decoded 16LE >> 6 equals decoded 10LE of the same picture
# - rejected combinations fail cleanly with the plugin's message

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

init_test formats "progressive loopback of every format" "$(loopback_minutes 27)"
require_plugin
require_mtl
require_ports
build_frame_tool
write_kahawai
write_env_info

LOSSLESS="$(write_config lossless '{"encoder": {"lossless": true}}')"
LOSSLESS_RCT="$(write_config lossless_rct '{"encoder": {"lossless": true, "rct": true}}')"
DECOMP_V0="$(write_config decomp_v0 '{"encoder": {"decomp_v": 0}}')"
DECOMP_V1="$(write_config decomp_v1 '{"encoder": {"decomp_v": 1}}')"

FORMATS=(YUV422PLANAR10LE YUV422PLANAR8 YUV422PLANAR12LE YUV420PLANAR8 YUV444PLANAR10LE
    YUV444PLANAR12LE GBRPLANAR10LE GBRPLANAR12LE)

# source picture of a format, generated once
src() {
    local f="$WORK_DIR/$1.yuv"
    [ -f "$f" ] || gen_picture "$1" "$f"
    echo "$f"
}

# common checks after a loopback that must deliver frames; prints the failure reason
loopback_problem() {
    local errors
    errors="$(log_errors "$TX_LOG" "$RX_LOG" | head -3)"
    if ! frames_ok; then
        echo "tx $TX_FRAMES / rx $RX_FRAMES frames"
    elif [ -n "$errors" ]; then
        echo "log errors: $(tr '\n' ' ' <<<"$errors")"
    fi
}

# lossless <case> <tx fmt> <rx fmt> <config> [source plane per output plane, e.g. "2 0 1"]
lossless() {
    local case_name="$1" tx="$2" rx="$3" cfg="$4" perm="${5:-}" source problem
    source="$(src "$tx")"
    run_samples "$case_name" "$tx" "$rx" "$source" "$cfg"
    problem="$(loopback_problem)"
    if [ -n "$problem" ]; then
        fail "$case_name" "$problem"
        return
    fi
    local fs
    fs="$(frame_size "$rx")"
    if [ -z "$perm" ]; then
        if cmp_frames "$RX_DUMP" "$source" "$fs" 3; then
            pass "$case_name" "3 frames bit-exact, rx $RX_FRAMES frames"
        else
            fail "$case_name" "received frames differ from the source"
        fi
        return
    fi
    # 4:4:4 planes all have the same size
    local ps i p src_plane bad="" perm_arr
    ps="$(plane_size "$rx" 0)"
    read -ra perm_arr <<<"$perm"
    for i in 0 1 2; do
        for p in 0 1 2; do
            src_plane="${perm_arr[$p]}"
            if ! cmp -s -i "$((i * fs + p * ps)):$((src_plane * ps))" -n "$ps" "$RX_DUMP" "$source"; then
                bad="frame $i plane $p != source plane $src_plane"
                break 2
            fi
        done
    done
    if [ -z "$bad" ]; then
        pass "$case_name" "3 frames bit-exact, output planes = source planes $perm"
    else
        fail "$case_name" "$bad"
    fi
}

# lossy <case> <fmt>: frames, logs and packets per frame within the codestream budget
lossy() {
    local case_name="$1" fmt="$2" problem
    run_samples "$case_name" "$fmt" "$fmt" "$(src "$fmt")" ""
    problem="$(loopback_problem)"
    # the samples encode at SAMPLE_BPP: codestream budget plus ST22 box header, in packets
    local budget=$((TEST_WIDTH * TEST_HEIGHT * SAMPLE_BPP / 8))
    local max_pkts=$(((budget + ST22_PKT_PAYLOAD - 1) / ST22_PKT_PAYLOAD + 1))
    if [ -n "$problem" ]; then
        fail "$case_name" "$problem"
    elif ! awk -v p="$PKTS_PER_FRAME" -v m="$max_pkts" 'BEGIN { exit !(p > 0 && p <= m) }'; then
        fail "$case_name" "$PKTS_PER_FRAME packets per frame, budget $max_pkts"
    else
        pass "$case_name" "rx $RX_FRAMES frames, $PKTS_PER_FRAME packets per frame (budget $max_pkts)"
    fi
}

# reject <case> <tx fmt> <rx fmt> <config> <message regex>: no frame decoded, message logged
reject() {
    local case_name="$1" tx="$2" rx="$3" cfg="$4" regex="$5"
    run_samples "$case_name" "$tx" "$rx" "$(src "$tx")" "$cfg"
    if [ "$RX_FRAMES" != 0 ]; then
        fail "$case_name" "rx decoded $RX_FRAMES frames, expected none"
    elif ! grep -qE "$regex" "$TX_LOG" "$RX_LOG"; then
        fail "$case_name" "message /$regex/ missing"
    else
        pass "$case_name" "$(grep -hE "$regex" "$TX_LOG" "$RX_LOG" | head -1 | sed 's/^MTL: [^,]*, //')"
    fi
}

# --- lossless, every format -----------------------------------------------------------------

for fmt in "${FORMATS[@]}"; do
    lossless "lossless_$fmt" "$fmt" "$fmt" "$LOSSLESS"
done

# the encoder caps MTL logs at plugin registration: bit n = st_frame_fmt n
HAS_16LE=""
if [ -n "$ENC_IN_CAPS" ]; then
    HAS_16LE=$(((ENC_IN_CAPS >> 15) & 1))
fi

# GBR stored G, B, R decodes into YUV444 as the codec components R, G, B
lossless gbr10_as_yuv444 GBRPLANAR10LE YUV444PLANAR10LE "$LOSSLESS" "2 0 1"
lossless gbr12_as_yuv444 GBRPLANAR12LE YUV444PLANAR12LE "$LOSSLESS" "2 0 1"
# reversible colour transform needs the R, G, B order to stay lossless
lossless lossless_rct_GBRPLANAR10LE GBRPLANAR10LE GBRPLANAR10LE "$LOSSLESS_RCT"
lossless lossless_rct_GBRPLANAR12LE GBRPLANAR12LE GBRPLANAR12LE "$LOSSLESS_RCT"

# --- lossy ----------------------------------------------------------------------------------

for fmt in "${FORMATS[@]}"; do
    lossy "lossy_$fmt" "$fmt"
    # kept for the 16LE comparison; a failed loopback may leave no dump
    if [ "$fmt" = YUV422PLANAR10LE ] && [ -f "$RX_DUMP" ]; then
        cp "$RX_DUMP" "$WORK_DIR/lossy_10le.rx.yuv"
    fi
done

if [ "$HAS_16LE" = 1 ]; then
    lossy lossy_YUV422PLANAR16LE YUV422PLANAR16LE
    # the same picture (10LE << 6) must give the same decoded samples
    fs="$(frame_size YUV422PLANAR16LE)"
    if [ ! -f "$RX_DUMP" ] || [ ! -f "$WORK_DIR/lossy_10le.rx.yuv" ]; then
        fail yuv422p16le_equals_10le "no received 16LE or 10LE frames to compare"
    elif out="$("$FRAME_TOOL" shift-cmp "$RX_DUMP" "$WORK_DIR/lossy_10le.rx.yuv" "$fs" 6)"; then
        pass yuv422p16le_equals_10le "decoded 16LE >> 6 = decoded 10LE, low 6 bits 0"
    else
        fail yuv422p16le_equals_10le "$(tr '\n' ' ' <<<"$out")"
    fi
else
    for c in lossy_YUV422PLANAR16LE yuv422p16le_equals_10le reject_12bit_to_16le reject_lossless_16le; do
        skip "$c" "plugin does not offer YUV422PLANAR16LE (encoder caps ${ENC_IN_CAPS:-not logged})"
    done
fi

# --- rejected combinations ------------------------------------------------------------------

reject reject_422_to_444 YUV422PLANAR10LE YUV444PLANAR10LE "" \
    'stream mismatch: .* expect .* YUV444PLANAR10LE'
if [ "$HAS_16LE" = 1 ]; then
    reject reject_12bit_to_16le YUV422PLANAR12LE YUV422PLANAR16LE "" \
        'stream mismatch: .* bit_depth 12 .* expect .* YUV422PLANAR16LE'
    # the library refuses lossless with MSB-aligned input
    reject reject_lossless_16le YUV422PLANAR16LE YUV422PLANAR16LE "$LOSSLESS" \
        'svt_jpeg_xs_encoder_init err'
fi
# 4:2:0 needs vertical decomposition: the samples' QUALITY mode with decomp_v 0 from the config
reject reject_420_decomp_v0 YUV420PLANAR8 YUV420PLANAR8 "$DECOMP_V0" 'needs decomp_v 1 or 2'

# SPEED mode (decomp_v 0) only through RxTxApp, the samples always use QUALITY
json="$WORK_DIR/speed420.json"
write_rxtx_json "$json" both YUV420PLANAR8 speed "$(src YUV420PLANAR8)"
for cfg_case in none decomp_v1; do
    log="$CASE_DIR/speed420_$cfg_case.log"
    cfg=""
    [ "$cfg_case" = decomp_v1 ] && cfg="$DECOMP_V1"
    run_rxtxapp "$log" "$json" "$TEST_SECONDS" "$TX_CPUS,$RX_CPUS" "$TX_LCORES,$RX_LCORES" "$cfg"
    if [ "$cfg_case" = none ]; then
        if grep -q 'needs decomp_v 1 or 2' "$log" && grep -q 'st22p_tx_create fail' "$log"; then
            pass reject_420_speed "SPEED mode refused: needs decomp_v 1 or 2"
        else
            fail reject_420_speed "TX session not refused, see $log"
        fi
    elif result="$(grep -m1 'app_rx_st22p_result' "$log")" && grep -q ', OK,' <<<"$result"; then
        pass speed420_decomp_v1 "${result#*app_rx_st22p_result(0), }"
    else
        fail speed420_decomp_v1 "${result:-no RX result}, see $log"
    fi
done

finish
