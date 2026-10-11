#!/bin/sh
# Plan section 7, bundle 8 (WSL): cut.py, then jit8_check (the emit headers) and dec8_check (the decoder's rows).
# Args: cases (default 200000), "model" = no ppu-as words, "old" = report the old mac form. JIT8_SRC: mutated tree.
set -e
cd "$(dirname "$0")/../.."
python3 misc/jit8_check/cut.py
INC="${JIT8_SRC:+-I$JIT8_SRC} -I/tmp/jit8_ext -I. -Imisc/jit8_check"
g++ -std=c++14 -O2 -Wall -Wno-unused-function $INC -o /tmp/jit8_check misc/jit8_check/jit8_check.cpp
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function $INC -o /tmp/dec8_check \
	misc/jit8_check/dec8_check.cpp
/tmp/jit8_check "${1:-200000}" $2
/tmp/dec8_check
