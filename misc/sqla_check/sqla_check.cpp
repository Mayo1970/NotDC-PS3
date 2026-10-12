// Host-only check for plan 5.27 LOOK-AHEAD, its 5.43 steps and JIT SQ COPY: ps3_jit_sqla.h against a reference from the
// SH4 opcode list (gen.py), on Crazy Taxi, Looney Tunes and Sonic Adventure 2 blocks and on random code. Not in the PS3 build.
#define main constprop_main
#include "../constprop_check/constprop_check.cpp"
#undef main

// mutate.py swaps in an edited copy.
#ifndef SQLA_HEADER
#define SQLA_HEADER "ps3/ps3_jit_sqla.h"
#endif
#include SQLA_HEADER

struct Ref { u8 flags, pref_reg; u16 writes, steps; };
enum { R_DEF = 1, R_PC = 2, R_DELAY = 4, R_BANK = 8, R_PREF = 16 };
static Ref g_ref[65536];

static bool ref_hit(u32 op, u32 n) { return (g_ref[op].flags & R_PREF) && g_ref[op].pref_reg == n; }
static bool ref_writes(u32 op, u32 n) { return (g_ref[op].writes >> n) & 1; }
// steps: an op whose only change of Rn is a move by a constant passes too.
static bool ref_pass(u32 op, u32 n, bool steps = false)
{
	const u8 f = g_ref[op].flags;
	const bool keeps = !ref_writes(op, n) || (steps && ((g_ref[op].steps >> n) & 1));
	return (f & R_DEF) && !(f & (R_PC | R_BANK)) && keeps && !ref_hit(op, n);
}
static bool ref_delay(u32 op, u32 n)
{
	const u8 f = g_ref[op].flags;
	return (f & R_DEF) && (f & R_DELAY) && !(f & R_BANK) && !ref_writes(op, n);
}

// Guest code by address, main RAM mirrors folded; unknown words read as an undefined opcode.
static std::map<u32, u16> g_mem;
static u32 g_reads;
static u32 fold(u32 a) { return (a & 0x00FFFFFF) | 0x0C000000; }
static u32 rd16(u32 a)
{
	g_reads++;
	auto it = g_mem.find(fold(a));
	return it == g_mem.end() ? 0xFFFF : it->second;
}
static void put(u32 a, std::initializer_list<u16> words)
{
	for (u16 w : words)
	{
		g_mem[fold(a)] = w;
		a += 2;
	}
}

// The scan rule with the reference's opcode meanings.
static bool ref_scan(u32 pc, u32 n, bool steps = false)
{
	if ((pc & 1) || (pc & 0x1C000000) != 0x0C000000)
		return false;
	bool slot = false;
	for (int k = 0; k < SQLA_SCAN_OPS; k++, pc += 2)
	{
		const u32 op = rd16(pc);
		if (ref_hit(op, n))
			return true;
		if (slot)
			return false;
		if (ref_delay(op, n))
			slot = true;
		else if (!ref_pass(op, n, steps))
			return false;
	}
	return false;
}

// Per mode. With steps, the class may differ from the plain one only on the three forms at their own Rn.
static u32 check_class(bool steps)
{
	u32 bad = 0, hit = 0, pass = 0, delay = 0, safe_stop = 0, moved = 0;
	for (u32 op = 0; op < 65536; op++)
		for (u32 n = 0; n < 16; n++)
		{
			const int c = sqla_class(op, n, steps);
			if (c != sqla_class(op, n))
			{
				const u32 hi = op >> 12, lo = op & 15;
				const bool form = (hi == 2 && lo >= 4 && lo <= 6) || hi == 7 || (hi == 15 && lo == 11);
				moved++;
				if (!steps || !form || ((op >> 8) & 15) != n || c != SQLA_PASS)
					bad++;
			}
			bool ok;
			switch (c)
			{
			case SQLA_HIT:   ok = ref_hit(op, n);   hit++;   break;
			case SQLA_PASS:  ok = ref_pass(op, n, steps);  pass++;  break;
			case SQLA_DELAY: ok = ref_delay(op, n); delay++; break;
			case SQLA_STOP:  ok = true; safe_stop += ref_pass(op, n, steps) || ref_delay(op, n); break;
			default:         ok = false;
			}
			if (ref_hit(op, n) && c != SQLA_HIT)
				ok = false;
			if (!ok && bad++ < 12)
				printf("class %04X n=%u: got %d, ref flags %02X writes %04X pref r%u\n", op, n, c,
				       g_ref[op].flags, g_ref[op].writes, g_ref[op].pref_reg);
		}
	// 16 registers x (3 x 16 pre-decrement stores + 16 fmov + 256 add #imm).
	if (moved != (steps ? 16u * 320 : 0u))
		bad++;
	printf("classifier%s: %u (op, n) pairs, hit %u pass %u delay %u, %u conservative stops, %u moved by steps, "
	       "%u wrong\n", steps ? " + steps" : "", 65536u * 16, hit, pass, delay, safe_stop, moved, bad);
	return bad;
}

