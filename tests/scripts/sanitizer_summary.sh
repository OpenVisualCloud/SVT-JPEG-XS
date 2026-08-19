#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Summarize sanitizer findings from one or more run logs into a Markdown report
# (written to $GITHUB_STEP_SUMMARY when set, otherwise stdout). Used by the
# sanitizer CI jobs, which run non-halting (halt_on_error=0) so a single pass
# collects every finding instead of stopping at the first one.
#
# Used as the job's final gate:
#   * any sanitizer finding         -> fail (unless --no-gate, e.g. integer)
#   * run did not complete cleanly  -> fail always (never a silent green)
#   * clean + complete              -> pass
#
# A clean/no-findings verdict does not by itself mean the whole surface was
# exercised (e.g. fast mode, unit tests skipped) - pass --coverage to say what
# actually ran, so the verdict can't be read as more thorough than it is.
#
# Usage: sanitizer_summary.sh [--no-gate] [--coverage <text>] <label> <log> [<log> ...]

set -u

gate=1
coverage=""
while true; do
    case "${1:-}" in
        --no-gate)   gate=0; shift ;;
        --coverage)  coverage="${2:?--coverage requires a value}"; shift 2 ;;
        *) break ;;
    esac
done

label="${1:?usage: sanitizer_summary.sh [--no-gate] [--coverage <text>] <label> <log> [<log> ...]}"
shift
requested=("$@")

out="${GITHUB_STEP_SUMMARY:-/dev/stdout}"

emit() { printf '%s\n' "$*" >> "$out"; }

emit "## Sanitizer findings: ${label}"
emit ""
if [ -n "$coverage" ]; then
    emit "_Coverage: ${coverage}_"
    emit ""
fi

# ---- Run completeness --------------------------------------------------------
# Every harness (ParallelAllTests.sh / ParallelScript.sh / parrallelUT.sh) prints
# "Exit <script> script with exit N" as its last line. A missing marker or a
# non-zero N means the suite crashed, samples were missing, or a step was skipped
# - none of which must pass silently just because no *sanitizer* line was logged.
run_incomplete=0
run_reason=""
existing=()
# ${requested[@]+"${requested[@]}"}: avoids an unbound-variable error under set -u
# on bash < 4.4 when requested is empty (plain "${requested[@]}" errors there).
for l in ${requested[@]+"${requested[@]}"}; do
    if [ ! -f "$l" ]; then
        run_incomplete=1
        run_reason="expected log '$(basename "$l")' was not produced (step failed or was skipped)"
        continue
    fi
    existing+=("$l")
    code=$(grep -oE "script with exit [0-9]+" "$l" | tail -1 | grep -oE "[0-9]+$")
    if [ -z "$code" ]; then
        run_incomplete=1
        run_reason="test run did not reach completion in '$(basename "$l")'"
    elif [ "$code" != "0" ]; then
        run_incomplete=1
        run_reason="test harness reported failures (exit $code) in '$(basename "$l")'"
    fi
done

# ---- Finding extraction ------------------------------------------------------
normalize() {
    sed -E '
        s#\(BuildId: [0-9a-f]+\)##g;
        s/\+0x[0-9a-fA-F]+//g;
        s/0x[0-9a-fA-F]+/0x_/g;
        s/pid=[0-9]+/pid=_/g;
        s/tid=[0-9]+/tid=_/g;
        s/ T[0-9]+/ T_/g;
        s/value [-0-9.eE+]+/value _/g;
        s#[^ (]*/(Source|tests|ffmpeg-plugin|imtl-plugin|gst-[^/]*)/#\1/#g;
        s/  +/ /g
    '
}

total=0
row_cap=100
section() {
    local heading="$1" body="$2" n
    [ -z "$body" ] && return 0
    n=$(printf '%s\n' "$body" | grep -c .)
    total=$((total + n))
    emit "### ${heading} (${n} distinct)"
    emit ""
    emit "| count | finding |"
    emit "|---:|---|"
    printf '%s\n' "$body" | sed -E 's/^ *([0-9]+) +(.*)$/| \1 | \2 |/' | head -$row_cap >> "$out"
    emit ""
    if [ "$n" -gt "$row_cap" ]; then
        emit "> Showing ${row_cap} of ${n} distinct findings - see the full log artifact for the rest."
        emit ""
    fi
}

if [ ${#existing[@]} -gt 0 ]; then
    # Match any 'file:line:col: runtime error:' report, not only ones under Source/,
    # so UB/Integer findings in tests/, plugins or headers are not silently dropped.
    ub=$(grep -hoE "[^ ]+:[0-9]+:[0-9]+: runtime error: .*" "${existing[@]}" 2>/dev/null \
            | normalize | sort | uniq -c | sort -rn)
    section "UndefinedBehavior / Integer" "$ub"

    ts=$(grep -h "SUMMARY: ThreadSanitizer:" "${existing[@]}" 2>/dev/null \
            | sed -E 's#\(/[^)]*\)##g' | normalize | sed -E 's/^.*SUMMARY: ThreadSanitizer: //' \
            | sort | uniq -c | sort -rn)
    section "ThreadSanitizer" "$ts"

    as=$(grep -h "SUMMARY: AddressSanitizer:" "${existing[@]}" 2>/dev/null \
            | normalize | sed -E 's/^.*SUMMARY: AddressSanitizer: //' \
            | sort | uniq -c | sort -rn)
    section "AddressSanitizer" "$as"

    ms=$(grep -h "SUMMARY: MemorySanitizer:" "${existing[@]}" 2>/dev/null \
            | normalize | sed -E 's/^.*SUMMARY: MemorySanitizer: //' \
            | sort | uniq -c | sort -rn)
    section "MemorySanitizer" "$ms"

    ls=$(grep -h "SUMMARY: LeakSanitizer:" "${existing[@]}" 2>/dev/null \
            | normalize | sed -E 's/^.*SUMMARY: LeakSanitizer: //' \
            | sort | uniq -c | sort -rn)
    section "LeakSanitizer" "$ls"
fi

# ---- Verdict -----------------------------------------------------------------
fail=0

if [ "$total" -gt 0 ]; then
    emit "> :warning: **${total} distinct issue(s) found by the '${label}' sanitizer.**"
    emit ">"
    emit "> Where to start: each row is a de-duplicated site (file:line for UBSan/Integer,"
    emit "> racing function for TSan, error kind for ASan/MSan). Full stack traces are in the"
    emit "> uploaded \`sanitizer-${label}-*\` artifact log."
    emit ""
    [ "$gate" -eq 1 ] && fail=1
fi

if [ "$run_incomplete" -eq 1 ]; then
    emit "> :x: **Run did not complete cleanly:** ${run_reason}."
    emit "> Failing the job so a broken run is not mistaken for a clean one."
    emit ""
    fail=1
fi

if [ "$total" -eq 0 ] && [ "$run_incomplete" -eq 0 ]; then
    emit "No sanitizer findings. :white_check_mark:"
fi

if [ "$fail" -eq 1 ]; then
    echo "sanitizer_summary: FAIL '${label}' (findings=${total} incomplete=${run_incomplete})" >&2
    exit 1
fi
echo "sanitizer_summary: PASS '${label}' (findings=${total}, report-only=$((1 - gate)))" >&2
exit 0
