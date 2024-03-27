#!/bin/sh
#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

dir=$(pwd)
rm -fr Debug
rm -fr coverage_tmp_dir
rm -fr coverage-report


#1.Set Clang compiler and CMake flags
export CC="clang"
export CXX="clang++"
export CFLAGS="-fprofile-instr-generate -fcoverage-mapping -fPIE"
export CXXFLAGS="-fprofile-instr-generate -fcoverage-mapping -fPIE"
#2. Build your application, for better accuracy Debug build is recommended 
mkdir Debug
cd Debug || return
if cmake ../../../ -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON;
then
    echo CMake OK
else
    echo CMake FAIL
    cd ..
    exit 1
fi

if make all;
then
    echo Build OK
else
    echo Build FAIL
    cd ..
    exit 1
fi

cd "$dir" || return

#directory with partial coverage data
mkdir coverage_tmp_dir

export LLVM_PROFILE_FILE=$dir/coverage_tmp_dir/coverage_file0.profraw;

if [ -f "test.sh" ]; then
    echo "Run local test..."
    ./test.sh
    echo "Run local test finish"
    cd "$dir" || return
fi

if ./../../Bin/Debug/SvtJpegxsUnitTests;
then
    echo UnitTests OK
else
    echo UnitTests FAIL
    exit 1
fi


#5. Merge partial coverage data into single file
if llvm-profdata-10 merge -sparse coverage_tmp_dir/coverage_file*.profraw -o coverage_tmp_dir/merged-coverage.profdata;
then
    echo Merge OK
else
    echo Merge FAIL
    exit 1
fi


#6. Export profiling data into lcov format (Your application is required to generate lcov file )
#llvm-cov-10 export ./../../Bin/Debug/SvtJpegxsUnitTests --instr-profile=merged-coverage.profdata --format=lcov  >lcov.data
if llvm-cov-10 export ./../../Bin/Debug/SvtJpegxsUnitTests --instr-profile=coverage_tmp_dir/merged-coverage.profdata --format=lcov  --ignore-filename-regex='.*/third_party.*|./*Build.*|./*tests/UnitTests.*' > coverage_tmp_dir/lcov.data;
then
    echo Export OK
else
    echo Export FAIL
    exit 1
fi


#7. Generate Code Coverage report (html format) in coverage-report directory
#sudo apt-get install lcov
if genhtml coverage_tmp_dir/lcov.data -o coverage-report;
then
    echo GenHTML OK
else
    echo GenHTML FAIL
    exit 1
fi

echo open coverage-report/index.html

echo Coverage DONE OK
exit 0
