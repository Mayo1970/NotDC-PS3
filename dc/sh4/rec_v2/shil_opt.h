#pragma once
// Plan 5.21 (Minicast M3 + M4): constant propagation over r0-r15 and dead value elimination on SHIL.
// AnalyseBlock runs it; misc/constprop_check runs the same file on the PC against a SHIL interpreter.
#include "decoder.h"

struct ShilOptStats
{
	u32 blocks, ops_in, ops_out;
	u32 fold, imm, shift, chain, readm, jdyn, pref, late, dve;
};
static ShilOptStats shil_opt_stats;

// No memory is read at compile time: without SMC page locks a baked value could go stale (plan 5.21).
struct ShilConstProp
{
	DecodedBlock* blk;
	vector<shil_opcode> out;
	u32  val[16];
	bool konst[16];
	u32  rb_val[16];	// what the emitted code last left in the register, when rb is set
	bool rb[16];
	bool static_ends;

	static bool tracked(const shil_param& p) { return p.is_r32i() && (u32)p._reg <= (u32)reg_r15; }
	bool is_const(const shil_param& p) const { return tracked(p) && konst[p._reg]; }
	u32 cval(const shil_param& p) const { return val[p._reg]; }

	static shil_param mk_imm(u32 v) { return shil_param(FMT_IMM, v); }
	static shil_param mk_reg(u32 r) { return shil_param((Sh4RegType)r); }

	void kill(const shil_param& p)
	{
		if (tracked(p))
			konst[p._reg] = rb[p._reg] = false;
	}
	// rb survives a new value: if the register already holds it, its write-back costs nothing.
	void set_const(u32 r, u32 v) { konst[r] = true; val[r] = v; }

	static shil_opcode mk_mov32(const shil_param& rd, u32 v)
	{
		shil_opcode o;
		o.op = shop_mov32;
		o.Flow = 0;
		o.flags = 0;
		o.rd = rd;
		o.rd2 = shil_param();
		o.rs1 = mk_imm(v);
		o.rs2 = shil_param();
		o.rs3 = shil_param();
		return o;
	}

	bool needs_writeback(u32 r) const { return konst[r] && !(rb[r] && rb_val[r] == val[r]); }

	void writeback(u32 r)
	{
		if (!needs_writeback(r))
			return;
		out.push_back(mk_mov32(mk_reg(r), val[r]));
		rb[r] = true;
		rb_val[r] = val[r];
	}
	void writeback(const shil_param& p)
	{
		if (tracked(p))
			writeback(p._reg);
	}
	void writeback_range(u32 first, u32 last)
	{
		for (u32 r = first; r <= last; r++)
			writeback(r);
	}
	void kill_range(u32 first, u32 last)
	{
		for (u32 r = first; r <= last; r++)
			konst[r] = rb[r] = false;
	}

	// Default: register sources hold their real values, register results are unknown.
	void fallback(const shil_opcode& o)
	{
		writeback(o.rs1);
		writeback(o.rs2);
		writeback(o.rs3);
		out.push_back(o);
		kill(o.rd);
		kill(o.rd2);
	}

	// A known result: tracked registers keep it pending, any other integer register gets a mov32.
	void set_or_mov32(const shil_param& rd, u32 v)
	{
		shil_opt_stats.fold++;
		if (tracked(rd))
			set_const(rd._reg, v);
		else
			out.push_back(mk_mov32(rd, v));
	}

	void rs2_to_imm(shil_opcode& o)
	{
		if (is_const(o.rs2))
		{
			o.rs2 = mk_imm(cval(o.rs2));
			shil_opt_stats.imm++;
		}
	}

	// rs1 constant, rs2 a register: a commutative op takes the constant in rs2, the backend's imm slot.
	void commute(shil_opcode& o)
	{
		if (is_const(o.rs1) && o.rs2.is_r32i())
		{
			const u32 a = cval(o.rs1);
			o.rs1 = o.rs2;
			o.rs2 = mk_imm(a);
			shil_opt_stats.imm++;
		}
	}

