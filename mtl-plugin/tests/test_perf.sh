#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Pipeline performance: RxTxApp loopback of YUV422PLANAR10LE (SPEED mode, 3 bpp) at
# TEST_WIDTH x TEST_HEIGHT TEST_FPS, RUNS times. Reports fps and the RX latency (receive time
# minus the frame's RTP timestamp, both on the host clock) as the mean over the runs without the
# lowest and the highest value, and with bpftrace on MTL's st22p probes the time of every
# pipeline stage (tools/st22p_stages.bt).
#
# PERF_MODE=split (default) runs TX and RX as two RxTxApp processes, each pinned to its own CPU
# set; PERF_MODE=single runs one process with both sessions. PERF_CONFIG is a plugin config file
# for the runs. PLUGIN_SO_B adds a second plugin build, run interleaved with PLUGIN_SO. CODEC_TIER=1
# also times SvtJpegxsEncApp / SvtJpegxsDecApp alone, pinned to TX_CPUS.

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
# shellcheck source=../../tests/scripts/perf_governor.sh
source "$REPO_DIR/tests/scripts/perf_governor.sh"

PERF_MODE="${PERF_MODE:-split}"
PERF_SECONDS="${PERF_SECONDS:-15}"
CODEC_TIER="${CODEC_TIER:-0}"
CODEC_FRAMES="${CODEC_FRAMES:-60}"
case "$PERF_MODE" in
    split | single) ;;
    *) die "PERF_MODE $PERF_MODE: expected split or single" ;;
esac

variants_cnt=1
[ -n "${PLUGIN_SO_B:-}" ] && variants_cnt=2
init_test perf "RxTxApp fps, latency and stage times, $RUNS runs, $PERF_MODE mode" \
    "$(((RUNS * variants_cnt * (PERF_SECONDS + 7) + 59) / 60)) min"
require_plugin
require_mtl
require_ports
command -v bpftrace >/dev/null || die "bpftrace not found: install it (apt install bpftrace), test_perf.sh times the pipeline stages with it"
LIBMTL="$(realpath "$(lib_path mtl "$RXTXAPP")")"
[ "$(sudo bpftrace -l "usdt:$LIBMTL:st22p:*" 2>/dev/null | wc -l)" -ge 9 ] ||
    die "$LIBMTL has no st22p USDT probes: build MTL with enable_usdt (its default)"
printf '   %-8s %s (st22p probes for bpftrace)\n' mtl "$LIBMTL"
build_frame_tool
write_kahawai "$PLUGIN_SO" "$WORK_DIR/kahawai_a.json"
VARIANTS=(a)
[ -n "${PLUGIN_SO_B:-}" ] && VARIANTS+=(b) && write_kahawai "$PLUGIN_SO_B" "$WORK_DIR/kahawai_b.json"
write_env_info
echo "PERF_MODE=$PERF_MODE PERF_SECONDS=$PERF_SECONDS PERF_CONFIG=${PERF_CONFIG:-} CODEC_TIER=$CODEC_TIER" \
    >>"$OUT_DIR/env.txt"
# the helper reports every step, keep that in the log
acquire_performance_governor >"$CASE_DIR/governor.log" 2>&1
# no INFO/WARN line when another process already holds the governor
grep -hE '^(INFO|WARN)' "$CASE_DIR/governor.log" | sed 's/^/   /' || true

FMT=YUV422PLANAR10LE
SOURCE="$WORK_DIR/$FMT.yuv"
gen_picture "$FMT" "$SOURCE"
write_rxtx_json "$WORK_DIR/tx.json" tx "$FMT" speed "$SOURCE"
write_rxtx_json "$WORK_DIR/rx.json" rx "$FMT" speed "$SOURCE"
write_rxtx_json "$WORK_DIR/both.json" both "$FMT" speed "$SOURCE"

# pipeline stages timed by tools/st22p_stages.bt, in its order
STAGES=(tx_queue tx_encode tx_wait tx_send rx_receive rx_queue rx_decode rx_handover total)
CSV="$CASE_DIR/perf.csv"
{
    printf 'run,variant,mode,result,fps,latency_ms,frames'
    for st in "${STAGES[@]}"; do printf ',%s_avg_ms,%s_max_ms' "$st" "$st"; done
    echo
} >"$CSV"

