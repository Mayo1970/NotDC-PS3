// Host-only check for plan 5.25: idle_loop.h's store test and pure-turn test against constprop_check's SHIL
// interpreter. A pure block run twice from any state must leave the same registers and read the same addresses.
#define main constprop_main
#include "../constprop_check/constprop_check.cpp"
#undef main

// mutate.py swaps in an edited copy.
#ifndef IDLE_HEADER
#define IDLE_HEADER "dc/sh4/rec_v2/idle_loop.h"
#endif
#include IDLE_HEADER

// ---- directed tests -------------------------------------------------------------

static DecodedBlock cond_blk(BlockEndType t = BET_Cond_0)
{
	DecodedBlock b = mkblk(t);
	b.start = 0x0C15A678;
	b.BranchBlock = 0x0C15A658;
	b.NextBlock = 0x0C15A680;
	return b;
}

static void push(DecodedBlock& b, const shil_opcode& o) { b.oplist.push_back(o); }

static void expect(const DecodedBlock& b, bool pure, bool stores, const char* what)
{
	const bool p = idle_turn_is_pure(b), s = idle_block_stores(b);
	if (p != pure || s != stores)
	{
		printf("directed %s: pure %d (want %d), stores %d (want %d)\n", what, p, pure, s, stores);
		dump_block(what, b);
		fflush(stdout);
		abort();
	}
}

static void t_crazy_taxi()
{
	// 0C15A678: mov.l @(disp,PC),r3; mov.l @r3,r2; tst r2,r2; bf 0C15A658
	DecodedBlock b = cond_blk();
	push(b, OP(shop_readm, R(reg_r3), I(0x0C15A6DC), P(), 4));
	push(b, OP(shop_readm, R(reg_r2), R(reg_r3), P(), 4));
	push(b, OP(shop_test, R(reg_sr_T), R(reg_r2), R(reg_r2)));
	expect(b, true, false, "CT 0C15A678");

	// 0C15A660: reads A, B and C, then C > A + B + 1 (pure; only the decoder's size rule leaves it out)
	DecodedBlock c = cond_blk();
	push(c, OP(shop_readm, R(reg_r1), I(0x0C15A6E4), P(), 4));
	push(c, OP(shop_readm, R(reg_r3), R(reg_r1), P(), 4));
	push(c, OP(shop_readm, R(reg_r0), I(0x0C15A6E8), P(), 4));
	push(c, OP(shop_readm, R(reg_r2), R(reg_r0), P(), 4));
	push(c, OP(shop_readm, R(reg_r1), I(0x0C15A6D8), P(), 4));
	push(c, OP(shop_add, R(reg_r2), R(reg_r2), R(reg_r3)));
	push(c, OP(shop_add, R(reg_r2), R(reg_r2), I(1)));
	push(c, OP(shop_readm, R(reg_r3), R(reg_r1), P(), 4));
	push(c, OP(shop_setab, R(reg_sr_T), R(reg_r3), R(reg_r2)));
	expect(c, true, false, "CT 0C15A660");
}

static void t_wait_shapes()
{
	// A flag through a register that only the loop reads: r4 is read, never written.
	DecodedBlock a = cond_blk(BET_Cond_1);
	push(a, OP(shop_readm, R(reg_r0), R(reg_r4), P(), 4));
	push(a, OP(shop_test, R(reg_sr_T), R(reg_r0), R(reg_r0)));
	expect(a, true, false, "flag @r4");

	// @(disp,GBR) into r0, then @(R0,Rm).
	DecodedBlock b = cond_blk();
	push(b, OP(shop_readm, R(reg_r0), R(reg_gbr), P(), 4, I(0x40)));
	push(b, OP(shop_readm, R(reg_r1), R(reg_r5), P(), 2, R(reg_r0)));
	push(b, OP(shop_and, R(reg_r1), R(reg_r1), I(0xFF)));
	push(b, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r0)));
	expect(b, true, false, "GBR + @(R0,Rm)");

	// The same reads the other way round: r0 is read as the index, then the GBR load overwrites it.
	DecodedBlock b2 = cond_blk();
	push(b2, OP(shop_readm, R(reg_r1), R(reg_r5), P(), 2, R(reg_r0)));
	push(b2, OP(shop_readm, R(reg_r0), R(reg_gbr), P(), 4, I(0x40)));
	push(b2, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r0)));
	expect(b2, false, false, "@(R0,Rm) + GBR");

	// bt.s: jcond copies T before the delay slot's read.
	DecodedBlock c = cond_blk(BET_Cond_1);
	push(c, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(c, OP(shop_seteq, R(reg_sr_T), R(reg_r1), I(0)));
	push(c, OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)));
	push(c, OP(shop_readm, R(reg_r3), R(reg_r2), P(), 4));
	expect(c, true, false, "bt.s");

	// A register written, read, then written again in one turn keeps no state.
	DecodedBlock d = cond_blk();
	push(d, OP(shop_readm, R(reg_r1), I(0x8C001000), P(), 4));
	push(d, OP(shop_add, R(reg_r1), R(reg_r1), I(4)));
	push(d, OP(shop_shl, R(reg_r1), R(reg_r1), I(2)));
	push(d, OP(shop_setgt, R(reg_sr_T), R(reg_r1), R(reg_r7)));
	expect(d, true, false, "rewritten in the turn");
}