	void step(shil_opcode o)
	{
		switch (o.op)
		{
		case shop_mov32:
		{
			bool known = false;
			u32 v = 0;
			if (o.rs1.is_imm())
				{ known = true; v = o.rs1._imm; }
			else if (is_const(o.rs1))
				{ known = true; v = cval(o.rs1); }
			if (known)
			{
				if (tracked(o.rd))
				{
					set_const(o.rd._reg, v);
					return;
				}
				// A float takes only fldi's two constants: they are what the 5.20 table load serves.
				if (o.rd.is_r32i() || (o.rd.is_r32f() && (v == 0 || v == 0x3F800000)))
				{
					o.rs1 = mk_imm(v);
					out.push_back(o);
					return;
				}
			}
			fallback(o);
			return;
		}

		case shop_add: case shop_sub: case shop_and: case shop_or: case shop_xor:
		{
			rs2_to_imm(o);
			if (is_const(o.rs1) && o.rs2.is_imm() && o.rd.is_r32i())
			{
				const u32 a = cval(o.rs1), b = o.rs2._imm;
				u32 r;
				switch (o.op)
				{
				case shop_add: r = a + b; break;
				case shop_sub: r = a - b; break;
				case shop_and: r = a & b; break;
				case shop_or:  r = a | b; break;
				default:       r = a ^ b; break;
				}
				set_or_mov32(o.rd, r);
				return;
			}
			if (o.op != shop_sub)
				commute(o);
			fallback(o);
			return;
		}

		case shop_mul_i32: case shop_mul_u16: case shop_mul_s16:
		{
			rs2_to_imm(o);
			if (is_const(o.rs1) && o.rs2.is_imm() && o.rd.is_r32i())
			{
				const u32 a = cval(o.rs1), b = o.rs2._imm;
				u32 r;
				if (o.op == shop_mul_u16)      r = (u32)(u16)a * (u32)(u16)b;
				else if (o.op == shop_mul_s16) r = (u32)((s32)(s16)a * (s32)(s16)b);
				else                           r = a * b;
				set_or_mov32(o.rd, r);
				return;
			}
			commute(o);
			fallback(o);
			return;
		}

		case shop_mul_u64: case shop_mul_s64:
			rs2_to_imm(o);
			commute(o);
			fallback(o);
			return;

		case shop_shld: case shop_shad:
		{
			if (!is_const(o.rs2) || !o.rd.is_r32i())
			{
				fallback(o);
				return;
			}
			// A known count makes the variable shift (two microcoded PPC shifts) one fixed shift.
			const s32 sh = (s32)cval(o.rs2);
			const bool arith = o.op == shop_shad;
			if (sh >= 0)
			{
				o.op = shop_shl;
				o.rs2 = mk_imm((u32)sh & 0x1F);
			}
			else if ((sh & 0x1F) == 0)
			{
				if (!arith)
				{
					shil_opt_stats.shift++;
					set_or_mov32(o.rd, 0);
					return;
				}
				o.op = shop_sar;
				o.rs2 = mk_imm(31);
			}
			else
			{
				o.op = arith ? shop_sar : shop_shr;
				o.rs2 = mk_imm((0u - (u32)sh) & 0x1F);
			}
			shil_opt_stats.shift++;
		}
		// fall through: rs1 may be known too
		case shop_shl: case shop_shr: case shop_sar: case shop_ror:
		{
			rs2_to_imm(o);
			if (is_const(o.rs1) && o.rs2.is_imm() && o.rd.is_r32i())
			{
				const u32 a = cval(o.rs1), k = o.rs2._imm & 0x1F;
				u32 r;
				switch (o.op)
				{
				case shop_shl: r = a << k; break;
				case shop_shr: r = a >> k; break;
				case shop_sar: r = (u32)((s32)a >> k); break;
				default:       r = k ? ((a >> k) | (a << (32 - k))) : a; break;
				}
				set_or_mov32(o.rd, r);
				return;
			}

			// shll16 + shll8 on one register: only the emitted previous op can read the value in between.
			if (o.rs2.is_imm() && o.rs2._imm < 32 && o.rd.is_r32i() &&
			    o.rs1.is_r32i() && o.rs1._reg == o.rd._reg && !out.empty())
			{
				shil_opcode& p = out.back();
				if (p.op == o.op && p.rs2.is_imm() && p.rs2._imm < 32 &&
				    p.rd.is_r32i() && p.rd._reg == o.rd._reg)
				{
					u32 total = p.rs2._imm + o.rs2._imm;
					shil_opt_stats.chain++;
					if (o.op == shop_ror)
					{
						total &= 31;
						if (total == 0)
						{
							// A plain move of p's source: its register may hold a newer pending constant
							// now, but the emitted code still has the value p read.
							const shil_opcode src = p;
							out.pop_back();
							if (!(src.rs1.is_r32i() && src.rs1._reg == o.rd._reg))
							{
								shil_opcode m = mk_mov32(o.rd, 0);
								m.rs1 = src.rs1;
								out.push_back(m);
							}
							return;
						}
					}
					else if (o.op == shop_sar)
					{
						if (total > 31)
							total = 31;
					}
					else if (total >= 32)
					{
						out.pop_back();
						set_or_mov32(o.rd, 0);
						return;
					}
					p.rs2._imm = total;
					return;
				}
			}
			fallback(o);
			return;
		}

		case shop_not: case shop_neg: case shop_ext_s8: case shop_ext_s16: case shop_swaplb:
		{
			if (is_const(o.rs1) && o.rd.is_r32i())
			{
				const u32 a = cval(o.rs1);
				u32 r;
				switch (o.op)
				{
				case shop_not:     r = ~a; break;
				case shop_neg:     r = 0 - a; break;
				case shop_ext_s8:  r = (u32)(s32)(s8)a; break;
				case shop_ext_s16: r = (u32)(s32)(s16)a; break;
				default:           r = (a & 0xFFFF0000) | ((a & 0xFF) << 8) | ((a >> 8) & 0xFF); break;
				}
				set_or_mov32(o.rd, r);
				return;
			}
			fallback(o);
			return;
		}

		case shop_cmpstr:
			if (is_const(o.rs1) && is_const(o.rs2) && o.rd.is_r32i())
			{
				const u32 t = cval(o.rs1) ^ cval(o.rs2);
				set_or_mov32(o.rd, ((t & 0xFF000000) == 0) | ((t & 0x00FF0000) == 0) |
				                   ((t & 0x0000FF00) == 0) | ((t & 0x000000FF) == 0));
				return;
			}
			fallback(o);
			return;

		case shop_test: case shop_seteq: case shop_setge: case shop_setgt:
		case shop_setae: case shop_setab:
		{
			rs2_to_imm(o);
			if (is_const(o.rs1) && o.rs2.is_imm() && o.rd.is_r32i())
			{
				const u32 a = cval(o.rs1), b = o.rs2._imm;
				u32 r;
				switch (o.op)
				{
				case shop_test:  r = (a & b) == 0; break;
				case shop_seteq: r = a == b; break;
				case shop_setge: r = (s32)a >= (s32)b; break;
				case shop_setgt: r = (s32)a > (s32)b; break;
				case shop_setae: r = a >= b; break;
				default:         r = a > b; break;
				}
				set_or_mov32(o.rd, r);
				return;
			}
			if (o.op == shop_test || o.op == shop_seteq)
				commute(o);
			fallback(o);
			return;
		}

		case shop_readm:
		{
			// The backend takes an immediate rs3 only as s16 (addi), and ignores rs3 when rs1 is an immediate.
			if (o.rs1.is_r32i() && is_const(o.rs3) && is_s16(cval(o.rs3)))
			{
				o.rs3 = mk_imm(cval(o.rs3));
				shil_opt_stats.imm++;
			}
			const u32 size = o.flags;
			if (is_const(o.rs1) && (o.rs3.is_null() || o.rs3.is_imm()))
			{
				// An integer load from a known address is addis + load; a float one would bounce through memory.
				if ((size == 1 || size == 2 || size == 4) && o.rd.is_r32i())
				{
					o.rs1 = mk_imm(cval(o.rs1) + (o.rs3.is_imm() ? o.rs3._imm : 0));
					o.rs3 = shil_param();
					out.push_back(o);
					kill(o.rd);
					kill(o.rd2);
					shil_opt_stats.readm++;
					return;
				}
			}
			else if (is_const(o.rs1) && o.rs3.is_r32i() && is_s16(cval(o.rs1)))
			{
				const u32 base = cval(o.rs1);
				o.rs1 = o.rs3;
				o.rs3 = mk_imm(base);
				shil_opt_stats.imm++;
			}
			fallback(o);
			return;
		}

		case shop_writem:
			if (is_const(o.rs3) && is_s16(cval(o.rs3)))
			{
				o.rs3 = mk_imm(cval(o.rs3));
				shil_opt_stats.imm++;
			}
			fallback(o);
			return;

		case shop_jdyn:
			if (is_const(o.rs1) && (o.rs2.is_null() || o.rs2.is_imm()) && static_ends &&
			    (blk->BlockType == BET_DynamicJump || blk->BlockType == BET_DynamicCall))
			{
				blk->BranchBlock = cval(o.rs1) + (o.rs2.is_imm() ? o.rs2._imm : 0);
				blk->BlockType = blk->BlockType == BET_DynamicJump ? BET_StaticJump : BET_StaticCall;
				shil_opt_stats.jdyn++;
				return;
			}
			fallback(o);
			return;

		// The interpreter reads and writes the whole context.
		case shop_ifb:
			writeback_range(0, 15);
			out.push_back(o);
			kill_range(0, 15);
			return;

		// UpdateSR may swap r0-r7 with the bank; r8-r15 go through the flush and reload unchanged.
		case shop_sync_sr:
			writeback_range(0, 7);
			out.push_back(o);
			kill_range(0, 7);
			return;

		case shop_pref:
			if (is_const(o.rs1) && (cval(o.rs1) >> 26) != 0x38)
			{
				shil_opt_stats.pref++;
				return;
			}
			fallback(o);
			return;

		default:
			fallback(o);
			return;
		}
	}