# start_stages <log>: bpftrace on the st22p probes of every process using LIBMTL
start_stages() {
    # shellcheck disable=SC2024 # the log is written as the user on purpose
    sudo bpftrace "$TESTS_DIR/tools/st22p_stages.bt" "$LIBMTL" </dev/null >"$1" 2>&1 &
    STAGES_PID=$!
    BG_PIDS+=("$STAGES_PID")
    local i
    for ((i = 0; i < 100; i++)); do
        grep -q '^Attaching' "$1" && return 0
        sleep 0.1
    done
    die "bpftrace did not attach, see $1"
}

# stop_stages: stop bpftrace, which then prints its maps (not in a subshell: wait needs the
# child)
stop_stages() {
    signal_sudo TERM "$STAGES_PID"
    wait "$STAGES_PID" || true
    restore_tty
}

# stage_values <log>: ",avg,max" in ms of every stage, in STAGES order
stage_values() {
    local st
    for st in "${STAGES[@]}"; do
        awk -v st="$st" -F'[]:[]+' '$2 == st && $1 == "@c" { c = $3 } $2 == st && $1 == "@s" { s = $3 }
            $2 == st && $1 == "@m" { m = $3 }
            END { printf ",%.3f,%.3f", c ? s / c / 1e6 : 0, m / 1e6 }' "$1"
    done
}

# one_run <run> <variant>: appends a line to perf.csv
one_run() {
    local run="$1" variant="$2" kahawai="$WORK_DIR/kahawai_$2.json"
    local rx_log="$CASE_DIR/run${1}_$2.rx.log" tx_log="$CASE_DIR/run${1}_$2.tx.log"
    local stages_log="$CASE_DIR/run${1}_$2.stages.log"
    start_stages "$stages_log"
    if [ "$PERF_MODE" = split ]; then
        # RX first; it stops PERF_SECONDS later while TX still sends, so its fps only covers
        # time with frames arriving
        set_mtl_env "${PERF_CONFIG:-}" "$kahawai"
        (cd "$WORK_DIR" && exec sudo env "${MTL_ENV[@]}" timeout -s INT $((PERF_SECONDS + 30)) \
            taskset -c "$RX_CPUS" "$RXTXAPP" --config_file "$WORK_DIR/rx.json" \
            --test_time "$PERF_SECONDS" --lcores "$RX_LCORES") </dev/null >"$rx_log" 2>&1 &
        local rx_pid=$!
        BG_PIDS+=("$rx_pid")
        sleep 2
        run_rxtxapp "$tx_log" "$WORK_DIR/tx.json" $((PERF_SECONDS + 4)) "$TX_CPUS" "$TX_LCORES" \
            "${PERF_CONFIG:-}" "$kahawai"
        wait "$rx_pid" || true
        restore_tty
    else
        run_rxtxapp "$rx_log" "$WORK_DIR/both.json" "$PERF_SECONDS" "$TX_CPUS,$RX_CPUS" \
            "$TX_LCORES,$RX_LCORES" "${PERF_CONFIG:-}" "$kahawai"
    fi
    stop_stages
    BG_PIDS=()
    local stages
    stages="$(stage_values "$stages_log")"
    # "app_rx_st22p_result(0), OK, fps 59.92, 958 frame received"; latency is printed every
    # stat period and at the end, with several the first one (session start) is left out
    awk -v run="$run" -v variant="$variant" -v mode="$PERF_MODE" '
        /app_rx_st22p_stat.*avrage latency/ {
            match($0, /latency [0-9.]+/); lat[n++] = substr($0, RSTART + 8, RLENGTH - 8)
        }
        /app_rx_st22p_result/ {
            res = ($0 ~ /, OK,/) ? "OK" : "FAILED"
            match($0, /fps [0-9.]+/); fps = substr($0, RSTART + 4, RLENGTH - 4)
            match($0, /[0-9]+ frame received/); frames = substr($0, RSTART, RLENGTH) + 0
        }
        END {
            for (i = n > 1 ? 1 : 0; i < n; i++) { sum += lat[i]; cnt++ }
            printf "%s,%s,%s,%s,%s,%s,%s", run, variant, mode, res ? res : "NO_RESULT",
                fps ? fps : 0, cnt ? sprintf("%.3f", sum / cnt) : 0, frames + 0
        }' "$rx_log" >>"$CSV"
    echo "$stages" >>"$CSV"
    # columns 8.. are avg,max per stage in STAGES order
    tail -1 "$CSV" | awk -F, -v runs="$RUNS" '{
        printf "   run %2d/%d  variant %s  %-9s rx %7.3f fps  rx latency %7.3f ms  %d frames\n", $1, runs, $2, $4, $5, $6, $7
        printf "      stage avg ms  tx: queue %6.2f  encode %6.2f  wait %6.2f  send %6.2f\n", $8, $10, $12, $14
        printf "                    rx: receive %6.2f  queue %6.2f  decode %6.2f  handover %6.2f  total %6.2f\n", $16, $18, $20, $22, $24
    }'
}

