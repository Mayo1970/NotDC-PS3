#!/bin/sh
# DMA code drop PC check (WSL): driver.cpp's code map against a model (jdma_check.cpp), then the lines that tie it to
# the compiles, the clears and the DMAs. Exit 1: a check failed, 2: no build. Arg: random rounds. JDMA_SRC: mutate.py.
cd "${JDMA_SRC:-$(dirname "$0")/../..}" || exit 2
T=${JDMA_TMP:-/tmp/jdma_check}
mkdir -p "$T" || exit 2
tr -d '\r' < dc/sh4/rec_v2/driver.cpp | sed -n '/^\/\/ jdma_check: begin$/,/^\/\/ jdma_check: end$/p' > "$T/region.inc"
[ -s "$T/region.inc" ] || { echo "ERROR: no jdma_check region in driver.cpp"; exit 2; }
g++ -std=c++14 -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
	-Imisc/jdma_check/fake -I"$T" -I. -o "$T/jdma_check" misc/jdma_check/jdma_check.cpp || exit 2
r=0
"$T/jdma_check" "${1:-60}" || r=1
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
D=dc/sh4/rec_v2/driver.cpp
# DoCheck guards an open page's blocks; a compile with no guard puts its lines in the map.
pair $D 1 'if (jdma_open(pc & RAM_MASK, len ? len : 2))' 'return true;'
pair $D 1 'const bool guard = !g_jit_mmu && DoCheck(blk->start, blk->sh4_code_size);' 'DynarecCodeEntry* rv = ngen_Compile(blk, guard);'
pair $D 1 'if (rv && !guard && !g_jit_mmu && IsOnRam(blk->start))' 'jdma_mark(blk->start & RAM_MASK, blk->sh4_code_size ? blk->sh4_code_size : 2);'
# Every cache clear empties the map; a reset also drops the open pages.
pair $D 1 'fb_reset();   // P101: the miss counters described the dropped blocks' 'jdma_cleared();'
pair $D 1 'jdma_reset();' 'recSh4_ClearCache();'
has $D 1 'extern u32 spg_VblankCountTotal;'
# The DMAs that write RAM report it.
has dc/gdrom/gdromv3.cpp 1 'rdv_DmaWrite(SB_GDSTARD,len_backup-len,JDMA_GDROM);'
has dc/pvr/pvr_sb_regs.cpp 1 'rdv_DmaWrite(src, (len + 3) & ~3u, JDMA_PVR);'
has dc/aica/aica_if.cpp 1 'rdv_DmaWrite(dst, len, JDMA_G2);'
has dc/aica/aica_if.cpp 1 'rdv_DmaWrite(dst, len, JDMA_G2_EXT1);'
has dc/aica/aica_if.cpp 1 'rdv_DmaWrite(dst, len, JDMA_G2_EXT2);'
has dc/maple/maple_if.cpp 1 'rdv_DmaWrite(header_2,words*4,JDMA_MAPLE);'
has dc/maple/maple_if.cpp 1 'rdv_DmaWrite(s_out[i].addr,s_out[i].words*4,JDMA_MAPLE);'
for f in dc/gdrom/gdromv3.cpp dc/pvr/pvr_sb_regs.cpp dc/aica/aica_if.cpp dc/maple/maple_if.cpp; do
	has $f 1 '#include "dc/sh4/rec_v2/jit_dma.h"'
done
# The I-cache turned on again drops the JIT code, as ICI does (Shenmue II calls code it just wrote after it).
pair dc/sh4/ccn.cpp 1 'else if (temp.ICE && !CCN_CCR.ICE)' 'sh4_cpu.ResetCache();'
# The log's names follow the enum.
has dc/sh4/rec_v2/jit_dma.h 1 'enum JitDma { JDMA_GDROM, JDMA_PVR, JDMA_G2, JDMA_G2_EXT1, JDMA_G2_EXT2, JDMA_MAPLE };'
has $D 1 'static const char* const k_jdma_names[] = { "GD-ROM", "PVR", "G2", "G2 EXT1", "G2 EXT2", "Maple" };'
[ $r -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $r
