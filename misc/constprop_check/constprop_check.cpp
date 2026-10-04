// Host-only check for plan 5.21 (port of Minicast's tests/constprop_test.cpp): shil_opt.h's passes against a
// SHIL interpreter and against ps3_driver.cpp's operand forms. Not part of the PS3 build.
#include "dc/sh4/rec_v2/decoder.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <map>
#include <random>

int msgboxf(const char* text, unsigned int, ...)
{
	va_list ap;
	va_start(ap, text);
	vprintf(text, ap);
	va_end(ap);
	fflush(stdout);
	abort();
}

// mutate.py swaps in an edited copy of the pass.
#ifndef CP_HEADER
#define CP_HEADER "dc/sh4/rec_v2/shil_opt.h"
#endif
#include CP_HEADER

#define CHECK(x) do { if (!(x)) { printf("CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); fflush(stdout); abort(); } } while (0)

// ---- op construction --------------------------------------------------------

static shil_param P() { return shil_param(); }
static shil_param I(u32 v) { return shil_param(FMT_IMM, v); }
static shil_param R(u32 r) { return shil_param((Sh4RegType)r); }
static shil_param DR(u32 n) { return shil_param((Sh4RegType)(regv_dr_0 + n)); }
static shil_param FV(u32 n) { return shil_param((Sh4RegType)(regv_fv_0 + n)); }
static shil_param XMTRX() { return shil_param(regv_xmtrx); }

// Same argument order as DecodedBlock::Emit.
static shil_opcode OP(shilop op, shil_param rd = P(), shil_param rs1 = P(), shil_param rs2 = P(), u32 flags = 0,
                      shil_param rs3 = P(), shil_param rd2 = P())
{
	shil_opcode o;
	o.op = op;
	o.Flow = 0;
	o.flags = flags;
	o.rd = rd;
	o.rd2 = rd2;
	o.rs1 = rs1;
	o.rs2 = rs2;
	o.rs3 = rs3;
	return o;
}

static DecodedBlock mkblk(BlockEndType t = BET_StaticJump)
{
	DecodedBlock b;
	b.start = 0x8C010000;
	b.cycles = 10;
	b.opcodes = 10;
	b.sh4_code_size = 20;
	b.BranchBlock = 0x8C020000;
	b.NextBlock = 0x8C010014;
	b.BlockType = t;
	return b;
}

static int count_op(const DecodedBlock& b, shilop op)
{
	int n = 0;
	for (auto& o : b.oplist)
		n += o.op == op;
	return n;
}

static const shil_opcode* find_op(const DecodedBlock& b, shilop op)
{
	for (auto& o : b.oplist)
		if (o.op == op)
			return &o;
	return nullptr;
}

static int find_mov_imm(const DecodedBlock& b, u32 rd, u32 imm)
{
	for (size_t i = 0; i < b.oplist.size(); i++)
	{
		const shil_opcode& o = b.oplist[i];
		if (o.op == shop_mov32 && o.rd.is_reg() && (u32)o.rd._reg == rd && o.rs1.is_imm() && o.rs1._imm == imm)
			return (int)i;
	}
	return -1;
}

static int index_of(const DecodedBlock& b, const shil_opcode* p) { return p ? (int)(p - &b.oplist[0]) : -1; }

// ---- PPC operand forms ----------------------------------------------------
// What ps3_driver.cpp's ngen_Compile accepts for each op; anything else verify()s or is silently wrong there.

static bool rs2_imm_ok(shilop op)
{
	switch (op)
	{
	case shop_add: case shop_sub: case shop_and: case shop_or: case shop_xor:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror:
	case shop_mul_i32: case shop_mul_u16: case shop_mul_s16: case shop_mul_u64: case shop_mul_s64:
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
	case shop_jdyn: case shop_ifb:
		return true;
	default:
		return false;
	}
}

static void check_ppc_forms(const DecodedBlock& b)
{
	for (auto& o : b.oplist)
	{
		if (o.rs1.is_imm())
			CHECK(o.op == shop_mov32 || o.op == shop_readm || o.op == shop_ifb);
		else if (o.op != shop_ifb && o.op != shop_sync_sr && o.op != shop_sync_fpscr)
			CHECK(o.rs1.is_reg());
		if (o.rs2.is_imm())
			CHECK(rs2_imm_ok(o.op));
		if (o.rs3.is_imm())
			CHECK(o.op == shop_readm || o.op == shop_writem || o.op == shop_ifb);

		switch (o.op)
		{
		case shop_readm:
			CHECK(o.rs1.is_imm() || o.rs1.is_r32i());
			CHECK(o.rs3.is_null() || o.rs3.is_r32i() || o.rs3.is_imm_s16());
			CHECK(o.flags == 1 || o.flags == 2 || o.flags == 4 || o.flags == 8);
			if (o.rs1.is_imm())
			{
				// ngen_Compile ignores rs3 on this path, and _vmem_read_const takes 1, 2 or 4 bytes only.
				CHECK(o.rs3.is_null());
				CHECK(o.flags != 8);
				CHECK(o.rd.is_r32i());
			}
			break;
		case shop_writem:
			CHECK(o.rs1.is_r32i());
			CHECK(o.rs2.is_reg());
			CHECK(o.rs3.is_null() || o.rs3.is_r32i() || o.rs3.is_imm_s16());
			break;
		case shop_jdyn:
			CHECK(o.rs1.is_r32i());
			CHECK(o.rs2.is_null() || o.rs2.is_imm());
			break;
		case shop_shld: case shop_shad:
			CHECK(o.rs2.is_reg());
			break;
		case shop_mov32:
			CHECK(o.rd.is_r32());
			if (o.rd.is_r32f() && o.rs1.is_imm())
				CHECK(o.rs1._imm == 0 || o.rs1._imm == 0x3F800000);
			break;
		case shop_pref:
			CHECK(o.rs1.is_r32i());
			break;
		default:
			break;
		}
	}
}

// ---- SHIL interpreter --------------------------------------------------------

static u32 hash32(u32 x)
{
	x ^= x >> 16; x *= 0x7feb352d;
	x ^= x >> 15; x *= 0x846ca68b;
	x ^= x >> 16;
	return x;
}

enum { EV_READ = 1, EV_WRITE, EV_CHK, EV_SQ };

struct Event
{
	u32 kind, a, b, c;
	bool operator==(const Event& e) const { return kind == e.kind && a == e.a && b == e.b && c == e.c; }
};

struct Interp
{
	u32 r[sh4_reg_count];
	u32 bank[8];
	std::map<u32, u8> mem;	// written bytes; any other byte is a hash of its address
	vector<Event> ev;
	bool has_jdyn = false;
	u32 jdyn_target = 0;

	u8 rd8(u32 a) const
	{
		auto it = mem.find(a);
		return it != mem.end() ? it->second : (u8)hash32(a * 2654435761u + 7);
	}
	u32 load(u32 a, u32 size)
	{
		u32 v = 0;
		for (u32 i = 0; i < size; i++)
			v |= (u32)rd8(a + i) << (8 * i);
		if (size == 1) v = (u32)(s32)(s8)v;
		if (size == 2) v = (u32)(s32)(s16)v;
		ev.push_back({EV_READ, a, size, 0});
		return v;
	}
	void store(u32 a, u32 size, u32 v)
	{
		for (u32 i = 0; i < size; i++)
			mem[a + i] = (u8)(v >> (8 * i));
		ev.push_back({EV_WRITE, a, size, size == 4 ? v : (v & ((1u << (8 * size)) - 1))});
	}

	u32 in(const shil_param& p, u32 i = 0) const
	{
		if (p.is_imm())
			return p._imm;
		CHECK(p.is_reg() && (u32)p._reg + i < (u32)sh4_reg_count);
		return r[p._reg + i];
	}
	u32 in0(const shil_param& p) const { return p.is_null() ? 0 : in(p); }
	void out(const shil_param& p, u32 v, u32 i = 0)
	{
		if (p.is_null())
			return;
		CHECK(p.is_reg() && (u32)p._reg + i < (u32)sh4_reg_count);
		r[p._reg + i] = v;
	}

	// pc_dyn is the JIT's djump register, which no C code reads (and a static end drops its jdyn).
	void checkpoint(u32 tag, u32 skip_lo, u32 skip_hi)
	{
		for (u32 k = 0; k < (u32)sh4_reg_count; k++)
			if ((k < skip_lo || k > skip_hi) && k != reg_pc_dyn)
				ev.push_back({EV_CHK, tag, k, r[k]});
	}

	void run(const vector<shil_opcode>& ops)
	{
		for (auto& o : ops)
			step(o);
	}