static void directed_class()
{
	CHECK(sqla_class(0xF4E9, 6) == SQLA_PASS);	// fmov @R14+,DR4
	CHECK(sqla_class(0xF4E9, 14) == SQLA_STOP);
	CHECK(sqla_class(0xF62B, 6) == SQLA_STOP);	// fmov DR2,@-R6
	CHECK(sqla_class(0xAFB8, 6) == SQLA_DELAY);	// bra
	CHECK(sqla_class(0x0683, 6) == SQLA_HIT);	// pref @R6
	CHECK(sqla_class(0x0683, 2) == SQLA_PASS);
	CHECK(sqla_class(0x2662, 6) == SQLA_PASS);	// mov.l R6,@R6
	CHECK(sqla_class(0x7520, 6) == SQLA_PASS);	// add #32,R5
	CHECK(sqla_class(0x7520, 5) == SQLA_STOP);
	CHECK(sqla_class(0x000B, 6) == SQLA_DELAY);	// rts
	CHECK(sqla_class(0x7620, 6) == SQLA_STOP);	// add #32,R6
	CHECK(sqla_class(0x002B, 6) == SQLA_STOP);	// rte
	CHECK(sqla_class(0x460E, 2) == SQLA_STOP);	// ldc R6,SR
	CHECK(sqla_class(0x8900, 6) == SQLA_STOP);	// bt
	CHECK(sqla_class(0xC7FF, 0) == SQLA_STOP);	// mova @(disp,PC),R0
	CHECK(sqla_class(0xC7FF, 6) == SQLA_PASS);
	CHECK(sqla_class(0x6266, 6) == SQLA_STOP);	// mov.l @R6+,R2
	// Plan 5.43: only the three forms pass, and only with steps.
	CHECK(sqla_class(0x2676, 6) == SQLA_STOP);	// mov.l R7,@-R6
	CHECK(sqla_class(0x2676, 6, true) == SQLA_PASS);
	CHECK(sqla_class(0x2676, 7, true) == SQLA_PASS);
	CHECK(sqla_class(0x2664, 6, true) == SQLA_PASS);	// mov.b R6,@-R6
	CHECK(sqla_class(0xF62B, 6, true) == SQLA_PASS);	// fmov DR2,@-R6
	CHECK(sqla_class(0x7620, 6, true) == SQLA_PASS);	// add #32,R6
	CHECK(sqla_class(0x6266, 6, true) == SQLA_STOP);	// mov.l @R6+,R2
	CHECK(sqla_class(0xF269, 6, true) == SQLA_STOP);	// fmov @R6+,FR2
	CHECK(sqla_class(0x4622, 6, true) == SQLA_STOP);	// sts.l PR,@-R6
	CHECK(sqla_class(0x366C, 6, true) == SQLA_STOP);	// add R6,R6
	CHECK(sqla_class(0xE620, 6, true) == SQLA_STOP);	// mov #32,R6
	CHECK(sqla_class(0x4610, 6, true) == SQLA_STOP);	// dt R6
	CHECK(sqla_class(0x0683, 6, true) == SQLA_HIT);
}

