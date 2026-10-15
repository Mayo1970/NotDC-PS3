#!/bin/sh
# Bundle 21, SPE AICA Defer, PC check (WSL): defer_check on ps3/ps3_aica_defer.h. DEFER_SRC: a tree of mutated files.
cd "$(dirname "$0")/../.." || exit 2
INC="${DEFER_SRC:+-I$DEFER_SRC} -I. -Iplugins -Imisc/tr4_check/fake"
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -D_LINUX -D_X64 $INC \
	-o /tmp/defer_check misc/defer_check/defer_check.cpp || exit 2
/tmp/defer_check ${SEEDS:-200}
