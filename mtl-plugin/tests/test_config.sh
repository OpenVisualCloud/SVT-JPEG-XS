#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Plugin config file loader (src/st22_svt_jpeg_xs_config.c), no MTL needed: builds
# tools/config_test.c against the loader with the plugin's compile flags and feeds it files.

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

init_test config "config file loader, no MTL needed" "10 s"
# the plugin build supplies the compile flags (with SVT_PREFIX built against that library)
require_plugin
write_env_info

compile_db="$PLUGIN_BUILD_DIR/compile_commands.json"
[ -f "$compile_db" ] || die "$compile_db not found, build the plugin first (mtl-plugin/build.sh build-only)"

# the include dirs and HAVE_* defines the plugin was built with
cmd="$(grep '"command".*st22_svt_jpeg_xs_config\.c' "$compile_db" | head -1)"
[ -n "$cmd" ] || die "no compile command for st22_svt_jpeg_xs_config.c in $compile_db"
cflags=()
for token in $cmd; do
    case "$token" in
        -I/* | -D*) cflags+=("$token") ;;
        -I*) cflags+=("-I$PLUGIN_BUILD_DIR/${token#-I}") ;;
    esac
done
libs="$(pkg-config --libs SvtJpegxs json-c)"
# run against the library pkg-config names, as the plugin build dir does
svt_libdir="$(pkg-config --variable=libdir SvtJpegxs)"

# build_harness <out> [defines to drop...]
build_harness() {
    local out="$1" flags=() flag drop
    shift
    for flag in "${cflags[@]}"; do
        for drop in "$@"; do
            [ "$flag" = "$drop" ] && continue 2
        done
        flags+=("$flag")
    done
    # shellcheck disable=SC2086 # pkg-config output is a word list
    cc -std=gnu11 -O1 -Wall -Werror "${flags[@]}" -I"$PLUGIN_DIR/src" -I"$PLUGIN_DIR/include" \
        -o "$out" "$TESTS_DIR/tools/config_test.c" "$PLUGIN_DIR/src/st22_svt_jpeg_xs_config.c" \
        $libs -Wl,-rpath,"$svt_libdir"
}

HARNESS="$WORK_DIR/config_test"
build_harness "$HARNESS" || die "building tools/config_test.c failed"
check_svt_lib "$HARNESS"

# run_case <case> <config file|""> [harness]: output in $OUT, exit status in $RC
run_case() {
    local log="$CASE_DIR/$1.log" harness="${3:-$HARNESS}"
    RC=0
    if [ -n "$2" ]; then
        SVT_JXS_MTL_PLUGIN_CONFIG="$2" "$harness" >"$log" 2>&1 || RC=$?
    else
        env -u SVT_JXS_MTL_PLUGIN_CONFIG "$harness" >"$log" 2>&1 || RC=$?
    fi
    OUT="$(cat "$log")"
}

# expect_ok <case> <file|""> [key=value...]: loads, applies, and every key=value is printed
expect_ok() {
    local case_name="$1" file="$2" kv missing=()
    shift 2
    run_case "$case_name" "$file"
    if [ "$RC" != 0 ]; then
        fail "$case_name" "exit $RC: $(grep -m1 -vE '^(enc|dec)\.' <<<"$OUT")"
        return
    fi
    for kv in "$@"; do
        grep -qxF "$kv" <<<"$OUT" || missing+=("$kv (got $(grep "^${kv%%=*}=" <<<"$OUT" || echo none))")
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        fail "$case_name" "${missing[*]}"
    else
        pass "$case_name"
    fi
}

# expect_fail <case> <file> <load return> <message regex>: load fails with that return value and
# the error message
expect_fail() {
    run_case "$1" "$2"
    if [ "$RC" = 0 ]; then
        fail "$1" "loaded, expected load=$3"
    elif ! grep -q "^load=$3 " <<<"$OUT"; then
        fail "$1" "expected load=$3, got $(grep -m1 '^load=' <<<"$OUT")"
    elif ! grep -qE "$4" <<<"$OUT"; then
        fail "$1" "message /$4/ missing: $(head -3 <<<"$OUT" | tr '\n' ' ')"
    else
        pass "$1"
    fi
}

# cfg <name> <content>: config file in the temp dir
cfg() {
    printf '%s\n' "$2" >"$WORK_DIR/$1.json"
    echo "$WORK_DIR/$1.json"
}

# --- no file, sample file -------------------------------------------------------------------

expect_ok no_env "" "load=0 log_level=2" "apply_encoder=0 apply_decoder=0"
grep -q "not set, use built-in defaults" <<<"$OUT" || fail no_env_message "defaults message missing"

# the sample config sets every key to its default: same parameters as an empty file, except
# the library verbosity its log_level selects
run_case empty "$(cfg empty '{"log_level": "info"}')"
empty_out="$(grep -E '^(enc|dec)\.' <<<"$OUT")"
run_case sample "$PLUGIN_DIR/sample_config.json"
if [ "$RC" != 0 ]; then
    fail sample_config "exit $RC: $(head -3 <<<"$OUT" | tr '\n' ' ')"
elif [ "$(grep -E '^(enc|dec)\.' <<<"$OUT")" != "$empty_out" ]; then
    fail sample_config "differs from library defaults: $(diff <(echo "$empty_out") <(grep -E '^(enc|dec)\.' <<<"$OUT") | grep '^>' | tr '\n' ' ')"
else
    pass sample_config "all keys at library defaults"
fi

# --- valid keys -----------------------------------------------------------------------------

expect_ok valid_keys "$(cfg valid '{
  "log_level": "debug",
  "encoder": {
    "lp": 8, "asm": "sse4_2", "cpu_profile": "cpu", "decomp_v": 1, "decomp_h": 3,
    "quantization": "uniform", "slice_height": 32, "coding_signs": "full", "coding_sigf": false,
    "coding_vpred": "zero_coefficients", "rc": "slice", "lossless": true, "coding_raw": false,
    "cap_compat": true, "rct": true, "stream_profile": "main422", "stream_level": "4k-1"
  },
  "decoder": {"lp": 4, "asm": "avx2"}
}')" "load=0 log_level=3" enc.threads_num=8 enc.use_cpu_flags=0x7f enc.cpu_profile=1 \
    enc.ndecomp_v=1 enc.ndecomp_h=3 enc.quantization=1 enc.slice_height=32 \
    enc.coding_signs_handling=2 enc.coding_significance=0 enc.coding_vertical_prediction_mode=2 \
    enc.rate_control_mode=2 enc.lossless_enable=1 enc.coding_raw_disable=1 enc.cap_compat=1 \
    enc.enable_color_transform=1 enc.profile_ppih_override=0x3540 enc.level_plev_override=0x2000 \
    enc.verbose=5 dec.threads_num=4 dec.use_cpu_flags=0x1ff dec.verbose=5

# numbers instead of names, lp 0 keeps the session's thread count
expect_ok numeric_values "$(cfg numeric '{"encoder": {"lp": 0, "quantization": 1, "rc": "3",
  "coding_signs": "0x2", "stream_profile": "0x3A40", "stream_level": 1024}}')" \
    enc.threads_num=5 enc.quantization=1 enc.rate_control_mode=3 enc.coding_signs_handling=2 \
    enc.profile_ppih_override=0x3a40 enc.level_plev_override=0x400

# log_level drives the library verbosity: error = errors only, warning/info = library default,
# debug = more; without log_level the library keeps its own
expect_ok log_error "$(cfg log_error '{"log_level": "error"}')" "load=0 log_level=0" enc.verbose=1 dec.verbose=1
grep -q "loaded" <<<"$OUT" && fail log_error_quiet "info messages shown at log_level error"
expect_ok log_warning "$(cfg log_warning '{"log_level": "warning"}')" "load=0 log_level=1" enc.verbose=2
expect_ok no_log_level "$(cfg no_log '{"encoder": {}}')" "load=0 log_level=2" enc.verbose=2 dec.verbose=0

# comments inside and after the top level object
expect_ok comments "$(cfg comments '// leading comment
{
  /* block */ "encoder": {"lp": 6} // trailing
}
// after the object
/* and a block
   over lines */')" enc.threads_num=6

