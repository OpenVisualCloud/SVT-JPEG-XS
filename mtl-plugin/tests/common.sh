#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Shared by the mtl-plugin test scripts: environment checks, temp dir, result bookkeeping and the
# MTL loopback runners. Source it, do not run it.
# shellcheck disable=SC2034 # variables set here are used by the sourcing test scripts

export LC_ALL=C

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$TESTS_DIR")"
REPO_DIR="$(dirname "$PLUGIN_DIR")"
HOST_ENV="$TESTS_DIR/.host.env"

# ports and CPU sets written by setup_host.sh; a variable already set in the environment wins
if [ -f "$HOST_ENV" ]; then
    while IFS='=' read -r key value; do
        case "$key" in
            '' | '#'*) continue ;;
        esac
        [ -n "${!key:-}" ] || export "$key=$value"
    done <"$HOST_ENV"
fi

TX_IP="${TX_IP:-192.168.17.101}"
RX_IP="${RX_IP:-192.168.17.102}"
# SVT_PREFIX: an SVT-JPEG-XS installed into its own prefix (cmake --install <build> --prefix
# <dir>); the plugin is then built against it into the results dir instead of using PLUGIN_SO
if [ -n "${SVT_PREFIX:-}" ]; then
    [ -z "${PLUGIN_SO:-}" ] && [ -z "${PLUGIN_BUILD_DIR:-}" ] ||
        { echo "ERROR: set SVT_PREFIX or PLUGIN_SO / PLUGIN_BUILD_DIR, not both" >&2; exit 125; }
    [ -d "$SVT_PREFIX" ] || { echo "ERROR: SVT_PREFIX $SVT_PREFIX is not a directory" >&2; exit 125; }
    SVT_PREFIX="$(realpath "$SVT_PREFIX")"
    svt_pc="$(find "$SVT_PREFIX" -name SvtJpegxs.pc -path '*/pkgconfig/*' 2>/dev/null | head -1)"
    [ -n "$svt_pc" ] || { echo "ERROR: no SvtJpegxs.pc under SVT_PREFIX $SVT_PREFIX" >&2; exit 125; }
    PKG_CONFIG_PATH="$(dirname "$svt_pc")${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PKG_CONFIG_PATH
    # install root inside SVT_PREFIX (with DESTDIR e.g. <prefix>/usr/local), as SvtJpegxs.pc
    # itself derives it, so a multiarch libdir (lib/x86_64-linux-gnu) works too
    SVT_ROOT="$(realpath "$(pkg-config --variable=prefix "$svt_pc")")"
fi
TEST_WIDTH="${TEST_WIDTH:-1920}"
TEST_HEIGHT="${TEST_HEIGHT:-1080}"
TEST_SECONDS="${TEST_SECONDS:-10}"
[ "$TEST_SECONDS" -ge 5 ] 2>/dev/null || { echo "ERROR: TEST_SECONDS must be a number of seconds, at least 5" >&2; exit 125; }
TEST_FPS="${TEST_FPS:-p59}"
RUNS="${RUNS:-10}"
KEEP_FILES="${KEEP_FILES:-0}"
if [ -z "${OUT_DIR:-}" ]; then
    OUT_DIR="$TESTS_DIR/results/$(date +%Y%m%d_%H%M%S)"
fi
export OUT_DIR
if [ -n "${SVT_PREFIX:-}" ]; then
    PLUGIN_BUILD_DIR="$OUT_DIR/plugin_build"
fi
PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR:-$PLUGIN_DIR/build}"
PLUGIN_SO="${PLUGIN_SO:-$PLUGIN_BUILD_DIR/libst_plugin_st22_svt_jpeg_xs.so}"

# MTL sends ST22 codestreams in RTP payloads of this many bytes (st_tx_video_session.c: UDP
# payload minus the RFC 9134 header, rounded down to 128)
ST22_PKT_PAYLOAD=1280
# bits per pixel the MTL pipeline samples and RxTxApp encode lossy streams with
SAMPLE_BPP=3

PASS_CNT=0
FAIL_CNT=0
SKIP_CNT=0
BG_PIDS=()
WORK_DIR=""

die() {
    echo "ERROR: $*" >&2
    exit 125
}

# --- result bookkeeping ---------------------------------------------------------------------