	void step(const shil_opcode& o)
	{
		switch (o.op)
		{
		case shop_mov32: out(o.rd, in(o.rs1)); break;
		case shop_mov64: { u32 a = in(o.rs1, 0), b = in(o.rs1, 1); out(o.rd, a, 0); out(o.rd, b, 1); break; }

		case shop_jdyn:
			has_jdyn = true;
			jdyn_target = in(o.rs1) + in0(o.rs2);
			r[reg_pc_dyn] = jdyn_target;
			break;
		case shop_jcond: r[reg_pc_dyn] = in(o.rs1); break;

		case shop_ifb:
		{
			// Every register is current at a fallback; then the handler may write any of them.
			checkpoint(1, 1, 0);
			const u32 seed = o.rs2._imm ^ (o.rs3._imm << 8);
			for (u32 k = reg_r0; k <= reg_r15; k++)
				r[k] ^= hash32(seed + k);
			for (u32 k = reg_fr_0; k <= reg_fr_15; k++)
				r[k] ^= hash32(seed + 40 + k);
			r[reg_sr_T] = hash32(seed + 77) & 1;
			r[reg_macl] ^= hash32(seed + 78);
			if (o.rs1._imm)
				r[reg_nextpc] = o.rs2._imm;
			if ((seed & 3) == 0)
				store(0x8C001000 + (seed & 0x3C), 4, hash32(seed + 99));
			break;
		}

		// UpdateSR may swap r0-r7 with the bank; r8-r15 may still hold a pending constant's old value.
		case shop_sync_sr:
			checkpoint(2, reg_r8, reg_r15);
			for (u32 k = 0; k < 8; k++)
				std::swap(r[reg_r0 + k], bank[k]);
			break;
		// UpdateFPSCR may swap the float banks; r0-r15 are not involved.
		case shop_sync_fpscr:
			checkpoint(3, reg_r0, reg_r15);
			for (u32 k = 0; k < 16; k++)
				std::swap(r[reg_fr_0 + k], r[reg_xf_0 + k]);
			break;

		case shop_pref:
		{
			const u32 a = in(o.rs1);
			if ((a >> 26) == 0x38)
				ev.push_back({EV_SQ, a, 0, 0});
			break;
		}

		case shop_readm:
		{
			const u32 a = in(o.rs1) + in0(o.rs3);
			if (o.flags == 8)
			{
				const u32 lo = load(a, 4), hi = load(a + 4, 4);
				out(o.rd, lo, 0);
				out(o.rd, hi, 1);
			}
			else
				out(o.rd, load(a, o.flags));
			break;
		}
		case shop_writem:
		{
			const u32 a = in(o.rs1) + in0(o.rs3);
			if (o.flags == 8)
			{
				store(a, 4, in(o.rs2, 0));
				store(a + 4, 4, in(o.rs2, 1));
			}
			else
				store(a, o.flags, in(o.rs2));
			break;
		}

		case shop_and: out(o.rd, in(o.rs1) & in(o.rs2)); break;
		case shop_or:  out(o.rd, in(o.rs1) | in(o.rs2)); break;
		case shop_xor: out(o.rd, in(o.rs1) ^ in(o.rs2)); break;
		case shop_add: out(o.rd, in(o.rs1) + in(o.rs2)); break;
		case shop_sub: out(o.rd, in(o.rs1) - in(o.rs2)); break;
		case shop_not: out(o.rd, ~in(o.rs1)); break;
		case shop_neg: out(o.rd, 0 - in(o.rs1)); break;
		case shop_ext_s8:  out(o.rd, (u32)(s32)(s8)in(o.rs1)); break;
		case shop_ext_s16: out(o.rd, (u32)(s32)(s16)in(o.rs1)); break;
		case shop_swaplb:
		{
			const u32 a = in(o.rs1);
			out(o.rd, (a & 0xFFFF0000) | ((a & 0xFF) << 8) | ((a >> 8) & 0xFF));
			break;
		}

		case shop_adc:
		{
			const u64 res = (u64)in(o.rs1) + in(o.rs2) + (in(o.rs3) & 1);
			out(o.rd, (u32)res);
			out(o.rd2, (u32)(res >> 32));
			break;
		}
		case shop_sbc:
		{
			const u64 res = (u64)in(o.rs1) - in(o.rs2) - (in(o.rs3) & 1);
			out(o.rd, (u32)res);
			out(o.rd2, (u32)(res >> 32) & 1);
			break;
		}
		case shop_negc:
		{
			const u64 res = (u64)0 - in(o.rs1) - (in(o.rs2) & 1);
			out(o.rd, (u32)res);
			out(o.rd2, (u32)(res >> 32) & 1);
			break;
		}
		case shop_addv:
		{
			const u32 a = in(o.rs1), b = in(o.rs2), res = a + b;
			out(o.rd, res);
			out(o.rd2, (~(a ^ b) & (a ^ res)) >> 31);
			break;
		}
		case shop_subv:
		{
			const u32 a = in(o.rs1), b = in(o.rs2), res = a - b;
			out(o.rd, res);
			out(o.rd2, ((a ^ b) & (a ^ res)) >> 31);
			break;
		}
		case shop_div1:
		{
			// shil_canonical.h's div1; SR.Q/M live in sr_status, which no param names.
			u32 rn = in(o.rs1);
			const u32 rm = in(o.rs2), T = in(o.rs3);
			u32& sr = r[reg_sr_status];
			const u32 old_q = (sr >> 8) & 1, m = (sr >> 9) & 1;
			u32 q = rn >> 31;
			rn = (rn << 1) | (T & 1);
			const u32 tmp0 = rn;
			u32 c;
			if (old_q == m) { rn -= rm; c = rn > tmp0; }
			else            { rn += rm; c = rn < tmp0; }
			q ^= m ^ c;
			sr = (sr & ~(1u << 8)) | (q << 8);
			out(o.rd, rn);
			out(o.rd2, q == m);
			break;
		}
		case shop_cmpstr:
		{
			const u32 t = in(o.rs1) ^ in(o.rs2);
			out(o.rd, ((t & 0xFF000000) == 0) | ((t & 0x00FF0000) == 0) | ((t & 0x0000FF00) == 0) | ((t & 0xFF) == 0));
			break;
		}
		case shop_mac_l:
		{
			// shil.cpp's sh4_mac_l: MACH:MACL is an accumulator no param names as a source.
			const u32 a = in(o.rs1), b = in(o.rs2), srs = in(o.rs3), oldl = r[reg_macl], oldh = r[reg_mach];
			const u64 mul = (u64)((s64)(s32)a * (s64)(s32)b);
			u32 res0 = oldl + (u32)mul;
			u32 res2 = (u32)(mul >> 32) + (oldl > res0 ? 1u : 0u);
			if (srs & 2)
			{
				res2 += (oldh & 0xFFFF);
				if ((s32)res2 < 0 && res2 < 0xFFFF8000u) { res2 = 0xFFFF8000u; res0 = 0; }
				if ((s32)res2 > 0 && res2 > 0x00007FFFu) { res2 = 0x00007FFFu; res0 = 0xFFFFFFFFu; }
			}
			else
				res2 += oldh;
			out(o.rd, res0);
			out(o.rd2, res2);
			break;
		}
		case shop_mac_w:
		{
			const u32 a = in(o.rs1), b = in(o.rs2), srs = in(o.rs3), oldl = r[reg_macl], oldh = r[reg_mach];
			const u32 prod = (u32)((s32)a * (s32)b);
			u32 res0 = oldl + prod, res2 = oldh;
			if (srs & 2)
			{
				const u32 dest = (s32)oldl < 0, src = ((s32)prod < 0) + dest, ans = ((s32)res0 < 0) + dest;
				if (ans == 1)
				{
					if (src == 0) res0 = 0x7FFFFFFFu;
					if (src == 2) res0 = 0x80000000u;
				}
			}
			else
				res2 += (u32)((s32)prod >> 31) + (oldl > res0 ? 1u : 0u);
			out(o.rd, res0);
			out(o.rd2, res2);
			break;
		}

		// The backend masks a shift count with & 0x1F (rlwinm/srawi immediates).
		case shop_shl: out(o.rd, in(o.rs1) << (in(o.rs2) & 0x1F)); break;
		case shop_shr: out(o.rd, in(o.rs1) >> (in(o.rs2) & 0x1F)); break;
		case shop_sar: out(o.rd, (u32)((s32)in(o.rs1) >> (in(o.rs2) & 0x1F))); break;
		case shop_ror:
		{
			const u32 a = in(o.rs1), k = in(o.rs2) & 0x1F;
			out(o.rd, k ? ((a >> k) | (a << (32 - k))) : a);
			break;
		}
		case shop_shld:
		{
			const u32 a = in(o.rs1), b = in(o.rs2);
			u32 v;
			if (!(b & 0x80000000))    v = a << (b & 0x1F);
			else if ((b & 0x1F) == 0) v = 0;
			else                      v = a >> ((~b & 0x1F) + 1);
			out(o.rd, v);
			break;
		}
		case shop_shad:
		{
			const s32 a = (s32)in(o.rs1);
			const u32 b = in(o.rs2);
			u32 v;
			if (!(b & 0x80000000))    v = (u32)a << (b & 0x1F);
			else if ((b & 0x1F) == 0) v = (u32)(a >> 31);
			else                      v = (u32)(a >> ((~b & 0x1F) + 1));
			out(o.rd, v);
			break;
		}

		case shop_mul_u16: out(o.rd, (u32)(u16)in(o.rs1) * (u32)(u16)in(o.rs2)); break;
		case shop_mul_s16: out(o.rd, (u32)((s32)(s16)in(o.rs1) * (s32)(s16)in(o.rs2))); break;
		case shop_mul_i32: out(o.rd, in(o.rs1) * in(o.rs2)); break;
		case shop_mul_u64:
		{
			const u64 p = (u64)in(o.rs1) * in(o.rs2);
			out(o.rd, (u32)p);
			out(o.rd2, (u32)(p >> 32));
			break;
		}
		case shop_mul_s64:
		{
			const u64 p = (u64)((s64)(s32)in(o.rs1) * (s64)(s32)in(o.rs2));
			out(o.rd, (u32)p);
			out(o.rd2, (u32)(p >> 32));
			break;
		}
		case shop_div32u:
		{
			const u32 a = in(o.rs1), b = in(o.rs2);
			out(o.rd, b ? a / b : 0xFFFFFFFF);
			out(o.rd2, b ? a % b : a);
			break;
		}
		case shop_div32s:
		{
			const s32 a = (s32)in(o.rs1), b = (s32)in(o.rs2);
			u32 q, m;
			if (b == 0)                                    { q = 0xFFFFFFFF; m = (u32)a; }
			else if (a == (s32)0x80000000 && b == -1)      { q = 0x80000000; m = 0; }
			else                                           { q = (u32)(a / b); m = (u32)(a % b); }
			out(o.rd, q);
			out(o.rd2, m);
			break;
		}
		case shop_div32p2:
		{
			u32 a = in(o.rs1);
			const u32 b = in(o.rs2);
			if (!in(o.rs3))
				a -= b;
			out(o.rd, a);
			break;
		}

		case shop_test:  out(o.rd, (in(o.rs1) & in(o.rs2)) == 0); break;
		case shop_seteq: out(o.rd, in(o.rs1) == in(o.rs2)); break;
		case shop_setge: out(o.rd, (s32)in(o.rs1) >= (s32)in(o.rs2)); break;
		case shop_setgt: out(o.rd, (s32)in(o.rs1) > (s32)in(o.rs2)); break;
		case shop_setae: out(o.rd, in(o.rs1) >= in(o.rs2)); break;
		case shop_setab: out(o.rd, in(o.rs1) > in(o.rs2)); break;

		// Float ops only move data here, so integer mixes stand in for the arithmetic.
		case shop_fadd: out(o.rd, in(o.rs1) * 3 + in(o.rs2)); break;
		case shop_fsub: out(o.rd, in(o.rs1) * 5 - in(o.rs2)); break;
		case shop_fmul: out(o.rd, (in(o.rs1) * 7) ^ in(o.rs2)); break;
		case shop_fdiv: out(o.rd, in(o.rs1) ^ (in(o.rs2) * 11)); break;
		case shop_fabs: out(o.rd, in(o.rs1) & 0x7FFFFFFF); break;
		case shop_fneg: out(o.rd, in(o.rs1) ^ 0x80000000); break;
		case shop_fsqrt: out(o.rd, in(o.rs1) * 13 + 1); break;
		case shop_fsrra: out(o.rd, in(o.rs1) * 17 + 3); break;
		case shop_fmac: out(o.rd, in(o.rs1) + in(o.rs2) * in(o.rs3)); break;
		case shop_fseteq: out(o.rd, in(o.rs1) == in(o.rs2)); break;
		case shop_fsetgt: out(o.rd, (s32)in(o.rs1) > (s32)in(o.rs2)); break;
		case shop_cvt_f2i_t: out(o.rd, in(o.rs1) * 19); break;
		case shop_cvt_i2f_n: out(o.rd, in(o.rs1) * 23); break;
		case shop_cvt_i2f_z: out(o.rd, in(o.rs1) * 29); break;
		case shop_fipr:
		{
			u32 s = 0;
			for (u32 i = 0; i < 4; i++)
				s += in(o.rs1, i) * in(o.rs2, i);
			out(o.rd, s);
			break;
		}
		case shop_ftrv:
		{
			u32 v[4], m[16], res[4];
			for (u32 j = 0; j < 4; j++) v[j] = in(o.rs1, j);
			for (u32 k = 0; k < 16; k++) m[k] = in(o.rs2, k);
			for (u32 i = 0; i < 4; i++)
				res[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i] * v[3];
			for (u32 i = 0; i < 4; i++) out(o.rd, res[i], i);
			break;
		}
		case shop_fsca:
		{
			const u32 a = in(o.rs1);
			out(o.rd, a * 3, 0);
			out(o.rd, a * 5 + 1, 1);
			break;
		}

		default:
			printf("interp: op %d not modelled\n", (int)o.op);
			fflush(stdout);
			abort();
		}
	}
};

