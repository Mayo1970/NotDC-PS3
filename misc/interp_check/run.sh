#!/bin/sh
# Plan section 7, bundle 20 (P46), PC check (WSL): the interpreter's op bodies against Flycast's SH4 op tests and
# bundle 20's cases, every row combination, then the source lines the rows need. IC_OVL: planted copies (mutate.py).
cd "$(dirname "$0")/../.." || exit 2
T=${IC_TMP:-/tmp/interp_check}
O=${IC_OVL:-}
mkdir -p "$T" || exit 2
python3 misc/interp_check/gen.py . "$T" || exit 2
# A planted copy in IC_OVL goes before the tree, as a header and as a source.
src() { if [ -n "$O" ] && [ -f "$O/$1" ]; then echo "$O/$1"; else echo "$1"; fi; }
SRCS="misc/interp_check/interp_check.cpp misc/interp_check/ic_cpu.cpp misc/interp_check/ic_fpu.cpp"
SRCS="$SRCS $(src dc/sh4/sh4_registers.cpp) $(src dc/sh4/sh4_selftest.cpp)"
CXX="g++ -std=c++14 -O2 -w -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all"
CXX="$CXX -Imisc/interp_check/fake ${O:+-I$O} -I. -Idc/sh4 -I$T"
# The OLD addv, subv and mul.l overflow an int: that build leaves the overflow check out; the FIXED-only one has it.
$CXX -fno-sanitize=signed-integer-overflow -o "$T/ic" $SRCS || exit 2
$CXX -o "$T/ic_ub" $SRCS || exit 2
r=0
"$T/ic" misc/fsca-table.bin || r=1
"$T/ic_ub" misc/fsca-table.bin fixed > "$T/ub.txt" 2>&1 || { echo "FAIL: the FIXED rows with every UBSan check"; tail -3 "$T/ub.txt"; r=1; }
tail -1 "$T/ub.txt"
[ -n "$O" ] && exit $r
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
P=ps3/ps3_presets.cpp
has $P 1 'int g_sh4_optest_preset    = 1;'
has $P 1 'int g_interp_fix_preset    = 1;'
has $P 1 'int g_fpscr_cause_preset   = 1;'
has $P 2 '{ "Sh4OpTests",    &g_sh4_optest_preset },'
has $P 2 '{ "InterpFix",     &g_interp_fix_preset },'
has $P 2 '{ "FpscrCause",    &g_fpscr_cause_preset },'
U=ps3/ps3_menu.cpp
has $U 1 '{ "SH4 Op Tests",'
has $U 1 '{ "Interp Fixes",'
has $U 1 '{ "FPSCR Cause",'
# Boot rows: the row only, no line in the live list.
has $U 1 '&g_sh4_optest_preset'
has $U 1 '&g_interp_fix_preset'
has $U 1 '&g_fpscr_cause_preset'
has dc/dc.cpp 1 '	if (g_sh4_optest_preset)'
has dc/dc.cpp 1 '		sh4_RunSelfTests();'
has Makefile 1 '$(BUILD)/dc/sh4/sh4_selftest.o: CXXFLAGS += -mminimal-toc'
has dc/sh4/rec_v2/decoder.cpp 1 '	return (lo>=6 && lo<=0xC) || (op&0xEF)==0x0D || (g_interp_fix_preset && (op&0xFF)==0x4D);'
[ $r = 0 ] && echo "PASS: 16 source line checks"
exit $r