# record <PASS|FAIL|SKIP> <case> <reason...>: one line on the console (reason shortened to the
# terminal width) and the full line in summary.txt
record() {
    local result="$1" case_name="$2"
    shift 2
    local reason="$*" width
    printf '%-4s %-14s %-36s %s\n' "$result" "$TEST_NAME" "$case_name" "$reason" >>"$OUT_DIR/summary.txt"
    if [ -t 1 ]; then
        width=$(($(tput cols 2>/dev/null || echo 120) - 46))
        [ "$width" -ge 20 ] && [ "${#reason}" -gt "$width" ] && reason="${reason:0:$((width - 3))}..."
    fi
    printf '   %-4s %-36s %s\n' "$result" "$case_name" "$reason"
}

pass() {
    PASS_CNT=$((PASS_CNT + 1))
    record PASS "$@"
}

fail() {
    FAIL_CNT=$((FAIL_CNT + 1))
    record FAIL "$@"
}

skip() {
    SKIP_CNT=$((SKIP_CNT + 1))
    record SKIP "$@"
}

# mm:ss of a number of seconds
duration() {
    printf '%d:%02d' $(($1 / 60)) $(($1 % 60))
}

# exit status of a test script: number of failed cases
finish() {
    echo "   $TEST_NAME: $PASS_CNT passed, $FAIL_CNT failed, $SKIP_CNT skipped in $(duration $((SECONDS - TEST_START)))"
    echo
    exit $((FAIL_CNT > 124 ? 124 : FAIL_CNT))
}

# --- setup and cleanup ----------------------------------------------------------------------

# terminal settings: sudo (use_pty) switches the terminal to raw mode while its command runs and
# two overlapping sudo calls (RX in the background, TX in the foreground) can leave it that way,
# so restore it after every MTL run and on exit
TTY_STATE=""
[ -t 0 ] && TTY_STATE="$(stty -g 2>/dev/null || true)"

restore_tty() {
    if [ -n "$TTY_STATE" ]; then
        stty "$TTY_STATE" 2>/dev/null || true
    fi
}

# give the CPU governor back (test_perf.sh), once; its steps go to governor.log
GOV_RELEASED=0
release_governor() {
    if [ "$GOV_RELEASED" = 0 ] && declare -F release_performance_governor >/dev/null; then
        GOV_RELEASED=1
        release_performance_governor >>"$CASE_DIR/governor.log" 2>&1 || true
        # the helper prints no INFO/WARN line when another holder keeps the governor
        grep -hE '^(INFO|WARN)' "$CASE_DIR/governor.log" 2>/dev/null | tail -1 | sed 's/^/   /' || true
    fi
}

# signal_sudo <signal> <pid of a background sudo>: signal sudo's command directly. Without a
# terminal sudo runs it in our process group and doesn't relay signals sent from that group.
signal_sudo() {
    sudo pkill "-$1" -P "$2" 2>/dev/null || true
    sudo kill "-$1" "$2" 2>/dev/null || true
}

cleanup() {
    local pid left=()
    # SIGINT lets the MTL apps stop cleanly, SIGTERM whatever still runs 2 s later
    for pid in "${BG_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null || sudo kill -0 "$pid" 2>/dev/null; then
            signal_sudo INT "$pid"
            left+=("$pid")
        fi
    done
    if [ "${#left[@]}" -gt 0 ]; then
        sleep 2
        for pid in "${left[@]}"; do
            signal_sudo TERM "$pid"
        done
    fi
    BG_PIDS=()
    release_governor
    if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
        if [ "$KEEP_FILES" = 1 ]; then
            echo "keeping temp files in $WORK_DIR"
        else
            # loopback dumps are written by the root MTL processes
            rm -rf "$WORK_DIR" 2>/dev/null || sudo rm -rf "$WORK_DIR"
        fi
    fi
    restore_tty
}

# init_test <name> <what it checks> <expected duration>: results dir, temp dir and cleanup
# trap for one test script
init_test() {
    TEST_NAME="$1"
    TEST_START=$SECONDS
    CASE_DIR="$OUT_DIR/$TEST_NAME"
    mkdir -p "$CASE_DIR"
    WORK_DIR="$(mktemp -d /tmp/mtl-plugin-test.XXXXXX)"
    trap cleanup EXIT
    trap 'exit 130' INT TERM
    echo "== $TEST_NAME: $2 (about $3)"
    echo "   logs     $CASE_DIR"
}

# minutes a number of loopback cases of TEST_SECONDS takes
loopback_minutes() {
    echo "$((($1 * (TEST_SECONDS + 6) + 59) / 60)) min"
}

