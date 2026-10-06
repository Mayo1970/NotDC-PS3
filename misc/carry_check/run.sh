#!/bin/sh
# Plan 5.2 (WSL): ps3_jit_carry.h against ppu-as words, then every form on a PPC64 model. Args: cases (default
# 200000), "model" skips the words, "old" also reports the pre-5.2 forms. CARRY_INC: dir of a mutated header.
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${CARRY_INC:+-I$CARRY_INC} -I. -Imisc/carry_check -o /tmp/carrycheck \
	misc/carry_check/carry_check.cpp
/tmp/carrycheck "${1:-200000}" $2
