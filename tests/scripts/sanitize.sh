#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# One entry point to build the library with a sanitizer, run the tests, and
# print a findings report. Used by the Sanitizer CI workflow and by developers
# who want to reproduce a finding locally with a single command.
#
# Usage:
#   tests/scripts/sanitize.sh <address|thread|undefined|integer|memory> [options]
#
# Options:
#   --jobs N        parallel build/test jobs         (default: nproc)
#   --samples DIR   conformance input files          (default: $INPUT_FILES_PATH,
#                                                      else /opt/samples, else
#                                                      <repo>/Conformance-tests)
#   --fast          run the reduced conformance subset (implied for memory)
#   --no-build      reuse existing binaries, skip the build step
#   --help          print this help and exit
#
# Per-sanitizer behaviour (kept identical to the CI matrix):
#   address/thread/undefined/integer  Release build, unit tests + full conformance
#   integer                           report-only (findings do not gate)
#   memory                            Debug build (Release does not build with MSan),
#                                     conformance only (gtest aborts under MSan),
#                                     fast subset, asm pinned to avx2 (AVX-512 => FPs)
#
# The run is non-halting (halt_on_error=0 + -fsanitize-recover for asan/msan), so a
# single pass enumerates every finding. The final report gates the exit code.

set -u

# ---- Resolve repo layout -----------------------------------------------------
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$script_dir/../.." && pwd)

usage() {
    cat <<'EOF'
Usage:
  tests/scripts/sanitize.sh <address|thread|undefined|integer|memory> [options]

Options:
  --jobs N        parallel build/test jobs   (default: nproc)
  --samples DIR   conformance input files    (default: $INPUT_FILES_PATH,
                                              else /opt/samples, else
                                              <repo>/Conformance-tests)
  --fast          run the reduced conformance subset (implied for memory)
  --no-build      reuse existing binaries, skip the build step
  --help          print this help and exit

Per-sanitizer behaviour (identical to the CI matrix):
  address/thread/undefined/integer  Release build, unit tests + full conformance
  integer                           report-only (findings do not gate)
  memory                            Debug build, conformance only, fast subset,
                                    asm pinned to avx2 (AVX-512 => MSan FPs)
EOF
}

# ---- Parse arguments ---------------------------------------------------------
sanitizer="${1:-}"
if [ -z "$sanitizer" ] || [ "$sanitizer" = "--help" ] || [ "$sanitizer" = "-h" ]; then
    usage
    [ -z "$sanitizer" ] && exit 2 || exit 0
fi
shift

jobs=$(nproc 2>/dev/null || echo 4)
samples=""
force_fast=0
do_build=1

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs)     jobs="$2"; shift 2 ;;
        --samples)  samples="$2"; shift 2 ;;
        --fast)     force_fast=1; shift ;;
        --no-build) do_build=0; shift ;;
        --help|-h)  usage; exit 0 ;;
        *) echo "sanitize.sh: unknown option '$1'" >&2; usage; exit 2 ;;
    esac
done

# ---- Per-sanitizer configuration ---------------------------------------------
build_type="Release"    # Bin/<build_type>
build_flag="release"    # build.sh argument
build_test="test"       # build unit tests too
run_unit=1
fast=""
export_asm=""
gate=""                 # --no-gate for report-only sanitizers
opt_name=""             # <SAN>_OPTIONS env var
opt_value=""

ubsan_opts="suppressions=$root/.github/config/ubsan_suppressions.txt:print_stacktrace=1:halt_on_error=0:exitcode=0"