// Crazy Taxi, 5.18 run 3: the loops' successors (bt.s target and fall-through) PREF through R6.
static void load_crazy_taxi()
{
	put(0x0C081D6E, { 0xF4E9, 0xAFB8, 0x0683 });		// fmov @R14+,DR4; bra; pref @R6 (slot)
	put(0x0C081D72, { 0x0683, 0xFFFF });			// pref @R6
	put(0x0C0820DA, { 0x2662, 0x7520, 0x0683, 0x000B, 0x7620 });	// mov.l R6,@R6; add #32,R5; pref @R6; rts
	put(0x0C0820E4, { 0x6263, 0x8900 });			// not taken from the dump: no PREF
}

// Looney Tunes race, the 2026-10-08 profile: block 8C0873B4 (7 stores through R6, then fschg) runs into 8C0873CA.
static void load_looney_tunes()
{
	put(0x0C0873CA, { 0x2676, 0x4010, 0x8FDD, 0x0683 });	// mov.l R7,@-R6; dt R0; bf.s; pref @R6 (slot)
}

static void directed_scan()
{
	load_crazy_taxi();
	load_looney_tunes();
	CHECK(!sqla_scan(0x0C0873CA, 6, rd16));
	CHECK(sqla_scan(0x0C0873CA, 6, rd16, true));
	CHECK(!sqla_scan(0x0C0873CA, 7, rd16, true));
	CHECK(!sqla_scan(0x0C0873CA, 0, rd16, true));		// dt R0 writes R0
	put(0x0C100080, { 0x2676, 0xF62B, 0x7620, 0x0683 });		// all three forms, then the PREF
	CHECK(sqla_scan(0x0C100080, 6, rd16, true));
	CHECK(!sqla_scan(0x0C100080, 6, rd16));
	put(0x0C100090, { 0x2676, 0x6623, 0x0683 });			// a step, then mov R2,R6
	CHECK(!sqla_scan(0x0C100090, 6, rd16, true));
	put(0x0C1000A0, { 0x2676, 0x6266, 0x0683 });			// a step, then mov.l @R6+,R2
	CHECK(!sqla_scan(0x0C1000A0, 6, rd16, true));
	CHECK(sqla_scan(0x0C081D6E, 6, rd16));
	CHECK(!sqla_scan(0x0C081D6E, 14, rd16));
	CHECK(sqla_scan(0x0C081D72, 6, rd16));
	CHECK(sqla_scan(0x8C081D72, 6, rd16));		// P1 and P2 mirrors
	CHECK(sqla_scan(0xAC0820DA, 6, rd16));
	CHECK(!sqla_scan(0x0C0820DA, 5, rd16));
	CHECK(!sqla_scan(0x0C0820E4, 6, rd16));
	put(0x0C100000, { 0x7620, 0x0683 });			// R6 changes first
	CHECK(!sqla_scan(0x0C100000, 6, rd16));
	put(0x0C100010, { 0xAFB8, 0x0009, 0x0683 });		// the slot is the last op
	CHECK(!sqla_scan(0x0C100010, 6, rd16));
	put(0x0C100020, { 0xAFB8, 0xA000, 0x0683 });		// a branch in the slot
	CHECK(!sqla_scan(0x0C100020, 6, rd16));
	put(0x0C100030, { 0x8900, 0x0683 });			// bt
	CHECK(!sqla_scan(0x0C100030, 6, rd16));
	put(0x0C100040, { 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0683 });
	CHECK(sqla_scan(0x0C100040, 6, rd16) == (SQLA_SCAN_OPS >= 6));
	put(0x0C100060, { 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0683 });
	CHECK(sqla_scan(0x0C100060, 6, rd16) == (SQLA_SCAN_OPS >= 7));
	const u32 before = g_reads;
	CHECK(!sqla_scan(0xA0000000, 6, rd16));		// BIOS, VRAM, P4, odd pc: no read at all
	CHECK(!sqla_scan(0x05000000, 6, rd16));
	CHECK(!sqla_scan(0xE0000000, 6, rd16));
	CHECK(!sqla_scan(0x0C081D73, 6, rd16));
	CHECK(g_reads == before);
}

