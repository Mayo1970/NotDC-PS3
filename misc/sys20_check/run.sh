#!/bin/sh
# Plan section 7, bundle 20, PC check (WSL): the system rows on their real sources (sys20_check.cpp), then the lines
# that do not build here. S20_OVL: planted copies, which go before the tree (mutate.py).
cd "$(dirname "$0")/../.." || exit 2
T=${S20_TMP:-/tmp/sys20_check}
O=${S20_OVL:-}
mkdir -p "$T/src" || exit 2
src() { if [ -n "$O" ] && [ -f "$O/$1" ]; then echo "$O/$1"; else echo "$1"; fi; }
# LF copies in one folder: their sibling includes come from the fakes first, then from the tree.
for f in dc/mem/sh4_internal_reg.cpp dc/mem/sb.cpp dc/mem/sh4_area0.cpp dc/sh4/sci.cpp plugs/ImgReader/elf_loader.cpp; do
	tr -d '\r' < "$(src $f)" > "$T/src/$(basename $f)" || exit 2
done
tr -d '\r' < "$(src dc/mem/sh4_mem.cpp)" | sed -n '/^\/\/ sys20_check: begin block$/,/^\/\/ sys20_check: end block$/p' \
	> "$T/src/block.inc"
[ -s "$T/src/block.inc" ] || { echo "FAIL: dc/mem/sh4_mem.cpp has no block section"; exit 2; }
CXX="g++ -std=c++14 -O1 -w -fsanitize=address,undefined -fno-sanitize-recover=all"
CXX="$CXX -Imisc/sys20_check/fake -I$T/src -I. -Idc/mem -Idc/sh4 -Iplugs/ImgReader"
H=misc/sys20_check
$CXX -o "$T/s20" $H/sys20_check.cpp $H/s20_regs.cpp $H/s20_sb.cpp $H/s20_area0.cpp $H/s20_sci.cpp $H/s20_elf.cpp \
	$H/s20_block.cpp || exit 2
r=0
"$T/s20" "$T" > "$T/out.txt" 2>&1 || r=1
grep -E "^(FAIL|PASS)" "$T/out.txt" | head -12
[ -n "$O" ] && exit $r
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
P=ps3/ps3_presets.cpp
for k in MemMap BlockWrites OnchipRegs HollyRegs RegSize ElfLoader; do has $P 2 "{ \"$k\","; done
has $P 1 'int g_mem_map_preset       = 1;'
has $P 1 'int g_block_write_preset   = 1;'
has $P 1 'int g_onchip_regs_preset   = 1;'
has $P 1 'int g_holly_regs_preset    = 1;'
has $P 1 'int g_reg_size_preset      = 1;'
has $P 1 'int g_elf_loader_preset    = 1;'
has $P 1 'int get_holly_regs_preset()    { return g_holly_regs_preset; }'
U=ps3/ps3_menu.cpp
for k in g_mem_map_preset g_block_write_preset g_onchip_regs_preset g_holly_regs_preset g_reg_size_preset g_elf_loader_preset
do
	has $U 1 "&$k"
done
has $U 1 '{ "SH4 / SYSTEM",  s_sys,   (int)(sizeof(s_sys) / sizeof(s_sys[0])) },'
has $U 2 'ps3_syscfg_print('
# P78's RFCR (bsc.cpp is not built here): read-only, 17 after a reset.
B=dc/sh4/bsc.cpp
has $B 1 '        BSC[(BSC_RFCR_addr & 0xFF) >> 2].flags         = REG_16BIT_READWRITE | REG_READ_DATA;'
has $B 1 '        BSC[(BSC_RFCR_addr & 0xFF) >> 2].writeFunction = write_BSC_RFCR_dropped;'
has $B 1 '    BSC_RFCR.full  = g_onchip_regs_preset ? 17 : 0x0000;'
has dc/pvr/pvr_if.cpp 1 '    if (g_holly_regs_preset)'
has dc/gdrom/gdromv3.cpp 1 '			if (get_holly_regs_preset())'
# _vmem keeps a handler id * 4 in the low 5 bits of a page entry: ids 0-7, 0 is its own. An eighth handler froze
# every P4 access through the table (bundle 20, HW 2026-10-14).
n=$(grep -rE "^[^/]*_vmem_register_handler(_Template[12]?)?\(" --include=*.cpp dc plugs ps3 | grep -vc "^dc/mem/_vmem.cpp:")
[ "$n" -le 7 ] || { echo "FAIL: $n _vmem handlers registered, the page table holds 7"; r=1; }
[ $r = 0 ] && echo "PASS: 28 source line checks ($n of 7 _vmem handlers)"
exit $r
