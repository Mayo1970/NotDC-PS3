#!/bin/sh
# Plan 5.18 (WSL): ps3_jit_hot.h on the PC. HOT_INC puts a mutated copy first (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${HOT_INC:+-I$HOT_INC} -I. -o /tmp/hotcheck misc/hot_check/hot_check.cpp
/tmp/hotcheck
