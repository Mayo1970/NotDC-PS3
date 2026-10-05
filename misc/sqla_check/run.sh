#!/bin/sh
# Plan 5.27 (WSL): ps3_jit_sqla.h against the SH4 opcode list. Args: seed (default 1), scan fuzz count (default
# 2000000). SQLA_HEADER: a mutated copy (mutate.py).
set -e
cd "$(dirname "$0")/../.."
python3 misc/sqla_check/gen.py /tmp/sqla_ref.bin
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake -w \
	${SQLA_HEADER:+-DSQLA_HEADER="\"$SQLA_HEADER\""} -o /tmp/sqlacheck misc/sqla_check/sqla_check.cpp
/tmp/sqlacheck "${1:-1}" "${2:-2000000}" /tmp/sqla_ref.bin