	// The pending constants go before the block's last op when it only writes sr_T and reads none of them:
	// JIT CR0 and T FORWARD both need the T producer to be the last op.
	void finish()
	{
		bool need[16];
		bool any = false;
		for (u32 r = 0; r < 16; r++)
			any |= need[r] = needs_writeback(r);

		size_t at = out.size();
		if (any && !out.empty() && (blk->BlockType == BET_Cond_0 || blk->BlockType == BET_Cond_1))
		{
			const shil_opcode& last = out.back();
			if (last.rd.is_reg() && last.rd._reg == reg_sr_T && !tracked(last.rd2))
			{
				const bool clash = (tracked(last.rs1) && need[last.rs1._reg]) ||
				                   (tracked(last.rs2) && need[last.rs2._reg]) ||
				                   (tracked(last.rs3) && need[last.rs3._reg]);
				if (clash)
					shil_opt_stats.late++;
				else
					at = out.size() - 1;
			}
		}

		vector<shil_opcode> tail(out.begin() + at, out.end());
		out.resize(at);
		writeback_range(0, 15);
		out.insert(out.end(), tail.begin(), tail.end());
	}
};

// static_ends: a jdyn to a known target may become a static end (off when OnlyDynamicEnds is set).
static void shil_constprop(DecodedBlock* blk, bool static_ends)
{
	ShilConstProp cp;
	cp.blk = blk;
	cp.static_ends = static_ends;
	for (u32 r = 0; r < 16; r++)
	{
		cp.val[r] = cp.rb_val[r] = 0;
		cp.konst[r] = cp.rb[r] = false;
	}
	cp.out.reserve(blk->oplist.size() + 16);
	for (size_t i = 0; i < blk->oplist.size(); i++)
		cp.step(blk->oplist[i]);
	cp.finish();
	blk->oplist.swap(cp.out);
}

