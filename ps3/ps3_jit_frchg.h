#pragma once
// Plan 5.29: an FPSCR sync where only FR changed (frchg) swaps the pinned FR bank with xf inline, not through
// UpdateFPSCR(). Needs ppc_emitter.h in scope; misc/frpref_check tests it on the PC.

// t0 = fpscr, t1 = fpscr ^ old_fpscr ^ FR (bit 21), compared with 0 into CR0: eq when FR alone changed. On ne
// the caller takes the old C path.
static inline void emit_frchg_test(u32 t0, u32 t1, u32 ctx, u32 fpscr_ofs, u32 old_ofs)
{
	ppc_lwz(t0, ctx, fpscr_ofs);
	ppc_lwz(t1, ctx, old_ofs);
	ppc_xorx(t1, t0, t1, 0);
	ppc_xoris(t1, t1, 0x0020);
	ppc_cmpli(ppc_cr0, t1, 0, 0);
}

// CR0 eq: old_fpscr = fpscr (t0), then fr[0..15] in pin0.. trade places with xf[0..15], 8 at a time via f0-f7.
// Each value takes the lfs or stfs of the old flush, swap and reload, so it is bit-exact; fr[] slots stay stale.
static inline void emit_frchg_swap(u32 t0, u32 ctx, u32 old_ofs, u32 xf_ofs, u32 pin0)
{
	ppc_stw(t0, ctx, old_ofs);
	for (u32 g = 0; g < 16; g += 8)
	{
		for (u32 i = 0; i < 8; i++)
			ppc_lfs(i, ctx, xf_ofs + 4 * (g + i));
		for (u32 i = 0; i < 8; i++)
			ppc_stfs(pin0 + g + i, ctx, xf_ofs + 4 * (g + i));
		for (u32 i = 0; i < 8; i++)
			ppc_fmrx(pin0 + g + i, i, 0);
	}
}