# --- unknown keys, bad values ---------------------------------------------------------------

expect_fail unknown_top "$(cfg unknown_top '{"encoderr": {}}')" -22 "unknown key encoderr$"
expect_fail unknown_enc "$(cfg unknown_enc '{"encoder": {"lossles": true}}')" -22 "unknown key encoder.lossles$"
expect_fail unknown_dec "$(cfg unknown_dec '{"decoder": {"rc": 1}}')" -22 "unknown key decoder.rc$"
expect_fail bad_uint "$(cfg bad_uint '{"encoder": {"decomp_v": 3}}')" -22 'encoder.decomp_v: invalid value 3, expected integer 0-2'
expect_fail bad_negative "$(cfg bad_neg '{"encoder": {"lp": -1}}')" -22 'encoder.lp: invalid value -1, expected integer 0-1024'
expect_fail bad_uint_string "$(cfg bad_uint_str '{"encoder": {"slice_height": "16"}}')" -22 'encoder.slice_height: invalid value "16"'
expect_fail bad_bool "$(cfg bad_bool '{"encoder": {"lossless": "yes"}}')" -22 'encoder.lossless: invalid value "yes", expected true or false'
expect_fail bad_name "$(cfg bad_name '{"encoder": {"asm": "neon"}}')" -22 'encoder.asm: invalid value "neon", expected "c", "mmx"'
expect_fail bad_numeric "$(cfg bad_numeric '{"encoder": {"rc": " 1"}}')" -22 'encoder.rc: invalid value " 1".* or number 0-3'
expect_fail bad_hex "$(cfg bad_hex '{"encoder": {"stream_profile": "0x"}}')" -22 'or number 0x0-0xFFFF'
expect_fail bad_log_level "$(cfg bad_log '{"log_level": "verbose"}')" -22 'log_level: invalid value "verbose", expected "error", "warning", "info", "debug"'
expect_fail section_type "$(cfg section_type '{"decoder": []}')" -22 '"decoder" is not a JSON object'
expect_fail top_type "$(cfg top_type '[1, 2]')" -22 'top level is not a JSON object'
expect_fail missing_file "$WORK_DIR/does_not_exist.json" -22 'fail to open .*does_not_exist.json: No such file'

