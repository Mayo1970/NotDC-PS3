#!/bin/sh
# P6 JIT EXIT (WSL): ps3_jit_exit.h against ppu-as words, then prologue + clobber + epilogue on a PPC64 model.
# EXIT_INC: an include dir with a mutated ps3/ps3_jit_exit.h (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${EXIT_INC:+-I$EXIT_INC} -I. -Imisc/exit_check -o /tmp/exitcheck \
	misc/exit_check/exit_check.cpp
/tmp/exitcheck "${1:-2000}"
