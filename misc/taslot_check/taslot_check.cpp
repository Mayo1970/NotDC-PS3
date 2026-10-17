// Host-only check for P87 JIT TA SLOT: ps3_jit_taslot.h's plan on blocks run by constprop_check's SHIL interpreter,
// with a model of the slot (r11), the PCW (r12), the cold paths and the flush; its emitters on a PPC64 model.
#define main constprop_main
#include "../constprop_check/constprop_check.cpp"
#undef main
#include <array>

static u32 g_code[64];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }
#include "emitter/PPCEmit/ppc_emitter.h"
void ppc_bx(void* dst, u32 LK)
{
	const snat offs = (u8*)dst - (u8*)emit_GetCCPtr();
	ppc_bx((u32)(offs >> 2), 0, LK);
}
#include "ps3/ps3_jit_sq.h"

// mutate.py swaps in an edited copy.
#ifndef TS_HEADER
#define TS_HEADER "ps3/ps3_jit_taslot.h"
#endif
#include TS_HEADER

// ---- what the driver does, written apart from the header --------------------------------------------------------

// The ops ps3_driver.cpp's switch emits itself; any other takes the canonical C fallback.
static bool native(shilop op)
{
	switch (op)
	{
	case shop_readm: case shop_writem: case shop_ifb: case shop_jdyn: case shop_jcond: case shop_mov64:
	case shop_mov32: case shop_add: case shop_sub: case shop_or: case shop_and: case shop_xor: case shop_shl:
	case shop_shr: case shop_sar: case shop_mul_i32: case shop_fadd: case shop_fsub: case shop_fmul: case shop_fdiv:
	case shop_mul_u16: case shop_mul_s16: case shop_mul_u64: case shop_mul_s64: case shop_div32u: case shop_div32s:
	case shop_div32p2: case shop_ror: case shop_shld: case shop_shad: case shop_adc: case shop_sbc: case shop_negc:
	case shop_addv: case shop_subv: case shop_cmpstr: case shop_swaplb: case shop_div1: case shop_mac_l:
	case shop_mac_w: case shop_test: case shop_seteq: case shop_setgt: case shop_setge: case shop_setab:
	case shop_setae: case shop_neg: case shop_not: case shop_ext_s8: case shop_ext_s16: case shop_fabs:
	case shop_fneg: case shop_fseteq: case shop_fsetgt: case shop_cvt_f2i_t: case shop_cvt_i2f_n:
	case shop_cvt_i2f_z: case shop_fmac: case shop_fipr: case shop_ftrv: case shop_fsca: case shop_sync_sr:
	case shop_sync_fpscr: case shop_pref: case shop_fsqrt: case shop_fsrra:
		return true;
	default:
		return false;
	}
}

// A C call on the op's own path (memory ops: see run_block), as the driver emits it.
static bool calls_c(const shil_opcode& o, bool fsqrt_inline, bool fschg_inline)
{
	if (!native(o.op))
		return true;
	switch (o.op)
	{
	case shop_ifb: case shop_mac_l: case shop_mac_w: case shop_sync_sr:
		return true;
	case shop_sync_fpscr:
		return !(fschg_inline && (o.flags & SYNC_FPSCR_SZ_ONLY));
	case shop_fsqrt: case shop_fsrra:
		return !fsqrt_inline;
	default:
		return false;
	}
}

// JIT SQ STORES' guess (sq_store_guess): a later PREF in the block through the store's base register.
static bool sq_guess(const DecodedBlock& b, size_t i)
{
	const shil_opcode& st = b.oplist[i];
	if (!st.rs1.is_reg() || (st.flags != 4 && st.flags != 8))
		return false;
	for (size_t j = i + 1; j < b.oplist.size(); j++)
		if (b.oplist[j].op == shop_pref && b.oplist[j].rs1.is_reg() && b.oplist[j].rs1._reg == st.rs1._reg)
			return true;
	return false;
}

static bool ram_ok(u32 ea) { return ((ea >> 26) & 7) == 3 && (ea >> 29) != 7; }
static bool sq_ok(u32 ea) { return (ea >> 26) == 0x38; }

// ---- the model -------------------------------------------------------------------------------------------------

enum { SL_GARBAGE = -3, SL_POISON = -2, SL_SPARE = -1 };   // r11; >= 0: a record of the TA buffer
static const u32 CAP = 40;                                  // records the buffer holds

static u32 g_blocks, g_groups, g_hits, g_copies, g_poison_seen, g_inner, g_full_flush, g_ram_flush, g_masks_part;

struct Model
{
	u32 sq[16];
	u32 rec[CAP + 1][8];
	u32 raw;
	int r11;
	u32 r12;
	bool r12_ok;
	u32 seed;
	u32 rnd() { seed = hash32(seed + 0x9E3779B9u); return seed; }
	int slot_now() const { return raw < CAP ? (int)raw : SL_SPARE; }
};

[[noreturn]] static void fail(const char* what, const DecodedBlock& b, const TsPlan& p, int at)
{
	printf("FAIL %s at op %d\n", what, at);
	for (u32 i = 0; i < p.n; i++)
		printf("  op %u role %02X mask %02X\n", i, p.role[i], p.mask[i]);
	dump_block(what, b);
	fflush(stdout);
	exit(1);
}

