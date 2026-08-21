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
#   --samples DIR   conformance input files          (default: $INPUT_FILES_PATH,
#                                                      else /opt/samples, else
#                                                      <repo>/Conformance-tests)
#   --no-build      reuse existing binaries, skip the build step
#   --help          print this help and exit
#
# Every sanitizer gets its own tailored tier of conformance tests instead of one
# generic scope - see the 'Tiered strategy' note below. All test execution is
# SEQUENTIAL: nothing here ever runs tests in parallel. address/thread/undefined/
# integer build Release; memory builds Debug (Release does not build with MSan)
# and asm is pinned to avx2 for memory (avoids avx512-specific false positives;
# not a noise-free guarantee - avx2/asm code can still have real findings).
# integer is report-only (findings do not gate). This script only exercises
# conformance tests - gtest unit tests are not run here (gtest aborts under MSan).
#
# The run is non-halting (halt_on_error=0 + -fsanitize-recover for asan/msan), so a
# single pass enumerates every finding. The final report gates the exit code.
#
# Tiered strategy (each tailored to what that sanitizer is actually looking for,
# every step below runs one after another - never in parallel):
#   address/undefined/integer/thread  broad code coverage across all three
#                       components, each run in full and sequentially:
#                       EncoderTest.sh, then DecoderConformanceTest.sh, then
#                       DecoderMultiFramesTest.sh. integer is the same suite,
#                       just report-only (findings are archived, not gated).
#                       For thread specifically: running EncoderTest.sh in full
#                       (not the 'fast' subset) naturally includes its existing
#                       --profile cpu-tagged call sites (test_rate_control,
#                       test_msb_aligned, etc.) in their own correct context -
#                       that's the only profile that spawns the multi-threaded
#                       DWT path TSan needs to see a race. No pre-existing
#                       dedicated "threading" mode/test list exists in the
#                       harness beyond that (checked EncoderTest.sh,
#                       DecoderConformanceTest.sh, DecoderMultiFramesTest.sh).
#   memory              EncoderTest.sh bounded to default_test_count cases, then
#                       DecoderConformanceTest.sh and DecoderMultiFramesTest.sh
#                       both run in 'fast' mode (reduced subset, e.g. AVX2-only) -
#                       encoder, decoder and multi-frame decoding are all
#                       memory-safety checked, still bounded given MSan's heavy
#                       per-test overhead.
#
# Locally validated: default_test_count sequential EncoderTest cases takes a few
# minutes with 0 unexpected findings. This scope was set after fixing
# RateControl_avx2.c's leftover-tail buffers, which used to read uninitialized
# stack bytes on ~every precinct/band and flood MSan with thousands of reports
# of the same finding, never finishing.

set -u

# ---- Resolve repo layout -----------------------------------------------------
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$script_dir/../.." && pwd)

usage() {
    cat <<'EOF'
Usage:
  tests/scripts/sanitize.sh <address|thread|undefined|integer|memory> [options]

Options:
  --samples DIR   conformance input files    (default: $INPUT_FILES_PATH,
                                              else /opt/samples, else
                                              <repo>/Conformance-tests)
  --no-build      reuse existing binaries, skip the build step
  --help          print this help and exit

Per-sanitizer behaviour is a tailored, sequential tier - no tests ever run in
parallel. See the top-of-file comment for the full rationale:
  address/undefined/integer/thread  EncoderTest.sh + DecoderConformanceTest.sh +
                     DecoderMultiFramesTest.sh, each run in full, sequentially.
                     integer is the same suite, report-only.
  memory              default_test_count encoder cases, then
                     DecoderConformanceTest.sh + DecoderMultiFramesTest.sh in
                     'fast' mode, sequential, asm pinned to avx2 (avoids
                     avx512-specific false positives; avx2/asm code can still
                     have real findings).
EOF
}

# ---- Parse arguments ---------------------------------------------------------
sanitizer="${1:-}"
if [ -z "$sanitizer" ] || [ "$sanitizer" = "--help" ] || [ "$sanitizer" = "-h" ]; then
    usage
    [ -z "$sanitizer" ] && exit 2 || exit 0
fi
shift

build_jobs=$(nproc 2>/dev/null || echo 4)  # compilation parallelism (no runtime overhead)
samples=""
do_build=1

while [ $# -gt 0 ]; do
    case "$1" in
        --samples)  samples="$2"; shift 2 ;;
        --no-build) do_build=0; shift ;;
        --help|-h)  usage; exit 0 ;;
        *) echo "sanitize.sh: unknown option '$1'" >&2; usage; exit 2 ;;
    esac
