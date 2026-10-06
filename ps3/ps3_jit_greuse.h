#pragma once
// Plan 5.31 JIT GUARD REUSE: a memory op whose base register passed the RAM or store-queue check earlier in the
// block skips its own check. Needs decoder.h and ppc_emitter.h in scope; misc/greuse_check tests it.

enum { GR_NONE, GR_RAM, GR_SQ };            // the check an op's fast path needs (from the driver)
enum { GR_NORMAL, GR_VALIDATE, GR_REUSE };  // what the op emits

// Reach from the checked address, in bytes. Exact unless that address lies within GR_LIMIT of the end of RAM's
// area or of the store queue's: no game walks a pointer across those in one block.
#define GR_LIMIT 64
// The flag of a check: CR bits 8-19 (cr2-cr4). C calls keep them (non-volatile), and the JIT writes only cr0.
#define GR_BIT0 8
#define GR_BITS 12
#define GR_MAX_OPS 1024

struct GrPlan
{
	u32 n;                  // ops planned; 0 = none
	u8  role[GR_MAX_OPS];
	u8  bit[GR_MAX_OPS];    // CR bit of a VALIDATE or REUSE op
	u32 validates, reuses;  // ops of each role
};

// p names GPR n (a register param, vectors and pairs by their whole range). No `r`: sh4_registers.h defines it.
static inline bool gr_writes(const shil_param& p, u32 n)
{
	const u32 g = (u32)reg_r0 + n;
	return p.is_reg() && (u32)p._reg <= g && g < (u32)p._reg + (u32)p.count();
}

// kind[i] is the check op i's fast path would run (GR_NONE: not a candidate); a candidate's base is rs1, a GPR,
// and its offset rs3 (an immediate) or 0. Every op is NORMAL unless it reuses, or validates for, another.
static void gr_plan(GrPlan& p, const DecodedBlock& b, const u8* kind)
{
	struct Base { int v; s32 d, off; u8 k; } st[16];
	int bit_of[GR_MAX_OPS];
	p.n = p.validates = p.reuses = 0;
	if (b.oplist.size() > GR_MAX_OPS)
		return;
	p.n = (u32)b.oplist.size();
	for (u32 k = 0; k < 16; k++)
		st[k] = Base{ -1, 0, 0, GR_NONE };
	u32 next = 0;
	for (u32 i = 0; i < p.n; i++)
	{
		const shil_opcode& o = b.oplist[i];
		p.role[i] = GR_NORMAL;
		p.bit[i] = 0;
		bit_of[i] = -1;
		const u32 br = o.rs1.is_r32i() ? (u32)o.rs1._reg - reg_r0 : 16;
		if (kind[i] != GR_NONE && br < 16)
		{
			const s32 off = o.rs3.is_imm() ? (s32)o.rs3._imm : 0;
			const s32 sz = o.flags == 8 ? 8 : (s32)o.flags;
			Base& s = st[br];
			const s32 dist = s.d + off - s.off;
			if (s.v >= 0 && s.k == kind[i] && dist >= -GR_LIMIT && dist + sz <= GR_LIMIT &&
			    (bit_of[s.v] >= 0 || next < GR_BITS))
			{
				if (bit_of[s.v] < 0)
				{
					bit_of[s.v] = (int)next++;
					p.role[s.v] = GR_VALIDATE;
					p.bit[s.v] = (u8)(GR_BIT0 + bit_of[s.v]);
					p.validates++;
				}
				p.role[i] = GR_REUSE;
				p.bit[i] = (u8)(GR_BIT0 + bit_of[s.v]);
				p.reuses++;
			}
			else
			{
				s.v = (int)i;
				s.d = 0;
				s.off = off;
				s.k = kind[i];
			}
		}
		// After the access: ifb and sync_sr may write any GPR. Adding or subtracting an immediate (@Rm+, @-Rn) moves
		// the base; any other write ends it.
		if (o.op == shop_ifb || o.op == shop_sync_sr)
		{
			for (u32 k = 0; k < 16; k++)
				st[k].v = -1;
			continue;
		}
		for (u32 k = 0; k < 16; k++)
		{
			if (st[k].v < 0 || !(gr_writes(o.rd, k) || gr_writes(o.rd2, k)))
				continue;
			if ((o.op == shop_add || o.op == shop_sub) && gr_writes(o.rs1, k) && o.rs2.is_imm() && !gr_writes(o.rd2, k))
			{
				const s64 d = st[k].d + (o.op == shop_add ? (s64)(s32)o.rs2._imm : -(s64)(s32)o.rs2._imm);
				if (d < -0x10000 || d > 0x10000)
					st[k].v = -1;
				else
					st[k].d = (s32)d;
			}
			else
				st[k].v = -1;
		}
	}
}

static inline u8 gr_role(const GrPlan& p, u32 i) { return i < p.n ? p.role[i] : (u8)GR_NORMAL; }

// VALIDATE, on the fast path after its check: the flag says the check passed. The op's cold path clears it.
static inline void emit_gr_set(u32 bit) { ppc_creqv(bit, bit, bit); }
static inline void emit_gr_clear(u32 bit) { ppc_crxor(bit, bit, bit); }

// REUSE: in place of the check, branch to the op's cold path while the flag is clear.
static inline ppc_label* emit_gr_branch(u32 bit)
{
	ppc_label* cold = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, bit, 0, 0, 0);
	return cold;
}