// The roles fit together: a store role only on a 32-bit or pair store with the SQ shape; one FIRST and one END per
// window, END a PREF; PCW on a 32-bit store of an integer register; ops in a window are ones the driver can run there.
static void check_structure(const DecodedBlock& b, const TsPlan& p, bool fsqrt_inline, bool fschg_inline)
{
	if (p.n == 0)
		return;
	if (p.n != b.oplist.size())
		fail("plan size", b, p, -1);
	bool open = false;
	u32 groups = 0;
	for (u32 i = 0; i < p.n; i++)
	{
		const shil_opcode& o = b.oplist[i];
		const u8 r = p.role[i];
		if ((r & TS_FIRST) && (open || !(r & TS_STORE)))
			fail("FIRST inside a window or not a store", b, p, (int)i);
		if (r & TS_FIRST)
			open = true;
		if (!!(r & TS_WIN) != open)
			fail("WIN outside a window, or a window op without WIN", b, p, (int)i);
		if ((r & TS_STORE) && (o.op != shop_writem || !sq_guess(b, i)))
			fail("a slot store without the store-queue shape", b, p, (int)i);
		if ((r & TS_PCW) && (!(r & TS_STORE) || o.flags != 4 || o.rs2.is_r32f()))
			fail("PCW not from a 32-bit store of an integer register", b, p, (int)i);
		if (open && calls_c(o, fsqrt_inline, fschg_inline))
			fail("a C call inside a window", b, p, (int)i);
		if (open && o.op == shop_readm && o.rs1.is_imm())
			fail("a load from a fixed address inside a window", b, p, (int)i);
		if (r & TS_END)
		{
			if (o.op != shop_pref || !open)
				fail("END not a PREF closing a window", b, p, (int)i);
			open = false;
			groups++;
		}
		else if (p.mask[i])
			fail("a mask off an END", b, p, (int)i);
	}
	if (open)
		fail("a window with no END", b, p, -1);
	if (groups != p.groups)
		fail("groups count", b, p, -1);
}

static void cold(Model& m, u8 role)
{
	// The fragment of a window op poisons r11 on every exit; any C call takes r12.
	m.r11 = (role & TS_WIN) ? SL_POISON : SL_GARBAGE;
	m.r12_ok = false;
}

static void sq_store(Model& m, u32 a, u32 w0, u32 w1, bool pair)
{
	if (pair)
	{
		m.sq[(a & 0x38) / 4] = w0;
		m.sq[(a & 0x38) / 4 + 1] = w1;
	}
	else
		m.sq[(a & 0x3C) / 4] = w0;
}

// One flush of the queue at a, as the PREF's C call does it; target TA (a record), RAM or none.
static void flush(Model& m, const DecodedBlock& b, const TsPlan& p, u32 i, u32 a, bool ta)
{
	const u8 role = ts_role(p, i);
	u32 canon[8];
	memcpy(canon, &m.sq[(a & 0x20) / 4], 32);
	if (role & TS_END)
	{
		if (m.r11 == SL_GARBAGE)
			fail("the flush got a slot a C call clobbered", b, p, (int)i);
		g_poison_seen += m.r11 == SL_POISON;
		if (ta && m.raw < CAP && m.r11 == (int)m.raw && !(a & 0x1F))
		{
			const u32 mask = p.mask[i];
			u32 got[8];
			memcpy(got, m.rec[m.raw], 32);
			for (u32 k = 0; k < 8; k++)
				if (!(mask & (1u << k)))
					got[k] = canon[k];
			if (mask & 1)
			{
				if (!m.r12_ok)
					fail("the flush took word 0 from r12, which holds no PCW", b, p, (int)i);
				if (m.r12 != canon[0])
					fail("the PCW in r12 is not word 0", b, p, (int)i);
			}
			if (memcmp(got, canon, 32) != 0)
			{
				for (u32 k = 0; k < 8; k++)
					printf("  word %u: slot %08X queue %08X%s\n", k, got[k], canon[k], (mask >> k) & 1 ? " (mask)" : "");
				fail("the record in the slot is not the queue", b, p, (int)i);
			}
			g_hits++;
			g_masks_part += mask != 0xFF;
		}
		else
			g_copies++;
	}
	else
		g_inner += (role & TS_WIN) != 0;
	if (ta)
	{
		if (m.raw < CAP)
			memcpy(m.rec[m.raw++], canon, 32);
		else
			g_full_flush++;
	}
	else
		g_ram_flush++;
	// The driver poisons r11 after a flush inside a window; any other call leaves it clobbered.
	m.r11 = (role & (TS_WIN | TS_END)) == TS_WIN ? SL_POISON : SL_GARBAGE;
	m.r12_ok = false;
}

// Runs the block as the JIT would with the plan: SQ-shaped stores test for the queue, other memory ops for RAM; a
// failed test takes the cold path (a C call). Each record stored is checked against the queue at its flush.
static void run_block(const DecodedBlock& b, const TsPlan& p, Interp x, Model& m, bool fsqrt_inline, bool fschg_inline)
{
	for (u32 i = 0; i < (u32)b.oplist.size(); i++)
	{
		const shil_opcode& o = b.oplist[i];
		const u8 role = ts_role(p, i);
		if (role & TS_FIRST)
			m.r11 = m.slot_now();
		if (o.op == shop_writem)
		{
			const u32 a = x.in(o.rs1) + x.in0(o.rs3);
			const bool pair = o.flags == 8;
			const u32 w0 = x.in(o.rs2, 0), w1 = pair ? x.in(o.rs2, 1) : 0;
			const bool guess = sq_guess(b, i);
			const bool fast = guess ? sq_ok(a) : ram_ok(a);
			if (sq_ok(a) && (o.flags == 4 || pair))
				sq_store(m, a, w0, w1, pair);
			x.step(o);
			if (!fast)
			{
				cold(m, role);
				continue;
			}
			if (role & TS_STORE)
			{
				if (m.r11 == SL_GARBAGE)
					fail("a slot store through a clobbered r11", b, p, (int)i);
				if (m.r11 >= 0)
				{
					if ((u32)m.r11 < m.raw)
						fail("a slot store into a stored record", b, p, (int)i);
					if (pair)
					{
						m.rec[m.r11][(a & 0x18) / 4] = w0;
						m.rec[m.r11][(a & 0x18) / 4 + 1] = w1;
					}
					else
						m.rec[m.r11][(a & 0x1C) / 4] = w0;
				}
			}
			if (role & TS_PCW)
			{
				m.r12 = w0;
				m.r12_ok = !pair && !o.rs2.is_r32f();
			}
			continue;
		}
		if (o.op == shop_readm)
		{
			// A fixed non-RAM address calls C inline, with no fragment to poison r11.
			const u32 a = x.in(o.rs1) + x.in0(o.rs3);
			x.step(o);
			if (!ram_ok(a))
				cold(m, o.rs1.is_imm() ? 0 : role);
			continue;
		}
		if (o.op == shop_pref)
		{
			const u32 a = x.in(o.rs1);
			x.step(o);
			if (sq_ok(a))
				flush(m, b, p, i, a, (m.rnd() & 7) != 0);
			continue;
		}
		if (calls_c(o, fsqrt_inline, fschg_inline))
		{
			m.r11 = SL_GARBAGE;
			m.r12_ok = false;
		}
		x.step(o);
	}
}