static void t_work_shapes()
{
	// Checksum: a moving pointer, a sum and a counter.
	DecodedBlock a = cond_blk();
	push(a, OP(shop_readm, R(reg_r0), R(reg_r4), P(), 1));
	push(a, OP(shop_add, R(reg_r4), R(reg_r4), I(1)));
	push(a, OP(shop_add, R(reg_r5), R(reg_r5), R(reg_r0)));
	push(a, OP(shop_sub, R(reg_r6), R(reg_r6), I(1)));
	push(a, OP(shop_seteq, R(reg_sr_T), R(reg_r6), I(0)));
	expect(a, false, false, "checksum");

	// List walk: p = p->next.
	DecodedBlock b = cond_blk();
	push(b, OP(shop_readm, R(reg_r4), R(reg_r4), P(), 4, I(4)));
	push(b, OP(shop_test, R(reg_sr_T), R(reg_r4), R(reg_r4)));
	expect(b, false, false, "list walk");

	// movt then a compare: T carries over from the last turn.
	DecodedBlock c = cond_blk();
	push(c, OP(shop_mov32, R(reg_r0), R(reg_sr_T)));
	push(c, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(c, OP(shop_seteq, R(reg_sr_T), R(reg_r1), R(reg_r0)));
	expect(c, false, false, "movt");

	// A delay loop that also reads: dt.
	DecodedBlock d = cond_blk();
	push(d, OP(shop_readm, R(reg_r0), R(reg_r8), P(), 4));
	push(d, OP(shop_sub, R(reg_r1), R(reg_r1), I(1)));
	push(d, OP(shop_seteq, R(reg_sr_T), R(reg_r1), I(0)));
	expect(d, false, false, "dt");

	// A move onto itself is refused too (the rule is by register, not by value).
	DecodedBlock e = cond_blk();
	push(e, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(e, OP(shop_mov32, R(reg_r0), R(reg_r0)));
	expect(e, false, false, "mov r0,r0");

	// adc's T in and out with no compare before it.
	DecodedBlock f = cond_blk();
	push(f, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(f, OP(shop_adc, R(reg_r3), R(reg_r1), R(reg_r1), 0, R(reg_sr_T), R(reg_sr_T)));
	expect(f, false, false, "adc T");
}

static void t_refused_ops()
{
	// div1 and mac.* touch SR.Q/M and MACH:MACL, which no param names.
	DecodedBlock a = cond_blk();
	push(a, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(a, OP(shop_seteq, R(reg_sr_T), R(reg_r1), I(0)));
	push(a, OP(shop_div1, R(reg_r3), R(reg_r1), R(reg_r2), 0, R(reg_sr_T), R(reg_sr_T)));
	expect(a, false, false, "div1");

	DecodedBlock b = cond_blk();
	push(b, OP(shop_readm, R(reg_temp), R(reg_r4), P(), 4));
	push(b, OP(shop_readm, R(reg_temp2), R(reg_r5), P(), 4));
	push(b, OP(shop_mac_l, R(reg_macl), R(reg_temp), R(reg_temp2), 0, R(reg_sr_status), R(reg_mach)));
	expect(b, false, false, "mac.l");

	// Float loads, single and pair.
	DecodedBlock c = cond_blk();
	push(c, OP(shop_readm, R(reg_fr_2), R(reg_r4), P(), 4));
	expect(c, false, false, "float readm");
	DecodedBlock d = cond_blk();
	push(d, OP(shop_readm, DR(1), R(reg_r4), P(), 8));
	expect(d, false, false, "pair readm");

	DecodedBlock e = cond_blk();
	push(e, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(e, OP(shop_sync_sr));
	expect(e, false, false, "sync_sr");
}

static void t_stores()
{
	DecodedBlock a = cond_blk();
	push(a, OP(shop_readm, R(reg_r1), R(reg_r2), P(), 4));
	push(a, OP(shop_writem, P(), R(reg_r3), R(reg_r1), 4));
	expect(a, false, true, "writem");

	DecodedBlock b = cond_blk();
	push(b, OP(shop_pref, P(), R(reg_r3)));
	expect(b, false, true, "pref");

	DecodedBlock c = cond_blk();
	push(c, OP(shop_ifb, P(), I(0), I(0x8C000000), 0, I(0x300E)));
	expect(c, false, true, "ifb");

	// tas.b
	DecodedBlock d = cond_blk();
	push(d, OP(shop_readm, R(reg_temp), R(reg_r4), P(), 1));
	push(d, OP(shop_seteq, R(reg_sr_T), R(reg_temp), I(0)));
	push(d, OP(shop_or, R(reg_temp), R(reg_temp), I(0x80)));
	push(d, OP(shop_writem, P(), R(reg_r4), R(reg_temp), 1));
	expect(d, false, true, "tas.b");
}

// ---- fuzzer ---------------------------------------------------------------------

static u32 g_pure, g_impure, g_stores, g_pure_reads;

// Idle-loop shapes most of the time, so pure blocks are common; any decoder shape the rest of the time.
static void idle_shape(Gen& g, DecodedBlock& b)
{
	auto& L = b.oplist;
	const u32 rn = g.gp(), rm = g.gp();
	switch (g.rnd(21))
	{
	// Not decoder shapes (there div1 and mac.* always update a register they read): they test the hidden operands.
	case 18:
		L.push_back(OP(shop_seteq, R(reg_sr_T), R(rm), I(0)));
		L.push_back(OP(shop_div1, R(reg_r9), R(rn), R(rm), 0, R(reg_sr_T), R(reg_sr_T)));
		break;
	case 19:
	{
		const u32 e = g.rnd(2) ? 4 : 2;
		L.push_back(OP(shop_readm, R(reg_temp), R(rn), P(), e));
		L.push_back(OP(shop_readm, R(reg_temp2), R(rm), P(), e));
		L.push_back(OP(e == 4 ? shop_mac_l : shop_mac_w, R(reg_macl), R(reg_temp), R(reg_temp2), 0,
		               R(reg_sr_status), R(reg_mach)));
		break;
	}
	case 20: L.push_back(OP(shop_sync_sr)); break;
	case 0: case 1: L.push_back(OP(shop_readm, R(rn), I(g.addr()), P(), 4)); break;
	case 2: L.push_back(OP(shop_readm, R(rn), R(rm), P(), g.size())); break;
	case 3: L.push_back(OP(shop_readm, R(rn), R(rm), P(), g.size(), I(4 * g.rnd(16)))); break;
	case 4: L.push_back(OP(shop_readm, R(rn), R(rm), P(), g.size(), R(reg_r0))); break;
	case 5:
		L.push_back(OP(shop_readm, R(rn), R(rm), P(), g.size()));
		if (rn != rm)
			L.push_back(OP(shop_add, R(rm), R(rm), I(4)));
		break;
	case 6: case 7:
	{
		static const shilop cm[] = {shop_test, shop_seteq, shop_setge, shop_setgt, shop_setae, shop_setab, shop_cmpstr};
		const shilop c = cm[g.rnd(7)];
		L.push_back(OP(c, R(reg_sr_T), R(rn), c != shop_cmpstr && g.rnd(2) ? I(g.konst()) : R(rm)));
		break;
	}
	case 8: L.push_back(OP(shop_add, R(rn), R(g.rnd(2) ? rn : rm), g.rnd(2) ? R(rm) : I(g.konst()))); break;
	case 9: L.push_back(OP(shop_mov32, R(rn), I(g.konst()))); break;
	case 10: L.push_back(OP(shop_mov32, R(rn), R(rm))); break;
	case 11: L.push_back(OP(shop_and, R(rn), R(rm), I(g.rnd(2) ? 0xFF : 0xFFFF))); break;
	case 12:
		L.push_back(OP(shop_sub, R(rn), R(rn), I(1)));
		L.push_back(OP(shop_seteq, R(reg_sr_T), R(rn), I(0)));
		break;
	case 13: L.push_back(OP(shop_mov32, R(rn), R(reg_sr_T))); break;
	case 14: L.push_back(OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T))); break;
	case 15: L.push_back(OP(shop_readm, R(reg_r0), R(reg_gbr), P(), 4, I(4 * g.rnd(64)))); break;
	default: g.op(b); break;
	}
}

static void fail_block(const char* why, u32 iter, const DecodedBlock& b)
{
	printf("iteration %u: %s\n", iter, why);
	dump_block("block", b);
	fflush(stdout);
	abort();
}

static void fuzz_one(Gen& g, u32 iter)
{
	g.pool.clear();
	for (u32 i = 0; i < 6; i++)
		g.pool.push_back((g.rnd(2) ? 0x8C000000 : 0xA0000000) + (g.r32() & 0xFFFFC0));

	DecodedBlock b = cond_blk(g.rnd(2) ? BET_Cond_1 : BET_Cond_0);
	for (u32 i = 0, n = 1 + g.rnd(5); i < n; i++)
		idle_shape(g, b);
	if (g.rnd(4))
		idle_shape(g, b);

	const bool pure = idle_turn_is_pure(b), stores = idle_block_stores(b);
	g_pure += pure;
	g_impure += !pure;
	g_stores += stores;

	Interp s = init_state(iter * 7 + 1);
	for (u32 k = reg_r0; k <= reg_r15; k++)
		if (g.rnd(2))
			s.r[k] = g.addr();

	Interp one = s;
	one.run(b.oplist);
	bool wrote = false;
	for (auto& e : one.ev)
		wrote |= e.kind == EV_WRITE || e.kind == EV_SQ;
	if (wrote && !stores)
		fail_block("memory written, but no store found", iter, b);
	if (!pure)
		return;

	// A second turn from the first one's end, with memory as it was.
	bool reads = false;
	for (auto& e : one.ev)
	{
		if (e.kind != EV_READ)
			fail_block("pure block did more than read", iter, b);
		reads = true;
	}
	g_pure_reads += reads;
	Interp two = one;
	two.ev.clear();
	two.run(b.oplist);
	for (u32 k = 0; k < (u32)sh4_reg_count; k++)
		if (one.r[k] != two.r[k])
		{
			printf("register %u: %08X after one turn, %08X after two\n", k, one.r[k], two.r[k]);
			fail_block("pure block kept state", iter, b);
		}
	for (u32 k = 0; k < 8; k++)
		if (one.bank[k] != two.bank[k])
			fail_block("pure block moved the bank", iter, b);
	if (one.ev.size() != two.ev.size())
		fail_block("second turn read a different count", iter, b);
	for (size_t i = 0; i < one.ev.size(); i++)
		if (!(one.ev[i] == two.ev[i]))
			fail_block("second turn read elsewhere", iter, b);
}

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? (u32)strtoul(argv[1], nullptr, 0) : 1;
	const u32 iters = argc > 2 ? (u32)strtoul(argv[2], nullptr, 0) : 200000;

	// IDLE_FUZZ_ONLY: mutate.py checks that the fuzzer alone catches a planted bug.
	if (!getenv("IDLE_FUZZ_ONLY"))
	{
		t_crazy_taxi();
		t_wait_shapes();
		t_work_shapes();
		t_refused_ops();
		t_stores();
		printf("directed tests: ok\n");
	}

	Gen g(seed);
	for (u32 i = 0; i < iters; i++)
		fuzz_one(g, i);
	printf("fuzz: %u blocks ok: %u pure (%u with reads), %u not pure, %u with stores\n",
	       iters, g_pure, g_pure_reads, g_impure, g_stores);
	// Both answers must be common, or the fuzzer proves little about either.
	CHECK(g_pure > iters / 10 && g_impure > iters / 10 && g_pure_reads > iters / 20 && g_stores > iters / 50);
	printf("PASS\n");
	return 0;
}
