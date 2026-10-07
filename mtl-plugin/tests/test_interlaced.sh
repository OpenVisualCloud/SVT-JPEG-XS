#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Interlaced loopback through the MTL ST22 pipeline samples (--interlaced). The plugin codes
# every field as its own picture:
# - lossless: every received field bit-exact with one of the two source fields, both seen
# - lossy: packets per field about half of the packets per progressive frame

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

init_test interlaced "interlaced loopback, one field at a time" "$(loopback_minutes 6)"
require_plugin
require_mtl
require_ports
build_frame_tool
write_kahawai
write_env_info

LOSSLESS="$(write_config lossless '{"encoder": {"lossless": true}}')"
FIELD_HEIGHT=$((TEST_HEIGHT / 2))

# compare_fields <dump> <source with two fields> <field bytes>: prints the source field each of
# the 3 received fields equals ("0", "1" or "none")
compare_fields() {
    local i k match
    for i in 0 1 2; do
        match=none
        for k in 0 1; do
            if cmp -s -i "$((i * $3)):$((k * $3))" -n "$3" "$1" "$2"; then
                match=$k
                break
            fi
        done
        echo "$match"
    done
}

# lossless_fields <case> <fmt>
lossless_fields() {
    local case_name="$1" fmt="$2" source="$WORK_DIR/$2.fields.yuv" fs errors matches
    gen_fields "$fmt" "$source"
    run_samples "$case_name" "$fmt" "$fmt" "$source" "$LOSSLESS" --interlaced
    errors="$(log_errors "$TX_LOG" "$RX_LOG" | head -3)"
    if ! frames_ok; then
        fail "$case_name" "tx $TX_FRAMES / rx $RX_FRAMES fields"
        return
    elif [ -n "$errors" ]; then
        fail "$case_name" "log errors: $(tr '\n' ' ' <<<"$errors")"
        return
    elif ! grep -q 'interlaced, coded per field' "$TX_LOG"; then
        fail "$case_name" "encoder session not created per field"
        return
    fi
    fs="$(frame_size "$fmt" "$FIELD_HEIGHT")"
    matches="$(compare_fields "$RX_DUMP" "$source" "$fs" | paste -sd' ')"
    if [[ "$matches" == *none* ]]; then
        fail "$case_name" "received fields match source fields: $matches"
    elif [[ "$matches" != *0* ]] || [[ "$matches" != *1* ]]; then
        fail "$case_name" "only one source field received: $matches"
    else
        pass "$case_name" "3 fields bit-exact (source fields $matches), rx $RX_FRAMES fields"
    fi
}

for fmt in YUV422PLANAR10LE YUV420PLANAR8 YUV444PLANAR10LE GBRPLANAR10LE; do
    lossless_fields "lossless_$fmt" "$fmt"
done

# --- lossy rate per field -------------------------------------------------------------------

# the samples halve the codestream size for interlaced: each field gets half a frame's budget
source="$WORK_DIR/YUV422PLANAR10LE.yuv"
gen_picture YUV422PLANAR10LE "$source"
run_samples lossy_progressive YUV422PLANAR10LE YUV422PLANAR10LE "$source" ""
progressive_pkts="$PKTS_PER_FRAME"
progressive_ok=""
frames_ok && progressive_ok=1

run_samples lossy_interlaced YUV422PLANAR10LE YUV422PLANAR10LE "$source" "" --interlaced
errors="$(log_errors "$TX_LOG" "$RX_LOG" | head -3)"
if [ -z "$progressive_ok" ]; then
    fail lossy_field_rate "progressive reference run failed, see lossy_progressive logs"
elif ! frames_ok; then
    fail lossy_field_rate "tx $TX_FRAMES / rx $RX_FRAMES fields"
elif [ -n "$errors" ]; then
    fail lossy_field_rate "log errors: $(tr '\n' ' ' <<<"$errors")"
elif awk -v f="$PKTS_PER_FRAME" -v p="$progressive_pkts" \
    'BEGIN { exit !(p > 0 && f / p >= 0.45 && f / p <= 0.55) }'; then
    pass lossy_field_rate "$PKTS_PER_FRAME packets per field, $progressive_pkts per progressive frame"
else
    fail lossy_field_rate "$PKTS_PER_FRAME packets per field vs $progressive_pkts per frame, expected about half"
fi

finish