// ---- generator -------------------------------------------------------------------------------------------------

struct TGen : Gen
{
	explicit TGen(u32 seed) : Gen(seed) {}

	// A store-queue half, now and then 4-aligned only, in RAM, or anywhere.
	u32 sq_addr()
	{
		switch (rnd(12))
		{
		case 0: return 0xE0000000u + 4 * rnd(16);
		case 1: return 0x8C000000u + (r32() & 0xFFFFE0);
		case 2: return r32();
		default: return 0xE0000000u + 32 * rnd(2) + (r32() & 0x03FFFFC0);
		}
	}
	u32 rb() { return reg_r0 + rnd(rnd(4) ? 8 : 16); }
	shil_param data32() { return rnd(3) ? R(gp()) : R(fr()); }

	// Ops the driver runs without a C call, and now and then one that calls C.
	void filler(DecodedBlock& b)
	{
		auto& L = b.oplist;
		switch (rnd(16))
		{
		case 0: L.push_back(OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(1 << 20)));
		        L.push_back(OP(shop_sync_fpscr, P(), P(), P(), rnd(4) ? SYNC_FPSCR_SZ_ONLY : 0)); break;
		case 1: L.push_back(OP(rnd(2) ? shop_fsqrt : shop_fsrra, R(fr()), R(fr()))); break;
		case 2: L.push_back(OP(shop_readm, R(gp()), R(gp()), P(), 4)); break;
		case 3: L.push_back(OP(shop_readm, DR(2 * rnd(8)), R(gp()), P(), 8)); break;
		case 4: L.push_back(OP(shop_pref, P(), R(gp()))); break;
		default: op(b); break;
		}
	}

	// Ops that leave r0-r15 alone and call no C, as in the games' vertex loops.
	void quiet(DecodedBlock& b)
	{
		auto& L = b.oplist;
		switch (rnd(5))
		{
		case 0: L.push_back(OP(shop_fmul, R(fr()), R(fr()), R(fr()))); break;
		case 1: L.push_back(OP(shop_fadd, R(fr()), R(fr()), R(fr()))); break;
		case 2: L.push_back(OP(shop_ftrv, FV(rnd(4)), FV(rnd(4)), XMTRX())); break;
		case 3: L.push_back(OP(shop_mov32, R(fr()), I(0x3F800000))); break;
		default: L.push_back(OP(shop_seteq, R(reg_sr_T), R(gp()), I(0))); break;
		}
	}

	// A whole record as the games write it: 32 bytes down from base + 32 by @-Rb, or stores at @(4k,Rb) with word 0
	// last; sometimes Crazy Taxi's word 0 again from a GPR that then takes the base.
	void clean_record(DecodedBlock& b)
	{
		auto& L = b.oplist;
		const u32 r = rb();
		const u32 base = rnd(8) ? 0xE0000000u + 32 * rnd(2) + (r32() & 0x03FFFFC0) :
		                 rnd(2) ? 0x8C000000u + (r32() & 0xFFFFE0) : 0xE0000004u + 32 * rnd(4);
		if (rnd(2))
		{
			L.push_back(OP(shop_mov32, R(r), I(base + 32)));
			for (u32 left = 32; left; )
			{
				const u32 sz = left % 8 == 0 && rnd(3) == 0 ? 8 : 4;
				L.push_back(OP(shop_sub, R(r), R(r), I(sz)));
				L.push_back(OP(shop_writem, P(), R(r), sz == 8 ? DR(2 * rnd(8)) : data32(), sz));
				left -= sz;
				for (u32 f = 0, nf = rnd(3); f < nf; f++)
					rnd(5) ? quiet(b) : filler(b);
			}
		}
		else
		{
			L.push_back(OP(shop_mov32, R(r), I(base)));
			for (u32 w = 7; w > 0; w--)
				if (rnd(4))
					L.push_back(OP(shop_writem, P(), R(r), data32(), 4, I(4 * w)));
			if (rnd(4))
				quiet(b);
			L.push_back(OP(shop_writem, P(), R(r), R(gp()), 4));
		}
		if (rnd(3) == 0)
		{
			const u32 x = gp();
			L.push_back(OP(shop_writem, P(), R(r), R(x), 4));
			L.push_back(OP(shop_mov32, R(x), R(r)));
		}
		L.push_back(OP(shop_pref, P(), R(r)));
		if (rnd(2))
			L.push_back(OP(shop_add, R(r), R(r), I(64)));
	}

	// One record: stores through Rb in the shapes the decoder emits, other ops between, then pref @Rb.
	void record(DecodedBlock& b)
	{
		if (rnd(2))
			return clean_record(b);
		auto& L = b.oplist;
		const u32 r = rb();
		if (rnd(3))
			L.push_back(OP(shop_mov32, R(r), I(sq_addr() + (rnd(2) ? 32 : 0))));
		for (u32 k = 0, n = 1 + rnd(10); k < n; k++)
		{
			switch (rnd(14))
			{
			case 0: case 1: case 2:   // @-Rb
				L.push_back(OP(shop_sub, R(r), R(r), I(4)));
				L.push_back(OP(shop_writem, P(), R(r), data32(), 4));
				break;
			case 3: case 4:           // fmov.d @-Rb
				L.push_back(OP(shop_sub, R(r), R(r), I(8)));
				L.push_back(OP(shop_writem, P(), R(r), DR(2 * rnd(8)), 8));
				break;
			case 5: case 6:           // @(disp,Rb), now and then 64 bytes off: the same queue word
				L.push_back(OP(shop_writem, P(), R(r), data32(), 4,
				               I(rnd(4) ? 4 * rnd(rnd(4) ? 8 : 16) : (u32)(4 * ((s32)rnd(40) - 16)))));
				break;
			case 7:                   // fmov.d @(R0... as disp), and @Rb
				if (rnd(2))
					L.push_back(OP(shop_writem, P(), R(r), DR(2 * rnd(8)), 8, I(8 * rnd(4))));
				else
					L.push_back(OP(shop_writem, P(), R(r), data32(), 4));
				break;
			case 8:                   // @-Rb with Rb as the data: the store goes first
				L.push_back(OP(shop_writem, P(), R(r), R(r), 4, I((u32)-4)));
				L.push_back(OP(shop_sub, R(r), R(r), I(4)));
				break;
			case 9:                   // the base moves, or is copied (Crazy Taxi's mov Rn,R2)
				if (rnd(2))
					L.push_back(OP(rnd(2) ? shop_add : shop_sub, R(r), R(r), I(4 * rnd(12))));
				else
					L.push_back(OP(shop_mov32, R(gp()), R(r)));
				break;
			case 10:                  // what ends a window
				switch (rnd(10))
				{
				case 0: L.push_back(OP(shop_writem, P(), R(r), R(gp()), 1 + rnd(2))); break;
				case 1: L.push_back(OP(shop_writem, P(), R(gp()), R(gp()), 4)); break;
				case 2: L.push_back(OP(shop_writem, P(), R(r), R(gp()), 4, R(reg_r0))); break;
				case 3: L.push_back(OP(shop_mov32, R(r), R(gp()))); break;
				case 4: L.push_back(OP(shop_ifb, P(), I(0), I(r32()), 0, I(r32() & 0xFFFF))); break;
				case 5: L.push_back(OP(shop_add, R(r), R(r), R(gp()))); break;
				case 6: L.push_back(OP(rnd(2) ? shop_add : shop_sub, R(r), R(gp()), I(4 * rnd(8)))); break;
				case 7: L.push_back(OP(shop_adc, R(gp()), R(gp()), R(gp()), 0, R(reg_sr_T), R(r))); break;
				case 8: L.push_back(OP(shop_sync_sr)); break;
				default: L.push_back(OP(shop_pref, P(), R(r))); break;
				}
				break;
			default:
				for (u32 f = 0, nf = rnd(3); f < nf; f++)
					filler(b);
				break;
			}
		}
		if (rnd(4) == 0)
		{
			// The PREF through a copy of the base: no record for the plan.
			const u32 c = rb();
			L.push_back(OP(shop_mov32, R(c), R(r)));
			L.push_back(OP(shop_pref, P(), R(c)));
		}
		else
			L.push_back(OP(shop_pref, P(), R(r)));
		if (rnd(2))
			L.push_back(OP(shop_add, R(r), R(r), I(64)));
	}

	void block(DecodedBlock& b)
	{
		for (u32 k = 0, n = rnd(3); k < n; k++)
			filler(b);
		for (u32 g = 0, ng = 1 + rnd(3); g < ng; g++)
		{
			record(b);
			for (u32 k = 0, n = rnd(3); k < n; k++)
				filler(b);
		}
		end(b);
	}

	Interp state(u32 seed)
	{
		Interp s = init_state(seed);
		for (u32 r = reg_r0; r <= reg_r15; r++)
			s.r[r] = rnd(4) ? sq_addr() + (rnd(2) ? 32 : 0) : addr();
		return s;
	}
};

