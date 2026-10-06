#pragma once
// Plan 5.36 SYS GATE: the main loop's slice-end count. Needs ppc_emitter.h in scope; misc/sysgate_check runs the
// emitted words on a PPC64 model and against ppu-as.

// One more slice end in the context's count; then cr0 holds count < due, signed (a kick sets due to 0).
static inline void emit_gate_count(u32 ctx, u32 ta, u32 tb, u32 elapsed_ofs, u32 due_ofs)
{
	ppc_lwz(ta, ctx, elapsed_ofs);
	ppc_addi(ta, ta, 1);
	ppc_stw(ta, ctx, elapsed_ofs);
	ppc_lwz(tb, ctx, due_ofs);
	ppc_cmp(ppc_cr0, ta, tb, 0);
}

// + RESUME, entered by a block's cycle exit with bl: the next slice's cycles and the count; nothing due returns to
// the block (bltlr). Else next_pc goes to the context and the caller emits the jump to the update.
static inline void emit_gate_resume(u32 ctx, u32 cyc, s32 timeslice, u32 ta, u32 tb, u32 elapsed_ofs, u32 due_ofs,
                                    u32 pc_reg, u32 pc_ofs)
{
	ppc_addi(cyc, cyc, timeslice);
	emit_gate_count(ctx, ta, tb, elapsed_ofs, due_ofs);
	ppc_bclrx(BO_TRUE, BI_CR0_LT, 0);
	ppc_stw(pc_reg, ctx, pc_ofs);
}