done

# ---- Per-sanitizer configuration ---------------------------------------------
build_type="Release"    # Bin/<build_type>
build_flag="release"    # build.sh argument
export_asm=""
gate=""                 # --no-gate for report-only sanitizers
opt_name=""             # <SAN>_OPTIONS env var
opt_value=""
# Locally validated tier sizing: default_test_count sequential EncoderTest cases,
# a few minutes each, 0 unexpected findings (see sanitizer-ci investigation notes).
# memory is the only sanitizer that still uses this bounded scope (see below).
default_test_count=50
# EncoderTest.sh's range:X-Y is X<=id<Y (exclusive upper bound), so 0-$default_test_count
# runs ids 0..default_test_count-1, i.e. exactly $default_test_count tests.
encoder_range="0-$default_test_count"
test_mode="broad_sequential"  # per-sanitizer tier; see the top-of-file comment.
                              # One of: broad_sequential (default) | memory_split

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
        build_type="Debug"; build_flag="debug"
        export_asm="avx2"     # avoids avx512-specific MSan false positives (not a noise-free
                               # guarantee - avx2/asm code can still have real findings)
        test_mode="memory_split"
        opt_name="MSAN_OPTIONS"; opt_value="halt_on_error=0:exitcode=0" ;;
    *)
        echo "sanitize.sh: unknown sanitizer '$sanitizer'" >&2
        echo "  expected: address | thread | undefined | integer | memory" >&2
        exit 2 ;;
esac

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
enc_log="$log_dir/$sanitizer-encoder.log"
dec_log="$log_dir/$sanitizer-decoder.log"
multi_log="$log_dir/$sanitizer-multiframe.log"

echo "=============================================================="
echo " Sanitizer : $sanitizer"
echo " Build     : $build_flag (Bin/$build_type)"
case "$test_mode" in
    memory_split)
        echo " Tests     : EncoderTest ids 0-$default_test_count, then DecoderConformanceTest + DecoderMultiFramesTest (fast), sequential" ;;
    *)
        echo " Tests     : EncoderTest + DecoderConformanceTest + DecoderMultiFramesTest, full, sequential" ;;
esac
echo " Samples   : $samples"
echo " Report    : ${gate:-gating}"
echo "=============================================================="

# ---- Build -------------------------------------------------------------------
if [ "$do_build" -eq 1 ]; then
    echo "== Build: $build_flag with $sanitizer sanitizer =="
    ( cd "$root/Build/linux" &&
      ./build.sh "$build_flag" sanitizer="$sanitizer" cc=clang cxx=clang++ jobs="$build_jobs" )
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

# ---- Conformance tests -------------------------------------------------------
echo "== Conformance tests: $sanitizer ($test_mode) =="
(
    cd "$root/tests/scripts" || exit 1
    chmod +x ./*.sh "$bin_dir"/* 2>/dev/null
    export LD_LIBRARY_PATH="$bin_dir"
    set -o pipefail
    case "$test_mode" in
        memory_split)
            ./EncoderTest.sh "$samples" "$dec_app" "range:$encoder_range" 2>&1 | tee "$enc_log"
            ./DecoderConformanceTest.sh "$samples" "$dec_app" fast 2>&1 | tee "$dec_log"
            ./DecoderMultiFramesTest.sh "$samples" "$dec_app" fast 2>&1 | tee "$multi_log" ;;
        *)
            # Full, sequential coverage of all three components - no test process
            # ever runs in parallel with another.
            ./EncoderTest.sh "$samples" "$dec_app" 2>&1 | tee "$enc_log"
            ./DecoderConformanceTest.sh "$samples" "$dec_app" 2>&1 | tee "$dec_log"
            ./DecoderMultiFramesTest.sh "$samples" "$dec_app" 2>&1 | tee "$multi_log" ;;
    esac
)
logs+=("$enc_log" "$dec_log" "$multi_log")

# ---- Report / gate -----------------------------------------------------------
echo "== Report: $sanitizer =="
case "$test_mode" in
    memory_split)
        coverage="EncoderTest ids 0-$default_test_count, DecoderConformanceTest + DecoderMultiFramesTest (fast), sequential" ;;
    *)
        coverage="EncoderTest + DecoderConformanceTest + DecoderMultiFramesTest, full, sequential" ;;
esac
"$root/tests/scripts/sanitizer_summary.sh" $gate --coverage "$coverage" "$sanitizer" "${logs[@]}"