static shil_param FR(u32 n) { return R(reg_fr_0 + n); }

// Block 0C081D5A as the decoder builds it (SZ=1 pair moves, bt.s with mov.l R7,@R6 in the slot).
static DecodedBlock loop_a()
{
	DecodedBlock b = mkblk(BET_Cond_1);
	b.start = 0x0C081D5A;
	b.BranchBlock = 0x0C081D72;
	b.NextBlock = 0x0C081D6E;
	b.oplist = {
		OP(shop_readm, DR(0), R(reg_r1), P(), 8),
		OP(shop_fmul, FR(6), FR(6), FR(7)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(2), 8),
		OP(shop_fmul, FR(5), FR(5), FR(7)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(0), 8),
		OP(shop_test, R(reg_sr_T), R(reg_r3), R(reg_r3)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(6), 8),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(4), 8),
		OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)),
		OP(shop_writem, P(), R(reg_r6), R(reg_r7), 4),
	};
	return b;
}

static std::vector<size_t> stores(const DecodedBlock& b)
{
	std::vector<size_t> v;
	for (size_t i = 0; i < b.oplist.size(); i++)
		if (b.oplist[i].op == shop_writem)
			v.push_back(i);
	return v;
}

static void directed_guess()
{
	load_crazy_taxi();
	DecodedBlock a = loop_a();
	for (size_t i : stores(a))
		CHECK(sqla_guess(&a, i, rd16));

	DecodedBlock t = a;					// only the taken path PREFs
	t.NextBlock = 0x0C0820E4;
	CHECK(sqla_guess(&t, 3, rd16));
	t.BranchBlock = 0x0C0820E4;
	CHECK(!sqla_guess(&t, 3, rd16));
	DecodedBlock u = a;					// only the fall-through PREFs
	u.BranchBlock = 0x0C0820E4;
	CHECK(sqla_guess(&u, 3, rd16));

	DecodedBlock s = a;					// static jump: BranchBlock only
	s.BlockType = BET_StaticJump;
	CHECK(sqla_guess(&s, 3, rd16));
	s.BranchBlock = 0x0C0820E4;
	CHECK(!sqla_guess(&s, 3, rd16));
	s.BranchBlock = 0x0C081D72;
	for (BlockEndType e : { BET_StaticCall, BET_DynamicJump, BET_DynamicCall, BET_DynamicRet, BET_StaticIntr })
	{
		s.BlockType = e;
		CHECK(!sqla_guess(&s, 3, rd16));
	}

	DecodedBlock m = a;					// R6 overwritten after the first store
	m.oplist.insert(m.oplist.begin() + 4, OP(shop_mov32, R(reg_r6), R(reg_r2)));
	CHECK(!sqla_guess(&m, 3, rd16));
	CHECK(sqla_guess(&m, 7, rd16));
	m.oplist[4] = OP(shop_add, R(reg_r6), R(reg_r2), I(8));	// not a step of R6
	CHECK(!sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_add, R(reg_r6), R(reg_r6), R(reg_r2));	// step by a register
	CHECK(!sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_add, R(reg_r6), R(reg_r6), I(32));	// step by an immediate
	CHECK(sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_ifb, P(), I(0), I(0), 0, I(0x0009));
	CHECK(!sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_sync_sr);
	CHECK(!sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_mov32, R(reg_r2), R(reg_r6));	// reads R6 only
	CHECK(sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_readm, R(reg_r6), R(reg_r1), P(), 4);	// load into R6
	CHECK(!sqla_guess(&m, 3, rd16));
	m.oplist[4] = OP(shop_div1, R(reg_r2), R(reg_r2), R(reg_r3), 0, P(), R(reg_r6));	// rd2 = R6
	CHECK(!sqla_guess(&m, 3, rd16));

	DecodedBlock r = a;					// other base registers
	r.oplist[13] = OP(shop_writem, P(), R(reg_r5), R(reg_r7), 4);
	CHECK(!sqla_guess(&r, 13, rd16));
	r.oplist[13] = OP(shop_writem, P(), R(reg_sr_T), R(reg_r7), 4);
	CHECK(!sqla_guess(&r, 13, rd16));
	r.oplist[13] = OP(shop_writem, P(), I(reg_r6), R(reg_r7), 4);	// an immediate address
	CHECK(!sqla_guess(&r, 13, rd16));

	// Plan 5.43, Looney Tunes 8C0873B4: stores through R6 with its steps, the block ends at fschg.
	load_looney_tunes();
	DecodedBlock l = mkblk(BET_StaticJump);
	l.start = 0x0C0873B4;
	l.BranchBlock = 0x0C0873CA;
	l.oplist = {
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(10), 8),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(4)),
		OP(shop_writem, P(), R(reg_r6), R(reg_r9), 4),
		OP(shop_fmul, FR(6), FR(6), FR(12)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(4)),
		OP(shop_writem, P(), R(reg_r6), R(reg_r8), 4),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(6), 8),
		OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(1 << 20)),
		OP(shop_sync_fpscr, P(), P(), P(), SYNC_FPSCR_SZ_ONLY),
	};
	for (size_t i : stores(l))
	{
		CHECK(!sqla_guess(&l, i, rd16));
		CHECK(sqla_guess(&l, i, rd16, true));
	}
	l.BranchBlock = 0x0C0820E4;				// no PREF there
	CHECK(!sqla_guess(&l, 1, rd16, true));
	DecodedBlock lc = l;					// a conditional end: either successor
	lc.BlockType = BET_Cond_0;
	lc.NextBlock = 0x0C0873CA;
	CHECK(sqla_guess(&lc, 1, rd16, true));
	CHECK(!sqla_guess(&lc, 1, rd16));
}

