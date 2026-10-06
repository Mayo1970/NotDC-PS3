#!/bin/sh
# Plan 5.36 PC check (WSL): the C side (sysgate_check) and the emitted words (gate_emit_check). Exit 1: a check
# failed, 2: no build. GATE_SRC: copies of the sources, one mutated (mutate.py). Args: seeds, slices, emit rounds.
cd "$(dirname "$0")/../.."
S="${GATE_SRC:-.}"
INC="-I$S -I. -Iplugins -Imisc/tr4_check/fake -Imisc/sysgate_check/fake -Iplugs/drkPvr -Idc/sh4 -Idc/aica"
SRC="$S/dc/sh4/tmu.cpp $S/dc/sh4/intc.cpp $S/plugs/drkPvr/SPG.cpp $S/plugs/drkPvr/regs.cpp"
SRC="$SRC $S/dc/sh4/sh4_interpreter.cpp $S/dc/aica/aica_if.cpp dc/sh4/sh4_sched.cpp"
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_LINUX -D_X64 $INC -w \
	-o /tmp/sysgate_check misc/sysgate_check/sysgate_check.cpp $SRC || exit 2
g++ -std=c++14 -O2 -Wall -Wno-unused-function -I$S -I. -Imisc/sysgate_check -o /tmp/gate_emit_check \
	misc/sysgate_check/gate_emit_check.cpp || exit 2
r=0
/tmp/sysgate_check "${1:-300}" "${2:-4000}" > /tmp/sysgate_check.out 2>&1 || r=1
grep -E "^(PASS|FAIL|  |ERROR|=+ERROR|SUMMARY)" /tmp/sysgate_check.out || true
/tmp/gate_emit_check "${3:-20000}" || r=1
exit $r