# write env.txt once per results dir: what was tested and with which settings
write_env_info() {
    local f="$OUT_DIR/env.txt"
    [ -f "$f" ] && return 0
    {
        echo "date: $(date -Iseconds)"
        echo "host: $(hostname)"
        echo "repo: $(git -C "$REPO_DIR" log -1 --format='%h %s' 2>/dev/null)"
        echo "plugin: $(md5sum "$PLUGIN_SO" 2>/dev/null)"
        echo "svt-jpeg-xs: $(lib_path SvtJpegxs) $(md5sum "$(lib_path SvtJpegxs)" 2>/dev/null | cut -d' ' -f1)"
        if [ -n "${MTL_ROOT:-}" ]; then
            echo "mtl: $MTL_ROOT $(cat "$MTL_ROOT/VERSION" 2>/dev/null)" \
                "$(git -C "$MTL_ROOT" log -1 --format='%h %s' 2>/dev/null)"
            echo "mtl lib: $(lib_path mtl) $(md5sum "$(lib_path mtl)" 2>/dev/null | cut -d' ' -f1)"
        fi
        echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
        local v
        for v in MTL_ROOT TX_PORT RX_PORT TX_IP RX_IP TX_CPUS RX_CPUS PLUGIN_SO PLUGIN_SO_B \
            SVT_PREFIX SOURCE_YUV TEST_WIDTH TEST_HEIGHT TEST_SECONDS TEST_FPS RUNS; do
            echo "$v=${!v:-}"
        done
    } >"$f"
}

# lib_path <name> [binary]: the lib<name>.so a binary (default the plugin) loads
lib_path() {
    ldd "${2:-$PLUGIN_SO}" 2>/dev/null | awk -v n="lib$1.so" 'index($1, n) == 1 { print $3; exit }'
}