// Sonic Adventure 2 8C141BBA, the 2026-10-12 profile: the vertex loop on itself. It reads through R2 (`pref @R2`),
// stores through R6 five times, and flushes with `pref @R3; mov R6,R3`.
static void load_sonic_adventure_2()
{
	put(0x0C141BBA, { 0xF429, 0xF629, 0xF029, 0xF229, 0x0283, 0xF5FD, 0x7620, 0xF60B, 0xF662, 0xF3FD, 0xF67D, 0xF7BC,
	                  0x76FC, 0xF63B, 0xF592, 0xF3AC, 0xF482, 0x4110, 0x0383, 0x6363, 0xF06C, 0xF75E, 0xF66B, 0xF34E,
	                  0xF67B, 0xF3FD, 0xF62B, 0x8FE3, 0x7620 });
}

static DecodedBlock loop_sa2()
{
	DecodedBlock b = mkblk(BET_Cond_0);
	b.start = b.BranchBlock = 0x0C141BBA;
	b.NextBlock = 0x0C141BF4;
	const shil_opcode fschg[2] = { OP(shop_xor, R(reg_fpscr), R(reg_fpscr), I(1 << 20)),
	                               OP(shop_sync_fpscr, P(), P(), P(), SYNC_FPSCR_SZ_ONLY) };
	b.oplist = {
		OP(shop_readm, FR(4), R(reg_r2), P(), 4), OP(shop_add, R(reg_r2), R(reg_r2), I(4)),
		OP(shop_readm, FR(6), R(reg_r2), P(), 4), OP(shop_add, R(reg_r2), R(reg_r2), I(4)),
		OP(shop_readm, FR(0), R(reg_r2), P(), 4), OP(shop_add, R(reg_r2), R(reg_r2), I(4)),
		OP(shop_readm, FR(2), R(reg_r2), P(), 4), OP(shop_add, R(reg_r2), R(reg_r2), I(4)),
		OP(shop_pref, P(), R(reg_r2)),
		OP(shop_add, R(reg_r6), R(reg_r6), I(32)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(4)),
		OP(shop_writem, P(), R(reg_r6), FR(0), 4),
		OP(shop_fmul, FR(6), FR(6), FR(6)),
		fschg[0], fschg[1],
		OP(shop_add, R(reg_r6), R(reg_r6), I(0xFFFFFFFC)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(2), 8),
		OP(shop_fmul, FR(5), FR(5), FR(9)),
		OP(shop_sub, R(reg_r1), R(reg_r1), I(1)),
		OP(shop_seteq, R(reg_sr_T), R(reg_r1), I(0)),
		OP(shop_pref, P(), R(reg_r3)),
		OP(shop_mov32, R(reg_r3), R(reg_r6)),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(6), 8),
		OP(shop_sub, R(reg_r6), R(reg_r6), I(8)),
		OP(shop_writem, P(), R(reg_r6), DR(6), 8),
		fschg[0], fschg[1],
		OP(shop_sub, R(reg_r6), R(reg_r6), I(4)),
		OP(shop_writem, P(), R(reg_r6), FR(2), 4),
		OP(shop_jcond, R(reg_pc_dyn), R(reg_sr_T)),
		OP(shop_add, R(reg_r6), R(reg_r6), I(32)),
	};
	return b;
}