case "$sanitizer" in
    address)
        opt_name="ASAN_OPTIONS"; opt_value="halt_on_error=0:exitcode=0:print_stacktrace=1" ;;
    thread)
        opt_name="TSAN_OPTIONS"; opt_value="halt_on_error=0:exitcode=0:history_size=4" ;;
    undefined)
        opt_name="UBSAN_OPTIONS"; opt_value="$ubsan_opts" ;;
    integer)
        opt_name="UBSAN_OPTIONS"; opt_value="$ubsan_opts"
        gate="--no-gate" ;;   # report-only: findings archived, do not fail the job
    memory)
        build_type="Debug"; build_flag="debug"; build_test=""
        run_unit=0            # gtest aborts during global init under MSan
        fast="fast"           # MSan is very slow; reduce the matrix
        export_asm="avx2"     # AVX-512 paths produce MSan false positives
        opt_name="MSAN_OPTIONS"; opt_value="halt_on_error=0:exitcode=0" ;;
    *)
        echo "sanitize.sh: unknown sanitizer '$sanitizer'" >&2
        echo "  expected: address | thread | undefined | integer | memory" >&2
        exit 2 ;;
esac

[ "$force_fast" -eq 1 ] && fast="fast"

# ---- Resolve conformance samples ---------------------------------------------
if [ -z "$samples" ]; then
    if [ -n "${INPUT_FILES_PATH:-}" ]; then
        samples="$INPUT_FILES_PATH"
    elif [ -d /opt/samples ]; then
        samples="/opt/samples"
    else
        samples="$root/Conformance-tests"
    fi
fi

bin_dir="$root/Bin/$build_type"
dec_app="$bin_dir/SvtJpegxsDecApp"
log_dir="$root/sanitizer-logs"
mkdir -p "$log_dir"
unit_log="$log_dir/$sanitizer-unit.log"
conf_log="$log_dir/$sanitizer-conformance.log"

echo "=============================================================="
echo " Sanitizer : $sanitizer"
echo " Build     : $build_flag (Bin/$build_type)${build_test:+ +unit-tests}"
echo " Tests     : $([ "$run_unit" -eq 1 ] && printf 'unit ')conformance${fast:+ ($fast)}"
echo " Samples   : $samples"
echo " Jobs      : $jobs"
echo " Report    : ${gate:-gating}"
echo "=============================================================="

# ---- Build -------------------------------------------------------------------
if [ "$do_build" -eq 1 ]; then
    echo "== Build: $build_flag with $sanitizer sanitizer =="
    ( cd "$root/Build/linux" &&
      ./build.sh "$build_flag" $build_test sanitizer="$sanitizer" cc=clang cxx=clang++ jobs="$jobs" )
    build_rc=$?
    if [ "$build_rc" -ne 0 ]; then
        echo "sanitize.sh: build failed (rc=$build_rc)" >&2
        exit "$build_rc"
    fi
fi

if [ ! -x "$dec_app" ]; then
    echo "sanitize.sh: decoder app not found at $dec_app (build first, or drop --no-build)" >&2
    exit 1
fi

# The <SAN>_OPTIONS are consumed by the instrumented processes below.
export "$opt_name=$opt_value"
[ -n "$export_asm" ] && export SANITIZER_ASM="$export_asm"

logs=()

# ---- Unit tests --------------------------------------------------------------
if [ "$run_unit" -eq 1 ]; then
    echo "== Unit tests: $sanitizer =="
    (
        cd "$bin_dir" || exit 1
        chmod +x ./* "$root/tests/scripts/parrallelUT.sh" 2>/dev/null
        export LD_LIBRARY_PATH="$bin_dir"
        set -o pipefail
        "$root/tests/scripts/parrallelUT.sh" ./SvtJpegxsUnitTests "$jobs" 2>&1 | tee "$unit_log"
    )
    logs+=("$unit_log")
fi

# ---- Conformance tests -------------------------------------------------------
echo "== Conformance tests: $sanitizer =="
(
    cd "$root/tests/scripts" || exit 1
    chmod +x ./*.sh "$bin_dir"/* 2>/dev/null
    export LD_LIBRARY_PATH="$bin_dir"
    set -o pipefail
    ./ParallelAllTests.sh "$jobs" "$samples" "$dec_app" $fast 2>&1 | tee "$conf_log"
)
logs+=("$conf_log")

# ---- Report / gate -----------------------------------------------------------
echo "== Report: $sanitizer =="
"$root/tests/scripts/sanitizer_summary.sh" $gate "$sanitizer" "${logs[@]}"