static const char* reg_name(u32 k)
{
	static char buf[16];
	snprintf(buf, sizeof(buf), "reg%u", k);
	return buf;
}

static void dump_block(const char* title, const DecodedBlock& b)
{
	printf("%s (type %d, branch %08X):\n", title, (int)b.BlockType, b.BranchBlock);
	for (auto& o : b.oplist)
	{
		auto pp = [](const shil_param& p) {
			if (p.is_null()) printf(" -");
			else if (p.is_imm()) printf(" #%08X", p._imm);
			else printf(" r%u/%u", (u32)p._reg, p.type);
		};
		printf("  op%-3d f%u rd", (int)o.op, o.flags); pp(o.rd);
		printf(" rd2"); pp(o.rd2);
		printf(" rs1"); pp(o.rs1);
		printf(" rs2"); pp(o.rs2);
		printf(" rs3"); pp(o.rs3);
		printf("\n");
	}
}

// Original and optimized runs must leave the same registers, memory trace and exit.
static bool same_run(const Interp& a, const Interp& b, const DecodedBlock& orig, const DecodedBlock& opt)
{
	const bool promoted = a.has_jdyn && !b.has_jdyn;
	for (u32 k = 0; k < (u32)sh4_reg_count; k++)
	{
		if (k == reg_pc_dyn && promoted)
			continue;
		if (a.r[k] != b.r[k])
		{
			printf("register %s differs: %08X != %08X\n", reg_name(k), a.r[k], b.r[k]);
			return false;
		}
	}
	for (u32 k = 0; k < 8; k++)
		if (a.bank[k] != b.bank[k])
		{
			printf("bank %u differs\n", k);
			return false;
		}
	if (a.ev.size() != b.ev.size())
	{
		printf("event count differs: %zu != %zu\n", a.ev.size(), b.ev.size());
		return false;
	}
	for (size_t i = 0; i < a.ev.size(); i++)
		if (!(a.ev[i] == b.ev[i]))
		{
			printf("event %zu differs: {%u %08X %08X %08X} != {%u %08X %08X %08X}\n", i,
			       a.ev[i].kind, a.ev[i].a, a.ev[i].b, a.ev[i].c, b.ev[i].kind, b.ev[i].a, b.ev[i].b, b.ev[i].c);
			return false;
		}
	if (b.has_jdyn && !a.has_jdyn)
	{
		printf("jdyn appeared\n");
		return false;
	}
	if (promoted)
	{
		const BlockEndType want = orig.BlockType == BET_DynamicJump ? BET_StaticJump : BET_StaticCall;
		if ((orig.BlockType != BET_DynamicJump && orig.BlockType != BET_DynamicCall) ||
		    opt.BlockType != want || opt.BranchBlock != a.jdyn_target)
		{
			printf("bad static end: type %d -> %d, branch %08X, target %08X\n",
			       (int)orig.BlockType, (int)opt.BlockType, opt.BranchBlock, a.jdyn_target);
			return false;
		}
	}
	else if (opt.BlockType != orig.BlockType || opt.BranchBlock != orig.BranchBlock ||
	         (a.has_jdyn && a.jdyn_target != b.jdyn_target))
	{
		printf("exit differs\n");
		return false;
	}
	return true;
}

// ---- directed tests -----------------------------------------------------------

static void opt(DecodedBlock& b, bool static_ends = true)
{
	shil_constprop(&b, static_ends);
	check_ppc_forms(b);
}

static void t_rd2_kill()
{
	// A div32u writing rd2 = r3 must drop r3's constant.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r3), I(5)));
	b.oplist.push_back(OP(shop_div32u, R(reg_r2), R(reg_r2), R(reg_r4), 0, P(), R(reg_r3)));
	b.oplist.push_back(OP(shop_add, R(reg_r5), R(reg_r3), I(1)));
	opt(b);
	const shil_opcode* add = find_op(b, shop_add);
	CHECK(add && add->rs1.is_r32i() && add->rs1._reg == reg_r3);
	CHECK(find_mov_imm(b, reg_r5, 6) < 0);
}