static size_t find_op(const DecodedBlock& b, shilop op, u32 reg)
{
	for (size_t j = 0; j < b.oplist.size(); j++)
		if (b.oplist[j].op == op && (op == shop_pref ? b.oplist[j].rs1 : b.oplist[j].rd)._reg == reg)
			return j;
	CHECK(false);
	return 0;
}

static void directed_copy()
{
	load_sonic_adventure_2();
	const DecodedBlock s = loop_sa2();
	CHECK(stores(s).size() == 5);
	for (size_t i : stores(s))
	{
		CHECK(sqcp_guess(&s, i));
		// The look-ahead does not take this loop at either level: its first six ops PREF through R2, not R6.
		CHECK(!sqla_guess(&s, i, rd16) && !sqla_guess(&s, i, rd16, true));
	}
	const size_t st = stores(s)[0], mv = find_op(s, shop_mov32, reg_r3), pf = find_op(s, shop_pref, reg_r3);
	CHECK(st < pf && pf < mv);

	DecodedBlock t = s;					// a copy of another register
	t.oplist[mv] = OP(shop_mov32, R(reg_r3), R(reg_r5));
	CHECK(!sqcp_guess(&t, st));
	t.oplist[mv] = OP(shop_add, R(reg_r3), R(reg_r6), I(0));	// R6 as a source, but not a mov
	CHECK(!sqcp_guess(&t, st));
	t.oplist[mv] = OP(shop_mov32, R(reg_r4), R(reg_r6));		// the copy goes where no PREF looks
	CHECK(!sqcp_guess(&t, st));
	t.oplist[mv] = OP(shop_mov32, R(reg_r3), I(reg_r6));		// a constant whose value is R6's number
	CHECK(!sqcp_guess(&t, st));
	t.oplist[mv] = OP(shop_mov32, R(reg_r4), R(reg_r6), P(), 0, P(), R(reg_r3));	// R3 as a second result: no copy
	CHECK(!sqcp_guess(&t, st));
	t = s;							// the PREF goes through another register, or a constant
	t.oplist[pf] = OP(shop_pref, P(), R(reg_r4));
	CHECK(!sqcp_guess(&t, st));
	t.oplist[pf] = OP(shop_pref, P(), I(0xE0000000));
	CHECK(!sqcp_guess(&t, st));
	t = s;							// the copy first: the PREF of the same turn
	std::swap(t.oplist[mv], t.oplist[pf]);
	for (size_t i : stores(t))
		CHECK(sqcp_guess(&t, i));

	// R3 with a second writer: a step, a load, another mov, an rd2.
	for (const shil_opcode& extra : { OP(shop_add, R(reg_r3), R(reg_r3), I(32)), OP(shop_readm, R(reg_r3), R(reg_r1), P(), 4),
	                                  OP(shop_mov32, R(reg_r3), R(reg_r5)),
	                                  OP(shop_div1, R(reg_r2), R(reg_r2), R(reg_r1), 0, P(), R(reg_r3)) })
		for (size_t at : { (size_t)2, s.oplist.size() })
		{
			t = s;
			t.oplist.insert(t.oplist.begin() + at, extra);
			CHECK(!sqcp_guess(&t, stores(t)[0]));
		}
	t = s;							// a second copy is no other writer
	t.oplist.insert(t.oplist.begin() + 2, OP(shop_mov32, R(reg_r3), R(reg_r6)));
	CHECK(sqcp_guess(&t, stores(t)[0]));
	for (const shil_opcode& stop : { OP(shop_ifb, P(), I(0), I(0), 0, I(0x0009)), OP(shop_sync_sr) })
		for (size_t at : { (size_t)0, s.oplist.size() })
		{
			t = s;
			t.oplist.insert(t.oplist.begin() + at, stop);
			CHECK(!sqcp_guess(&t, stores(t)[0]));
		}

	t = s;							// other bases: the copy's target, a third register, no GPR, an immediate
	for (const shil_param& base : { R(reg_r3), R(reg_r5), R(reg_sr_T), I(reg_r6) })
	{
		t.oplist[st] = OP(shop_writem, P(), base, FR(0), 4);
		CHECK(!sqcp_guess(&t, st));
	}

	// The loops of the other shapes hold no copy.
	const DecodedBlock a = loop_a();
	for (size_t i : stores(a))
		CHECK(!sqcp_guess(&a, i));
}

