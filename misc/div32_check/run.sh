#!/bin/sh
# Plan 5.38 (WSL): div32_match.h + ps3_jit_div32.h against the SH4 division chain, ppu-as words and the real
# shil_canonical.h ops. Args: cases per row (default 200000), "model" skips the words. DIV32_INC: a mutant's dir.
set -e
cd "$(dirname "$0")/../.."
INC="${DIV32_INC:+-I$DIV32_INC} -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake -Imisc/div32_check"
g++ -O2 -w $INC -c -o /tmp/div32canon.o misc/div32_check/canon.cpp
g++ -O2 -Wall -Wno-unused-function $INC -c -o /tmp/div32check.o misc/div32_check/div32_check.cpp
g++ -o /tmp/div32check /tmp/div32check.o /tmp/div32canon.o
/tmp/div32check "${1:-200000}" $2
