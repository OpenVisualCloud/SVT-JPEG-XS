#!/bin/bash
#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

set -e

function usage()
{
    echo "Usage: $0 [debug]"
    exit 0
}

buildtype=release
build_only=0

if [ -n "$1" ];  then
    case $1 in
      "debug")
           buildtype=debug
           ;;
      "plain")
           buildtype=plain
           ;;
      "build-only")
           build_only=1
           ;;
       *)
           usage
           ;;
    esac
fi

WORKSPACE=$PWD
PLUGINS_CPU_BUILD_DIR=${WORKSPACE}/build

# build plugins
meson "${PLUGINS_CPU_BUILD_DIR}" -Dbuildtype=$buildtype
pushd "${PLUGINS_CPU_BUILD_DIR}"
ninja
if [ $build_only -eq 0 ]; then
    sudo ninja install
fi
popd
