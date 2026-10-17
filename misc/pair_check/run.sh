#!/bin/sh
# P105 JIT PAIR CALLS, PC check (WSL): FlushCold's 64-bit C calls (ps3/ps3_driver.cpp) and ps3/ps3_jit_pair.h on a
# PPC64 model, into dc/mem/_vmem.cpp's handler path. Args: cases. PAIR_SRC: a mutated tree (mutate.py). Exit 2: no build.
cd "${PAIR_SRC:-$(dirname "$0")/../..}" || exit 2
T=${PAIR_TMP:-/tmp/pair_check}
mkdir -p "$T" || exit 2
D=ps3/ps3_driver.cpp
V=dc/mem/_vmem.cpp
# The lines from the first that holds A to the one before the next line that holds B.
cut_lines() {
	tr -d '\r' < "$1" | awk -v a="$2" -v b="$3" 'p && index($0, b) { exit } index($0, a) { p = 1 } p' > "$4"
	[ -s "$4" ] || { echo "FAIL: $1 has no lines from: $2"; exit 2; }
}
cut_lines $D 'static void ppc_clrldi32(u32 A,u32 S)' '// ppc_li:' "$T/clrldi.inc"
cut_lines $D 'else if (c.kind==1)              // --- read pair (64): ReadMem64' 'else if (c.kind==2)' "$T/read.inc"
cut_lines $D 'else                             // --- write pair (64): WriteMem64 ---' 'if (c.taslot)' "$T/write.inc"
cut_lines $V '// Core read dispatcher (templated on access size).' '// Resolves [Address, Address+size)' "$T/vmem.inc"
g++ -std=c++14 -O2 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all -I. -I"$T" \
	-o "$T/pcheck" misc/pair_check/pair_check.cpp || exit 2
r=0
"$T/pcheck" "${1:-20000}" || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
# The count of lines that hold A and whose next line holds B.
pair() {
	n=$(tr -d '\r' < "$1" | awk -v a="$3" -v b="$4" 'p && index($0, b) { n++ } { p = index($0, a) } END { print n + 0 }')
	if [ "$n" != "$2" ]; then
		echo "FAIL: $1 does not have $2 time(s) the line / the next line: $3 / $4"
		r=1
	fi
}
# The cut branches are FlushCold's, and its C calls are the only live 64-bit ones (the DSI forms never engage).
has $D 1 'else if (c.kind==1)              // --- read pair (64): ReadMem64'
has $D 1 'else                             // --- write pair (64): WriteMem64 ---'
has $D 1 'void* f=c.fuct? c.fuct : (void*)ReadMem64;'
has $D 1 'ppc_call_c(&WriteMem64);'
has $D 2 'fm_branch((void*)ReadMem64,1)'
has $D 2 'fm_branch((void*)WriteMem64,1)'
pair $D 1 '// PS3: no resumable DSI handler, so fastmem can never engage' 'return false;'
# The RAM paths' word order, which the model holds the MMIO path to.
has $D 1 'ppc_stwx(ppc_rarg2,ppc_rarg1,ppc_rarg0);	// word[addr]   = high'
has $D 1 'ppc_stwx(ppc_rarg3,ppc_rarg1,ppc_rarg0);	// word[addr+4] = low'
has $D 1 'ppc_stfsx(fs0,ppc_rarg1,ppc_rarg0);	// word[addr]   = fs0'
has $D 1 'ppc_lwzx(ppc_rarg3,ppc_rarg2,ppc_rarg0);	// word[addr]'
has $D 1 'ppc_ori(ppc_rrv0,ppc_rarg3,0);			// r3 = word[addr]'
has $D 1 'ppc_lfsx(fd0,ppc_rarg2,ppc_rarg0);	// word[addr]   -> fd0'
# The row: its latch, FIXED by default (Matt), boot only.
has $D 1 's_jo.pair=get_jit_pair_calls_preset()!=0;'
has $D 1 '#include "ps3/ps3_jit_pair.h"'
P=ps3/ps3_presets.cpp
has $P 1 'int g_jit_pair_calls_preset = 1;'
has $P 1 '{ "JitPairCalls",  &g_jit_pair_calls_preset },'
has $P 1 'int get_jit_pair_calls_preset() { return g_jit_pair_calls_preset; }'
M=ps3/ps3_menu.cpp
has $M 1 '{ "JIT PAIR CALLS",  &g_jit_pair_calls_preset, OPT_CHOICE, k_div32,'
has $M 1 '&g_jit_pair_calls_preset'
[ $r = 0 ] && echo "PASS: 20 source line checks"
exit $r
