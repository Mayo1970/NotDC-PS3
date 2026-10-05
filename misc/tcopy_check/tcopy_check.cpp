// Host-only check for plan 5.26: ps3_jit_tcopy.h as ngen_Compile drives it, on constprop_check's SHIL
// interpreter. The jcond register must hold the T the old code loads at the jcond, from every start state.
#define main constprop_main
#include "../constprop_check/constprop_check.cpp"
#undef main

// mutate.py swaps in an edited copy.
#ifndef TCOPY_HEADER
#define TCOPY_HEADER "ps3/ps3_jit_tcopy.h"
#endif
#include TCOPY_HEADER

static bool writes_t(const shil_opcode& o)
{
	return (o.rd.is_r32i() && o.rd._reg == reg_sr_T) || (o.rd2.is_r32i() && o.rd2._reg == reg_sr_T);
}

static int jcond_at(const DecodedBlock& b)
{
	for (size_t i = 0; i < b.oplist.size(); i++)
		if (b.oplist[i].op == shop_jcond)
			return (int)i;
	return -1;
}

// What ps3_driver.cpp emits, read from its code: these ops store sr_T inline, once, from a register.
static bool backend_inline_t_store(shilop op)
{
	switch (op)
	{
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt: case shop_setae: case shop_setab:
	case shop_cmpstr: case shop_fseteq: case shop_fsetgt: case shop_mov32:
	case shop_and: case shop_or: case shop_xor: case shop_shl: case shop_shr: case shop_sar:
	case shop_adc: case shop_sbc: case shop_negc: case shop_addv: case shop_subv: case shop_div1:
		return true;
	default:
		return false;
	}
}

// The writer the copy should ride on, found independently: the last op before the jcond that writes T, writes
// djump (jdyn) or runs the interpreter; only an inline T store qualifies.
static int oracle_k(const DecodedBlock& b)
{
	const int j = jcond_at(b);
	if (j < 0 || !b.oplist[j].rs1.is_r32i() || b.oplist[j].rs1._reg != reg_sr_T)
		return -1;
	for (int k = j - 1; k >= 0; k--)
	{
		const shil_opcode& o = b.oplist[k];
		if (o.op == shop_ifb || o.op == shop_jdyn)
			return -1;
		if (writes_t(o))
			return backend_inline_t_store(o.op) ? k : -1;
	}
	return -1;
}

struct Emu
{
	int  k;              // tcopy_writer's choice
	int  copies;         // `mr djump,D` emitted
	int  copy_at;
	bool jcond;
	bool held;           // the jcond emitted nothing
	u32  t_at_jcond;     // sr_T when the jcond runs: what the old code loads
	u32  djump_at_end;   // what the block end compares
};

// ngen_Compile's use of TCopy, run on the interpreter: an explicit T writer's inline store calls tcopy_store,
// and an accepted store also sets djump. jdyn writes djump; a jcond without the copy loads sr_T into it.
static Emu emulate(const DecodedBlock& b, bool on, Interp& x, u32 junk, const TCopy* force = nullptr)
{
	Emu e = {};
	e.copy_at = -1;
	u32 djump = junk;
	TCopy tc;
	tcopy_begin(tc, on ? &b : nullptr);
	if (force)
		tc = *force;
	e.k = tc.k;
	for (size_t i = 0; i < b.oplist.size(); i++)
	{
		const shil_opcode& o = b.oplist[i];
		tcopy_op_begin(tc, i);
		if (o.op == shop_jcond)
		{
			const bool t_rs1 = o.rs1.is_r32i() && o.rs1._reg == reg_sr_T;
			e.jcond = true;
			e.held = tc.held && t_rs1;
			e.t_at_jcond = x.in(o.rs1);
			if (!e.held)
				djump = x.in(o.rs1);
		}
		x.step(o);
		if (writes_t(o) && backend_inline_t_store(o.op) && tcopy_store(tc))
		{
			djump = x.r[reg_sr_T];
			e.copies++;
			e.copy_at = (int)i;
		}
		if (o.op == shop_jdyn)
			djump = x.r[reg_pc_dyn];
		tcopy_op_end(tc, o);
	}
	e.djump_at_end = djump;
	return e;
}

