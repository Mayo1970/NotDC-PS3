#!/bin/sh
# Plan 5.26 (WSL): ps3_jit_tcopy.h against the SHIL interpreter. Args: seed (default 1), fuzz blocks (default
# 200000). TCOPY_HEADER: a mutated copy (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake -w \
	${TCOPY_HEADER:+-DTCOPY_HEADER="\"$TCOPY_HEADER\""} -o /tmp/tcopycheck misc/tcopy_check/tcopy_check.cpp
/tmp/tcopycheck "${1:-1}" "${2:-200000}"