static void plan_and_run(TGen& g, const DecodedBlock& b, u32 it)
{
	const bool fsqrt_inline = g.rnd(4) != 0, fschg_inline = g.rnd(4) != 0;
	static TsPlan p;
	ts_plan(p, b, fsqrt_inline, fschg_inline);
	check_structure(b, p, fsqrt_inline, fschg_inline);
	g_blocks++;
	g_groups += p.groups;
	for (u32 s = 0; s < 3; s++)
	{
		Model m;
		m.seed = it * 3 + s;
		for (u32 k = 0; k < 16; k++)
			m.sq[k] = m.rnd();
		for (u32 r = 0; r <= CAP; r++)
			for (u32 k = 0; k < 8; k++)
				m.rec[r][k] = m.rnd();
		m.raw = m.rnd() % 16 == 0 ? CAP - m.rnd() % 3 : m.rnd() % 8;
		m.r11 = SL_GARBAGE;
		m.r12 = m.rnd();
		m.r12_ok = false;
		run_block(b, p, g.state(it * 3 + s), m, fsqrt_inline, fschg_inline);
	}
}

// ---- directed tests -------------------------------------------------------------------------------------------

// masks: the END ops' masks in order.
static void expect(const char* what, const DecodedBlock& b, const std::vector<u8>& want, std::vector<u8> masks,
                   bool fsqrt = true, bool fschg = true)
{
	static TsPlan p;
	ts_plan(p, b, fsqrt, fschg);
	check_structure(b, p, fsqrt, fschg);
	CHECK(want.size() == b.oplist.size());
	u32 ends = 0;
	for (u32 i = 0; i < b.oplist.size(); i++)
	{
		if (p.role[i] != want[i])
		{
			printf("%s: op %u role %02X, want %02X\n", what, i, p.role[i], want[i]);
			fail(what, b, p, (int)i);
		}
		if (!(p.role[i] & TS_END))
			continue;
		if (ends >= masks.size() || p.mask[i] != masks[ends])
		{
			printf("%s: mask %02X, want %02X\n", what, p.mask[i], ends < masks.size() ? masks[ends] : 0);
			fail(what, b, p, (int)i);
		}
		ends++;
	}
	CHECK(ends == masks.size());
}

