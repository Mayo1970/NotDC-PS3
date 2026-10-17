#!/bin/sh
# P87 JIT TA SLOT (WSL): ps3_jit_taslot.h's plan against the SHIL interpreter with a model of the slot and the flush,
# its emitters on a PPC64 model. Args: seed (default 1), fuzz blocks (default 200000). TS_HEADER: a mutated copy.
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake \
	-Imisc/taslot_check -w ${TS_HEADER:+-DTS_HEADER="\"$TS_HEADER\""} -o /tmp/taslotcheck \
	misc/taslot_check/taslot_check.cpp
/tmp/taslotcheck "${1:-1}" "${2:-200000}"