// Ops with no effect but their register results; anything touching memory, flow or hidden state stays.
static bool shil_dve_pure(shilop op)
{
	switch (op)
	{
	case shop_mov32: case shop_mov64:
	case shop_and: case shop_or: case shop_xor: case shop_not:
	case shop_add: case shop_sub: case shop_neg:
	case shop_adc: case shop_sbc: case shop_negc: case shop_addv: case shop_subv:
	case shop_cmpstr: case shop_swaplb:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror: case shop_shld: case shop_shad:
	case shop_ext_s8: case shop_ext_s16:
	case shop_mul_u16: case shop_mul_s16: case shop_mul_i32: case shop_mul_u64: case shop_mul_s64:
	case shop_div32u: case shop_div32s: case shop_div32p2:
	case shop_cvt_f2i_t: case shop_cvt_i2f_n: case shop_cvt_i2f_z:
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
	case shop_fadd: case shop_fsub: case shop_fmul: case shop_fdiv: case shop_fabs: case shop_fneg:
	case shop_fsqrt: case shop_fipr: case shop_ftrv: case shop_fmac: case shop_fsrra: case shop_fsca:
	case shop_fseteq: case shop_fsetgt:
		return true;
	default:
		return false;
	}
}

static void dve_mark(bool* live, const shil_param& p, bool v)
{
	if (!p.is_reg())
		return;
	for (int c = 0; c < p.count(); c++)
	{
		verify((u32)p._reg + c < (u32)sh4_reg_count);
		live[p._reg + c] = v;
	}
}