// The copy rule per register: a `pref @Rm`, a `mov Rn,Rm`, no other writer of Rm; no ifb and no sync_sr.
static bool ref_copy(const DecodedBlock& b, size_t i)
{
	const shil_param& s = b.oplist[i].rs1;
	if (!s.is_r32i() || s._reg > reg_r15)
		return false;
	for (const shil_opcode& o : b.oplist)
		if (o.op == shop_ifb || o.op == shop_sync_sr)
			return false;
	for (u32 m = 0; m < 16; m++)
	{
		bool pref = false, copy = false, other = false;
		for (const shil_opcode& o : b.oplist)
		{
			if (o.op == shop_pref)
			{
				pref |= o.rs1.is_r32i() && o.rs1._reg == m;
				continue;
			}
			const bool w = o.rd.is_r32i() && o.rd._reg == m, w2 = o.rd2.is_r32i() && o.rd2._reg == m;
			const bool c = w && o.op == shop_mov32 && o.rs1.is_r32i() && o.rs1._reg == s._reg;
			copy |= c;
			other |= (w && !c) || w2;
		}
		if (pref && copy && !other)
			return true;
	}
	return false;
}

static u32 fuzz_copy(u32 seed, u32 iters)
{
	std::mt19937 g(seed + 77);
	static const u32 regs[] = { reg_r2, reg_r3, reg_r4, reg_r6, reg_r15, reg_macl };
	auto rr = [&] { return R(regs[g() % 6]); };
	u32 bad = 0, hits = 0, sites = 0;
	for (u32 t = 0; t < iters; t++)
	{
		DecodedBlock b = mkblk(BET_Cond_0);
		const u32 len = 2 + g() % 7;
		for (u32 k = 0; k < len; k++)
			switch (g() % 16)
			{
			case 0: case 1: case 2: b.oplist.push_back(OP(shop_mov32, rr(), rr())); break;
			case 3: case 4: case 5: b.oplist.push_back(OP(shop_pref, P(), g() % 8 ? rr() : I(0xE0000000))); break;
			case 6: case 7: case 8: case 9:
				b.oplist.push_back(OP(shop_writem, P(), g() % 8 ? rr() : I(0x8C000000), FR(0), 4));
				break;
			case 10: { const shil_param x = rr(); b.oplist.push_back(OP(shop_add, x, x, I(4))); } break;
			case 11: b.oplist.push_back(OP(shop_readm, rr(), rr(), P(), 4)); break;
			case 12: b.oplist.push_back(OP(shop_mov32, rr(), I(g()))); break;
			case 13: b.oplist.push_back(OP(shop_mul_u64, rr(), rr(), rr(), 0, P(), rr())); break;
			case 14:
				b.oplist.push_back(g() % 6 ? OP(shop_fmul, FR(1), FR(1), FR(2))
				                   : g() & 1 ? OP(shop_sync_sr) : OP(shop_ifb, P(), I(0), I(0), 0, I(0x0009)));
				break;
			default: b.oplist.push_back(OP(shop_add, rr(), rr(), rr())); break;
			}
		for (size_t i : stores(b))
		{
			const bool got = sqcp_guess(&b, i), want = ref_copy(b, i);
			sites++;
			hits += got;
			if (got != want && bad++ < 8)
				printf("copy block %u store %u: got %d want %d (%u ops)\n", t, (u32)i, got, want, (u32)b.oplist.size());
		}
	}
	printf("copy fuzz: %u blocks, %u stores, %u hits, %u wrong\n", iters, sites, hits, bad);
	return bad + (hits < 100);
}