static void t_writem_forms()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0x8C300000)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(8)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r2), I(0xCAFE0000)));
	b.oplist.push_back(OP(shop_writem, P(), R(reg_r1), R(reg_r2), 4, R(reg_r0)));
	opt(b);
	const shil_opcode* wm = find_op(b, shop_writem);
	CHECK(wm && wm->rs1.is_r32i() && wm->rs2.is_r32i());
	CHECK(wm->rs3.is_imm() && wm->rs3._imm == 8);
	const int m1 = find_mov_imm(b, reg_r1, 0x8C300000), m2 = find_mov_imm(b, reg_r2, 0xCAFE0000);
	CHECK(m1 >= 0 && m1 < index_of(b, wm));
	CHECK(m2 >= 0 && m2 < index_of(b, wm));
}

static void t_shld_shad()
{
	struct Case { shilop in; s32 amount; shilop out; u32 imm; };
	static const Case cases[] = {
		{ shop_shld,   5, shop_shl,  5 }, { shop_shld,  -5, shop_shr,  5 }, { shop_shld, -33, shop_shr,  1 },
		{ shop_shad,   9, shop_shl,  9 }, { shop_shad,  -9, shop_sar,  9 }, { shop_shad, -32, shop_sar, 31 },
		{ shop_shld,  37, shop_shl,  5 }, { shop_shad, -64, shop_sar, 31 },
	};
	for (auto& c : cases)
	{
		DecodedBlock b = mkblk();
		b.oplist.push_back(OP(shop_mov32, R(reg_r1), I((u32)c.amount)));
		b.oplist.push_back(OP(c.in, R(reg_r0), R(reg_r4), R(reg_r1)));
		opt(b);
		const shil_opcode* s = find_op(b, c.out);
		CHECK(s && s->rs2.is_imm() && s->rs2._imm == c.imm);
		CHECK(count_op(b, c.in) == 0);
	}
	// shld by -32 is zero whatever the source.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I((u32)-32)));
	b.oplist.push_back(OP(shop_shld, R(reg_r0), R(reg_r4), R(reg_r1)));
	opt(b);
	CHECK(count_op(b, shop_shld) == 0);
	CHECK(find_mov_imm(b, reg_r0, 0) >= 0);
}

static void t_sync_sr()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(5)));    // banked
	b.oplist.push_back(OP(shop_mov32, R(reg_r12), I(7)));   // not banked
	b.oplist.push_back(OP(shop_sync_sr));
	b.oplist.push_back(OP(shop_add, R(reg_r2), R(reg_r1), I(1)));
	b.oplist.push_back(OP(shop_add, R(reg_r13), R(reg_r12), I(1)));
	opt(b);
	const shil_opcode* sync = find_op(b, shop_sync_sr);
	const int m1 = find_mov_imm(b, reg_r1, 5);
	CHECK(sync && m1 >= 0 && m1 < index_of(b, sync));
	const shil_opcode* add = find_op(b, shop_add);
	CHECK(add && add->rs1.is_r32i() && add->rs1._reg == reg_r1);
	CHECK(count_op(b, shop_add) == 1);
	CHECK(find_mov_imm(b, reg_r13, 8) >= 0);
}

static void t_commutative_swap()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(5)));
	b.oplist.push_back(OP(shop_add, R(reg_r1), R(reg_r0), R(reg_r2)));
	b.oplist.push_back(OP(shop_sub, R(reg_r3), R(reg_r0), R(reg_r2)));
	b.oplist.push_back(OP(shop_seteq, R(reg_sr_T), R(reg_r0), R(reg_r4)));
	b.oplist.push_back(OP(shop_setgt, R(reg_sr_T), R(reg_r0), R(reg_r5)));
	opt(b);
	const shil_opcode* add = find_op(b, shop_add);
	CHECK(add && add->rs1._reg == reg_r2 && add->rs2.is_imm() && add->rs2._imm == 5);
	const shil_opcode* eq = find_op(b, shop_seteq);
	CHECK(eq && eq->rs1._reg == reg_r4 && eq->rs2.is_imm() && eq->rs2._imm == 5);
	// sub and setgt are not symmetric: r0 is materialized and stays a register.
	const shil_opcode* sub = find_op(b, shop_sub);
	CHECK(sub && sub->rs1.is_r32i() && sub->rs1._reg == reg_r0);
	const shil_opcode* gt = find_op(b, shop_setgt);
	CHECK(gt && gt->rs1.is_r32i() && gt->rs1._reg == reg_r0);
	const int mv = find_mov_imm(b, reg_r0, 5);
	CHECK(mv >= 0 && mv < index_of(b, sub));
}

static void t_writeback_dedup()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(5)));
	b.oplist.push_back(OP(shop_writem, P(), R(reg_r8), R(reg_r0), 4));
	b.oplist.push_back(OP(shop_writem, P(), R(reg_r9), R(reg_r0), 4));
	opt(b);
	int movs = 0;
	for (auto& o : b.oplist)
		movs += o.op == shop_mov32 && o.rd.is_r32i() && o.rd._reg == reg_r0;
	CHECK(movs == 1);
}

static void t_readm_promote()
{
	// MMIO too: the load still runs, through the handler _vmem_read_const picks; 8-bit included.
	const u32 sizes[] = {1, 2, 4};
	for (u32 sz : sizes)
	{
		DecodedBlock b = mkblk();
		b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0xA05F8000)));
		b.oplist.push_back(OP(shop_readm, R(reg_r0), R(reg_r1), P(), sz, I(0x10)));
		opt(b);
		const shil_opcode* rm = find_op(b, shop_readm);
		CHECK(rm && rm->rs1.is_imm() && rm->rs1._imm == 0xA05F8010 && rm->rs3.is_null());
	}
	// A float or 64-bit destination keeps the register form: the imm path would bounce it through memory.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0x8C300000)));
	b.oplist.push_back(OP(shop_readm, R(reg_fr_0), R(reg_r1), P(), 4));
	b.oplist.push_back(OP(shop_readm, DR(1), R(reg_r1), P(), 8));
	opt(b);
	for (auto& o : b.oplist)
		if (o.op == shop_readm)
			CHECK(o.rs1.is_r32i() && o.rs1._reg == reg_r1);
	const int mv = find_mov_imm(b, reg_r1, 0x8C300000);
	CHECK(mv >= 0 && mv < index_of(b, find_op(b, shop_readm)));
}

static void t_readm_rs3()
{
	// r0-indexed loads: a small constant r0 becomes the displacement, a large one stays a register.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(0x40)));
	b.oplist.push_back(OP(shop_readm, R(reg_r1), R(reg_r4), P(), 4, R(reg_r0)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(0x12340)));
	b.oplist.push_back(OP(shop_readm, R(reg_r2), R(reg_r4), P(), 4, R(reg_r0)));
	opt(b);
	const shil_opcode* r1 = find_op(b, shop_readm);
	CHECK(r1 && r1->rs3.is_imm() && r1->rs3._imm == 0x40);
	CHECK(find_mov_imm(b, reg_r0, 0x40) < 0 && find_mov_imm(b, reg_r0, 0x12340) >= 0);
	for (auto& o : b.oplist)
		if (o.op == shop_readm && o.rd._reg == reg_r2)
			CHECK(o.rs3.is_r32i() && o.rs3._reg == reg_r0);
	// A small constant base with a register index swaps into base + displacement.
	DecodedBlock c = mkblk();
	c.oplist.push_back(OP(shop_mov32, R(reg_r4), I(0x60)));
	c.oplist.push_back(OP(shop_readm, R(reg_r1), R(reg_r4), P(), 4, R(reg_r0)));
	opt(c);
	const shil_opcode* rm = find_op(c, shop_readm);
	CHECK(rm && rm->rs1.is_r32i() && rm->rs1._reg == reg_r0 && rm->rs3.is_imm() && rm->rs3._imm == 0x60);
}

static void t_pref()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0x8C001000)));
	b.oplist.push_back(OP(shop_pref, P(), R(reg_r1)));
	opt(b);
	CHECK(count_op(b, shop_pref) == 0);

	DecodedBlock c = mkblk();
	c.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0xE0000020)));
	c.oplist.push_back(OP(shop_pref, P(), R(reg_r1)));
	opt(c);
	const shil_opcode* pf = find_op(c, shop_pref);
	CHECK(pf && pf->rs1.is_r32i() && pf->rs1._reg == reg_r1);
	const int mv = find_mov_imm(c, reg_r1, 0xE0000020);
	CHECK(mv >= 0 && mv < index_of(c, pf));
}

static void t_cmp_fold()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(5)));
	b.oplist.push_back(OP(shop_test, R(reg_sr_T), R(reg_r0), I(4)));
	opt(b);
	CHECK(count_op(b, shop_test) == 0);
	CHECK(find_mov_imm(b, reg_sr_T, 0) >= 0);
}

static void t_jdyn()
{
	DecodedBlock b = mkblk(BET_DynamicCall);
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(0x8C0100F0)));
	b.oplist.push_back(OP(shop_jdyn, R(reg_pc_dyn), R(reg_r0), I(8)));
	opt(b);
	CHECK(b.BlockType == BET_StaticCall && b.BranchBlock == 0x8C0100F8);
	CHECK(count_op(b, shop_jdyn) == 0);
	CHECK(find_mov_imm(b, reg_r0, 0x8C0100F0) >= 0);

	// OnlyDynamicEnds (JitSafe / JitDynEnds): the end stays dynamic.
	DecodedBlock c = mkblk(BET_DynamicJump);
	c.oplist.push_back(OP(shop_mov32, R(reg_r0), I(0x8C0100F0)));
	c.oplist.push_back(OP(shop_jdyn, R(reg_pc_dyn), R(reg_r0)));
	opt(c, false);
	CHECK(c.BlockType == BET_DynamicJump && count_op(c, shop_jdyn) == 1);
	const int mv = find_mov_imm(c, reg_r0, 0x8C0100F0);
	CHECK(mv >= 0 && mv < index_of(c, find_op(c, shop_jdyn)));
}

