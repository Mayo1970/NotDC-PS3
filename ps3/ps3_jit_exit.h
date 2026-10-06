#pragma once
// P6 JIT EXIT: the main loop's way back to C on a stop request. Needs the emitter (ppc_emit, ppc_emitter.h).

// Prologue: the ABI's f14-f31 (FPU PIN) and the CR (cr2-cr4 are callee-saved: JIT GUARD REUSE) into the frame.
static inline void emit_exit_saves(u32 fpr_ofs, u32 cr_ofs)
{
	for (u32 i = 0; i < 18; i++)
		ppc_stfd(ppc_f14 + i, ppc_sp, fpr_ofs + i * 8);
	ppc_mfcr(ppc_r0);
	ppc_stw(ppc_r0, ppc_sp, cr_ofs);
}

// Epilogue: f14-f31, cr2-cr4 and r13-r31 back from the frame, the frame popped, then blr to the C caller.
static inline void emit_exit_epilogue(u32 gpr_ofs, u32 fpr_ofs, u32 cr_ofs, u32 frame)
{
	for (u32 i = 0; i < 18; i++)
		ppc_lfd(ppc_f14 + i, ppc_sp, fpr_ofs + i * 8);
	ppc_lwz(ppc_r0, ppc_sp, cr_ofs);
	ppc_mtcrf(0x38, ppc_r0);
	for (u32 i = 0; i < 19; i++)
		ppc_emit(0xE8000000 | ((ppc_r13 + i) << 21) | (ppc_sp << 16) | ((gpr_ofs + i * 8) & 0xFFFC));   // ld
	ppc_addi(ppc_sp, ppc_sp, frame);
	ppc_emit(0xE8000000 | (ppc_r0 << 21) | (ppc_sp << 16) | 16);   // ld r0,16(r1): LR from the caller's frame
	ppc_mtspr(ppc_spr_lr, ppc_r0);
	ppc_emit(0x4E800020);   // blr, as ppu-as writes it (ppc_bclrx with BI_CR0_EQ sets an unused BI)
}
