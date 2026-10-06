#pragma once
// Plan 5.30: a pref outside the store queue prefetches its main-RAM line on the host (dcbt). Only a hint, so an
// address outside RAM just touches some RAM line. Needs ppc_emitter.h in scope; misc/frpref_check tests it.

// t = mem_b.data + (addr & 0xFFFFFF), fastmem's address; ram_hi = mem_b.data >> 16 (sub-2 GB, 64 KB aligned).
// t must not be r0: addis reads rA = 0 as the value 0.
static inline void emit_pref_dcbt(u32 t, u32 areg, u32 ram_hi)
{
	ppc_rlwinmx(t, areg, 0, 8, 31, 0);
	ppc_addis(t, t, ram_hi);
	ppc_dcbt(0, t);
}
