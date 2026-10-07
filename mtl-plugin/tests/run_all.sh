#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Run the tests one after another into one results dir: config, formats, interlaced, perf
# (TESTS selects a subset, e.g. TESTS="config formats"). With PF_PORT set, runs setup_host.sh
# first when tests/.host.env is missing or stale. Exit status: number of failed cases.

# shellcheck source-path=SCRIPTDIR
set -eo pipefail

# host setup first: common.sh exports the ports and CPU sets from .host.env, which must already
# be the new one
setup="$(dirname "${BASH_SOURCE[0]}")/setup_host.sh"
if [ -n "${PF_PORT:-}" ] && ! "$setup" --check >/dev/null; then
    "$setup" || { echo "ERROR: host setup failed" >&2; exit 125; }
fi

# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

TESTS="${TESTS:-config formats interlaced perf}"

mkdir -p "$OUT_DIR"
: >"$OUT_DIR/summary.txt"

# MTL loopbacks need the two ports to themselves
if [[ "$TESTS" != config ]]; then
    tx="${TX_PORT:-$(sed -n 's/^TX_PORT=//p' "$HOST_ENV" 2>/dev/null)}"
    rx="${RX_PORT:-$(sed -n 's/^RX_PORT=//p' "$HOST_ENV" 2>/dev/null)}"
    [ -n "$tx" ] && [ -n "$rx" ] || die "TX_PORT / RX_PORT not set and no $HOST_ENV: set them or PF_PORT"
    for port in "$tx" "$rx"; do
        if port_busy "$port"; then
            die "port $port is in use, another MTL application is running"
        fi
    done
fi

echo "mtl-plugin tests: $TESTS"
echo "   results  $OUT_DIR"
[ -n "${MTL_ROOT:-}" ] && echo "   mtl      $MTL_ROOT ($(cat "$MTL_ROOT/VERSION" 2>/dev/null))"
[[ "$TESTS" != config ]] && echo "   ports    TX $tx, RX $rx"
echo

summary="$OUT_DIR/summary.txt"
aborted=""
ran=()
declare -A took
# count <PASS|FAIL|SKIP> [test]
count() {
    awk -v r="$1" -v t="${2:-}" '$1 == r && (t == "" || $2 == t) { n++ } END { print n + 0 }' "$summary"
}

# Ctrl+C: the running test cleans up and exits with 130, then stop here
trap 'aborted=1' INT
for t in $TESTS; do
    script="$TESTS_DIR/test_$t.sh"
    [ -x "$script" ] || die "no test $t ($script)"
    start=$SECONDS
    rc=0
    "$script" || rc=$?
    took[$t]=$((SECONDS - start))
    ran+=("$t")
    if [ -n "$aborted" ] || [ "$rc" -ge 128 ]; then
        aborted=1
        break
    fi
    if [ "$rc" = 125 ]; then
        printf '%-4s %-14s %-36s %s\n' FAIL "$t" not_started "the test could not start (missing setting or tool), see the console" >>"$summary"
        echo
    elif [ "$rc" != "$(count FAIL "$t")" ]; then
        # a test exits with its number of failed cases: anything else means it stopped early
        printf '%-4s %-14s %-36s %s\n' FAIL "$t" not_finished "exited with status $rc after $(count FAIL "$t") failed cases, see the console" >>"$summary"
        echo "   $t stopped early (exit status $rc)"
        echo
    fi
done
# exit status: every failed case, including tests that didn't start or finish
failed="$(count FAIL)"

total_time=0
echo "== summary"
printf '   %-12s %7s %7s %8s %7s\n' test passed failed skipped time
for t in "${ran[@]}"; do
    printf '   %-12s %7d %7d %8d %7s\n' "$t" "$(count PASS "$t")" "$(count FAIL "$t")" \
        "$(count SKIP "$t")" "$(duration "${took[$t]}")"
    total_time=$((total_time + took[$t]))
done
printf '   %-12s %7d %7d %8d %7s\n' total "$(count PASS)" "$(count FAIL)" "$(count SKIP)" \
    "$(duration "$total_time")"
echo
# perf results, from the pipeline cases
perf_lines="$(awk '$2 == "perf" && $3 ~ /^(pipeline|codec)/ { $1 = $2 = ""; sub(/^ +/, ""); print "   perf " $0 }' "$summary")"
if [ -n "$perf_lines" ]; then
    echo "$perf_lines"
    echo
fi
if [ -s "$OUT_DIR/perf/stages.txt" ]; then
    sed 's/^/   /' "$OUT_DIR/perf/stages.txt"
    echo
fi
for r in FAIL SKIP; do
    label=failed
    [ "$r" = SKIP ] && label=skipped
    if [ "$(count "$r")" = 0 ]; then
        echo "   $label cases: none"
    else
        echo "   $label cases:"
        awk -v r="$r" '$1 == r { printf "     %-12s %-36s", $2, $3; $1 = $2 = $3 = ""; sub(/^ +/, ""); print " " $0 }' "$summary"
    fi
done
echo "   details  $summary, logs per test in $OUT_DIR/<test>/"
echo
cases=$(($(count PASS) + $(count FAIL) + $(count SKIP)))
plural=s
[ "$cases" = 1 ] && plural=""
if [ -n "$aborted" ]; then
    echo "RESULT: ABORTED (interrupted in test ${ran[-1]}, $cases case$plural done)"
    exit 130
elif [ "$(count FAIL)" = 0 ]; then
    echo "RESULT: PASS ($cases case$plural)"
else
    echo "RESULT: FAIL ($(count FAIL) of $cases case$plural failed)"
fi
exit $((failed > 124 ? 124 : failed))
