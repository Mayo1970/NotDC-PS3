#!/bin/sh
# Plan 5.25 (WSL): idle_loop.h against the SHIL interpreter, ps3_jit_idle.h against ppu-as and a PPC64 model.
# Args: seed (default 1), fuzz blocks (default 200000). IDLE_HEADER / IDLE_INC: mutated copies (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake -w \
	${IDLE_HEADER:+-DIDLE_HEADER="\"$IDLE_HEADER\""} -o /tmp/idlecheck misc/idle_check/idle_check.cpp
/tmp/idlecheck "${1:-1}" "${2:-200000}"
g++ -O2 -Wall -Wno-unused-function ${IDLE_INC:+-I$IDLE_INC} -I. -Imisc/idle_check -o /tmp/idleemit \
	misc/idle_check/idle_emit_check.cpp
/tmp/idleemit