for ((run = 1; run <= RUNS; run++)); do
    for variant in "${VARIANTS[@]}"; do
        one_run "$run" "$variant"
    done
done

# trimmed_mean <csv> <column> [variant]: mean without the lowest and the highest value (with 3+
# rows), of the rows for that variant (column 2) if given
trimmed_mean() {
    awk -F, -v c="$2" -v v="${3:-}" 'NR > 1 && (v == "" || $2 == v) { x[n++] = $c }
        END {
            if (!n) { print "n/a"; exit }
            min = max = x[0]
            for (i = 0; i < n; i++) { s += x[i]; if (x[i] < min) min = x[i]; if (x[i] > max) max = x[i] }
            if (n >= 3) { s -= min + max; n -= 2 }
            printf "%.3f (min %.3f max %.3f)", s / n, min, max
        }' "$1"
}

SUMMARY="$CASE_DIR/perf_summary.txt"
: >"$SUMMARY"
STAGES_TXT="$CASE_DIR/stages.txt"
: >"$STAGES_TXT"

# what every stage covers, for the table
declare -A STAGE_TEXT=(
    [tx_queue]="raw frame waits for the encoder"
    [tx_encode]="encode"
    [tx_wait]="codestream waits for the transport"
    [tx_send]="wait for the transmit slot, NIC reads the packets"
    [rx_receive]="paced transmission until the codestream is complete at RX"
    [rx_queue]="codestream waits for the decoder"
    [rx_decode]="decode"
    [rx_handover]="decoded frame waits for the RX app"
    [total]="raw frame in at TX to decoded frame out at RX"
)
for variant in "${VARIANTS[@]}"; do
    so="$PLUGIN_SO"
    [ "$variant" = b ] && so="$PLUGIN_SO_B"
    ok="$(awk -F, -v v="$variant" 'NR > 1 && $2 == v && $4 == "OK"' "$CSV" | wc -l)"
    {
        echo "== variant $variant: $so ($(md5sum "$so" | cut -c1-8)), mode $PERF_MODE"
        echo "fps         $(trimmed_mean "$CSV" 5 "$variant")"
        echo "latency_ms  $(trimmed_mean "$CSV" 6 "$variant")"
        echo "result      $ok/$RUNS OK"
    } >>"$SUMMARY"
    # per stage: mean of the run averages without the lowest and highest, and the worst frame
    {
        printf 'pipeline stages, variant %s: ms per frame, mean of the run averages and the slowest frame\n' "$variant"
        printf '  %-12s %8s %8s  %s\n' stage avg max ""
        col=8
        for st in "${STAGES[@]}"; do
            awk -F, -v v="$variant" -v c="$col" -v name="$st" -v text="${STAGE_TEXT[$st]}" '
                NR > 1 && $2 == v { x[n++] = $c; if ($(c + 1) > m) m = $(c + 1) }
                END {
                    min = max = x[0]
                    for (i = 0; i < n; i++) { s += x[i]; if (x[i] < min) min = x[i]; if (x[i] > max) max = x[i] }
                    if (n >= 3) { s -= min + max; n -= 2 }
                    printf "  %-12s %8.3f %8.3f  %s\n", name, n ? s / n : 0, m, text
                }' "$CSV"
            col=$((col + 2))
        done
    } >>"$STAGES_TXT"
    if [ "$ok" = "$RUNS" ]; then
        pass "pipeline_$variant" "fps $(trimmed_mean "$CSV" 5 "$variant"), latency ms" \
            "$(trimmed_mean "$CSV" 6 "$variant"), $ok/$RUNS OK"
    else
        fail "pipeline_$variant" "$ok/$RUNS runs OK, see $CSV"
    fi
