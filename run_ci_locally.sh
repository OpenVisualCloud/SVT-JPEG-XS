#!/bin/bash
# Local runner for .github/workflows/base_build.yaml (Linux jobs only)
# Windows jobs are skipped.
#
# Usage: ./run_ci_locally.sh [OPTIONS]
#   --skip-deps          Skip apt-get dependency installation
#   --skip-build         Skip the build step (use existing Bin/)
#   --skip-unit-release  Skip Release unit tests
#   --skip-unit-debug    Skip Debug unit tests
#   --skip-valgrind      Skip Valgrind unit tests
#   --skip-conformance   Skip conformance tests
#   --jobs N             Parallel job count for tests (default: nproc)
#   --build-jobs N       Parallel job count for build (default: 10)
#   --input-files PATH   Path to sample files for conformance tests (default: /opt/samples)
#   --only-build         Run build step only (implies --skip-unit-* --skip-valgrind --skip-conformance)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Defaults
SKIP_DEPS=false
SKIP_BUILD=false
SKIP_UNIT_RELEASE=false
SKIP_UNIT_DEBUG=false
SKIP_VALGRIND=false
SKIP_CONFORMANCE=false
JOBS=$(nproc)
BUILD_JOBS=10
INPUT_FILES_PATH=/opt/samples

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-deps)         SKIP_DEPS=true ;;
        --skip-build)        SKIP_BUILD=true ;;
        --skip-unit-release) SKIP_UNIT_RELEASE=true ;;
        --skip-unit-debug)   SKIP_UNIT_DEBUG=true ;;
        --skip-valgrind)     SKIP_VALGRIND=true ;;
        --skip-conformance)  SKIP_CONFORMANCE=true ;;
        --only-build)
            SKIP_UNIT_RELEASE=true
            SKIP_UNIT_DEBUG=true
            SKIP_VALGRIND=true
            SKIP_CONFORMANCE=true
            ;;
        --jobs)        JOBS="$2";       shift ;;
        --build-jobs)  BUILD_JOBS="$2"; shift ;;
        --input-files) INPUT_FILES_PATH="$2"; shift ;;
        -h|--help)
            sed -n '3,14p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# ── Helpers ────────────────────────────────────────────────────────────────────
PASS=0; FAIL=0
step_results=()

run_step() {
    local name="$1"; shift
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  STEP: $name"
    echo "════════════════════════════════════════════════════════════"
    if "$@"; then
        echo "  RESULT: PASS — $name"
        PASS=$((PASS + 1))
        step_results+=("  [PASS] $name")
    else
        local rc=$?
        echo "  RESULT: FAIL (exit $rc) — $name"
        FAIL=$((FAIL + 1))
        step_results+=("  [FAIL] $name")
        return $rc
    fi
}

skip_step() {
    local name="$1"
    echo ""
    echo "  SKIP: $name"
    step_results+=("  [SKIP] $name")
}