static void fail(const char* what, const DecodedBlock& b, const Emu& e)
{
	printf("%s: k %d (oracle %d), copies %d at %d, held %d, T at jcond %u, djump at end %08X\n", what, e.k,
	       oracle_k(b), e.copies, e.copy_at, e.held, e.t_at_jcond, e.djump_at_end);
	dump_block(what, b);
	fflush(stdout);
	abort();
}

static u32 g_junk = 0x5EED0001;

// The rules for one block from one start state. The junk djump stands for whatever r31 held before.
static Emu check_block(const DecodedBlock& b, const Interp& start, const char* what)
{
	Interp x = start;
	g_junk = hash32(g_junk);
	const Emu e = emulate(b, true, x, g_junk | 2);
	if (e.k != oracle_k(b))
		fail(what, b, e);
	if (e.jcond && e.djump_at_end != e.t_at_jcond)
		fail(what, b, e);
	if (e.k >= 0 && (!e.held || e.copies != 1 || e.copy_at != e.k))
		fail(what, b, e);
	if (e.k < 0 && (e.copies != 0 || e.held))
		fail(what, b, e);

	// Option off: never a copy, the jcond always loads.
	Interp y = start;
	const Emu off = emulate(b, false, y, g_junk);
	if (off.copies || off.held || off.k != -1 || (off.jcond && off.djump_at_end != off.t_at_jcond))
		fail("option off", b, off);
	return e;
}

static void expect_k(const DecodedBlock& b, int k, const char* what)
{
	for (u32 s = 0; s < 8; s++)
	{
		const Emu e = check_block(b, init_state(s + 1000), what);
		if (e.k != k)
		{
			printf("directed %s: k %d, want %d\n", what, e.k, k);
			fail(what, b, e);
		}
	}
}

// ---- directed tests -----------------------------------------------------------------

static DecodedBlock cond_blk(BlockEndType t = BET_Cond_0)
{
	DecodedBlock b = mkblk(t);
	b.BranchBlock = 0x8C010100;
	b.NextBlock = 0x8C010020;
	return b;
}

static void push(DecodedBlock& b, const shil_opcode& o) { b.oplist.push_back(o); }
static void jcond(DecodedBlock& b) { push(b, OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T))); }

static void t_shapes_518()
{
	// The top 5.18 shape: tst; add #4,Rn; bf.s with a delay slot.
	DecodedBlock a = cond_blk();
	push(a, OP(shop_test, R(reg_sr_T), R(reg_r2), R(reg_r2)));
	push(a, OP(shop_add, R(reg_r4), R(reg_r4), I(4)));
	jcond(a);
	push(a, OP(shop_mov32, R(reg_r1), R(reg_r5)));
	expect_k(a, 0, "tst; add; bf.s");

	// tst #imm,R0, the andi. form.
	DecodedBlock a2 = cond_blk(BET_Cond_1);
	push(a2, OP(shop_test, R(reg_sr_T), R(reg_r0), I(0x80)));
	jcond(a2);
	expect_k(a2, 0, "tst #imm; bt.s");

	// dt near the top of a long block: 24 ops between.
	DecodedBlock b = cond_blk();
	push(b, OP(shop_sub, R(reg_r6), R(reg_r6), I(1)));
	push(b, OP(shop_seteq, R(reg_sr_T), R(reg_r6), I(0)));
	for (u32 i = 0; i < 6; i++)
	{
		push(b, OP(shop_readm, R(reg_r1), R(reg_r4), P(), 4, I(4 * i)));
		push(b, OP(shop_add, R(reg_r2), R(reg_r2), R(reg_r1)));
		push(b, OP(shop_writem, P(), R(reg_r5), R(reg_r2), 4, I(4 * i)));
		push(b, OP(shop_fadd, R(reg_fr_1), R(reg_fr_1), R(reg_fr_2)));
	}
	jcond(b);
	push(b, OP(shop_add, R(reg_r4), R(reg_r4), I(24)));
	expect_k(b, 1, "dt, 24 ops, bf.s");

	// fcmp/gt just before bf.s (0C042D96).
	DecodedBlock c = cond_blk();
	push(c, OP(shop_fsetgt, R(reg_sr_T), R(reg_fr_3), R(reg_fr_4)));
	jcond(c);
	push(c, OP(shop_fmul, R(reg_fr_5), R(reg_fr_5), R(reg_fr_6)));
	expect_k(c, 0, "fcmp/gt; bf.s");

	// No T writer: T comes from the block before.
	DecodedBlock d = cond_blk(BET_Cond_1);
	push(d, OP(shop_add, R(reg_r4), R(reg_r4), I(4)));
	jcond(d);
	expect_k(d, -1, "no writer");
}