enum { W = TS_WIN, S = TS_WIN | TS_STORE, F = TS_WIN | TS_STORE | TS_FIRST, C = TS_WIN | TS_STORE | TS_PCW,
       E = TS_WIN | TS_END };

// Crazy Taxi's vertex loop 0C081BC4 (FPSCR.SZ 1): four fmov.d @-R6, then mov.l R2,@R6 over word 0, mov R6,R2 and
// pref @R6. The pref @R4 inside the window is a RAM prefetch at run time.
static DecodedBlock crazy_taxi(std::vector<u8>& want)
{
	DecodedBlock b = mkblk(BET_Cond_1);
	auto& L = b.oplist;
	auto add = [&](shil_opcode o, u8 r) { L.push_back(o); want.push_back(r); };
	add(OP(shop_fadd, R(reg_fr_2), R(reg_fr_2), R(reg_fr_3)), 0);
	add(OP(shop_mov32, R(reg_fr_3), I(0)), 0);
	add(OP(shop_sub, R(reg_r3), R(reg_r3), I(1)), 0);
	add(OP(shop_seteq, R(reg_sr_T), R(reg_r3), I(0)), 0);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), 0);
	add(OP(shop_writem, P(), R(reg_r6), DR(2), 8), F);
	add(OP(shop_fsub, R(reg_fr_2), R(reg_fr_2), R(reg_fr_2)), W);
	add(OP(shop_pref, P(), R(reg_r4)), W);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I((u32)-32)), W);
	add(OP(shop_readm, DR(10), R(reg_r1), P(), 8), W);
	add(OP(shop_add, R(reg_r1), R(reg_r1), I(8)), W);
	add(OP(shop_fmul, R(reg_fr_6), R(reg_fr_6), R(reg_fr_7)), W);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), W);
	add(OP(shop_writem, P(), R(reg_r6), DR(0), 8), S);
	add(OP(shop_fadd, R(reg_fr_2), R(reg_fr_2), R(reg_fr_11)), W);
	add(OP(shop_mov32, R(reg_fr_11), I(0x3F800000)), W);
	add(OP(shop_fmul, R(reg_fr_5), R(reg_fr_5), R(reg_fr_7)), W);
	add(OP(shop_readm, DR(0), R(reg_r1), P(), 8), W);
	add(OP(shop_add, R(reg_r1), R(reg_r1), I(8)), W);
	add(OP(shop_add, R(reg_r5), R(reg_r5), I(32)), W);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), W);
	add(OP(shop_writem, P(), R(reg_r6), DR(6), 8), S);
	add(OP(shop_ftrv, FV(2), FV(2), XMTRX()), W);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), W);
	add(OP(shop_writem, P(), R(reg_r6), DR(4), 8), S);
	add(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4), C);
	add(OP(shop_mov32, R(reg_r2), R(reg_r6)), W);
	add(OP(shop_pref, P(), R(reg_r6)), E);
	add(OP(shop_add, R(reg_r6), R(reg_r6), I(64)), 0);
	add(OP(shop_readm, R(reg_r0), R(reg_r4), P(), 4), 0);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I(4)), 0);
	add(OP(shop_readm, R(reg_r14), R(reg_r4), P(), 4), 0);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I(4)), 0);
	add(OP(shop_mov32, R(reg_fpul), R(reg_fr_15)), 0);
	add(OP(shop_fipr, R(reg_fr_15), FV(3), FV(3)), 0);
	add(OP(shop_mov32, R(reg_fr_11), I(0x3F800000)), 0);
	add(OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)), 0);
	add(OP(shop_fdiv, R(reg_fr_11), R(reg_fr_11), R(reg_fr_8)), 0);
	return b;
}

