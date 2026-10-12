#pragma once
// Plan 5.27 LOOK-AHEAD: a store whose base register a static successor block PREFs through is taken as a
// store-queue store. Needs decoder.h in scope; misc/sqla_check tests it against the SH4 opcode list.

enum { SQLA_STOP, SQLA_PASS, SQLA_DELAY, SQLA_HIT };

// For a scan that follows Rn: HIT = pref @Rn, PASS = no Rn write or bank switch, DELAY = a delayed branch (its slot
// runs next), STOP = the rest. steps (plan 5.43): mov.x Rm,@-Rn, fmov FRm,@-Rn, add #imm,Rn move Rn by a constant: PASS.
static inline int sqla_class(u32 op, u32 n, bool steps = false)
{
	const u32 rn = (op >> 8) & 15, rm = (op >> 4) & 15, lo = op & 15;
	const int wn  = rn == n ? SQLA_STOP : SQLA_PASS;			// writes Rn
	const int sn  = (rn == n && !steps) ? SQLA_STOP : SQLA_PASS;	// moves Rn by a constant
	const int wnm = (rn == n || rm == n) ? SQLA_STOP : SQLA_PASS;	// writes Rn and Rm
	const int w0  = n == 0 ? SQLA_STOP : SQLA_PASS;			// writes R0
	switch (op >> 12)
	{
	case 0x0:
		switch (lo)
		{
		case 0x2: return (rm <= 4 || rm >= 8) ? wn : SQLA_STOP;	// stc SR/GBR/VBR/SSR/SPC/Rm_BANK,Rn
		case 0x3:
			if (rm == 0 || rm == 2) return SQLA_DELAY;		// bsrf, braf
			if (rm == 8) return rn == n ? SQLA_HIT : SQLA_PASS;	// pref
			return (rm >= 9 && rm <= 0xC) ? SQLA_PASS : SQLA_STOP;	// ocbi, ocbp, ocbwb, movca.l
		case 0x4: case 0x5: case 0x6: case 0x7: return SQLA_PASS;	// mov.x Rm,@(R0,Rn), mul.l
		case 0x8: return (rn == 0 && rm <= 5 && rm != 3) ? SQLA_PASS : SQLA_STOP;	// clrt sett clrmac clrs sets
		case 0x9:
			if (rn == 0 && rm <= 1) return SQLA_PASS;		// nop, div0u
			return rm == 2 ? wn : SQLA_STOP;			// movt
		case 0xA: return (rm <= 3 || rm == 5 || rm == 6 || rm == 15) ? wn : SQLA_STOP;	// sts, stc SGR/DBR
		case 0xB: return op == 0x000B ? SQLA_DELAY : SQLA_STOP;	// rts; sleep and rte stop
		case 0xC: case 0xD: case 0xE: return wn;			// mov.x @(R0,Rm),Rn
		case 0xF: return wnm;					// mac.l @Rm+,@Rn+
		default: return SQLA_STOP;
		}
	case 0x1: return SQLA_PASS;					// mov.l Rm,@(disp,Rn)
	case 0x2:
		if (lo <= 2) return SQLA_PASS;				// mov.x Rm,@Rn
		if (lo == 3) return SQLA_STOP;
		if (lo <= 6) return sn;					// mov.x Rm,@-Rn
		if (lo == 7 || lo == 8 || lo == 0xC || lo >= 0xE) return SQLA_PASS;	// div0s tst cmp/str mulu muls
		return wn;						// and xor or xtrct
	case 0x3:
		if (lo == 1 || lo == 9) return SQLA_STOP;
		if (lo <= 3 || (lo >= 5 && lo <= 7) || lo == 0xD) return SQLA_PASS;	// cmp/x, dmulu.l, dmuls.l
		return wn;						// div1 sub subc subv add addc addv
	case 0x4:
		switch (op & 0xFF)
		{
		case 0x00: case 0x01: case 0x04: case 0x05: case 0x08: case 0x09: case 0x10: case 0x18: case 0x19:
		case 0x20: case 0x21: case 0x24: case 0x25: case 0x28: case 0x29:
			return wn;						// shifts, rotates, dt
		case 0x11: case 0x15: case 0x1B:				// cmp/pz, cmp/pl, tas.b
		case 0x0A: case 0x1A: case 0x2A: case 0x5A: case 0x6A:	// lds Rm,MACH/MACL/PR/FPUL/FPSCR
			return SQLA_PASS;
		case 0x0B: case 0x2B:
			return SQLA_DELAY;					// jsr, jmp
		}
		if (lo == 0xC || lo == 0xD) return wn;			// shad, shld
		return lo == 0xF ? wnm : SQLA_STOP;			// mac.w; ldc, lds.l, sts.l, stc.l stop
	case 0x5: return wn;						// mov.l @(disp,Rm),Rn
	case 0x6: return (lo >= 4 && lo <= 6) ? wnm : wn;		// @Rm+ forms also write Rm
	case 0x7: return sn;						// add #imm,Rn
	case 0x8:
		switch (rn)
		{
		case 0x0: case 0x1: case 0x8: return SQLA_PASS;		// mov.x R0,@(disp,Rn), cmp/eq #imm,R0
		case 0x4: case 0x5: return w0;				// mov.x @(disp,Rm),R0
		case 0xD: case 0xF: return SQLA_DELAY;			// bt/s, bf/s
		default: return SQLA_STOP;				// bt, bf
		}
	case 0x9: return wn;						// mov.w @(disp,PC),Rn
	case 0xA: case 0xB: return SQLA_DELAY;			// bra, bsr
	case 0xC:
		if (rn == 3) return SQLA_STOP;				// trapa
		if ((rn >= 4 && rn <= 7) || (rn >= 9 && rn <= 0xB)) return w0;	// loads to R0, mova, and/xor/or #imm
		return SQLA_PASS;					// GBR stores, tst #imm, x.b #imm,@(R0,GBR)
	case 0xD: case 0xE: return wn;				// mov.l @(disp,PC),Rn, mov #imm,Rn
	default:
		if (lo == 0x9) return rm == n ? SQLA_STOP : SQLA_PASS;	// fmov @Rm+,FRn
		if (lo == 0xB) return sn;				// fmov FRm,@-Rn
		if (lo == 0xF) return SQLA_STOP;
		if (lo != 0xD) return SQLA_PASS;
		if (rm == 0xC || rm == 0xD) return SQLA_STOP;
		return (rm == 0xF && (rn & 7) == 7) ? SQLA_STOP : SQLA_PASS;	// 0xF7FD, 0xFFFD are not defined
	}
}