static bool dve_any_live(const bool* live, const shil_param& p)
{
	bool any = false;
	for (int c = 0; c < p.count(); c++)
		any |= live[p._reg + c];
	return any;
}

// Backward liveness; everything is live at the block end, as the next block may read any register.
static void shil_dead_values(DecodedBlock* blk)
{
	bool live[sh4_reg_count];
	for (u32 r = 0; r < (u32)sh4_reg_count; r++)
		live[r] = true;

	vector<shil_opcode>& ops = blk->oplist;
	vector<bool> dead(ops.size(), false);
	for (size_t n = ops.size(); n-- > 0;)
	{
		const shil_opcode& o = ops[n];
		if (o.op == shop_ifb || o.op == shop_sync_sr || o.op == shop_sync_fpscr)
		{
			for (u32 r = 0; r < (u32)sh4_reg_count; r++)
				live[r] = true;
			continue;
		}

		if (shil_dve_pure(o.op) && (o.rd.is_reg() || o.rd2.is_reg()) &&
		    !(o.rd.is_reg() && dve_any_live(live, o.rd)) && !(o.rd2.is_reg() && dve_any_live(live, o.rd2)))
		{
			dead[n] = true;
			shil_opt_stats.dve++;
			continue;
		}

		dve_mark(live, o.rd, false);
		dve_mark(live, o.rd2, false);
		// Inputs no param names: div1 reads SR.Q/M, mac.l/mac.w add to MACH:MACL.
		if (o.op == shop_div1)
			live[reg_sr_status] = true;
		if (o.op == shop_mac_l || o.op == shop_mac_w)
			live[reg_macl] = live[reg_mach] = true;
		dve_mark(live, o.rs1, true);
		dve_mark(live, o.rs2, true);
		dve_mark(live, o.rs3, true);
	}

	size_t w = 0;
	for (size_t n = 0; n < ops.size(); n++)
		if (!dead[n])
			ops[w++] = ops[n];
	ops.resize(w);
}