// Looney Tunes' race loop 8C0873A6 (FPSCR.SZ 1 at its start): fschg, eight 32-bit @-R6 stores (R7 last, as word 0),
// fschg, dt, bf.s with pref @R6 in the slot. Without the inline fschg only the last store is in the record.
static DecodedBlock looney(std::vector<u8>& want, bool fschg)
{
	DecodedBlock b = mkblk(BET_Cond_0);
	auto& L = b.oplist;
	// The roles with the inline fschg, then without it.
	auto add = [&](shil_opcode o, u8 r, u8 r_c) { L.push_back(o); want.push_back(fschg ? r : r_c); };
	auto st = [&](shil_param d, u8 r)
	{
		add(OP(shop_sub, R(reg_r6), R(reg_r6), I(4)), r == F ? 0 : W, 0);
		add(OP(shop_writem, P(), R(reg_r6), d, 4), r, 0);
	};
	add(OP(shop_readm, R(reg_r8), R(reg_r2), P(), 4), 0, 0);
	add(OP(shop_add, R(reg_r2), R(reg_r2), I(4)), 0, 0);
	add(OP(shop_add, R(reg_r6), R(reg_r6), I(64)), 0, 0);
	add(OP(shop_readm, R(reg_r9), R(reg_r2), P(), 4), 0, 0);
	add(OP(shop_add, R(reg_r2), R(reg_r2), I(4)), 0, 0);
	add(OP(shop_fdiv, R(reg_fr_12), R(reg_fr_12), R(reg_fr_4)), 0, 0);
	add(OP(shop_pref, P(), R(reg_r3)), 0, 0);
	add(OP(shop_fipr, R(reg_fr_11), FV(2), FV(3)), 0, 0);
	add(OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(1 << 20)), 0, 0);
	add(OP(shop_sync_fpscr, P(), P(), P(), SYNC_FPSCR_SZ_ONLY), 0, 0);
	add(OP(shop_mov32, R(reg_fr_10), I(0x3F800000)), 0, 0);
	st(R(reg_fr_10), F);
	st(R(reg_fr_11), S);
	st(R(reg_r9), S);
	add(OP(shop_fmul, R(reg_fr_6), R(reg_fr_6), R(reg_fr_12)), W, 0);
	st(R(reg_r8), S);
	add(OP(shop_fmul, R(reg_fr_7), R(reg_fr_7), R(reg_fr_12)), W, 0);
	st(R(reg_fr_12), S);
	st(R(reg_fr_6), S);
	st(R(reg_fr_7), S);
	add(OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(1 << 20)), W, 0);
	add(OP(shop_sync_fpscr, P(), P(), P(), SYNC_FPSCR_SZ_ONLY), W, 0);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(4)), W, 0);
	add(OP(shop_writem, P(), R(reg_r6), R(reg_r7), 4), C, F | C);
	add(OP(shop_sub, R(reg_r0), R(reg_r0), I(1)), W, W);
	add(OP(shop_seteq, R(reg_sr_T), R(reg_r0), I(0)), W, W);
	add(OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)), W, W);
	add(OP(shop_pref, P(), R(reg_r6)), E, E);
	return b;
}

static void t_games()
{
	std::vector<u8> want;
	DecodedBlock ct = crazy_taxi(want);
	expect("Crazy Taxi", ct, want, {0xFF});
	want.clear();
	DecodedBlock lt = looney(want, true);
	expect("Looney Tunes", lt, want, {0xFF}, true, true);
	want.clear();
	DecodedBlock lt0 = looney(want, false);
	expect("Looney Tunes, fschg by C call", lt0, want, {0x01}, true, false);
}

// Each case: stores of words 1..3 through R6 at @(4..12,R6), the case's op, a store of word 0 from R2, pref @R6.
// cut: the op ends the window, so only the word-0 store is in the record (mask 1).
static void t_cuts()
{
	struct Case { const char* what; shil_opcode mid; int kind; };   // 0 cut, 1 stays in the window, 2 no record
	const Case cases[] = {
		{"ifb", OP(shop_ifb, P(), I(0), I(5), 0, I(6)), 0},
		{"sync_sr", OP(shop_sync_sr), 0},
		{"mac.l", OP(shop_mac_l, R(reg_macl), R(reg_r1), R(reg_r3), 0, R(reg_sr_status), R(reg_mach)), 0},
		{"fsrra by C call", OP(shop_fsrra, R(reg_fr_1), R(reg_fr_1)), 0},
		{"frchg", OP(shop_sync_fpscr), 0},
		{"load from a fixed address", OP(shop_readm, R(reg_r1), I(0xA05F8000), P(), 4), 0},
		{"store through another reg", OP(shop_writem, P(), R(reg_r5), R(reg_r1), 4), 0},
		{"byte store through R6", OP(shop_writem, P(), R(reg_r6), R(reg_r1), 1, I(16)), 0},
		{"@(R0,R6)", OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, R(reg_r0)), 0},
		{"R6 loaded", OP(shop_readm, R(reg_r6), R(reg_r1), P(), 4), 0},
		{"R6 from another reg", OP(shop_add, R(reg_r6), R(reg_r5), I(4)), 0},
		{"R6 plus a register", OP(shop_add, R(reg_r6), R(reg_r6), R(reg_r8)), 0},
		{"R6 shifted", OP(shop_shl, R(reg_r6), R(reg_r6), I(1)), 0},
		{"R6 as rd2", OP(shop_adc, R(reg_r1), R(reg_r1), R(reg_r3), 0, R(reg_sr_T), R(reg_r6)), 0},
		{"pref @R4", OP(shop_pref, P(), R(reg_r4)), 1},
		{"load @R1", OP(shop_readm, R(reg_r3), R(reg_r1), P(), 4), 1},
		{"fsrra inline", OP(shop_fsrra, R(reg_fr_1), R(reg_fr_1)), 1},
		{"store to the other half", OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, I(32)), 1},
		{"store before the half", OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, I((u32)-4)), 1},
		{"R2 overwritten after its store", OP(shop_mov32, R(reg_r2), R(reg_r6)), 1},
	};
	for (const Case& c : cases)
	{
		const bool fsqrt = strcmp(c.what, "fsrra by C call") != 0;
		DecodedBlock b = mkblk();
		auto& L = b.oplist;
		for (u32 w = 1; w <= 3; w++)
			L.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, I(4 * w)));
		L.push_back(c.mid);
		const bool late = c.kind == 1 && c.mid.op == shop_mov32;   // the R2 case goes after the word-0 store
		if (late)
			L.pop_back();
		L.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
		if (late)
			L.push_back(c.mid);
		L.push_back(OP(shop_pref, P(), R(reg_r6)));
		std::vector<u8> want(L.size(), 0);
		if (c.kind == 0)
		{
			want[4] = F | C;
			want[5] = E;
			expect(c.what, b, want, {0x01}, fsqrt, true);
		}
		else
		{
			want[0] = F;
			want[1] = want[2] = S;
			want[3] = late ? C : W;
			want[4] = late ? W : C;
			want[5] = E;
			expect(c.what, b, want, {0x0F}, fsqrt, true);
		}
	}
	// A pref @R6 between: it ends a record of its own (word 0 from the queue), and the last store is the next one.
	DecodedBlock pr = mkblk();
	for (u32 w = 1; w <= 3; w++)
		pr.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, I(4 * w)));
	pr.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	pr.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
	pr.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	expect("pref @R6", pr, {F, S, S, E, F | C, E}, {0x0E, 0x01});
	// No record: the last store of word 0 comes from a float register, or is a pair; a pair at an odd word.
	const shil_opcode last[] = {
		OP(shop_writem, P(), R(reg_r6), R(reg_fr_3), 4),
		OP(shop_writem, P(), R(reg_r6), DR(2), 8),
	};
	for (const shil_opcode& o : last)
	{
		DecodedBlock b = mkblk();
		b.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
		b.oplist.push_back(o);
		b.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
		expect("word 0 not from an integer register", b, std::vector<u8>(3, 0), {});
	}
	DecodedBlock odd = mkblk();
	odd.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
	odd.oplist.push_back(OP(shop_writem, P(), R(reg_r6), DR(2), 8, I(4)));
	odd.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	expect("a pair at an odd word", odd, {0, 0, 0}, {});
	// Two records through one base: the second PREF's window starts after the first PREF.
	DecodedBlock two = mkblk();
	two.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
	two.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	two.oplist.push_back(OP(shop_add, R(reg_r6), R(reg_r6), I(32)));
	two.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r3), 4, I(4)));
	two.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
	two.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	expect("two records", two, {F | C, E, 0, F, C, E}, {0x01, 0x03});
	// The queue wraps at 64 bytes: @(-40,R6) is word 6 of the PREF's half, after the older store of word 6.
	DecodedBlock wr = mkblk();
	wr.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r1), 4, I(24)));
	wr.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r3), 4, I((u32)-40)));
	wr.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4));
	wr.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	expect("64-byte wrap", wr, {F, S, C, E}, {0x41});
	// Offsets follow the base: after add #8 a store at @(-8) is word 0.
	DecodedBlock mv = mkblk();
	mv.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r3), 4, I(28)));
	mv.oplist.push_back(OP(shop_add, R(reg_r6), R(reg_r6), I(8)));
	mv.oplist.push_back(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4, I((u32)-8)));
	mv.oplist.push_back(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)));
	mv.oplist.push_back(OP(shop_pref, P(), R(reg_r6)));
	expect("moving base", mv, {F, W, C, W, E}, {0x81});
}