static void t_f32_mov()
{
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_mov32, R(reg_r0), I(0x3F800000)));
	b.oplist.push_back(OP(shop_mov32, R(reg_fr_0), R(reg_r0)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(0x40490FDB)));
	b.oplist.push_back(OP(shop_mov32, R(reg_fr_1), R(reg_r1)));
	opt(b);
	for (auto& o : b.oplist)
	{
		if (o.op != shop_mov32 || !o.rd.is_r32f())
			continue;
		if (o.rd._reg == reg_fr_0)
			CHECK(o.rs1.is_imm() && o.rs1._imm == 0x3F800000);
		if (o.rd._reg == reg_fr_1)
			CHECK(o.rs1.is_r32i() && o.rs1._reg == reg_r1);
	}
}

static void t_shift_chains()
{
	// shll16; shll8; shll2; shll -> one shl #27
	DecodedBlock b = mkblk();
	for (u32 a : {16u, 8u, 2u, 1u})
		b.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(a)));
	opt(b);
	CHECK(count_op(b, shop_shl) == 1 && find_op(b, shop_shl)->rs2._imm == 27);

	DecodedBlock b2 = mkblk();
	b2.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(16)));
	b2.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(16)));
	opt(b2);
	CHECK(count_op(b2, shop_shl) == 0 && find_mov_imm(b2, reg_r4, 0) >= 0);

	DecodedBlock b3 = mkblk();
	b3.oplist.push_back(OP(shop_sar, R(reg_r4), R(reg_r4), I(20)));
	b3.oplist.push_back(OP(shop_sar, R(reg_r4), R(reg_r4), I(20)));
	opt(b3);
	CHECK(count_op(b3, shop_sar) == 1 && find_op(b3, shop_sar)->rs2._imm == 31);

	DecodedBlock b4 = mkblk();
	b4.oplist.push_back(OP(shop_ror, R(reg_r4), R(reg_r4), I(16)));
	b4.oplist.push_back(OP(shop_ror, R(reg_r4), R(reg_r4), I(16)));
	opt(b4);
	CHECK(b4.oplist.empty());

	// A reader in between sees the intermediate value: no merge.
	DecodedBlock b5 = mkblk();
	b5.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(16)));
	b5.oplist.push_back(OP(shop_add, R(reg_r5), R(reg_r4), I(1)));
	b5.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(8)));
	opt(b5);
	CHECK(count_op(b5, shop_shl) == 2);

	// Mixed kinds never merge (shll16; shlr16 is a zero-extend).
	DecodedBlock b6 = mkblk();
	b6.oplist.push_back(OP(shop_shl, R(reg_r4), R(reg_r4), I(16)));
	b6.oplist.push_back(OP(shop_shr, R(reg_r4), R(reg_r4), I(16)));
	opt(b6);
	CHECK(count_op(b6, shop_shl) == 1 && count_op(b6, shop_shr) == 1);
}

static Interp init_state(u32 seed)
{
	Interp s;
	for (u32 k = 0; k < (u32)sh4_reg_count; k++)
		s.r[k] = hash32(seed * 977 + k);
	s.r[reg_sr_T] &= 1;
	for (u32 k = 0; k < 8; k++)
		s.bank[k] = hash32(seed * 31 + 500 + k);
	return s;
}

static bool equivalent(const DecodedBlock& before, const DecodedBlock& after, u32 seed)
{
	Interp a = init_state(seed), b = init_state(seed);
	a.run(before.oplist);
	b.run(after.oplist);
	return same_run(a, b, before, after);
}

static void t_ror_pending_source()
{
	// A full rotation becomes a move from the first ror's source, which got a newer constant in between.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_ror, R(reg_r4), R(reg_r5), I(16)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r5), I(3)));
	b.oplist.push_back(OP(shop_ror, R(reg_r4), R(reg_r4), I(16)));
	const DecodedBlock before = b;
	opt(b);
	CHECK(equivalent(before, b, 1));
}

