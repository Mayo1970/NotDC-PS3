#pragma once
// Plan 5.20: ftrv and fipr without FPR copies, ftrv's four sums interleaved. Needs ppc_emitter.h in scope;
// misc/fv_check runs what these emit against the old operation order.

#define FV_MEM 0xFF
struct FvElem { u32 reg; u32 ofs; };   // reg: the pinned FPR, or FV_MEM for the context slot at ofs

// The FPR holding element e: its pinned one, or scratch d after an lfs from the context.
static inline u32 fv_src(const FvElem& e, u32 d, u32 ctx)
{
	if (e.reg != FV_MEM)
		return e.reg;
	ppc_lfs(d, ctx, e.ofs);
	return d;
}

static inline void fv_put(const FvElem& e, u32 s, u32 ctx)
{
	if (e.reg == FV_MEM)
		ppc_stfs(s, ctx, e.ofs);
	else if (e.reg != s)
		ppc_fmrx(e.reg, s, 0);
}

// ftrv in place: v[i] = m[i]*v0 + m[4+i]*v1 + m[8+i]*v2 + m[12+i]*v3, m[k] at xofs + 4k. Each sum keeps the old
// order and operand roles, so results are bit-exact; the four sums run side by side, one column at a time.
static inline void emit_ftrv(const FvElem v[4], u32 xofs, u32 ctx)
{
	const bool pinned = v[0].reg != FV_MEM && v[1].reg != FV_MEM && v[2].reg != FV_MEM && v[3].reg != FV_MEM;
	u32 in[4], acc[4];
	for (u32 j = 0; j < 4; j++)
	{
		if (pinned)
		{
			// Column 0 overwrites v1-v3 with their sums, so they are copied aside; v0's sum is written last.
			in[j] = j ? ppc_f8 + j : v[0].reg;
			if (j)
				ppc_fmrx(in[j], v[j].reg, 0);
			acc[j] = v[j].reg;
		}
		else
		{
			in[j] = fv_src(v[j], ppc_f8 + j, ctx);
			acc[j] = ppc_f4 + j;
		}
	}
	for (u32 j = 0; j < 4; j++)
	{
		for (u32 i = 0; i < 4; i++)
			ppc_lfs(ppc_f0 + i, ctx, xofs + 4 * (4 * j + i));
		for (u32 k = 0; k < 4; k++)
		{
			const u32 i = pinned ? (k + 1) & 3 : k;
			if (j == 0)
				ppc_fmulsx(acc[i], ppc_f0 + i, in[0], 0);
			else
				ppc_fmaddsx(acc[i], ppc_f0 + i, acc[i], in[j], 0);
		}
	}
	if (!pinned)
		for (u32 i = 0; i < 4; i++)
			fv_put(v[i], acc[i], ctx);
}

// fipr: rd = a0*b0 + a1*b1 + a2*b2 + a3*b3 in the old order, pinned operands read in place. rd is one of
// the a elements, so the sum stays in f0 until the end.
static inline void emit_fipr(const FvElem a[4], const FvElem b[4], const FvElem& rd, u32 ctx)
{
	for (u32 i = 0; i < 4; i++)
	{
		const u32 x = fv_src(a[i], ppc_f1, ctx);
		const u32 y = fv_src(b[i], ppc_f2, ctx);
		if (i == 0)
			ppc_fmulsx(ppc_f0, x, y, 0);
		else
			ppc_fmaddsx(ppc_f0, x, ppc_f0, y, 0);
	}
	fv_put(rd, ppc_f0, ctx);
}