// ---- the emitters on a PPC64 model ------------------------------------------------------------------------------

struct Cpu
{
	u64 g[32];
	u32 f[32];
	u32 cr0;
	std::map<u64, u32> mem;
	int bad;
};

static u64 rotl32x2(u32 x, u32 n) { const u64 d = ((u64)x << 32) | x; n &= 63; return n ? (d << n) | (d >> (64 - n)) : d; }
static u64 mask64(u32 mb, u32 me)
{
	u64 m = 0;
	for (u32 b = mb;; b = (b + 1) & 63)
	{
		m |= 1ull << (63 - b);
		if (b == me)
			break;
	}
	return m;
}

static void st32(Cpu& c, u64 ea, u32 v)
{
	if (ea & 3)
		c.bad = 1;
	c.mem[ea] = v;
}

// Runs g_code from 0 to its end or to a taken branch; returns the pc it stopped at.
static u32 run_ppc(Cpu& c)
{
	u32 pc = 0;
	while (pc < g_n)
	{
		const u32 i = g_code[pc];
		const u32 op = i >> 26, rS = (i >> 21) & 31, rA = (i >> 16) & 31, rB = (i >> 11) & 31;
		const s64 d = (s16)(i & 0xFFFF);
		const u64 base = rA ? c.g[rA] : 0;
		switch (op)
		{
		case 14: c.g[rS] = base + (u64)d; break;                                   // addi
		case 15: c.g[rS] = base + (u64)(d << 16); break;                           // addis
		case 24: c.g[rA] = c.g[rS] | (i & 0xFFFF); break;                          // ori
		case 32: c.g[rS] = c.mem.count(base + d) ? c.mem[base + d] : 0xDEAD0000u; break;   // lwz
		case 36: st32(c, base + d, (u32)c.g[rS]); break;                           // stw
		case 52: st32(c, base + d, c.f[rS]); break;                                // stfs
		case 21:                                                                    // rlwinm
		{
			const u32 sh = rB, mb = (i >> 6) & 31, me = (i >> 1) & 31;
			c.g[rA] = rotl32x2((u32)c.g[rS], sh) & mask64(mb + 32, me + 32);
			break;
		}
		case 10:                                                                    // cmpli cr0, 32-bit
		{
			const u32 a = (u32)c.g[rA], v = i & 0xFFFF;
			c.cr0 = a < v ? 8 : a > v ? 4 : 2;
			break;
		}
		case 16:                                                                    // bc (BO 4/12, cr0 bits)
		{
			const u32 bo = rS, bi = rA, bit = 8 >> (bi & 3);
			const bool set = (c.cr0 & bit) != 0;
			if ((bo & 0x1C) == 4 ? !set : (bo & 0x1C) == 12 ? set : true)
				return pc;
			break;
		}
		case 31:
			switch ((i >> 1) & 0x3FF)
			{
			case 151: st32(c, base + c.g[rB], (u32)c.g[rS]); break;                // stwx
			case 663: st32(c, base + c.g[rB], c.f[rS]); break;                     // stfsx
			case 266: c.g[rS] = c.g[rA] + c.g[rB]; break;                          // add
			default: printf("ppc: X-form %u not modelled\n", (i >> 1) & 0x3FF); exit(1);
			}
			break;
		default:
			printf("ppc: opcode %u not modelled\n", op);
			exit(1);
		}
		pc++;
	}
	return pc;
}

