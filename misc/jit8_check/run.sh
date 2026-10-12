#!/bin/sh
# Plan section 7, bundle 8 (WSL): cut.py, then jit8_check (emit headers), dec8_check (decoder rows) and hash8_check
# (JIT IDLE HASH). Args: cases (default 200000), "model" = no ppu-as words, "old" = old mac form. JIT8_SRC: mutated tree.
set -e
cd "$(dirname "$0")/../.."
python3 misc/jit8_check/cut.py
INC="${JIT8_SRC:+-I$JIT8_SRC} -I/tmp/jit8_ext -I. -Imisc/jit8_check"
g++ -std=c++14 -O2 -Wall -Wno-unused-function $INC -o /tmp/jit8_check misc/jit8_check/jit8_check.cpp
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function $INC -o /tmp/dec8_check \
	misc/jit8_check/dec8_check.cpp
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function $INC -o /tmp/hash8_check \
	misc/jit8_check/hash8_check.cpp
/tmp/jit8_check "${1:-200000}" $2
/tmp/dec8_check
/tmp/hash8_check
