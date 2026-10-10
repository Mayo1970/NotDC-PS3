// Host-only check for plan 5.27 LOOK-AHEAD and its 5.43 steps: ps3_jit_sqla.h against a reference from the SH4 opcode
// list (gen.py), on the Crazy Taxi and Looney Tunes blocks and on random successor code. Not part of the PS3 build.
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
		printf("directed tests: ok\n");
	}
	bad += check_class(false);
	bad += check_class(true);
	bad += fuzz_scan(seed, iters, false);
	bad += fuzz_scan(seed, iters, true);
	printf("%s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
