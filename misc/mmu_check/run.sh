#!/bin/sh
# Plan section 7, bundle 13, PC check (WSL): the sources' mmu_check sections, new against old (rows OFF) and against a
# model (rows ON), then the lines that do not build on the PC. Exit 2: no build. Args: cases. MMU_SRC, MMU_TMP: mutate.py.
cd "${MMU_SRC:-$(dirname "$0")/../..}" || exit 2
T=${MMU_TMP:-/tmp/mmu_check}
N=${1:-300}
mkdir -p "$T" || exit 2
# One section of a source, with LF line ends.
section() {
	tr -d '\r' < "$1" | sed -n "/^\/\/ mmu_check: begin $2\$/,/^\/\/ mmu_check: end $2\$/p" > "$T/sec_$2.inc"
	if [ ! -s "$T/sec_$2.inc" ]; then
		echo "FAIL: $1 has no section $2"
		exit 2
	fi
}
section dc/sh4/ccn.cpp ccn
section dc/mem/mmu.cpp mmu
section dc/mem/sh4_internal_reg.cpp p4
section dc/mem/sh4_internal_reg.cpp ocr
section dc/sh4/dmac.cpp dmac
CXX="g++ -std=c++14 -O2 -w -fsanitize=address,undefined -fno-sanitize-recover=all -Imisc/mmu_check/fake -I. -I$T -Imisc/mmu_check"
$CXX -o "$T/chk_new" misc/mmu_check/mmu_check.cpp || exit 2
$CXX -DWORLD_OLD -o "$T/chk_old" misc/mmu_check/mmu_check.cpp || exit 2
r=0
"$T/chk_old" off "$N" > "$T/trace_old.txt" || { echo "FAIL: the old code is not the model's OLD path"; tail -1 "$T/trace_old.txt"; r=1; }
"$T/chk_new" off "$N" > "$T/trace_new.txt" || { echo "FAIL: the new code with every row OFF"; tail -1 "$T/trace_new.txt"; r=1; }
if cmp -s "$T/trace_old.txt" "$T/trace_new.txt"; then
	echo "PASS: $N cases, every row OFF: the new code is the old code"
else
	echo "FAIL: with every row OFF the new code is not the old code"
	r=1
fi
"$T/chk_new" mix "$N" || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
# The first line with each text; the lines must come in this order.
order() {
	f=$1; shift; last=0
	for t in "$@"; do
		n=$(tr -d '\r' < "$f" | grep -nF -- "$t" | head -1 | cut -d: -f1)
		if [ -z "$n" ] || [ "$n" -le "$last" ]; then
			echo "FAIL: $f: out of order or missing: $t"
			r=1
			return
		fi
		last=$n
	done
}
# The readers of MMUCR: no bitfield read is left outside ccn.h's three helpers and the handler.
D=ps3/ps3_driver.cpp
has $D 1 '	if (!get_jit_guard_reuse_preset() || !fm_on() || mmucr_at() || block->oplist.size()>GR_MAX_OPS)'
has $D 1 '				if (mmucr_at())'
has $D 1 '				if (get_jit_prefetch_preset() && s_fm_ok && !mmucr_at())'
has $D 0 'CCN_MMUCR.'
S=dc/sh4/rec_v2/shil_canonical.h
has $S 1 '    if (mmucr_at())'
has $S 0 'CCN_MMUCR.'
I=dc/sh4/sh4_cpu_misc.h
has $I 1 '		if (mmucr_at())'
has $I 1 '	const u32 urc = mmucr_urc();'
has $I 1 '	UTLB[urc].Data = CCN_PTEL;'
has $I 1 '	UTLB[urc].Address = CCN_PTEH;'
has $I 1 '	UTLB_Sync(urc);'
has $I 0 'CCN_MMUCR.'
H=dc/sh4/ccn.h
has $H 1 'static inline u32 mmucr_at()  { return g_mmu_bits_preset ? MMUCR_AT(CCN_MMUCR.reg_data)  : CCN_MMUCR.AT; }'
has $H 1 'static inline u32 mmucr_urc() { return g_mmu_bits_preset ? MMUCR_URC(CCN_MMUCR.reg_data) : CCN_MMUCR.URC; }'
has $H 1 'static inline u32 mmucr_urb() { return g_mmu_bits_preset ? MMUCR_URB(CCN_MMUCR.reg_data) : CCN_MMUCR.URB; }'
C=dc/sh4/ccn.cpp
has $C 1 '    CCN[(CCN_MMUCR_addr&0xFF)>>2].writeFunction = CCN_MMUCR_write;'
has $C 1 '    ccn_mmucr_selftest();'
has $C 1 '    t.reg_data = 0x0000A905;'
# P4 Mirrors: after the catch-all handler, so the two areas replace it.
G=dc/mem/sh4_mem.cpp
order $G '	map_p4();' '	if (g_p4_mirror_preset)' '		map_area1(0xE0);' '		map_area3(0xE0);'
has $G 1 '	if (g_p4_mirror_preset)'
has $G 1 '		map_area1(0xE0);'
has $G 1 '		map_area3(0xE0);'
R=dc/mem/sh4_internal_reg.cpp
has $R 2 '	u32 ofs = ocr_offset(addr);'
has $R 0 '	u32 ofs = addr & OnChipRAM_MASK;'
has $R 1 '		p4_stray_note(addr, sz, false);'
has $R 1 '		p4_stray_note(addr, sz, true);'
A=dc/sh4/dmac.cpp
has $A 1 '	DMAC[((DMAC_DMAOR_addr) & 0xFF) >> 2].writeFunction = WriteDMAOR;'
P=ps3/ps3_presets.cpp
# The defaults: MMUCR Bits FIXED since its HW run (Matt, 2026-10-12); the other rows at the old path until theirs.
has $P 1 'int g_mmu_bits_preset      = 1;'
has $P 1 'int g_p4_mirror_preset     = 0;'
has $P 1 'int g_ocr_banks_preset     = 0;'
has $P 1 'int g_ch2_dma_fix_preset   = 0;'
has $P 2 '{ "MmuBits",       &g_mmu_bits_preset },'
has $P 2 '{ "P4Mirror",      &g_p4_mirror_preset },'
has $P 2 '{ "OcrBanks",      &g_ocr_banks_preset },'
has $P 2 '{ "Ch2DmaFix",     &g_ch2_dma_fix_preset },'
M=ps3/ps3_menu.cpp
has $M 1 '{ "MMUCR Bits",      &g_mmu_bits_preset,      OPT_CHOICE, k_div32,     0, 0,0,0, NULL,'
has $M 1 '{ "P4 Mirrors",      &g_p4_mirror_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Cache RAM Banks", &g_ocr_banks_preset,     OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "Ch2 DMA Rules",   &g_ch2_dma_fix_preset,   OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "MEMORY",        s_memory, (int)(sizeof(s_memory) / sizeof(s_memory[0])) },'
# The row and the live list name a live row; the other three act at boot: the row only.
has $M 2 '&g_ch2_dma_fix_preset'
has $M 1 '&g_mmu_bits_preset'
has $M 1 '&g_p4_mirror_preset'
has $M 1 '&g_ocr_banks_preset'
has $M 2 'ps3_memcfg_print('
[ $r = 0 ] && echo "PASS: 45 source line checks"
exit $r
