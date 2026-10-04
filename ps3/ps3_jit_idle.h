#pragma once
// Plan 5.25 JIT IDLE SKIP: the work mark and the idle check. Needs ppc_emitter.h in scope; misc/idle_check runs
// both on a PPC64 model and against ppu-as.

// Registers the check uses: context base, cycles left, three temps, then the pinned SH4 GPRs (r11 is in memory).
struct IdleRegs { u32 ctx, cyc, ta, tb, tc, pin_first, pin_count; };

// One per idle block, 16-byte aligned so lo + 8 stays in the 16-bit reach.
struct IdleEnt { u32 checks, skips, hash, pad; };

// A block that may store marks the turn as work: the context base is never 0.
static inline void emit_idle_mark(u32 ctx, u32 work_ofs)
{
	ppc_stw(ctx, ctx, work_ofs);
}

// On an idle loop's backward branch: hash the SH4 GPRs; with no work mark and the same hash as this block's last
// check, set cyc to 0 so the next poll ends the timeslice. Pinned GPRs carry junk high words: only low words count.
static inline void emit_idle_check(const IdleRegs& g, u32 r11_ofs, u32 work_ofs, const IdleEnt* ent)
{
	const u32 a = (u32)(uintptr_t)ent;
	const s32 lo = (s16)(a & 0xFFFF);
	ppc_addis(g.tc, 0, (a - (u32)lo) >> 16);
	ppc_lwz(g.ta, g.ctx, r11_ofs);
	for (u32 k = 0; k < g.pin_count; k++)
	{
		if (k)
			ppc_rlwinmx(g.ta, g.ta, 5, 0, 31, 0);
		ppc_xorx(g.ta, g.ta, g.pin_first + k, 0);
	}
	ppc_lwz(g.tb, g.tc, (u32)(lo + 8));
	ppc_stw(g.ta, g.tc, (u32)(lo + 8));
	ppc_xorx(g.tb, g.tb, g.ta, 0);
	ppc_lwz(g.ta, g.ctx, work_ofs);
	ppc_orx(g.ta, g.ta, g.tb, 0);
	ppc_li(g.tb, 0);
	ppc_stw(g.tb, g.ctx, work_ofs);
	ppc_lwz(g.tb, g.tc, (u32)lo);
	ppc_addi(g.tb, g.tb, 1);
	ppc_stw(g.tb, g.tc, (u32)lo);
	ppc_cmpli(ppc_cr0, g.ta, 0, 0);
	ppc_label* busy = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	ppc_li(g.cyc, 0);
	ppc_lwz(g.tb, g.tc, (u32)(lo + 4));
	ppc_addi(g.tb, g.tb, 1);
	ppc_stw(g.tb, g.tc, (u32)(lo + 4));
	busy->MarkLabel();
}