static void t_cancels()
{
	// An interpreter op between the writer and the jcond may write sr.T in memory.
	DecodedBlock a = cond_blk();
	push(a, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(a, OP(shop_ifb, P(), I(0), I(0x8C000000), 0, I(0x300E)));
	jcond(a);
	expect_k(a, -1, "ifb between");

	// jdyn writes the jcond register (not a decoder shape; the rule must still hold).
	DecodedBlock b = cond_blk();
	push(b, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(b, OP(shop_jdyn, R(reg_pc_dyn), R(reg_r3)));
	jcond(b);
	expect_k(b, -1, "jdyn between");

	// A writer whose backend store is not inline (readm into sr_T: a cold path may store it).
	DecodedBlock c = cond_blk();
	push(c, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(c, OP(shop_readm, R(reg_sr_T), R(reg_r4), P(), 4));
	jcond(c);
	expect_k(c, -1, "readm into sr_T");

	// A jcond on something other than sr_T.
	DecodedBlock d = cond_blk();
	push(d, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(d, OP(shop_jcond, R(reg_pc_dyn), R(reg_r0)));
	expect_k(d, -1, "jcond on r0");

	// bt without a delay slot: no jcond, so no copy.
	DecodedBlock e = cond_blk(BET_Cond_1);
	push(e, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(e, OP(shop_add, R(reg_r4), R(reg_r4), I(4)));
	expect_k(e, -1, "bt");

	// An interpreter op before the writer does not matter.
	DecodedBlock f = cond_blk();
	push(f, OP(shop_ifb, P(), I(0), I(0x8C000000), 0, I(0x300E)));
	push(f, OP(shop_setgt, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	jcond(f);
	expect_k(f, 1, "ifb before the writer");
}

static void t_writers()
{
	// The last writer wins; adc writes T through rd2.
	DecodedBlock a = cond_blk();
	push(a, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(a, OP(shop_adc, R(reg_r3), R(reg_r3), R(reg_r4), 0, R(reg_sr_T), R(reg_sr_T)));
	push(a, OP(shop_add, R(reg_r5), R(reg_r5), I(1)));
	jcond(a);
	expect_k(a, 1, "cmp; addc; bf.s");

	// The delay slot writes T after the jcond: the branch still follows the first T.
	DecodedBlock b = cond_blk(BET_Cond_1);
	push(b, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	jcond(b);
	push(b, OP(shop_setgt, R(reg_sr_T), R(reg_r3), R(reg_r4)));
	expect_k(b, 0, "slot writes T");

	// div0s: the last of its T writes is the shr.
	DecodedBlock c = cond_blk();
	push(c, OP(shop_sar, R(reg_sr_T), R(reg_r1), I(31)));
	push(c, OP(shop_and, R(reg_sr_T), R(reg_sr_T), I(1 << 8)));
	push(c, OP(shop_or, R(reg_sr_status), R(reg_sr_status), R(reg_sr_T)));
	push(c, OP(shop_xor, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(c, OP(shop_shr, R(reg_sr_T), R(reg_sr_T), I(31)));
	jcond(c);
	expect_k(c, 4, "div0s");

	// Every writer the backend stores inline, as the last T writer.
	const shil_opcode w[] = {
		OP(shop_test, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_seteq, R(reg_sr_T), R(reg_r1), I(5)),
		OP(shop_setge, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_setgt, R(reg_sr_T), R(reg_r1), I(0)),
		OP(shop_setae, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_setab, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_cmpstr, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_fseteq, R(reg_sr_T), R(reg_fr_1), R(reg_fr_2)),
		OP(shop_fsetgt, R(reg_sr_T), R(reg_fr_1), R(reg_fr_2)),
		OP(shop_mov32, R(reg_sr_T), I(1)),
		OP(shop_and, R(reg_sr_T), R(reg_r1), I(1)),
		OP(shop_or, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_xor, R(reg_sr_T), R(reg_r1), R(reg_r2)),
		OP(shop_shl, R(reg_sr_T), R(reg_r1), I(3)),
		OP(shop_shr, R(reg_sr_T), R(reg_r1), I(31)),
		OP(shop_sar, R(reg_sr_T), R(reg_r1), I(31)),
		OP(shop_adc, R(reg_r3), R(reg_r3), R(reg_r4), 0, R(reg_sr_T), R(reg_sr_T)),
		OP(shop_sbc, R(reg_r3), R(reg_r3), R(reg_r4), 0, R(reg_sr_T), R(reg_sr_T)),
		OP(shop_negc, R(reg_r3), R(reg_r4), R(reg_sr_T), 0, P(), R(reg_sr_T)),
		OP(shop_addv, R(reg_r3), R(reg_r3), R(reg_r4), 0, P(), R(reg_sr_T)),
		OP(shop_subv, R(reg_r3), R(reg_r3), R(reg_r4), 0, P(), R(reg_sr_T)),
		OP(shop_div1, R(reg_r3), R(reg_r3), R(reg_r4), 0, R(reg_sr_T), R(reg_sr_T)),
	};
	for (const shil_opcode& o : w)
	{
		DecodedBlock d = cond_blk(BET_Cond_1);
		push(d, OP(shop_mov32, R(reg_sr_T), I(0)));
		push(d, o);
		push(d, OP(shop_mul_i32, R(reg_macl), R(reg_r5), R(reg_r6)));
		jcond(d);
		char name[48];
		snprintf(name, sizeof(name), "writer op %d", (int)o.op);
		expect_k(d, 1, name);
	}
}

static void t_state_machine()
{
	// An armed op that never stores sr_T inline: the jcond must load (the driver's miss count).
	DecodedBlock a = cond_blk();
	push(a, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	jcond(a);
	TCopy f = { 0, false, false };
	DecodedBlock a2 = a;
	a2.oplist[0].op = shop_readm;   // same params, but readm's store is not modelled as inline
	a2.oplist[0].rs2 = P();
	a2.oplist[0].flags = 4;
	for (u32 s = 0; s < 4; s++)
	{
		Interp x = init_state(s + 2000);
		const Emu e = emulate(a2, true, x, 0xDEAD0000, &f);
		if (e.held || e.copies || e.djump_at_end != e.t_at_jcond)
			fail("armed op without an inline store", a2, e);
	}

	// A copy that a later interpreter op makes stale is dropped at that op.
	DecodedBlock b = cond_blk();
	push(b, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r2)));
	push(b, OP(shop_ifb, P(), I(0), I(0x8C000000), 0, I(0x300E)));
	jcond(b);
	for (u32 s = 0; s < 16; s++)
	{
		Interp x = init_state(s + 3000);
		const Emu e = emulate(b, true, x, 0xDEAD0000, &f);
		if (e.held || e.copies != 1 || e.djump_at_end != e.t_at_jcond)
			fail("copy before an ifb", b, e);
	}

	// The same for jdyn and for a later T writer the analysis would not skip.
	DecodedBlock c = b;
	c.oplist[1] = OP(shop_jdyn, R(reg_pc_dyn), R(reg_r3));
	DecodedBlock d = b;
	d.oplist[1] = OP(shop_setgt, R(reg_sr_T), R(reg_r3), R(reg_r4));
	for (u32 s = 0; s < 16; s++)
	{
		Interp x = init_state(s + 4000), y = init_state(s + 4000);
		const Emu e = emulate(c, true, x, 0xDEAD0000, &f);
		if (e.held || e.djump_at_end != e.t_at_jcond)
			fail("copy before a jdyn", c, e);
		const Emu g = emulate(d, true, y, 0xDEAD0000, &f);
		if (g.held || g.copies != 1 || g.djump_at_end != g.t_at_jcond)
			fail("copy before another writer", d, g);
	}
}

// ---- fuzzer ---------------------------------------------------------------------------

struct TGen : Gen
{
	explicit TGen(u32 seed) : Gen(seed) {}

	// One T-writing instruction as the decoder emits it (some write T more than once).
	void writer(DecodedBlock& b)
	{
		auto& L = b.oplist;
		const u32 rn = gp(), rm = gp();
		switch (rnd(15))
		{
		case 0:   // tst Rm,Rn / tst #imm,R0
			if (rnd(2))
				L.push_back(OP(shop_test, R(reg_sr_T), R(rn), R(rm)));
			else
				L.push_back(OP(shop_test, R(reg_sr_T), R(reg_r0), I(rnd(256))));
			break;
		case 1: case 2:
		{
			static const shilop cm[] = {shop_seteq, shop_setge, shop_setgt, shop_setae, shop_setab};
			L.push_back(OP(cm[rnd(5)], R(reg_sr_T), R(rn), rnd(2) ? R(rm) : I(konst())));
			break;
		}
		case 3: L.push_back(OP(shop_cmpstr, R(reg_sr_T), R(rn), R(rm))); break;
		case 4:   // dt
			L.push_back(OP(shop_sub, R(rn), R(rn), I(1)));
			L.push_back(OP(shop_seteq, R(reg_sr_T), R(rn), I(0)));
			break;
		case 5: L.push_back(OP(rnd(2) ? shop_fseteq : shop_fsetgt, R(reg_sr_T), R(fr()), R(fr()))); break;
		case 6: L.push_back(OP(shop_mov32, R(reg_sr_T), I(rnd(2)))); break;   // sett / clrt
		case 7:   // shll / shlr / shal / shar
			if (rnd(2))
				L.push_back(OP(shop_shr, R(reg_sr_T), R(rn), I(31)));
			else
				L.push_back(OP(shop_and, R(reg_sr_T), R(rn), I(1)));
			L.push_back(OP(rnd(2) ? shop_shl : rnd(2) ? shop_shr : shop_sar, R(rn), R(rn), I(1)));
			break;
		case 8: L.push_back(OP(rnd(2) ? shop_adc : shop_sbc, R(rn), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T))); break;
		case 9: L.push_back(OP(shop_negc, R(rn), R(rm), R(reg_sr_T), 0, P(), R(reg_sr_T))); break;
		case 10: L.push_back(OP(rnd(2) ? shop_addv : shop_subv, R(rn), R(rn), R(rm), 0, P(), R(reg_sr_T))); break;
		case 11: L.push_back(OP(shop_div1, R(rn), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T))); break;
		case 12:   // div0s
			L.push_back(OP(shop_and, R(reg_sr_status), R(reg_sr_status), I(~0x300u)));
			L.push_back(OP(shop_sar, R(reg_sr_T), R(rn), I(31)));
			L.push_back(OP(shop_and, R(reg_sr_T), R(reg_sr_T), I(1 << 8)));
			L.push_back(OP(shop_or, R(reg_sr_status), R(reg_sr_status), R(reg_sr_T)));
			L.push_back(OP(shop_xor, R(reg_sr_T), R(rn), R(rm)));
			L.push_back(OP(shop_shr, R(reg_sr_T), R(reg_sr_T), I(31)));
			break;
		case 13:   // rotcl / rotcr: T out, then ops that only read the old T
			if (rnd(2))
			{
				L.push_back(OP(shop_mov32, R(reg_temp), R(reg_sr_T)));
				L.push_back(OP(shop_shr, R(reg_sr_T), R(rn), I(31)));
				L.push_back(OP(shop_shl, R(rn), R(rn), I(1)));
			}
			else
			{
				L.push_back(OP(shop_shl, R(reg_temp), R(reg_sr_T), I(31)));
				L.push_back(OP(shop_and, R(reg_sr_T), R(rn), I(1)));
				L.push_back(OP(shop_shr, R(rn), R(rn), I(1)));
			}
			L.push_back(OP(shop_or, R(rn), R(rn), R(reg_temp)));
			break;
		default:   // tas.b
			L.push_back(OP(shop_readm, R(reg_temp), R(rn), P(), 1));
			L.push_back(OP(shop_seteq, R(reg_sr_T), R(reg_temp), I(0)));
			L.push_back(OP(shop_or, R(reg_temp), R(reg_temp), I(0x80)));
			L.push_back(OP(shop_writem, P(), R(rn), R(reg_temp), 1));
			break;
		}
	}

	// bt.s / bf.s: a writer, then 0-24 instructions (any shape, interpreter ops too), the jcond, the slot.
	void jcond_block(DecodedBlock& b)
	{
		auto& L = b.oplist;
		for (u32 i = 0, n = rnd(4); i < n; i++)
			op(b);
		if (rnd(10))
			writer(b);
		const u32 d = rnd(5) < 2 ? 0 : rnd(3) ? 1 + rnd(4) : 5 + rnd(20);
		for (u32 i = 0; i < d; i++)
			op(b);
		if (rnd(12) == 0)
			L.push_back(OP(shop_ifb, P(), I(0), I(r32()), 0, I(r32() & 0xFFFF)));
		b.BlockType = rnd(2) ? BET_Cond_1 : BET_Cond_0;
		L.push_back(OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)));
		const u32 slot = rnd(4);
		if (slot == 0)
			writer(b);
		else if (slot == 1)
			op(b);
	}
};

static u32 g_blocks, g_jcond, g_copy, g_dist_far, g_cut_ifb, g_cut_other, g_none, g_slot_t;
static u32 g_kind[256];

static void fuzz_one(TGen& g, u32 iter)
{
	g.pool.clear();
	for (u32 i = 0; i < 6; i++)
		g.pool.push_back((g.rnd(2) ? 0x8C000000 : 0xA0000000) + (g.r32() & 0xFFFFC0));

	DecodedBlock b = mkblk();
	b.start = 0x8C000000 + (g.r32() & 0xFFFFFE);
	b.BranchBlock = 0x8C000000 + (g.r32() & 0xFFFFFE);
	if (g.rnd(8))
		g.jcond_block(b);
	else
	{
		for (u32 i = 0, n = 1 + g.rnd(12); i < n; i++)
			g.op(b);
		g.end(b);
	}

	// The SHIL passes run before the backend when JIT CONSTPROP / JIT DEAD VALUES are ON.
	if (iter % 4 != 3)
		shil_constprop(&b, g.rnd(5) != 0);
	if (iter % 4 != 2)
		shil_dead_values(&b);

	g_blocks++;
	for (u32 s = 0; s < 3; s++)
	{
		Interp st = init_state(iter * 3 + s);
		for (u32 r = reg_r0; r <= reg_r15; r++)
			if (g.rnd(2))
				st.r[r] = g.addr();
		const Emu e = check_block(b, st, "fuzz");
		if (s)
			continue;
		const int j = jcond_at(b);
		if (j < 0)
			continue;
		g_jcond++;
		if (e.k >= 0)
		{
			g_copy++;
			g_kind[b.oplist[e.k].op & 255]++;
			g_dist_far += j - e.k > 5;
		}
		else
		{
			int k = j - 1;
			while (k >= 0 && !writes_t(b.oplist[k]) && b.oplist[k].op != shop_ifb && b.oplist[k].op != shop_jdyn)
				k--;
			if (k < 0)
				g_none++;
			else if (b.oplist[k].op == shop_ifb)
				g_cut_ifb++;
			else
				g_cut_other++;
		}
		for (size_t i = j + 1; i < b.oplist.size(); i++)
			g_slot_t += writes_t(b.oplist[i]) && e.k >= 0;
	}
}

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? (u32)strtoul(argv[1], nullptr, 0) : 1;
	const u32 iters = argc > 2 ? (u32)strtoul(argv[2], nullptr, 0) : 200000;

	// TCOPY_FUZZ_ONLY: mutate.py checks which planted bugs the fuzzer alone catches.
	if (!getenv("TCOPY_FUZZ_ONLY"))
	{
		t_shapes_518();
		t_cancels();
		t_writers();
		t_state_machine();
		printf("directed tests: ok\n");
	}

	TGen g(seed);
	for (u32 i = 0; i < iters; i++)
		fuzz_one(g, i);
	printf("fuzz: %u blocks ok, %u with a jcond: %u copies (%u more than 5 ops before the jcond, %u with a T"
	       " write in the slot), %u loads (%u no writer, %u ifb, %u other writer)\n",
	       g_blocks, g_jcond, g_copy, g_dist_far, g_slot_t, g_jcond - g_copy, g_none, g_cut_ifb, g_cut_other);
	printf("copies by writer op:");
	for (u32 k = 0; k < 256; k++)
		if (g_kind[k])
			printf(" %u:%u", k, g_kind[k]);
	printf("\n");
	// Every case must be common enough, or the fuzzer proves little about it.
	CHECK(g_jcond > iters / 2 && g_copy > iters / 4 && g_dist_far > iters / 50 && g_slot_t > iters / 50);
	CHECK(g_none > iters / 400 && g_cut_ifb > iters / 200);
	const shilop kinds[] = {shop_test, shop_seteq, shop_setge, shop_setgt, shop_setae, shop_setab, shop_cmpstr,
	                        shop_fseteq, shop_fsetgt, shop_mov32, shop_and, shop_shr, shop_adc, shop_sbc,
	                        shop_negc, shop_addv, shop_subv, shop_div1};
	for (shilop k : kinds)
		CHECK(g_kind[k] > 0);
	printf("PASS\n");
	return 0;
}