#define SQLA_SCAN_OPS 6

// From pc, up to SQLA_SCAN_OPS ops; a delayed branch's slot is the last op scanned. Main RAM only, so the
// read has no side effect.
static inline bool sqla_scan(u32 pc, u32 n, u32 (*rd16)(u32), bool steps = false)
{
	if ((pc & 1) || (pc & 0x1C000000) != 0x0C000000)
		return false;
	bool slot = false;
	for (int k = 0; k < SQLA_SCAN_OPS; k++, pc += 2)
	{
		const int c = sqla_class(rd16(pc), n, steps);
		if (c == SQLA_HIT)
			return true;
		if (c == SQLA_STOP || slot)
			return false;
		slot = c == SQLA_DELAY;
	}
	return false;
}

// The store's base register keeps its value to the block end (steps by an immediate aside) and a static
// successor PREFs through it. It only picks the shape: emit_sq_store still tests the address.
static inline bool sqla_guess(const DecodedBlock* block, size_t i, u32 (*rd16)(u32), bool steps = false)
{
	const shil_opcode& st = block->oplist[i];
	if (!st.rs1.is_r32i() || st.rs1._reg > reg_r15)
		return false;
	const Sh4RegType n = st.rs1._reg;
	for (size_t j = i + 1; j < block->oplist.size(); j++)
	{
		const shil_opcode& o = block->oplist[j];
		if (o.op == shop_ifb || o.op == shop_sync_sr)
			return false;
		const bool step = (o.op == shop_add || o.op == shop_sub) && o.rs1.is_r32i() && o.rs1._reg == n &&
		                  o.rs2.is_imm();
		if ((o.rd.is_r32i() && o.rd._reg == n && !step) || (o.rd2.is_r32i() && o.rd2._reg == n))
			return false;
	}
	switch (block->BlockType)
	{
	case BET_Cond_0:
	case BET_Cond_1:
		return sqla_scan(block->BranchBlock, n, rd16, steps) || sqla_scan(block->NextBlock, n, rd16, steps);
	case BET_StaticJump:
		return sqla_scan(block->BranchBlock, n, rd16, steps);
	default:
		return false;
	}
}

// JIT SQ COPY: the block PREFs through Rm, and Rm's only writer in the block is `mov Rn,Rm` with Rn the store's
// base (before or after the PREF: a loop carries the copy to its next turn). It only picks the shape, as above.
static inline bool sqcp_guess(const DecodedBlock* block, size_t i)
{
	const shil_param& base = block->oplist[i].rs1;
	if (!base.is_r32i() || base._reg > reg_r15)
		return false;
	u32 copies = 0, others = 0, prefs = 0;	// one bit per GPR
	for (size_t j = 0; j < block->oplist.size(); j++)
	{
		const shil_opcode& o = block->oplist[j];
		// The interpreter and a bank switch can write any GPR.
		if (o.op == shop_sync_sr || o.op == shop_ifb)
			return false;
		if (o.op == shop_pref)
		{
			if (o.rs1.is_r32i() && o.rs1._reg <= reg_r15)
				prefs |= 1u << o.rs1._reg;
			continue;
		}
		const bool copy = o.op == shop_mov32 && o.rs1.is_r32i() && o.rs1._reg == base._reg;
		if (o.rd.is_r32i() && o.rd._reg <= reg_r15)
			(copy ? copies : others) |= 1u << o.rd._reg;
		if (o.rd2.is_r32i() && o.rd2._reg <= reg_r15)
			others |= 1u << o.rd2._reg;
	}
	return (prefs & copies & ~others) != 0;
}