done

# --- codec only -----------------------------------------------------------------------------

if [ "$CODEC_TIER" = 1 ]; then
    # the apps installed with SVT_PREFIX, else the ones on PATH; installed apps have no runpath,
    # so point them at the library installed with them
    app_dir="${SVT_ROOT:+$SVT_ROOT/bin/}"
    if [ -n "${SVT_PREFIX:-}" ]; then
        LD_LIBRARY_PATH="$(pkg-config --variable=libdir SvtJpegxs)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        export LD_LIBRARY_PATH
    fi
    ENC_APP="${ENC_APP:-$(command -v "${app_dir}SvtJpegxsEncApp" || true)}"
    DEC_APP="${DEC_APP:-$(command -v "${app_dir}SvtJpegxsDecApp" || true)}"
    [ -x "$ENC_APP" ] && [ -x "$DEC_APP" ] || die "SvtJpegxsEncApp / SvtJpegxsDecApp not found, set ENC_APP and DEC_APP"
    check_svt_lib "$ENC_APP"
    check_svt_lib "$DEC_APP"
    codec_in="$WORK_DIR/codec_in.yuv"
    for ((i = 0; i < CODEC_FRAMES; i++)); do cat "$SOURCE"; done >"$codec_in"
    codec_csv="$CASE_DIR/codec.csv"
    echo "run,encode_fps,encode_latency_ms,decode_fps,decode_latency_ms" >"$codec_csv"
    for ((run = 1; run <= RUNS; run++)); do
        log="$CASE_DIR/codec_run$run.log"
        {
            taskset -c "$TX_CPUS" "$ENC_APP" -i "$codec_in" \
                -w "$TEST_WIDTH" -h "$TEST_HEIGHT" --colour-format yuv422 --input-depth 10 \
                --bpp "$SAMPLE_BPP" -n "$CODEC_FRAMES" -b "$WORK_DIR/codec.jxs" --no-progress 1
            taskset -c "$TX_CPUS" "$DEC_APP" \
                -i "$WORK_DIR/codec.jxs" -o /dev/null
        } >"$log" 2>&1 || true
        awk -v run="$run" '
            /Finish .* frames, average/ { match($0, /average [0-9.]+/); v = substr($0, RSTART + 8, RLENGTH - 8)
                                          if (ef == "") ef = v; else df = v }
            /^Latency:/                 { match($0, /average [0-9.]+/); v = substr($0, RSTART + 8, RLENGTH - 8)
                                          if (el == "") el = v; else dl = v }
            END { printf "%s,%s,%s,%s,%s\n", run, ef + 0, el + 0, df + 0, dl + 0 }' "$log" >>"$codec_csv"
    done
    for col in 2:encode_fps 3:encode_latency_ms 4:decode_fps 5:decode_latency_ms; do
        echo "${col#*:} $(trimmed_mean "$codec_csv" "${col%%:*}")"
    done >>"$SUMMARY"
    if awk -F, 'NR > 1 && ($2 == 0 || $4 == 0) { bad = 1 } END { exit bad }' "$codec_csv"; then
        pass codec_tier "encode $(grep encode_fps "$SUMMARY" | cut -d' ' -f2) fps," \
            "decode $(grep decode_fps "$SUMMARY" | cut -d' ' -f2) fps"
    else
        fail codec_tier "an encode or decode run gave no result, see $codec_csv"
    fi
fi

sed 's/^/   /' "$STAGES_TXT"
release_governor
finish
