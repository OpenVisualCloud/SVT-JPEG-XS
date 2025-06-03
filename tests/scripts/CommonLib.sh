#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
#param 'help' print script params


function print_help() {
    echo "Help for scripts:"
    echo "    Example: ./$0 ../../../Conformance-tests/TEST_ED2/ ../../Bin/RelWithDebInfo/SvtJpegxsDecApp.exe [valgrind] [range:0-100] [help] [...]"
    echo '    1: path to test resources with dir encoder_tests'
    echo '    2: path to decoder bin SvtJpegxsDecApp and expected that will exist SvtJpegxsEncApp in this same directory'
    echo '    "num": return number of test to execute Get return by: source ./script && echo ${SCRIPT_TESTS_NUMBER}'
    echo '    "range:X": Run only test with id X'
    echo '    "range:X-Y": Run only test with id: X<=id && id<Y'
    echo '    "valgrind": Run tests with valgrind'
    echo '    "dec": then local run with decode yuv and left results in tmp'
    echo '    "tmp:temp_dir_name": Set custom temp_dir_name'
    echo '    "fast": Run reduced number of tests, example only AVX2'
    echo '    "help": Print help and exit'
    echo
}

#Global variables
path_global=$1
exec_dec=$2
valgrind=""
decode_flag=0
range_min=0
range_max=9999999
tmp_dir="tmp"
run_fast=0

#Common lib variables
help_flag=0
test_id=0
tests_execute=0
tests_ignore=0
test_id_run=0

for ARG in "$@"; do
    #echo $ARG
    if [ "$ARG" = "valgrind" ]; then
        valgrind="valgrind --leak-check=full --error-exitcode=255 "
    fi
    if [ "$ARG" = "dec" ]; then
        decode_flag=1
    fi
    if [ "$ARG" = "help" ]; then
        help_flag=1
    fi
    if [ "$ARG" = "fast" ]; then
        run_fast=1
    fi
    if [[ $ARG = range:* ]]; then
        rangeall=$ARG
        rangeall=${rangeall#"range:"}
        echo Range param: $rangeall
        if [[ $ARG = *-* ]]; then
            min_max=(${rangeall//-/ })
            range_min=${min_max[0]}
            range_max=${min_max[1]}
        else
            range_min=$rangeall
            range_max=$((range_min+1))
        fi
    fi
    if [ "$ARG" = "num" ]; then
        echo "Test only number of tests! But nothing execute!"
        range_min=0
        range_max=0
    fi
    if [[ $ARG = tmp:* ]]; then
        tmp_dir=$ARG
        tmp_dir=${tmp_dir#"tmp:"}
        echo tmp_dir: $tmp_dir

    fi
done

if [[ $valgrind == "" ]]; then
    echo "Valgrind: OFF"
else
    echo "Valgrind: ON"
fi

echo "Range:           $range_min-$range_max"

if (($run_fast != 0)); then
    echo "!!RUN REDUCED NUMBER OF TESTS!!!, Example only AVX2"
fi

if (($help_flag != 0)); then
    print_help
     if ((!($range_min == 0 && $range_min == $range_max))); then
        #No exit when use source to get variable
        exit 1
    fi
fi

function common_lib_update_test_id_run_return_1_to_ignore() {
    test_id_run=$test_id
    test_id=$((test_id+1))

    if (($test_id_run < $range_min || $test_id_run >= $range_max)); then
        # Ignore test
        tests_ignore=$((tests_ignore+1))
        return 1
    fi
    tests_execute=$((tests_execute+1))

    test_id_print=$(printf "%04d" $test_id_run)
    echo "################################ $test_id_print ################################"
    return 0
}

function common_lib_end_summary() {
    echo "Tests execute: $tests_execute Tests Ignore: $tests_ignore Sum tests: $test_id"
    if (($range_min == 0 && $range_min == $range_max)); then
        unset SCRIPT_TESTS_NUMBER
        export SCRIPT_TESTS_NUMBER=$test_id
        echo "Num of tests to execute: SCRIPT_TESTS_NUMBER=$SCRIPT_TESTS_NUMBER range: (0-$test_id) Get variable bye: "'source ./script && echo ${SCRIPT_TESTS_NUMBER}'
    fi
}