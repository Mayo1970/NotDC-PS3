#pragma once
// Plan 5.3 BCACHE: the dynamic-end lookup on bm_bcache[], 8-byte {addr, code} entries (blockmanager.h). Needs
// ppc_emitter.h; misc/jit8_check runs it on a PPC64 model and against ppu-as.

// pc = the target, junk above its low word allowed, kept for the miss path. A hit jumps to the entry's code, a miss
// falls through. base = &bm_bcache[0], 8-byte aligned, below 0x7FFF8000 (addis from 0). i, p, v scratch, p not r0.
static inline void emit_bcache_lookup(u32 pc, const void* base, u32 i, u32 p, u32 v)
{
	const u32 a = (u32)(uintptr_t)base;
	const s32 lo = (s16)(a & 0xFFFF);
	ppc_rlwinmx(i, pc, 1, 15, 28, 0);		// ((pc >> 2) & 16383) * 8
	ppc_addis(p, 0, (a - (u32)lo) >> 16);
	ppc_addx(p, p, i, 0, 0);
	ppc_lwz(v, p, (u32)lo);			// entry.addr; cmpw reads the low words only
	ppc_cmp(ppc_cr0, v, pc, 0);
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 4, 0, 0);	// bne miss
	ppc_lwz(v, p, (u32)lo + 4);			// entry.code
	ppc_mtctr(v);
	ppc_bcctrx(BO_ALWAYS, BI_CR0_EQ, 0);
}
