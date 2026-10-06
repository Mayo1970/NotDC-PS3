#!/bin/sh
# Plan 5.31 (WSL): ps3_jit_greuse.h against the SHIL interpreter and ppu-as. Args: seed (default 1), fuzz blocks
# (default 200000). GREUSE_HEADER: a mutated copy (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake \
	-Imisc/greuse_check -w ${GREUSE_HEADER:+-DGREUSE_HEADER="\"$GREUSE_HEADER\""} -o /tmp/greusecheck \
	misc/greuse_check/greuse_check.cpp
/tmp/greusecheck "${1:-1}" "${2:-200000}"
