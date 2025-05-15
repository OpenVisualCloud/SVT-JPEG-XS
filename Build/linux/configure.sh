#!/bin/bash
#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#


 
if [ "$EUID" -eq 0 ]; then
   echo "*************************  setting up linux enviroment...  *************************"
  lscpu
  apt-get update
  apt-get -y install nasm
  echo "*************************  linux enviroment setup complete  *************************"

else
  echo "Please run as root"
  exit 1
fi