// The record's store as the driver emits it: r11 loaded first, the SQ test and store, then the slot store. Both
// stores land, nothing else is written, and the address and data registers keep their values.
static void t_emit()
{
	const u32 SQ_BASE = 0x11E4D300, SLOT_VAR = 0x10345670, POISON = 0x10F00A60;
	u64 st = 0x243F6A8885A308D3ull;
	auto rnd = [&]() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; };
	u32 runs = 0;
	for (u32 it = 0; it < 20000; it++)
	{
		const bool pair = rnd() & 1, fpr = (rnd() & 3) == 0, sq = (rnd() & 7) != 0;
		const u32 areg = pair ? (u32)ppc_rarg0 : (rnd() & 1) ? (u32)ppc_rarg0 : 14 + (u32)(rnd() % 15);
		const u32 ea = pair ? (u32)ppc_rarg1 : (u32)ppc_rarg2;
		const u32 d0 = fpr ? 14 + (u32)(rnd() % 17) : pair ? (u32)ppc_rarg2 : (rnd() & 1) ? (u32)ppc_rarg1 : 14 + (u32)(rnd() % 15);
		const u32 d1 = fpr ? d0 + 1 : (u32)ppc_rarg3;
		if (!fpr && !pair && d0 == areg)
			continue;
		u32 addr = sq ? 0xE0000000u | ((u32)rnd() & 0x03FFFFFC & (pair ? ~7u : ~3u)) : (u32)rnd() & ~3u;
		if (!sq && sq_ok(addr))
			addr ^= 0x10000000u;
		const u32 slot = 0x20000000u + 32 * (u32)(rnd() % 1000);
		Cpu c;
		memset(c.g, 0, sizeof(c.g));
		for (u32 k = 0; k < 32; k++)
		{
			c.g[k] = rnd();
			c.f[k] = (u32)rnd();
		}
		c.g[areg] = ((u64)rnd() << 32) | addr;   // a pinned register's upper half is garbage
		c.cr0 = 0;
		c.bad = 0;
		c.mem[SLOT_VAR] = slot;
		const Cpu c0 = c;
		g_n = 0;
		emit_ts_load(SLOT_VAR);
		ppc_label* other = emit_sq_check(areg);
		(void)other;
		const u32 body = g_n;
		emit_sq_body(areg, ea, pair, fpr, d0, d1, SQ_BASE);
		emit_ts_store(areg, ea, pair, fpr, d0, d1);
		const u32 end = g_n;
		CHECK(end - body <= (pair ? 9u : 5u));
		const u32 pc = run_ppc(c);
		if (c.g[TS_SLOT] != slot)
		{
			printf("FAIL emit: r11 %llX, slot %08X\n", (unsigned long long)c.g[TS_SLOT], slot);
			exit(1);
		}
		std::map<u64, u32> want = c0.mem;
		if (sq)
		{
			const u32 v0 = fpr ? c0.f[d0] : (u32)c0.g[d0], v1 = fpr ? c0.f[d1] : (u32)c0.g[d1];
			want[SQ_BASE + (addr & (pair ? 0x38 : 0x3C))] = v0;
			want[slot + (addr & (pair ? 0x18 : 0x1C))] = v0;
			if (pair)
			{
				want[SQ_BASE + (addr & 0x38) + 4] = v1;
				want[slot + (addr & 0x18) + 4] = v1;
			}
			CHECK(pc == end);
		}
		else
			CHECK(pc == body - 1);
		if (c.mem != want || c.bad)
		{
			printf("FAIL emit: pair %d fpr %d addr %08X slot %08X: memory differs\n", pair, fpr, addr, slot);
			exit(1);
		}
		for (u32 k = 0; k < 32; k++)
		{
			if (k == 0 || k == ea || k == TS_SLOT)
				continue;
			if (c.g[k] != c0.g[k] || c.f[k] != c0.f[k])
			{
				printf("FAIL emit: register %u changed\n", k);
				exit(1);
			}
		}
		runs++;
	}
	g_n = 0;
	emit_ts_poison(POISON);
	Cpu c;
	memset(c.g, 0xA5, sizeof(c.g));
	c.bad = 0;
	run_ppc(c);
	CHECK(c.g[TS_SLOT] == POISON);
	CHECK(TS_SLOT == 11 && TS_PCWR == 12);
	printf("emit: %u stores ok\n", runs);
}

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? (u32)strtoul(argv[1], nullptr, 0) : 1;
	const u32 iters = argc > 2 ? (u32)strtoul(argv[2], nullptr, 0) : 200000;

	// TS_FUZZ_ONLY: mutate.py checks which planted bugs the fuzzer alone catches.
	if (!getenv("TS_FUZZ_ONLY"))
	{
		t_emit();
		t_games();
		t_cuts();
		printf("directed tests: ok\n");
	}

	TGen g(seed);
	for (u32 it = 0; it < iters; it++)
	{
		g.pool.clear();
		for (u32 i = 0; i < 6; i++)
			g.pool.push_back((g.rnd(2) ? 0x8C000000u : 0xE0000000u) + (g.r32() & 0xFFFFC0));
		DecodedBlock b = mkblk();
		g.block(b);
		if (b.oplist.size() > TS_MAX_OPS)
			continue;
		plan_and_run(g, b, it);
	}
	printf("fuzz: %u blocks, %u records; flushes: %u from the slot (%u with words from the queue), %u copies (%u with"
	       " r11 poisoned), %u inside a window, %u with the buffer full, %u to RAM\n",
	       g_blocks, g_groups, g_hits, g_masks_part, g_copies, g_poison_seen, g_inner, g_full_flush, g_ram_flush);
	// Every case must be common enough, or the fuzzer proves little about it.
	CHECK(g_groups > iters / 2 && g_hits > iters / 4 && g_masks_part > iters / 50 && g_copies > iters / 20);
	CHECK(g_poison_seen > iters / 200 && g_inner > iters / 200 && g_full_flush > iters / 200);
	printf("PASS\n");
	return 0;
}