# ── Job: Install dependencies ──────────────────────────────────────────────────
do_install_deps() {
    local missing=()
    command -v cmake    &>/dev/null || missing+=(cmake)
    command -v nasm     &>/dev/null || missing+=(nasm)
    command -v valgrind &>/dev/null || missing+=(valgrind)
    command -v make     &>/dev/null || missing+=(make)
    command -v gcc      &>/dev/null || missing+=(gcc)
    command -v g++      &>/dev/null || missing+=(g++)

    if [[ ${#missing[@]} -eq 0 ]]; then
        echo "  All dependencies already installed, skipping apt-get."
        return 0
    fi

    echo "  Missing: ${missing[*]}"
    sudo apt-get update
    sudo apt-get -y install "${missing[@]}"
}

# ── Job: linux-build ──────────────────────────────────────────────────────────
do_build() {
    cd "$REPO_ROOT/Build/linux"
    ./build.sh --jobs="$BUILD_JOBS" --all --test
}

# ── Job: linux-unit-tests-release ─────────────────────────────────────────────
do_unit_release() {
    cd "$REPO_ROOT/Bin/Release"
    chmod +x ./*
    chmod +x "$REPO_ROOT/tests/scripts/parrallelUT.sh"
    "$REPO_ROOT/tests/scripts/parrallelUT.sh" ./SvtJpegxsUnitTests "$JOBS"
}

# ── Job: linux-unit-tests-debug ───────────────────────────────────────────────
do_unit_debug() {
    cd "$REPO_ROOT/Bin/Debug"
    chmod +x ./*
    chmod +x "$REPO_ROOT/tests/scripts/parrallelUT.sh"
    "$REPO_ROOT/tests/scripts/parrallelUT.sh" ./SvtJpegxsUnitTests "$JOBS"
}

# ── Job: linux-unit-tests-valgrind ────────────────────────────────────────────
do_unit_valgrind() {
    cd "$REPO_ROOT/Bin/Release"
    chmod +x ./*
    chmod +x "$REPO_ROOT/tests/scripts/parrallelUT.sh"
    export LD_LIBRARY_PATH="$(pwd)"
    "$REPO_ROOT/tests/scripts/parrallelUT.sh" ./SvtJpegxsUnitTests "$JOBS" valgrind
}

# ── Job: linux-conformance-tests ──────────────────────────────────────────────
do_conformance() {
    if [[ ! -d "$INPUT_FILES_PATH" ]]; then
        echo "ERROR: Input samples directory not found: $INPUT_FILES_PATH"
        echo "       Pass --input-files <path> or --skip-conformance to skip."
        return 1
    fi
    cd "$REPO_ROOT/tests/scripts"
    chmod -R 755 "$REPO_ROOT/tests/scripts/"
    chmod -R 755 "$REPO_ROOT/Bin/Release/"
    ./ParallelAllTests.sh "$JOBS" "$INPUT_FILES_PATH" "$REPO_ROOT/Bin/Release/SvtJpegxsDecApp"
}

# ── Main ──────────────────────────────────────────────────────────────────────
echo ""
echo "Local CI Runner — SVT-JPEG-XS base_build.yaml (Linux)"
echo "  Repo:        $REPO_ROOT"
echo "  Build jobs:  $BUILD_JOBS"
echo "  Test jobs:   $JOBS"
echo "  Input files: $INPUT_FILES_PATH"
echo ""

if $SKIP_DEPS; then
    skip_step "Install dependencies"
else
    run_step "Install dependencies" do_install_deps
fi

if $SKIP_BUILD; then
    skip_step "linux-build"
else
    run_step "linux-build" do_build
fi

if $SKIP_UNIT_RELEASE; then
    skip_step "linux-unit-tests-release"
else
    run_step "linux-unit-tests-release" do_unit_release
fi

if $SKIP_UNIT_DEBUG; then
    skip_step "linux-unit-tests-debug"
else
    run_step "linux-unit-tests-debug" do_unit_debug
fi

if $SKIP_VALGRIND; then
    skip_step "linux-unit-tests-valgrind"
else
    run_step "linux-unit-tests-valgrind" do_unit_valgrind
fi

if $SKIP_CONFORMANCE || [[ ! -d "$INPUT_FILES_PATH" ]]; then
    [[ ! -d "$INPUT_FILES_PATH" ]] && echo "  NOTE: Skipping conformance tests (input directory not found: $INPUT_FILES_PATH)"
    skip_step "linux-conformance-tests"
else
    run_step "linux-conformance-tests" do_conformance
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════════"
echo "  SUMMARY"
echo "════════════════════════════════════════════════════════════"
for r in "${step_results[@]}"; do echo "$r"; done
echo ""
echo "  Passed: $PASS   Failed: $FAIL"
echo "════════════════════════════════════════════════════════════"

[[ $FAIL -eq 0 ]]