static void t_cr0_order()
{
	// The block's last compare stays last: the pending constants go before it.
	DecodedBlock b = mkblk(BET_Cond_1);
	b.oplist.push_back(OP(shop_mov32, R(reg_r1), I(5)));
	b.oplist.push_back(OP(shop_mov32, R(reg_r2), I(0x12345678)));
	b.oplist.push_back(OP(shop_setgt, R(reg_sr_T), R(reg_r3), R(reg_r4)));
	const DecodedBlock before = b;
	opt(b);
	CHECK(b.oplist.back().op == shop_setgt);
	CHECK(find_mov_imm(b, reg_r1, 5) >= 0 && find_mov_imm(b, reg_r2, 0x12345678) >= 0);
	CHECK(equivalent(before, b, 2));

	// It reads r1 as a register and r1 changes after it: that write-back must follow it.
	DecodedBlock c = mkblk(BET_Cond_0);
	c.oplist.push_back(OP(shop_mov32, R(reg_r1), I(5)));
	c.oplist.push_back(OP(shop_setgt, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	c.oplist.push_back(OP(shop_mov32, R(reg_r1), I(7)));
	const DecodedBlock cbefore = c;
	opt(c);
	CHECK(c.oplist.back().op == shop_mov32);
	CHECK(equivalent(cbefore, c, 3));
}

static void t_dve()
{
	// Overwritten pure values die; the last write of each register lives (the next block may read it).
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_add, R(reg_r0), R(reg_r1), R(reg_r2)));
	b.oplist.push_back(OP(shop_add, R(reg_r0), R(reg_r3), R(reg_r4)));
	b.oplist.push_back(OP(shop_test, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	b.oplist.push_back(OP(shop_test, R(reg_sr_T), R(reg_r3), R(reg_r4)));
	shil_dead_values(&b);
	CHECK(b.oplist.size() == 2 && b.oplist[0].rs1._reg == reg_r3 && b.oplist[1].rs1._reg == reg_r3);

	// A readm is never removed, but its write still kills the value before it.
	DecodedBlock b2 = mkblk();
	b2.oplist.push_back(OP(shop_mov32, R(reg_r0), R(reg_r5)));
	b2.oplist.push_back(OP(shop_readm, R(reg_r0), R(reg_r8), P(), 4));
	shil_dead_values(&b2);
	CHECK(b2.oplist.size() == 1 && b2.oplist[0].op == shop_readm);

	// A barrier may read anything.
	DecodedBlock b3 = mkblk();
	b3.oplist.push_back(OP(shop_add, R(reg_r0), R(reg_r1), R(reg_r2)));
	b3.oplist.push_back(OP(shop_ifb, P(), I(0), I(0x8C000000), 0, I(0)));
	b3.oplist.push_back(OP(shop_add, R(reg_r0), R(reg_r3), R(reg_r4)));
	shil_dead_values(&b3);
	CHECK(count_op(b3, shop_add) == 2);

	// A two-result op dies only when both results are dead.
	DecodedBlock b4 = mkblk();
	b4.oplist.push_back(OP(shop_mul_u64, R(reg_macl), R(reg_r1), R(reg_r2), 0, P(), R(reg_mach)));
	b4.oplist.push_back(OP(shop_mov32, R(reg_macl), R(reg_r3)));
	shil_dead_values(&b4);
	CHECK(count_op(b4, shop_mul_u64) == 1);
	b4.oplist.push_back(OP(shop_mov32, R(reg_mach), R(reg_r4)));
	shil_dead_values(&b4);
	CHECK(count_op(b4, shop_mul_u64) == 0);

	// A dead chain collapses: the consumer dies first, then its feeder.
	DecodedBlock b6 = mkblk();
	b6.oplist.push_back(OP(shop_add, R(reg_r0), R(reg_r1), R(reg_r2)));
	b6.oplist.push_back(OP(shop_shl, R(reg_r0), R(reg_r0), I(4)));
	b6.oplist.push_back(OP(shop_mov32, R(reg_r0), R(reg_r3)));
	shil_dead_values(&b6);
	CHECK(b6.oplist.size() == 1 && b6.oplist[0].op == shop_mov32);

	// div1 reads SR.Q/M: div0u's clear of sr_status stays although ldc's write (no read) covers it later.
	DecodedBlock b7 = mkblk();
	b7.oplist.push_back(OP(shop_and, R(reg_sr_status), R(reg_sr_status), I(~0x300u)));
	b7.oplist.push_back(OP(shop_div1, R(reg_r1), R(reg_r1), R(reg_r2), 0, R(reg_sr_T), R(reg_sr_T)));
	b7.oplist.push_back(OP(shop_and, R(reg_sr_status), R(reg_r3), I(0x700083F2)));
	shil_dead_values(&b7);
	CHECK(count_op(b7, shop_and) == 2);

	// mac.l adds to MACH:MACL: clrmac's two moves stay.
	DecodedBlock b8 = mkblk();
	b8.oplist.push_back(OP(shop_mov32, R(reg_mach), I(0)));
	b8.oplist.push_back(OP(shop_mov32, R(reg_macl), I(0)));
	b8.oplist.push_back(OP(shop_mac_l, R(reg_macl), R(reg_temp), R(reg_temp2), 0, R(reg_sr_status), R(reg_mach)));
	shil_dead_values(&b8);
	CHECK(count_op(b8, shop_mov32) == 2);

	// A vector result covers several registers: fsca writes fr2 and fr3, so fr2's fadd is dead.
	DecodedBlock b9 = mkblk();
	b9.oplist.push_back(OP(shop_fadd, R(reg_fr_2), R(reg_fr_4), R(reg_fr_5)));
	b9.oplist.push_back(OP(shop_fadd, R(reg_fr_6), R(reg_fr_4), R(reg_fr_5)));
	b9.oplist.push_back(OP(shop_fsca, DR(1), R(reg_fpul)));
	b9.oplist.push_back(OP(shop_ftrv, FV(1), FV(1), XMTRX()));
	shil_dead_values(&b9);
	CHECK(count_op(b9, shop_fadd) == 1 && b9.oplist[0].rd._reg == reg_fr_6);
}

// ---- fuzzer ---------------------------------------------------------------------

static u32 g_opt_blocks, g_promoted, g_cr0_kept, g_cr0_lost;

struct Gen
{
	std::mt19937 rng;
	vector<u32> pool;	// addresses blocks share, so loads see stores
	explicit Gen(u32 seed) : rng(seed) {}
	u32 rnd(u32 n) { return rng() % n; }
	u32 r32() { return rng(); }
	// r0-r7 most of the time, so values meet; r11 is the one GPR the PS3 does not pin.
	u32 gp() { return rnd(4) ? reg_r0 + rnd(8) : reg_r0 + rnd(16); }
	u32 fr() { return reg_fr_0 + rnd(16); }
	u32 konst()
	{
		switch (rnd(9))
		{
		case 0: return rnd(64);
		case 1: return (u32)-(s32)(1 + rnd(64));
		case 2: return (u32)((s32)rnd(80) - 40);       // shld/shad counts, both signs
		case 3: return r32();
		case 4: return pool[rnd(pool.size())];
		case 5: return 0xE0000000 + 4 * rnd(32);        // store queue
		case 6: return 0x40 * rnd(16);                  // r0 displacements
		case 7: return 0xFFFF0000 + rnd(0x10000);
		default: return 1u << rnd(32);
		}
	}
	u32 addr() { return pool[rnd(pool.size())] + 4 * rnd(4); }
	u32 size() { static const u32 s[] = {1, 2, 4}; return s[rnd(3)]; }

	// One shape the decoder emits.
	void op(DecodedBlock& b)
	{
		auto& L = b.oplist;
		const u32 rn = gp(), rm = gp();
		switch (rnd(46))
		{
		case 0: case 1: case 2: L.push_back(OP(shop_mov32, R(rn), I(konst()))); break;   // mov #imm / mova
		case 3: L.push_back(OP(shop_mov32, R(rn), I(addr()))); break;
		case 4: L.push_back(OP(shop_mov32, R(rn), R(rm))); break;
		case 5: L.push_back(OP(shop_mov32, R(rn), R(reg_sr_T))); break;                    // movt
		case 6: L.push_back(OP(shop_mov32, R(rnd(2) ? reg_macl : reg_fpul), R(rn))); break; // lds
		case 7: L.push_back(OP(shop_mov32, R(rn), R(rnd(2) ? reg_mach : reg_pr))); break;   // sts
		case 8:
		{
			static const shilop alu[] = {shop_add, shop_sub, shop_and, shop_or, shop_xor};
			L.push_back(OP(alu[rnd(5)], R(rn), R(rn), rnd(2) ? R(rm) : I(konst())));
			break;
		}
		case 9: L.push_back(OP(shop_add, R(rn), R(rn), I((u32)(s32)(s8)r32()))); break;    // add #simm8
		case 10: L.push_back(OP(shop_and, R(rn), R(rm), I(rnd(2) ? 0xFF : 0xFFFF))); break; // extu
		case 11:
		{
			static const shilop un[] = {shop_not, shop_neg, shop_ext_s8, shop_ext_s16, shop_swaplb};
			L.push_back(OP(un[rnd(5)], R(rn), R(rm)));
			break;
		}
		case 12: case 13:
		{
			// shll / shlr / shal / shar set T first; shll2/8/16 and shlr2/8/16 do not.
			static const u32 amt[] = {1, 2, 8, 16};
			const u32 e = amt[rnd(4)];
			const shilop s = rnd(3) == 0 ? shop_sar : rnd(2) ? shop_shl : shop_shr;
			if (e == 1)
				L.push_back(s == shop_shl ? OP(shop_shr, R(reg_sr_T), R(rn), I(31)) : OP(shop_and, R(reg_sr_T), R(rn), I(1)));
			L.push_back(OP(s, R(rn), R(rn), I(e)));
			break;
		}
		case 14:
		{
			// chains like shll16 + shll8 + shll2; a count of 0 is a legal form too
			static const shilop sh[] = {shop_shl, shop_shr, shop_sar, shop_ror};
			const shilop s = sh[rnd(4)];
			for (u32 i = 0, n = 2 + rnd(3); i < n; i++)
				L.push_back(OP(s, R(rn), R(rn), I(rnd(8) ? 1 + rnd(31) : 0)));
			break;
		}
		case 15:
		{
			// rotl / rotr / swap.w, and swap.w twice (a full rotation) with rm reloaded in between
			const u32 k = rnd(4);
			if (k == 0) { L.push_back(OP(shop_shr, R(reg_sr_T), R(rm), I(31))); L.push_back(OP(shop_ror, R(rn), R(rm), I(31))); }
			if (k == 1) { L.push_back(OP(shop_and, R(reg_sr_T), R(rm), I(1))); L.push_back(OP(shop_ror, R(rn), R(rm), I(1))); }
			if (k == 2) L.push_back(OP(shop_ror, R(rn), R(rm), I(16)));
			if (k == 3)
			{
				L.push_back(OP(shop_ror, R(rn), R(rm), I(16)));
				if (rnd(2))
					L.push_back(OP(shop_mov32, R(rm), I(konst())));
				L.push_back(OP(shop_ror, R(rn), R(rn), I(16)));
			}
			break;
		}
		case 16: case 17:
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rm), I(konst())));
			L.push_back(OP(rnd(2) ? shop_shld : shop_shad, R(rn), R(rn), R(rm)));
			break;
		case 18:
		{
			static const shilop ml[] = {shop_mul_i32, shop_mul_u16, shop_mul_s16};
			L.push_back(OP(ml[rnd(3)], R(reg_macl), R(rn), R(rm)));
			break;
		}
		case 19: L.push_back(OP(rnd(2) ? shop_mul_u64 : shop_mul_s64, R(reg_macl), R(rn), R(rm), 0, P(), R(reg_mach))); break;
		case 20: case 21:
		{
			static const shilop cm[] = {shop_test, shop_seteq, shop_setge, shop_setgt, shop_setae, shop_setab};
			L.push_back(OP(cm[rnd(6)], R(reg_sr_T), R(rn), rnd(2) ? R(rm) : I(rnd(2) ? 0 : konst())));
			break;
		}
		case 22: L.push_back(OP(shop_cmpstr, R(reg_sr_T), R(rn), R(rm))); break;
		case 23:   // dt
			L.push_back(OP(shop_sub, R(rn), R(rn), I(1)));
			L.push_back(OP(shop_seteq, R(reg_sr_T), R(rn), I(0)));
			break;
		case 24:
		{
			const u32 k = rnd(4);
			if (k == 0) L.push_back(OP(rnd(2) ? shop_adc : shop_sbc, R(rn), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T)));
			if (k == 1) L.push_back(OP(shop_negc, R(rn), R(rm), R(reg_sr_T), 0, P(), R(reg_sr_T)));
			if (k == 2) L.push_back(OP(rnd(2) ? shop_addv : shop_subv, R(rn), R(rn), R(rm), 0, P(), R(reg_sr_T)));
			if (k == 3) L.push_back(OP(shop_div1, R(rn), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T)));
			break;
		}
		case 25:   // div0u, div0s, then sometimes the div1 steps MatchDiv32 did not take
			L.push_back(OP(shop_and, R(reg_sr_status), R(reg_sr_status), I(~0x300u)));
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(reg_sr_T), I(0)));
			else
			{
				L.push_back(OP(shop_sar, R(reg_sr_T), R(rn), I(31)));
				L.push_back(OP(shop_and, R(reg_sr_T), R(reg_sr_T), I(1 << 8)));
				L.push_back(OP(shop_or, R(reg_sr_status), R(reg_sr_status), R(reg_sr_T)));
				L.push_back(OP(shop_xor, R(reg_sr_T), R(rn), R(rm)));
				L.push_back(OP(shop_shr, R(reg_sr_T), R(reg_sr_T), I(31)));
			}
			for (u32 i = 0, n = rnd(2) ? 1 + rnd(3) : 0; i < n; i++)
				L.push_back(OP(shop_div1, R(rn), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T)));
			break;
		case 26:
		{
			// the matched 32-bit division
			u32 r1 = gp(), r2 = gp(), r3 = gp();
			if (r2 == r1) r2 = r1 ^ 1;
			if (r3 == r1 || r3 == r2) r3 = reg_r15 - (r1 & 3);
			L.push_back(OP(rnd(2) ? shop_div32u : shop_div32s, R(r1), R(r1), R(r2), 0, P(), R(r3)));
			L.push_back(OP(shop_and, R(reg_sr_T), R(r1), I(1)));
			L.push_back(OP(rnd(2) ? shop_shr : shop_sar, R(r1), R(r1), I(1)));
			L.push_back(OP(shop_div32p2, R(r3), R(r3), R(r2), 0, R(reg_sr_T)));
			break;
		}
		case 27:   // mac.l / mac.w, clrmac sometimes first
			if (rnd(2))
			{
				L.push_back(OP(shop_mov32, R(reg_mach), I(0)));
				L.push_back(OP(shop_mov32, R(reg_macl), I(0)));
			}
			{
				const u32 e = rnd(2) ? 4 : 2;
				L.push_back(OP(shop_readm, R(reg_temp), R(rm), P(), e));
				L.push_back(OP(shop_add, R(rm), R(rm), I(e)));
				L.push_back(OP(shop_readm, R(reg_temp2), R(rn), P(), e));
				L.push_back(OP(shop_add, R(rn), R(rn), I(e)));
				L.push_back(OP(e == 4 ? shop_mac_l : shop_mac_w, R(reg_macl), R(reg_temp), R(reg_temp2), 0, R(reg_sr_status), R(reg_mach)));
			}
			break;
		case 28:   // xtrct
			L.push_back(OP(shop_shr, R(reg_temp), R(rn), I(16)));
			L.push_back(OP(shop_shl, R(rn), R(rm), I(16)));
			L.push_back(OP(shop_or, R(rn), R(rn), R(reg_temp)));
			break;
		case 29:   // rotcl / rotcr
			if (rnd(2))
			{
				L.push_back(OP(shop_mov32, R(reg_temp), R(reg_sr_T)));
				L.push_back(OP(shop_shr, R(reg_sr_T), R(rn), I(31)));
				L.push_back(OP(shop_shl, R(rn), R(rn), I(1)));
				L.push_back(OP(shop_or, R(rn), R(rn), R(reg_temp)));
			}
			else
			{
				L.push_back(OP(shop_shl, R(reg_temp), R(reg_sr_T), I(31)));
				L.push_back(OP(shop_and, R(reg_sr_T), R(rn), I(1)));
				L.push_back(OP(shop_shr, R(rn), R(rn), I(1)));
				L.push_back(OP(shop_or, R(rn), R(rn), R(reg_temp)));
			}
			break;
		case 30: case 31: case 32:
		{
			// loads: @Rm, @(disp,Rm), @(R0,Rm), @Rm+ and PC-relative literals
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rm), I(addr())));
			const u32 k = rnd(5), sz = size();
			if (k == 0) L.push_back(OP(shop_readm, R(rn), R(rm), P(), sz));
			if (k == 1) L.push_back(OP(shop_readm, R(rn), R(rm), P(), sz, I(sz * rnd(16))));
			if (k == 2)
			{
				if (rnd(2))
					L.push_back(OP(shop_mov32, R(reg_r0), I(konst())));
				L.push_back(OP(shop_readm, R(rn), R(rm), P(), sz, R(reg_r0)));
			}
			if (k == 3)
			{
				L.push_back(OP(shop_readm, R(rn), R(rm), P(), sz));
				if (rn != rm)
					L.push_back(OP(shop_add, R(rm), R(rm), I(sz)));
			}
			if (k == 4) L.push_back(OP(shop_readm, R(rn), I(addr()), P(), rnd(2) ? 4 : 2));
			break;
		}
		case 33: case 34:
		{
			// stores: @Rn, @(disp,Rn), @(R0,Rn), @-Rn
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rn), I(addr())));
			const u32 k = rnd(4), sz = size();
			if (k == 0) L.push_back(OP(shop_writem, P(), R(rn), R(rm), sz));
			if (k == 1) L.push_back(OP(shop_writem, P(), R(rn), R(rm), sz, I(sz * rnd(16))));
			if (k == 2)
			{
				if (rnd(2))
					L.push_back(OP(shop_mov32, R(reg_r0), I(konst())));
				L.push_back(OP(shop_writem, P(), R(rn), R(rm), sz, R(reg_r0)));
			}
			if (k == 3)
			{
				if (rn != rm)
				{
					L.push_back(OP(shop_sub, R(rn), R(rn), I(sz)));
					L.push_back(OP(shop_writem, P(), R(rn), R(rm), sz));
				}
				else
				{
					L.push_back(OP(shop_writem, P(), R(rn), R(rm), sz, I((u32)-(s32)sz)));
					L.push_back(OP(shop_sub, R(rn), R(rn), I(sz)));
				}
			}
			break;
		}
		case 35:   // GBR loads and stores
			if (rnd(2))
				L.push_back(OP(shop_readm, R(reg_r0), R(reg_gbr), P(), 4, I(4 * rnd(64))));
			else
				L.push_back(OP(shop_writem, P(), R(reg_gbr), R(reg_r0), 4, I(4 * rnd(64))));
			break;
		case 36:   // tas.b
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rn), I(addr())));
			L.push_back(OP(shop_readm, R(reg_temp), R(rn), P(), 1));
			L.push_back(OP(shop_seteq, R(reg_sr_T), R(reg_temp), I(0)));
			L.push_back(OP(shop_or, R(reg_temp), R(reg_temp), I(0x80)));
			L.push_back(OP(shop_writem, P(), R(rn), R(reg_temp), 1));
			break;
		case 37:   // pref, sometimes to a known address
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rn), I(rnd(2) ? 0xE0000000 + 32 * rnd(64) : addr())));
			L.push_back(OP(shop_pref, P(), R(rn)));
			break;
		case 38:
			L.push_back(OP(shop_ifb, P(), I(rnd(2)), I(r32()), 0, I(r32() & 0xFFFF)));
			break;
		case 39:   // fschg / frchg
			L.push_back(OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(rnd(2) ? 1 << 20 : 1 << 21)));
			L.push_back(OP(shop_sync_fpscr));
			break;
		case 40:
		{
			static const shilop fb[] = {shop_fadd, shop_fsub, shop_fmul, shop_fdiv};
			L.push_back(OP(fb[rnd(4)], R(fr()), R(fr()), R(fr())));
			break;
		}
		case 41:
		{
			const u32 k = rnd(6);
			if (k == 0) L.push_back(OP(shop_mov32, R(fr()), I(rnd(2) ? 0 : 0x3F800000)));   // fldi
			if (k == 1) L.push_back(OP(shop_mov32, R(fr()), R(fr())));
			if (k == 2) L.push_back(OP(shop_mov32, R(reg_fpul), R(fr())));
			if (k == 3) L.push_back(OP(shop_mov32, R(fr()), R(reg_fpul)));
			if (k == 4) L.push_back(OP(shop_cvt_f2i_t, R(reg_fpul), R(fr())));
			if (k == 5) L.push_back(OP(shop_cvt_i2f_n, R(fr()), R(reg_fpul)));
			break;
		}
		case 42:
		{
			const u32 k = rnd(5), n = rnd(4);
			if (k == 0) L.push_back(OP(shop_ftrv, FV(n), FV(n), XMTRX()));
			if (k == 1) L.push_back(OP(shop_fipr, R(reg_fr_0 + 4 * n + 3), FV(n), FV(rnd(4))));
			if (k == 2) L.push_back(OP(shop_fsca, DR(rnd(8)), R(reg_fpul)));
			if (k == 3) L.push_back(OP(shop_fmac, R(fr()), R(fr()), R(fr()), 0, R(reg_fr_0)));
			if (k == 4) L.push_back(OP(rnd(2) ? shop_fseteq : shop_fsetgt, R(reg_sr_T), R(fr()), R(fr())));
			break;
		}
		case 43:   // float loads and stores, single and pair
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rn), I(addr())));
			if (rnd(2))
				L.push_back(OP(shop_readm, rnd(2) ? R(fr()) : DR(rnd(8)), R(rn), P(), 4));
			else
				L.push_back(OP(shop_writem, P(), R(rn), R(fr()), 4, rnd(2) ? P() : R(reg_r0)));
			if (L.back().rd.is_r64f())
				L.back().flags = 8;
			break;
		case 44:   // fmov.d: 64-bit moves and transfers
			if (rnd(2))
				L.push_back(OP(shop_mov64, DR(rnd(8)), DR(rnd(8))));
			else
				L.push_back(OP(shop_writem, P(), R(rn), DR(rnd(8)), 8));
			break;
		default:   // stc SR / stc.l SR, @-Rn
			if (rnd(2))
			{
				L.push_back(OP(shop_mov32, R(rn), R(reg_sr_status)));
				L.push_back(OP(shop_or, R(rn), R(rn), R(reg_sr_T)));
			}
			else
			{
				L.push_back(OP(shop_mov32, R(reg_temp), R(reg_sr_status)));
				L.push_back(OP(shop_or, R(reg_temp), R(reg_temp), R(reg_sr_T)));
				L.push_back(OP(shop_writem, P(), R(rn), R(reg_temp), 4, I((u32)-4)));
				L.push_back(OP(shop_add, R(rn), R(rn), I((u32)-4)));
			}
			break;
		}
	}

	// The block end, as dec_End and the branch handlers leave it.
	void end(DecodedBlock& b)
	{
		auto& L = b.oplist;
		const u32 rn = gp();
		switch (rnd(9))
		{
		case 0:   // bra + delay slot, or the cycle limit
			b.BlockType = BET_StaticJump;
			if (rnd(2)) op(b);
			break;
		case 1:   // bsr
			b.BlockType = BET_StaticCall;
			L.push_back(OP(shop_mov32, R(reg_pr), I(b.start + 0x40)));
			if (rnd(2)) op(b);
			break;
		case 2: case 3:   // jmp / braf / jsr / bsrf with a delay slot
		{
			const bool call = rnd(2);
			b.BlockType = call ? BET_DynamicCall : BET_DynamicJump;
			if (call)
				L.push_back(OP(shop_mov32, R(reg_pr), I(b.start + 0x40)));
			if (rnd(2))
				L.push_back(OP(shop_mov32, R(rn), I(0x8C000000 + (r32() & 0xFFFFFE))));
			L.push_back(OP(shop_jdyn, R(reg_pc_dyn), R(rn), rnd(2) ? I(b.start + 4 * rnd(64)) : P()));
			if (rnd(2)) op(b);
			break;
		}
		case 4:   // rts
			b.BlockType = BET_DynamicRet;
			L.push_back(OP(shop_jdyn, R(reg_pc_dyn), R(reg_pr)));
			if (rnd(2)) op(b);
			break;
		case 5: case 6:   // bt / bf: often a compare right before, sometimes more ops after it
		{
			b.BlockType = rnd(2) ? BET_Cond_1 : BET_Cond_0;
			static const shilop cm[] = {shop_test, shop_seteq, shop_setge, shop_setgt, shop_setae, shop_setab, shop_cmpstr};
			if (rnd(4))
			{
				const shilop c = cm[rnd(7)];
				L.push_back(OP(c, R(reg_sr_T), R(gp()), c != shop_cmpstr && rnd(2) ? I(konst()) : R(gp())));
			}
			for (u32 i = 0, n = rnd(4) == 0 ? 1 + rnd(2) : 0; i < n; i++)
				L.push_back(OP(shop_mov32, R(gp()), I(konst())));
			break;
		}
		case 7:   // bt.s / bf.s
			b.BlockType = rnd(2) ? BET_Cond_1 : BET_Cond_0;
			L.push_back(OP(shop_seteq, R(reg_sr_T), R(gp()), rnd(2) ? I(konst()) : R(gp())));
			L.push_back(OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)));
			if (rnd(2)) op(b);
			break;
		default:  // ldc Rn,SR or rte
			if (rnd(2))
			{
				b.BlockType = BET_StaticIntr;
				L.push_back(OP(shop_and, R(reg_sr_status), R(rn), I(0x700083F2)));
				L.push_back(OP(shop_and, R(reg_sr_T), R(rn), I(1)));
				L.push_back(OP(shop_sync_sr));
			}
			else
			{
				b.BlockType = BET_DynamicIntr;
				L.push_back(OP(shop_and, R(reg_sr_status), R(reg_ssr), I(0x700083F2)));
				L.push_back(OP(shop_and, R(reg_sr_T), R(reg_ssr), I(1)));
				L.push_back(OP(shop_sync_sr));
				L.push_back(OP(shop_jdyn, R(reg_pc_dyn), R(reg_spc)));
				if (rnd(2)) op(b);
			}
			break;
		}
	}

	void run_one(u32 iter)
	{
		pool.clear();
		for (u32 i = 0; i < 6; i++)
			pool.push_back((rnd(2) ? 0x8C000000 : 0xA0000000) + (r32() & 0xFFFFC0));

		DecodedBlock b = mkblk();
		b.start = 0x8C000000 + (r32() & 0xFFFFFE);
		b.BranchBlock = 0x8C000000 + (r32() & 0xFFFFFE);
		for (u32 i = 0, n = 2 + rnd(30); i < n; i++)
			op(b);
		end(b);

		const bool static_ends = rnd(5) != 0;
		const bool use_cp = iter % 8 != 7, use_dve = iter % 8 != 6;
		const DecodedBlock orig = b;
		const ShilOptStats s0 = shil_opt_stats;
		if (use_cp)
			shil_constprop(&b, static_ends);
		if (use_dve)
			shil_dead_values(&b);

		bool ok = true;
		if (use_cp && !use_dve && b.oplist.size() > orig.oplist.size())
		{
			printf("constprop grew the block: %zu -> %zu\n", orig.oplist.size(), b.oplist.size());
			ok = false;
		}
		if (!static_ends && b.BlockType != orig.BlockType)
		{
			printf("static end with OnlyDynamicEnds\n");
			ok = false;
		}

		// JIT CR0 / T FORWARD: a block that ended on its compare still does, unless it was folded or clashed.
		const shil_opcode* ol = orig.oplist.empty() ? nullptr : &orig.oplist.back();
		if (ok && use_cp && ol && (orig.BlockType == BET_Cond_0 || orig.BlockType == BET_Cond_1) &&
		    ol->rd.is_reg() && ol->rd._reg == reg_sr_T && ol->op != shop_mov32)
		{
			const shil_opcode& nl = b.oplist.back();
			if (nl.rd.is_reg() && nl.rd._reg == reg_sr_T)
				g_cr0_kept++;
			else if (shil_opt_stats.late != s0.late || shil_opt_stats.fold != s0.fold)
				g_cr0_lost++;
			else
			{
				printf("compare no longer last\n");
				ok = false;
			}
		}

		for (u32 k = 0; ok && k < 3; k++)
		{
			Interp x = init_state(iter * 3 + k), y = init_state(iter * 3 + k);
			// some starts put pool addresses in the registers a block loads through
			for (u32 r = reg_r0; r <= reg_r15; r++)
				if (rnd(2))
					x.r[r] = y.r[r] = addr();
			x.run(orig.oplist);
			y.run(b.oplist);
			ok = same_run(x, y, orig, b);
		}

		if (!ok)
		{
			printf("iteration %u (cp %d, dve %d, static ends %d)\n", iter, use_cp, use_dve, static_ends);
			dump_block("original", orig);
			dump_block("optimized", b);
			fflush(stdout);
			abort();
		}
		check_ppc_forms(b);
		g_opt_blocks += b.oplist.size() < orig.oplist.size();
		g_promoted += b.BlockType != orig.BlockType;
	}
};

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? (u32)strtoul(argv[1], nullptr, 0) : 1;
	const u32 iters = argc > 2 ? (u32)strtoul(argv[2], nullptr, 0) : 200000;

	// CP_FUZZ_ONLY: mutate.py checks that the fuzzer alone catches a planted bug.
	if (!getenv("CP_FUZZ_ONLY"))
	{
		t_rd2_kill();
		t_writem_forms();
		t_shld_shad();
		t_sync_sr();
		t_commutative_swap();
		t_writeback_dedup();
		t_readm_promote();
		t_readm_rs3();
		t_pref();
		t_cmp_fold();
		t_jdyn();
		t_f32_mov();
		t_shift_chains();
		t_ror_pending_source();
		t_cr0_order();
		t_dve();
		printf("directed tests: ok\n");
	}

	memset(&shil_opt_stats, 0, sizeof(shil_opt_stats));
	Gen g(seed);
	for (u32 i = 0; i < iters; i++)
		g.run_one(i);

	const ShilOptStats& s = shil_opt_stats;
	printf("fuzz: %u blocks ok, %u smaller, %u static ends | fold %u imm %u shift %u chain %u readm %u pref %u"
	       " late %u dve %u | cr0 kept %u lost %u\n",
	       iters, g_opt_blocks, g_promoted, s.fold, s.imm, s.shift, s.chain, s.readm, s.pref, s.late, s.dve,
	       g_cr0_kept, g_cr0_lost);
	// Each transform must have fired, or the fuzzer proves nothing about it.
	CHECK(g_opt_blocks > iters / 4 && g_promoted > 0);
	CHECK(s.fold && s.imm && s.shift && s.chain && s.readm && s.pref && s.late && s.dve);
	printf("PASS\n");
	return 0;
}