# --- syntax errors report the line ----------------------------------------------------------

expect_fail syntax_line4 "$(cfg syntax '{
  "encoder": {
    "lp": 4
    "asm": "avx2"
  }
}')" -22 'syntax.json:4: '
# the file ends with a newline, so the end of file is on line 2
expect_fail syntax_eof "$(cfg eof '{"encoder": {"lp": 4}')" -22 'eof.json:2: unexpected end of file'
expect_fail trailing_data "$(cfg trailing '{"encoder": {}}
{"decoder": {}}')" -22 'trailing.json:2: unexpected data after the top level value'
expect_fail open_comment "$(cfg open_comment '{"encoder": {}}
/* not closed')" -22 'open_comment.json:3: unexpected end of file'

# --- size limit -----------------------------------------------------------------------------

# exactly 1 MiB loads, one byte more is refused
size_file="$WORK_DIR/size.json"
{
    printf '{"encoder": {}}\n//'
    head -c $((1048576 - 19)) /dev/zero | tr '\0' 'x'
    printf '\n'
} >"$size_file"
expect_ok size_1mib "$size_file" "load=0 log_level=2"
printf 'x' >>"$size_file"
expect_fail size_over_1mib "$size_file" -22 'larger than 1048576 bytes'

# --- keys the installed library does not support --------------------------------------------

# a library without the lossless field: plugin start must fail on the key, not ignore it
HARNESS_OLD="$WORK_DIR/config_test_old_lib"
if build_harness "$HARNESS_OLD" -DHAVE_ENC_LOSSLESS_ENABLE; then
    run_case unsupported_key "$(cfg unsupported '{"encoder": {"lossless": true}}')" "$HARNESS_OLD"
    if [ "$RC" != 0 ] && grep -q '^load=-95 ' <<<"$OUT" &&
        grep -q 'encoder.lossless is not supported by the installed SVT-JPEG-XS library' <<<"$OUT"; then
        pass unsupported_key "lossless rejected without HAVE_ENC_LOSSLESS_ENABLE"
    else
        fail unsupported_key "$(head -3 <<<"$OUT" | tr '\n' ' ')"
    fi
else
    fail unsupported_key "building the harness without HAVE_ENC_LOSSLESS_ENABLE failed"
fi

finish
