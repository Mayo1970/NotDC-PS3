#pragma once
// Plan 5.24: CR0 read with mfocrf, and FPUL <-> FR moves and conversions with one trip through memory less.
// Needs ppc_emitter.h in scope; misc/ppe_check tests it on the PC.

// mfocrf rD,fxm reads one CR field (one bit set in fxm); mfcr reads all eight and is microcoded on the PPE.
static inline void ppc_mfocrf(u32 D, u32 fxm)
{
	ppc_emit(0x7C100026 | (D << 21) | (fxm << 12));
}

// rD = CR0 bit b (IBM order: LT 0, GT 1, EQ 2) as 0/1. The rlwinm keeps only that bit, so the fields that
// mfocrf leaves undefined do not matter.
static inline void emit_cr0_bit(u32 D, u32 b, bool focrf)
{
	if (focrf)
		ppc_mfocrf(D, 0x80);
	else
		ppc_mfcr(D);
	ppc_rlwinmx(D, D, b + 1, 31, 31, 0);
}

// flds FRm,FPUL with FRm pinned in fs: one stfs to the integer slot at dst (was stfs to FRm's own slot, lwz,
// stw: a load-hit-store).
static inline void emit_flds_direct(u32 fs, u32 ctx, u32 dst)
{
	ppc_stfs(fs, ctx, dst);
}

// fsts FPUL,FRn with FRn pinned in fd: one lfs from the integer slot at src (was lwz, stw to FRn's own slot,
// lfs: a load-hit-store).
static inline void emit_fsts_direct(u32 fd, u32 ctx, u32 src)
{
	ppc_lfs(fd, ctx, src);
}

// ftrc: fctiwz into f0, then its low word. Direct: stfiwx to the context word at dst (t holds dst). Else stfd
// to the scratch slot and lwz into t (a load-hit-store), which the caller stores.
static inline void emit_ftrc(u32 fa, u32 ctx, u32 scratch, u32 dst, u32 t, bool direct)
{
	ppc_fctiwzx(0, fa, 0);
	if (direct)
	{
		ppc_addi(t, 0, dst);
		ppc_stfiwx(0, ctx, t);
		return;
	}
	ppc_stfd(0, ctx, scratch);
	ppc_lwz(t, ctx, scratch + 4);
}

// float: the s32 in x (clobbered) to an exact double in f0, bitcast(0x43300000:x^0x80000000) - 2^52 - 2^31,
// through the scratch slot. bias_at: address of that subtrahend (below 2 GB); 0 builds it in the slot too.
static inline void emit_i2f(u32 x, u32 t, u32 fb, u32 ctx, u32 scratch, u32 bias_at)
{
	if (bias_at)
	{
		const s32 lo = (s16)(bias_at & 0xFFFF);
		ppc_addis(t, 0, (bias_at - (u32)lo) >> 16);
		ppc_lfd(fb, t, (u32)lo);
	}
	ppc_xoris(x, x, 0x8000);
	ppc_addis(t, 0, 0x4330);
	ppc_stw(t, ctx, scratch);
	ppc_stw(x, ctx, scratch + 4);
	ppc_lfd(0, ctx, scratch);
	if (!bias_at)
	{
		ppc_addis(t, 0, 0x4330);
		ppc_stw(t, ctx, scratch);
		ppc_addis(t, 0, 0x8000);
		ppc_stw(t, ctx, scratch + 4);
		ppc_lfd(fb, ctx, scratch);
	}
	ppc_fsubx(0, 0, fb, 0);
}