# check_svt_lib <binary>: print the libSvtJpegxs it loads; with SVT_PREFIX it must be the one there
check_svt_lib() {
    local lib
    lib="$(lib_path SvtJpegxs "$1")"
    [ -n "$lib" ] || die "$1 does not resolve libSvtJpegxs"
    lib="$(realpath "$lib")"
    printf '   %-8s %s loads %s (%s)\n' library "$(basename "$1")" "$lib" "$(md5sum "$lib" | cut -c1-8)"
    if [ -n "${SVT_PREFIX:-}" ] && [[ "$lib" != "$SVT_PREFIX"/* ]]; then
        die "$(basename "$1") loads $lib, not the library in SVT_PREFIX $SVT_PREFIX"
    fi
}

# with SVT_PREFIX: build the plugin against it, once per results dir
build_plugin_for_prefix() {
    local stamp="$PLUGIN_BUILD_DIR/.svt_prefix"
    if [ -f "$PLUGIN_SO" ] && [ "$(cat "$stamp" 2>/dev/null)" = "$SVT_PREFIX" ]; then
        return 0
    fi
    echo "   building the plugin against $SVT_PREFIX"
    rm -rf "$PLUGIN_BUILD_DIR"
    mkdir -p "$(dirname "$PLUGIN_BUILD_DIR")"
    if ! meson setup "$PLUGIN_BUILD_DIR" "$PLUGIN_DIR" -Dbuildtype=release >"$OUT_DIR/plugin_build.log" 2>&1 ||
        ! ninja -C "$PLUGIN_BUILD_DIR" >>"$OUT_DIR/plugin_build.log" 2>&1; then
        die "building the plugin against $SVT_PREFIX failed, see $OUT_DIR/plugin_build.log"
    fi
    echo "$SVT_PREFIX" >"$stamp"
}

require_plugin() {
    mkdir -p "$OUT_DIR"
    [ -n "${SVT_PREFIX:-}" ] && build_plugin_for_prefix
    [ -f "$PLUGIN_SO" ] || die "plugin $PLUGIN_SO not found, build it (mtl-plugin/build.sh) or set PLUGIN_SO"
    PLUGIN_SO="$(realpath "$PLUGIN_SO")"
    printf '   %-8s %s (%s)\n' plugin "$PLUGIN_SO" "$(md5sum "$PLUGIN_SO" | cut -c1-8)"
    check_svt_lib "$PLUGIN_SO"
    if [ -n "${PLUGIN_SO_B:-}" ]; then
        [ -f "$PLUGIN_SO_B" ] || die "PLUGIN_SO_B $PLUGIN_SO_B not found"
        PLUGIN_SO_B="$(realpath "$PLUGIN_SO_B")"
        check_svt_lib "$PLUGIN_SO_B"
    fi
}

require_mtl() {
    [ -n "${MTL_ROOT:-}" ] || die "MTL_ROOT is not set: point it to a built Media Transport Library source tree"
    TX_SAMPLE="$MTL_ROOT/build/app/TxSt22PipelineSample"
    RX_SAMPLE="$MTL_ROOT/build/app/RxSt22PipelineSample"
    RXTXAPP="$MTL_ROOT/tests/tools/RxTxApp/build/RxTxApp"
    local app
    for app in "$TX_SAMPLE" "$RX_SAMPLE" "$RXTXAPP"; do
        [ -x "$app" ] || die "$app not found, build MTL first ($MTL_ROOT/build.sh)"
    done
}

# vfio group of a VF is open: another MTL process uses the port
port_busy() {
    local group
    group="$(basename "$(readlink "/sys/bus/pci/devices/$1/iommu_group")")"
    sudo fuser -s "/dev/vfio/$group" 2>/dev/null
}

require_ports() {
    [ -n "${TX_PORT:-}" ] && [ -n "${RX_PORT:-}" ] ||
        die "TX_PORT and RX_PORT are not set: set them to two VFs on vfio-pci, or run setup_host.sh with PF_PORT"
    [ "$TX_PORT" != "$RX_PORT" ] || die "TX_PORT and RX_PORT must be different VFs"
    local port drv
    for port in "$TX_PORT" "$RX_PORT"; do
        [ -e "/sys/bus/pci/devices/$port" ] || die "port $port not found"
        drv="$(basename "$(readlink "/sys/bus/pci/devices/$port/driver" 2>/dev/null)")"
        [ "$drv" = vfio-pci ] || die "port $port is bound to '${drv:-no driver}', not vfio-pci"
        if port_busy "$port"; then
            die "port $port is in use by another process (MTL app running?)"
        fi
    done
    default_cpu_sets
}

# expand_cpulist "0-3,8" -> "0 1 2 3 8"
expand_cpulist() {
    local part parts range cpu_list=()
    IFS=',' read -ra parts <<<"$1"
    for part in "${parts[@]}"; do
        if [[ "$part" == *-* ]]; then
            mapfile -t range < <(seq "${part%-*}" "${part#*-}")
            cpu_list+=("${range[@]}")
        else
            cpu_list+=("$part")
        fi
    done
    echo "${cpu_list[@]}"
}

# primary_cpus <numa node>: the first hardware thread of every core on the node, without CPU 0
primary_cpus() {
    local cpu first
    for cpu in $(expand_cpulist "$(cat "/sys/devices/system/node/node$1/cpulist")"); do
        [ "$cpu" = 0 ] && continue
        first="$(cut -d, -f1 "/sys/devices/system/cpu/cpu$cpu/topology/thread_siblings_list" | cut -d- -f1)"
        [ "$first" = "$cpu" ] && echo "$cpu"
    done
}

# TX_CPUS / RX_CPUS default to two disjoint sets of 8 physical cores on the NIC's NUMA node, no
# SMT siblings; with only one of them set, the other takes 8 of the remaining cores. MTL lcores
# take the first two CPUs of each set, codec threads the rest.
default_cpu_sets() {
    local node pool cpu tx_set=() rx_set=()
    node="$(cat "/sys/bus/pci/devices/$TX_PORT/numa_node" 2>/dev/null)"
    [ -n "$node" ] && [ "$node" -ge 0 ] || node=0
    [ -n "${TX_CPUS:-}" ] && read -ra tx_set <<<"$(expand_cpulist "$TX_CPUS")"
    [ -n "${RX_CPUS:-}" ] && read -ra rx_set <<<"$(expand_cpulist "$RX_CPUS")"
    if [ "${#tx_set[@]}" = 0 ] || [ "${#rx_set[@]}" = 0 ]; then
        # cores of the node not in the set already given
        pool=()
        for cpu in $(primary_cpus "$node"); do
            [[ " ${tx_set[*]} ${rx_set[*]} " == *" $cpu "* ]] || pool+=("$cpu")
        done
        if [ "${#tx_set[@]}" = 0 ] && [ "${#rx_set[@]}" = 0 ]; then
            [ "${#pool[@]}" -ge 16 ] || die "NUMA node $node has ${#pool[@]} usable cores, need 16: set TX_CPUS and RX_CPUS"
            tx_set=("${pool[@]:0:8}")
            rx_set=("${pool[@]:8:8}")
        elif [ "${#tx_set[@]}" = 0 ]; then
            [ "${#pool[@]}" -ge 3 ] || die "no 3 cores left on NUMA node $node for TX next to RX_CPUS: set TX_CPUS"
            tx_set=("${pool[@]:0:8}")
        else
            [ "${#pool[@]}" -ge 3 ] || die "no 3 cores left on NUMA node $node for RX next to TX_CPUS: set RX_CPUS"
            rx_set=("${pool[@]:0:8}")
        fi
    fi
    for cpu in "${tx_set[@]}"; do
        [[ " ${rx_set[*]} " != *" $cpu "* ]] || die "TX_CPUS and RX_CPUS overlap (CPU $cpu): TX and RX need their own cores"
    done
    [ "${#tx_set[@]}" -ge 3 ] && [ "${#rx_set[@]}" -ge 3 ] ||
        die "TX_CPUS and RX_CPUS need at least 3 CPUs each (2 for MTL, the rest for the codec)"
    TX_CPUS="$(IFS=,; echo "${tx_set[*]}")"
    RX_CPUS="$(IFS=,; echo "${rx_set[*]}")"
    TX_LCORES="${tx_set[0]},${tx_set[1]}"
    RX_LCORES="${rx_set[0]},${rx_set[1]}"
    export TX_CPUS RX_CPUS
}

# --- tools and content ----------------------------------------------------------------------

build_frame_tool() {
    FRAME_TOOL="$WORK_DIR/frame_tool"
    cc -std=gnu11 -O2 -Wall -Werror -o "$FRAME_TOOL" "$TESTS_DIR/tools/frame_tool.c" ||
        die "building tools/frame_tool.c failed"
}

# frame_size <fmt> [height]: bytes of one picture
frame_size() {
    "$FRAME_TOOL" size "$1" "$TEST_WIDTH" "${2:-$TEST_HEIGHT}" | cut -d' ' -f1
}

# plane_size <fmt> <plane> [height]
plane_size() {
    "$FRAME_TOOL" size "$1" "$TEST_WIDTH" "${3:-$TEST_HEIGHT}" | cut -d' ' -f$(($2 + 2))
}

# gen_picture <fmt> <out>: one progressive frame, synthetic or derived from SOURCE_YUV
gen_picture() {
    "$FRAME_TOOL" gen "$1" "$TEST_WIDTH" "$TEST_HEIGHT" "$2" 0 ${SOURCE_YUV:+"$SOURCE_YUV"} ||
        die "generating $1 content failed"
}

# gen_fields <fmt> <out>: one interlaced frame as two different fields, first field first; the
# TX sample sends the file in field-sized chunks
gen_fields() {
    local h=$((TEST_HEIGHT / 2)) phase
    : >"$2"
    for phase in 0 1; do
        "$FRAME_TOOL" gen "$1" "$TEST_WIDTH" "$h" "$2.f$phase" "$phase" \
            ${SOURCE_YUV:+"$SOURCE_YUV" field} || die "generating $1 field $phase failed"
        cat "$2.f$phase" >>"$2"
        rm -f "$2.f$phase"
    done
}

# write_config <name> <json>: plugin config file in the temp dir, prints its path
write_config() {
    echo "$2" >"$WORK_DIR/$1.json"
    echo "$WORK_DIR/$1.json"
}

# --- MTL runners ----------------------------------------------------------------------------

# kahawai.json listing only the plugin under test (MTL reads KAHAWAI_CFG_PATH)
write_kahawai() {
    local so="${1:-$PLUGIN_SO}" out="${2:-$WORK_DIR/kahawai.json}"
    cat >"$out" <<EOF
{
  "plugins": [
    {
      "enabled": 1,
      "name": "st22_svt_jpegxs",
      "path": "$so"
    }
  ]
}
EOF
}

# set_mtl_env [plugin config] [kahawai.json]: environment for the MTL processes; sudo drops the
# caller's environment, so it is passed with sudo env. The plugin finds libSvtJpegxs through its
# runpath (build dir: the library it was built against) or the system library path.
set_mtl_env() {
    MTL_ENV=("KAHAWAI_CFG_PATH=${2:-$WORK_DIR/kahawai.json}")
    [ -n "${1:-}" ] && MTL_ENV+=("SVT_JXS_MTL_PLUGIN_CONFIG=$1")
    return 0
}

# frame rate name of the MTL samples for TEST_FPS (RxTxApp names, p59 = 59.94)
sample_fps() {
    case "$TEST_FPS" in
        p119) echo 119.88 ;;
        p59) echo 59.94 ;;
        p29) echo 29.97 ;;
        p23) echo 23.98 ;;
        p*) echo "${TEST_FPS#p}" ;;
        *) die "TEST_FPS $TEST_FPS: expected p23, p24, p25, p29, p30, p50, p59, p60, p100, p119 or p120" ;;
    esac
}

# run_samples <case> <tx_fmt> <rx_fmt> <tx_file> <plugin config|""> [args for both samples...]
# Starts RxSt22PipelineSample, then TxSt22PipelineSample 4 s later, and stops both with SIGINT
# after TEST_SECONDS. RX dumps its last 3 frames (fields when interlaced) to $RX_DUMP. Sets
# RX_FRAMES, TX_FRAMES (frames the encoder returned), PKTS_PER_FRAME, ENC_IN_CAPS and the log
# paths TX_LOG / RX_LOG.
run_samples() {
    local case_name="$1" tx_fmt="$2" rx_fmt="$3" tx_file="$4" cfg="$5"
    shift 5
    local fps
    fps="$(sample_fps)"
    TX_LOG="$CASE_DIR/$case_name.tx.log"
    RX_LOG="$CASE_DIR/$case_name.rx.log"
    RX_DUMP="$WORK_DIR/$case_name.rx.yuv"
    set_mtl_env "$cfg"

    # run in the temp dir: the TX sample draws ./logo.yuv onto every frame if it exists; stdin is
# /dev/null, the MTL processes never read it
    (cd "$WORK_DIR" && exec sudo env "${MTL_ENV[@]}" timeout -s INT $((TEST_SECONDS + 6)) \
        taskset -c "$RX_CPUS" "$RX_SAMPLE" --p_port "$RX_PORT" --p_sip "$RX_IP" \
        --p_rx_ip "$TX_IP" --st22_codec jpegxs --pipeline_fmt "$rx_fmt" --width "$TEST_WIDTH" \
        --height "$TEST_HEIGHT" --fps "$fps" --rx_url "$RX_DUMP" --rx_dump --lcores "$RX_LCORES" \
        "$@") </dev/null >"$RX_LOG" 2>&1 &
    local rx_pid=$!
    BG_PIDS=("$rx_pid")
    sleep 4
    (cd "$WORK_DIR" && exec sudo env "${MTL_ENV[@]}" timeout -s INT "$TEST_SECONDS" \
        taskset -c "$TX_CPUS" "$TX_SAMPLE" --p_port "$TX_PORT" --p_sip "$TX_IP" \
        --p_tx_ip "$RX_IP" --st22_codec jpegxs --pipeline_fmt "$tx_fmt" --width "$TEST_WIDTH" \
        --height "$TEST_HEIGHT" --fps "$fps" --tx_url "$tx_file" --lcores "$TX_LCORES" \
        "$@") </dev/null >"$TX_LOG" 2>&1 || true
    wait "$rx_pid" || true
    BG_PIDS=()
    restore_tty
    [ -f "$RX_DUMP" ] && sudo chown "$(id -u):$(id -g)" "$RX_DUMP"

    RX_FRAMES="$(sed -n 's/.*received frames \([0-9]*\).*/\1/p' "$RX_LOG" | tail -1)"
    TX_FRAMES="$(sed -n 's/.*encoder_free_session([0-9]*), total \([0-9]*\) encode frames.*/\1/p' "$TX_LOG" | tail -1)"
    RX_FRAMES="${RX_FRAMES:-0}"
    TX_FRAMES="${TX_FRAMES:-0}"
    # periodic TX session stats: "frames <n> pkts <n>:<n>"
    PKTS_PER_FRAME="$(awk '/TX_VIDEO_SESSION\(.*\): fps .* frames [0-9]+ pkts/ {
            for (i = 1; i < NF; i++) {
                if ($i == "frames") f += $(i + 1)
                if ($i == "pkts") { split($(i + 1), p, ":"); n += p[1] }
            }
        } END { printf "%.1f", f ? n / f : 0 }' "$TX_LOG")"
    ENC_IN_CAPS="$(sed -n 's/.*st22_encoder_register.* cap(\(0x[0-9a-f]*\):.*/\1/p' "$TX_LOG" | head -1)"
}

# error lines in MTL / plugin logs, except known benign ones:
# - the TX sample has no logo.yuv to draw
# - "st22_{en,de}coder_get_frame(0), invalid type": MTL race, the plugin's worker thread asks for
#   a frame before st22p_{tx,rx}_create has set the session type; once at session start
log_errors() {
    grep -hiE 'error|err =|fail|mismatch|giving up|invalid' "$@" |
        grep -vE 'open logo\.yuv fail|_get_frame\([0-9]+\), invalid type' || true
}

# frames_ok: RX got (nearly) every frame TX encoded, and TX sent at least half the frames
# TEST_SECONDS at TEST_FPS gives after about 3 s of MTL start-up
frames_ok() {
    local min
    min="$(awk -v f="$(sample_fps)" -v t="$TEST_SECONDS" 'BEGIN { printf "%d", f * (t - 3) / 2 }')"
    [ "$TX_FRAMES" -ge "$min" ] && [ "$RX_FRAMES" -ge $((TX_FRAMES - 3)) ]
}

# cmp_frames <dump> <source> <bytes> <count>: the first <count> pictures of <dump> each equal
# the first picture of <source>
cmp_frames() {
    local i
    for ((i = 0; i < $4; i++)); do
        cmp -s -i "$((i * $3)):0" -n "$3" "$1" "$2" || return 1
    done
}

# write_rxtx_json <out> <tx|rx|both> <fmt> <quality> <url> [interlaced true|false]
write_rxtx_json() {
    local out="$1" mode="$2" fmt="$3" quality="$4" url="$5" interlaced="${6:-false}"
    local ifs tx="" rx="" tx_if=0 rx_if=1
    case "$mode" in
        tx) ifs="{\"name\": \"$TX_PORT\", \"ip\": \"$TX_IP\"}"; tx_if=0 ;;
        rx) ifs="{\"name\": \"$RX_PORT\", \"ip\": \"$RX_IP\"}"; rx_if=0 ;;
        *) ifs="{\"name\": \"$TX_PORT\", \"ip\": \"$TX_IP\"}, {\"name\": \"$RX_PORT\", \"ip\": \"$RX_IP\"}" ;;
    esac
    if [ "$mode" != rx ]; then
        tx="{\"dip\": [\"$RX_IP\"], \"interface\": [$tx_if], \"st22p\": [{\"replicas\": 1,
      \"start_port\": 50000, \"payload_type\": 114, \"width\": $TEST_WIDTH, \"height\": $TEST_HEIGHT,
      \"fps\": \"$TEST_FPS\", \"interlaced\": $interlaced, \"codec\": \"JPEG-XS\", \"device\": \"AUTO\",
      \"quality\": \"$quality\", \"pack_type\": \"codestream\", \"input_format\": \"$fmt\",
      \"codec_thread_count\": 2, \"st22p_url\": \"$url\"}]}"
    fi
    if [ "$mode" != tx ]; then
        rx="{\"ip\": [\"$TX_IP\"], \"interface\": [$rx_if], \"st22p\": [{\"replicas\": 1,
      \"start_port\": 50000, \"payload_type\": 114, \"width\": $TEST_WIDTH, \"height\": $TEST_HEIGHT,
      \"fps\": \"$TEST_FPS\", \"interlaced\": $interlaced, \"codec\": \"JPEG-XS\", \"device\": \"AUTO\",
      \"pack_type\": \"codestream\", \"output_format\": \"$fmt\", \"codec_thread_count\": 2,
      \"display\": false, \"measure_latency\": true}]}"
    fi
    cat >"$out" <<EOF
{
  "interfaces": [$ifs],
  "tx_sessions": [$tx],
  "rx_sessions": [$rx]
}
EOF
}

# run_rxtxapp <log> <json> <seconds> <cpus> <lcores> [plugin config] [kahawai.json]
run_rxtxapp() {
    set_mtl_env "${6:-}" "${7:-}"
    (cd "$WORK_DIR" && exec sudo env "${MTL_ENV[@]}" timeout -s INT $(($3 + 30)) \
        taskset -c "$4" "$RXTXAPP" --config_file "$2" --test_time "$3" --lcores "$5") \
        </dev/null >"$1" 2>&1 || true
    restore_tty
}
