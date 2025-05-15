#!/bin/sh
#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#


  echo "*************************  setting up linux enviroment...  *************************"
  if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
  fi

  lscpu
  apt-get update
  apt-get -y install nasm
  echo "*************************  linux enviroment setup complete  *************************"



