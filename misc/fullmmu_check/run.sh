#!/bin/sh
# Plan section 7, bundle 20 (P38), PC check (WSL): sh4_mmu_int.cpp on the PC (fullmmu_check.cpp), then the lines that
# do not build here: the row, the engine's choice at reset. FM_SRC: a tree with a planted copy of the engine (mutate.py).
cd "$(dirname "$0")/../.." || exit 2
T=${FM_TMP:-/tmp/fullmmu_check}
E=${FM_SRC:-.}/dc/sh4/sh4_mmu_int.cpp
mkdir -p "$T" || exit 2
CXX="g++ -std=c++14 -O2 -w -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow"
CXX="$CXX -fno-sanitize-recover=all -Imisc/fullmmu_check/fake -Imisc/interp_check/fake -I. -Idc/sh4 -I$T"
# The harness objects; a mutant run (FM_SRC) takes those of the last full run.
H="$T/h_check.o $T/h_cpu.o $T/h_fpu.o $T/h_regs.o"
if [ -z "$FM_SRC" ]; then
	python3 misc/interp_check/gen.py . "$T" || exit 2
	tr -d '\r' < dc/sh4/intc.cpp | sed -n '/^bool Do_Exeption(u32 epc, u32 expEvn, u32 CallVect)$/,/^}$/p' > "$T/do_exception.inc"
	[ -s "$T/do_exception.inc" ] || { echo "FAIL: dc/sh4/intc.cpp has no Do_Exeption"; exit 2; }
	$CXX -c -o "$T/h_check.o" misc/fullmmu_check/fullmmu_check.cpp || exit 2
	$CXX -c -o "$T/h_cpu.o" misc/interp_check/ic_cpu.cpp || exit 2
	$CXX -c -o "$T/h_fpu.o" misc/interp_check/ic_fpu.cpp || exit 2
	$CXX -c -o "$T/h_regs.o" dc/sh4/sh4_registers.cpp || exit 2
fi
$CXX -include misc/fullmmu_check/quiet.h -c -o "$T/engine.o" "$E" || exit 2
$CXX -o "$T/fm" $H "$T/engine.o" || exit 2
r=0
"$T/fm" misc/fsca-table.bin || r=1
[ -n "$FM_SRC" ] && exit $r
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
D=dc/dc.cpp
has $D 1 '		sh4_fullmmu_select();'
[ "$(tr -d '\r' < $D | grep -B1 -F '		sh4_fullmmu_select();' | head -1)" = '	if (!manual)' ] || {
	echo "FAIL: $D: the engine choice is not under if (!manual)"; r=1; }
has $D 1 "	g_disc_wince = ip[0x3E] == '1' || memcmp(&ip[0x60], \"0WINCEOS.BIN\", 12) == 0;"
has $D 1 '	g_disc_wince = false;'
M=dc/mem/mmu.cpp
has $M 1 '	else if (!g_fullmmu_active || s_remap_logs++ < 16)'
has $M 1 '	if (g_fullmmu_active && s_remap_logs++ >= 16)'
P=ps3/ps3_presets.cpp
has $P 1 'int g_full_mmu_preset      = 1;'
has $P 2 '{ "FullMmu",       &g_full_mmu_preset },'
U=ps3/ps3_menu.cpp
has $U 1 '{ "SH4 MMU",'
has $U 1 '&g_full_mmu_preset'
[ $r = 0 ] && echo "PASS: 10 source line checks"
exit $r