static u16 rand_op(std::mt19937& g)
{
	const u32 r = g(), n = (g() >> 4) & 15, m = (g() >> 8) & 15;
	switch (r % 16)
	{
	case 0: case 1: return 0x0083 | n << 8;			// pref @Rn
	case 2: return 0xF009 | n << 8 | m << 4;			// fmov @Rm+,FRn
	case 3: return 0xF00B | n << 8 | m << 4;			// fmov FRm,@-Rn
	case 4: return 0x2002 | n << 8 | m << 4;			// mov.l Rm,@Rn
	case 5: return 0x7000 | n << 8 | (g() & 0xFF);		// add #imm,Rn
	case 6: return (g() & 1 ? 0xA000 : 0x8D00) | (g() & 0xFF);	// bra, bt/s
	case 7: return g() & 1 ? 0x000B : 0x400B | n << 8;		// rts, jsr
	case 8: return 0x6003 | n << 8 | m << 4;			// mov Rm,Rn
	case 9: return 0x0009;
	case 10: return (0x2004 + g() % 3) | n << 8 | m << 4;	// mov.x Rm,@-Rn
	default: return (u16)g();
	}
}

static u32 fuzz_scan(u32 seed, u32 iters, bool steps)
{
	std::mt19937 g(seed);
	u32 bad = 0, hits = 0, missed = 0;
	for (u32 t = 0; t < iters; t++)
	{
		g_mem.clear();
		const u32 pc = 0x0C200000 + (g() & 0xFFFE);
		const u32 len = 1 + g() % 9;
		for (u32 k = 0; k < len; k++)
			g_mem[fold(pc + 2 * k)] = rand_op(g);
		const u32 n = g() & 15;
		const bool got = sqla_scan(pc, n, rd16, steps), want = ref_scan(pc, n, steps);
		hits += got;
		missed += want && !got;
		if (got && !want && bad++ < 8)
		{
			printf("scan pc=%08X n=%u:", pc, n);
			for (u32 k = 0; k < len; k++)
				printf(" %04X", g_mem[fold(pc + 2 * k)]);
			printf("\n");
		}
	}
	printf("scan fuzz%s: %u sequences, %u hits, %u reference hits not taken, %u wrong\n", steps ? " + steps" : "",
	       iters, hits, missed, bad);
	return bad;
}

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? atoi(argv[1]) : 1, iters = argc > 2 ? atoi(argv[2]) : 2000000;
	FILE* f = fopen(argc > 3 ? argv[3] : "/tmp/sqla_ref.bin", "rb");
	CHECK(f && fread(g_ref, sizeof(g_ref), 1, f) == 1);
	fclose(f);
	u32 bad = 0;
	// SQLA_NO_DIRECTED: only the reference checks, so mutate.py can count what they catch alone.
	if (!getenv("SQLA_NO_DIRECTED"))
	{
		directed_class();
		directed_scan();
		directed_guess();
		directed_copy();
		printf("directed tests: ok\n");
	}
	bad += check_class(false);
	bad += check_class(true);
	bad += fuzz_scan(seed, iters, false);
	bad += fuzz_scan(seed, iters, true);
	bad += fuzz_copy(seed, iters / 4);
	printf("%s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
