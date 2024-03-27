#!/bin/sh
#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

cmake -G "Eclipse CDT4 - Ninja" ../../ -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

