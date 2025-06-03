#!/bin/bash
#
# Copyright(c) 2025 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#


function check_vcpus(){
    echo $(lscpu | awk '/^CPU\(s\):/{print $NF}')
}



function run_unit_tests() {
    local UNIT_TESTS_BINARY_DIR=$4
    local CONFORMANCE_TEST_SCRIPT=$5
    local ENABLE_VALGRIND=$6
    local PARAM=""
    if [ "${ENABLE_VALGRIND}" == "true" ]; then PARAM="valgrind"; fi

    TEST_BINARY=$(find ${UNIT_TESTS_BINARY_DIR} -name "*UnitTests" )
    echo "Run UT: ${TEST_BINARY}"
    cd ${CONFORMANCE_TEST_SCRIPT}
    ./ParallelUT.sh ${TEST_BINARY} $NUM_JOBS $PARAM
    ret=$?
    if [[ $ret -ne 0 ]]; then
        echo "******************* Validation fail *******************"
        exit $ret
    fi
}

function run_confornance_tests() {
    local BINARY_DIR=$1
    local ENCODED_FILES_DIR=$2
    local CONFORMANCE_TEST_SCRIPT=$3
    local ENABLE_VALGRIND=$4

    if [ ! -d "${ENCODED_FILES_DIR}" ]
    then
        echo "${ENCODED_FILES_DIR} not exist, aborting"
        exit 1
    fi

    local PARAM=""
    if [ "${ENABLE_VALGRIND}" == "true" ]; then PARAM="valgrind"; fi

    DEC_BIN_DIR="$(find ${BINARY_DIR} -name "*DecApp")"
    cd ${CONFORMANCE_TEST_SCRIPT}
    ./ParallelAllTests.sh $NUM_JOBS ${ENCODED_FILES_DIR} ${DEC_BIN_DIR} $PARAM
    ret=$?
    echo "Detect error: $ret"
    if [[ $ret -ne 0 ]]; then
        echo "******************* Validation fail *******************"
        exit $ret
    fi
}

function build_project_binaries() 
{
echo "*******************building project ${OS}*******************"
cd ${ROOT_DIRECTORY}/Build/${OS}
#Build Release and Debug
./build.sh   --all --test
ret=$?
if [[ $ret -ne 0 ]]; then
    echo "******************* Validation fail *******************"
    exit $ret
fi
cd ${ROOT_DIRECTORY}/tests
}

INPUT_FILES_PATH="${INPUT_FILES_PATH:-"$(pwd)/images"}"
ROOT_DIRECTORY="${ROOT_DIRECTORY:-"$(dirname $(pwd))"}"
OS="linux"
RUN_UNIT_TESTS="${RUN_UNIT_TESTS:-true}"
RUN_CONFORMANCE_TESTS="${RUN_CONFORMANCE_TESTS:-true}"
BUILD_CONFIGS=("Debug" "Release")
VALGRIND_PARAMS=("false" "true")
NUM_JOBS=$(check_vcpus)

if [ ! -d ${INPUT_FILES_PATH} ]; then
    echo " "
    echo "******************* no INPUT_FILES_PATH provided exiting. *******************"
    exit 1
fi

# Allow sourcing of the script.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]
then
    build_project_binaries

    for VALGRIND_PARAM in ${VALGRIND_PARAMS[@]}
    do 
        echo "******************* Valgrind enabled ${VALGRIND_PARAM} *******************"
        LOG_FILE_SUFFIX=""
        if [ "${VALGRIND_PARAM}" == "true" ]; then LOG_FILE_SUFFIX="Valgrind";  fi

        for BUILD_TYPE in ${BUILD_CONFIGS[@]}
        do      
            BINARIES_DIR="${ROOT_DIRECTORY}/Bin/${BUILD_TYPE}"
            BUILD_SCRIPT_DIR="${ROOT_DIRECTORY}/Build/${OS}"
            if [ "${RUN_UNIT_TESTS}" == "true" ]
            then
                # params cannot be empty, otherwise other params are wrogly parsed
                if [ -z "${UNIT_TESTS_BUILD_PARAMS}" ]; then UNIT_TESTS_BUILD_PARAMS=${BUILD_TYPE}; fi
                echo "******************* Running unit tests ${OS} ${BUILD_TYPE} *******************"
                run_unit_tests   ${BUILD_SCRIPT_DIR}\
                                 ${UNIT_TESTS_BUILD_PARAMS}\
                                 ${BUILD_TYPE}\
                                 ${BINARIES_DIR}\
                                 ${ROOT_DIRECTORY}/tests/FunctionalTests \
                                 ${VALGRIND_PARAM}

                cd ${ROOT_DIRECTORY}/tests
            fi
            if [ "${RUN_CONFORMANCE_TESTS}" == "true" ]
            then
                echo "******************* Running conformance tests ${OS} ${BUILD_TYPE} *******************"       
                run_confornance_tests ${BINARIES_DIR}\
                                      ${INPUT_FILES_PATH}\
                                      ${ROOT_DIRECTORY}/tests/FunctionalTests \
                                      ${VALGRIND_PARAM}
                cd ${ROOT_DIRECTORY}/tests
            fi
        done
    done
    echo " "
    echo "******************* Validation done *******************"
fi
