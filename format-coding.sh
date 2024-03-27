#!/bin/bash

#Sets the script directory to the current working directory
cd "${0%/*}"
#Set default basch configuration
set -e

find Source -regex '.*\.\(cc\|cpp\|hpp\|cu\|c\|h\)' -exec clang-format -i {} \;
find tests -regex '.*\.\(cc\|cpp\|hpp\|cu\|c\|h\)' -exec clang-format -i {} \;
