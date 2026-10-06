// Host-only check for plan 5.31: ps3_jit_greuse.h's plan on blocks run by constprop_check's SHIL interpreter, and
// its emitters against ppu-as. A REUSE op may skip its check only where that check passes.
#define main constprop_main
#include "../constprop_check/constprop_check.cpp"
#undef main

static u32 g_code[64];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }
#include "emitter/PPCEmit/ppc_emitter.h"

// mutate.py swaps in an edited copy.
#ifndef GREUSE_HEADER
#define GREUSE_HEADER "ps3/ps3_jit_greuse.h"
#endif
#include GREUSE_HEADER
#include "greuse_words.inc"

// The reach and the flag bits the driver relies on: a header that differs must fail here.
static const s32 TEST_LIMIT = 64;
static const u32 FLAG_LO = 8, FLAG_HI = 20;   // cr2-cr4, which C calls keep

// The two checks, as fm_emit_guard and emit_sq_check test an address.
static bool ram_ok(u32 ea) { return ((ea >> 26) & 7) == 3 && (ea >> 29) != 7; }
static bool sq_ok(u32 ea) { return (ea >> 26) == 0x38; }
static bool ok_of(u8 k, u32 ea) { return k == GR_SQ ? sq_ok(ea) : ram_ok(ea); }

// The documented exception: the checked address within TEST_LIMIT (+8 for a pair) of its 64 MB area's ends.
static bool near_edge(u32 ea)
{
	const u32 o = ea & 0x03FFFFFF;
	return o < (u32)TEST_LIMIT || o >= 0x04000000u - (u32)TEST_LIMIT - 8;
}

static u32 g_blocks, g_reuse, g_fast, g_cold, g_edge, g_validate, g_bits_full;

// The driver's kinds: readm and writem with a GPR base and an immediate or no offset need the RAM check; a
// 32-bit or pair store with a later pref on its base takes the SQ shape. shuffle: any kind, to stress the plan.
static void kinds_of(const DecodedBlock& b, u8* kind, Gen* shuffle)
{
	for (size_t i = 0; i < b.oplist.size(); i++)
	{
		const shil_opcode& o = b.oplist[i];
		u8 k = GR_NONE;
		if ((o.op == shop_readm || o.op == shop_writem) && o.rs1.is_r32i() && (o.rs3.is_null() || o.rs3.is_imm()))
		{
			k = GR_RAM;
			if (o.op == shop_writem && (o.flags == 4 || o.flags == 8))
				for (size_t j = i + 1; j < b.oplist.size(); j++)
					if (b.oplist[j].op == shop_pref && b.oplist[j].rs1.is_reg() && b.oplist[j].rs1._reg == o.rs1._reg)
						k = GR_SQ;
			if (shuffle && shuffle->rnd(4) == 0)
				k = shuffle->rnd(2) ? GR_RAM : GR_SQ;
		}
		kind[i] = k;
	}
}

static void fail(const char* what, const DecodedBlock& b, const GrPlan& p, int at)
{
	printf("FAIL %s at op %d\n", what, at);
	for (u32 i = 0; i < p.n; i++)
		printf("  op %u role %u bit %u\n", i, p.role[i], p.bit[i]);
	dump_block(what, b);
	fflush(stdout);
	exit(1);
}

// Each REUSE names the flag of an earlier VALIDATE on the same base and kind, through only immediate adds and
// subtracts of that base, within the reach; flags are cr2-cr4, one per VALIDATE; a VALIDATE has a REUSE.
static void check_structure(const DecodedBlock& b, const GrPlan& p, const u8* kind)
{
	if (p.n == 0)
		return;
	if (p.n != b.oplist.size())
		fail("plan size", b, p, -1);
	int owner[32];
	for (u32 k = 0; k < 32; k++)
		owner[k] = -1;
	std::vector<int> used(p.n, 0);
	for (u32 i = 0; i < p.n; i++)
	{
		const shil_opcode& o = b.oplist[i];
		if (p.role[i] == GR_NORMAL)
			continue;
		if (kind[i] == GR_NONE || !o.rs1.is_r32i() || o.rs1._reg > reg_r15)
			fail("role on an op that is no candidate", b, p, (int)i);
		if (p.bit[i] < FLAG_LO || p.bit[i] >= FLAG_HI)
			fail("flag outside cr2-cr4", b, p, (int)i);
		if (p.role[i] == GR_VALIDATE)
		{
			if (owner[p.bit[i]] >= 0)
				fail("two VALIDATEs share a flag", b, p, (int)i);
			owner[p.bit[i]] = (int)i;
			continue;
		}
		const int v = owner[p.bit[i]];
		if (v < 0 || v >= (int)i)
			fail("REUSE before its VALIDATE", b, p, (int)i);
		const shil_opcode& ov = b.oplist[v];
		if (ov.rs1._reg != o.rs1._reg || kind[v] != kind[i])
			fail("REUSE of another base or kind", b, p, (int)i);
		const u32 r = o.rs1._reg;
		s64 d = 0;
		for (int j = v; j < (int)i; j++)
		{
			const shil_opcode& w = b.oplist[j];
			if (w.op == shop_ifb || w.op == shop_sync_sr)
				fail("REUSE across ifb or sync_sr", b, p, (int)i);
			const bool wr = (w.rd.is_reg() && (u32)w.rd._reg <= r && r < (u32)w.rd._reg + (u32)w.rd.count()) ||
			                (w.rd2.is_reg() && (u32)w.rd2._reg <= r && r < (u32)w.rd2._reg + (u32)w.rd2.count());
			if (!wr)
				continue;
			if ((w.op == shop_add || w.op == shop_sub) && w.rd.is_r32i() && w.rd._reg == r && w.rs1.is_r32i() &&
			    w.rs1._reg == r && w.rs2.is_imm() && w.rd2.is_null())
				d += w.op == shop_add ? (s64)(s32)w.rs2._imm : -(s64)(s32)w.rs2._imm;
			else
				fail("REUSE after the base was rewritten", b, p, (int)i);
		}
		const s32 off_v = ov.rs3.is_imm() ? (s32)ov.rs3._imm : 0, off = o.rs3.is_imm() ? (s32)o.rs3._imm : 0;
		const s64 dist = d + off - off_v, sz = o.flags == 8 ? 8 : (s64)o.flags;
		if (dist < -TEST_LIMIT || dist + sz > TEST_LIMIT)
			fail("REUSE beyond the reach", b, p, (int)i);
		used[v] = 1;
	}
	for (u32 i = 0; i < p.n; i++)
		if (p.role[i] == GR_VALIDATE && !used[i])
			fail("VALIDATE with no REUSE", b, p, (int)i);
}

// At run time a VALIDATE's fast path sets its flag, its cold path clears it; a REUSE with the flag set runs unchecked
// (ops clobber cr0-cr1). Unchecked must be right unless the checked address was near its area's end.
static void check_run(const DecodedBlock& b, const GrPlan& p, const u8* kind, Interp st, u32 seed)
{
	u32 cr = hash32(seed);
	u32 ea_v[32] = {};
	for (u32 i = 0; i < p.n; i++)
	{
		const shil_opcode& o = b.oplist[i];
		cr = (cr & 0x00FFFFFFu) | (hash32(seed * 131 + i) & 0xFF000000u);
		if (p.role[i] != GR_NORMAL)
		{
			const u32 ea = st.r[o.rs1._reg] + (o.rs3.is_imm() ? o.rs3._imm : 0);
			const u32 m = 0x80000000u >> p.bit[i];
			if (p.role[i] == GR_VALIDATE)
			{
				cr = ok_of(kind[i], ea) ? cr | m : cr & ~m;
				ea_v[p.bit[i]] = ea;
				g_validate++;
			}
			else if (cr & m)
			{
				g_fast++;
				if (!ok_of(kind[i], ea))
				{
					if (!near_edge(ea_v[p.bit[i]]))
					{
						printf("checked %08X, this op %08X (kind %u)\n", ea_v[p.bit[i]], ea, kind[i]);
						fail("REUSE fast path on an address its check would refuse", b, p, (int)i);
					}
					g_edge++;
				}
			}
			else
				g_cold++;
		}
		st.step(o);
	}
}

static void plan_and_check(const DecodedBlock& b, const u8* kind, Interp* states, u32 n_states, u32 seed)
{
	static GrPlan p;
	gr_plan(p, b, kind);
	check_structure(b, p, kind);
	g_blocks++;
	g_reuse += p.reuses;
	u32 nv = 0;
	for (u32 i = 0; i < p.n; i++)
		nv += p.role[i] == GR_VALIDATE;
	if (nv != p.validates)
		fail("validates count", b, p, -1);
	g_bits_full += nv == GR_BITS;
	for (u32 s = 0; s < n_states; s++)
		check_run(b, p, kind, states[s], seed + s);
}

// ---- generator ----------------------------------------------------------------------

struct GGen : Gen
{
	explicit GGen(u32 seed) : Gen(seed) {}

	// RAM, store queue, near an area's end, VRAM, anything.
	u32 near_addr()
	{
		static const u32 edges[] = {0x8C000000u, 0x90000000u, 0x0C000000u, 0x10000000u, 0xAC000000u, 0xB0000000u,
		                            0xCC000000u, 0xD0000000u, 0xE0000000u, 0xE4000000u, 0xEC000000u, 0xF0000000u};
		switch (rnd(7))
		{
		case 0: return 0x8C000000u + (r32() & 0xFFFFFC);
		case 1: return 0xE0000000u + (r32() & 0x03FFFFFC);
		case 2: case 3: return edges[rnd(12)] + (u32)((s32)rnd(320) - 160);
		case 4: return 0xE0000000u + 32 * rnd(64);
		case 5: return 0xA5000000u + (r32() & 0xFFFFFC);
		default: return r32();
		}
	}
	// What a base register holds: mostly RAM or the store queue, often near an area's end.
	u32 base_addr()
	{
		switch (rnd(20))
		{
		case 0: return r32();
		case 1: case 2: case 3: case 4: return near_addr();
		case 5: case 6: case 7: case 8: case 9: case 10: return 0xE0000000u + (r32() & 0x03FFFFFC);
		default: return (rnd(2) ? 0x8C000000u : 0x0C000000u) + (r32() & 0x03FFFFFC);
		}
	}
	u32 nbase = 5;
	u32 base() { return reg_r0 + rnd(nbase); }
	s32 disp()
	{
		switch (rnd(4))
		{
		case 0: return 4 * (s32)rnd(16);
		case 1: return -4 * (s32)rnd(16);
		case 2: return 8 * ((s32)rnd(24) - 12);
		default: return (s32)(r32() & 0xFFFC) - 0x8000;
		}
	}
	u32 sz() { static const u32 s[] = {1, 2, 4, 4, 8}; return s[rnd(5)]; }

	void mem_op(DecodedBlock& b)
	{
		auto& L = b.oplist;
		const u32 rn = base(), rm = gp(), s = sz();
		const shil_param data = s == 8 ? DR(2 * rnd(8)) : R(rm);
		const shil_param dest = s == 8 ? DR(2 * rnd(8)) : R(gp());
		switch (rnd(12))
		{
		case 0: L.push_back(OP(shop_readm, dest, R(rn), P(), s)); break;
		case 1: L.push_back(OP(shop_readm, dest, R(rn), P(), s, I((u32)disp()))); break;
		case 2:   // @Rm+
			L.push_back(OP(shop_readm, dest, R(rn), P(), s));
			L.push_back(OP(shop_add, R(rn), R(rn), I(s)));
			break;
		case 3: L.push_back(OP(shop_writem, P(), R(rn), data, s)); break;
		case 4: L.push_back(OP(shop_writem, P(), R(rn), data, s, I((u32)disp()))); break;
		case 5:   // @-Rn, as the decoder emits it
			L.push_back(OP(shop_sub, R(rn), R(rn), I(s)));
			L.push_back(OP(shop_writem, P(), R(rn), data, s));
			break;
		case 6:
			L.push_back(OP(rnd(2) ? shop_add : shop_sub, R(rn), R(rn), I((u32)(rnd(3) ? (s32)rnd(80) - 40 : disp()))));
			break;
		case 7: L.push_back(OP(shop_pref, P(), R(rn))); break;
		case 8: L.push_back(OP(shop_mov32, R(rn), I(base_addr()))); break;
		case 9: L.push_back(OP(shop_readm, R(rn), R(rn), P(), 4)); break;   // the base loads itself
		case 10:
			if (rnd(2))
				L.push_back(OP(shop_add, R(rn), R(rn), R(rm)));
			else   // rd = rn from another source plus an immediate: a new value, not a step
				L.push_back(OP(rnd(2) ? shop_add : shop_sub, R(rn), R(rm == rn ? reg_r7 : rm), I(4 * rnd(8))));
			break;
		default: L.push_back(OP(shop_readm, dest, R(rn), P(), s, R(reg_r0))); break;   // @(R0,Rn): no candidate
		}
	}

	void block(DecodedBlock& b)
	{
		auto& L = b.oplist;
		nbase = rnd(8) ? 5 : 14;
		for (u32 i = 0, n = rnd(8) ? 2 + rnd(40) : 80 + rnd(160); i < n; i++)
		{
			const u32 k = rnd(20);
			if (k < 13)
				mem_op(b);
			else if (k < 17)
				op(b);   // constprop_check's decoder shapes
			else if (k == 17)
				L.push_back(OP(shop_ifb, P(), I(0), I(r32()), 0, I(r32() & 0xFFFF)));
			else if (k == 18)
				L.push_back(OP(shop_sync_sr));
			else   // an op whose second destination is a GPR
				L.push_back(OP(shop_adc, R(gp()), R(gp()), R(gp()), 0, R(reg_sr_T), R(base())));
		}
	}

	Interp state(u32 seed)
	{
		Interp s = init_state(seed);
		for (u32 r = reg_r0; r <= reg_r15; r++)
			s.r[r] = base_addr();
		for (u32 k = 0; k < 8; k++)
			s.bank[k] = near_addr();
		return s;
	}
};

// ---- directed tests ---------------------------------------------------------------------

static void expect_roles(const char* what, const DecodedBlock& b, const u8* want)
{
	u8 kind[GR_MAX_OPS];
	kinds_of(b, kind, nullptr);
	static GrPlan p;
	gr_plan(p, b, kind);
	check_structure(b, p, kind);
	for (u32 i = 0; i < b.oplist.size(); i++)
		if (p.role[i] != want[i])
		{
			printf("%s: op %u role %u, want %u\n", what, i, p.role[i], want[i]);
			fail(what, b, p, (int)i);
		}
}

enum { N = GR_NORMAL, V = GR_VALIDATE, U = GR_REUSE };

// Crazy Taxi's vertex loop 0C081BC4 in SHIL (5.18 dump): SQ stores through R6, pair loads through R1, loads
// through R4; the prefs at the end. Every second access of a base reuses the first one's check.
static void t_vertex_loop()
{
	DecodedBlock b = mkblk();
	auto& L = b.oplist;
	u8 want[64];
	u32 n = 0;
	auto add = [&](shil_opcode o, u8 r) { L.push_back(o); want[n++] = r; };
	add(OP(shop_fadd, R(reg_fr_2), R(reg_fr_2), R(reg_fr_3)), N);
	add(OP(shop_add, R(reg_r3), R(reg_r3), I((u32)-1)), N);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), N);
	add(OP(shop_writem, P(), R(reg_r6), DR(2), 8), V);
	add(OP(shop_pref, P(), R(reg_r4)), N);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I((u32)-32)), N);
	add(OP(shop_readm, DR(10), R(reg_r1), P(), 8), V);
	add(OP(shop_add, R(reg_r1), R(reg_r1), I(8)), N);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), N);
	add(OP(shop_writem, P(), R(reg_r6), DR(0), 8), U);
	add(OP(shop_readm, DR(0), R(reg_r1), P(), 8), U);
	add(OP(shop_add, R(reg_r1), R(reg_r1), I(8)), N);
	add(OP(shop_add, R(reg_r5), R(reg_r5), I(32)), N);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), N);
	add(OP(shop_writem, P(), R(reg_r6), DR(6), 8), U);
	add(OP(shop_sub, R(reg_r6), R(reg_r6), I(8)), N);
	add(OP(shop_writem, P(), R(reg_r6), DR(4), 8), U);
	add(OP(shop_writem, P(), R(reg_r6), R(reg_r2), 4), U);
	add(OP(shop_mov32, R(reg_r2), R(reg_r6)), N);
	add(OP(shop_pref, P(), R(reg_r6)), N);
	add(OP(shop_add, R(reg_r6), R(reg_r6), I(64)), N);
	add(OP(shop_readm, R(reg_r0), R(reg_r4), P(), 4), V);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I(4)), N);
	add(OP(shop_readm, R(reg_r14), R(reg_r4), P(), 4), U);
	add(OP(shop_add, R(reg_r4), R(reg_r4), I(4)), N);
	expect_roles("vertex loop", b, want);
}

// What ends a group: another write of the base, ifb, sync_sr, a second destination, the reach, another kind.
static void t_cuts()
{
	struct Case { const char* what; shil_opcode mid; u8 second; };
	const Case cases[] = {
		{"mov", OP(shop_mov32, R(reg_r1), R(reg_r2)), V},
		{"add reg", OP(shop_add, R(reg_r1), R(reg_r1), R(reg_r2)), V},
		{"add from another reg", OP(shop_add, R(reg_r1), R(reg_r2), I(4)), V},
		{"sub from another reg", OP(shop_sub, R(reg_r1), R(reg_r2), I(4)), V},
		{"shift", OP(shop_shl, R(reg_r1), R(reg_r1), I(2)), V},
		{"load into the base", OP(shop_readm, R(reg_r1), R(reg_r3), P(), 4), V},
		{"ifb", OP(shop_ifb, P(), I(0), I(5), 0, I(6)), V},
		{"sync_sr", OP(shop_sync_sr), V},
		{"rd2", OP(shop_adc, R(reg_r2), R(reg_r2), R(reg_r3), 0, R(reg_sr_T), R(reg_r1)), V},
		{"far add", OP(shop_add, R(reg_r1), R(reg_r1), I(64)), V},
		{"near add", OP(shop_add, R(reg_r1), R(reg_r1), I(60)), U},
		{"sub", OP(shop_sub, R(reg_r1), R(reg_r1), I(64)), U},
		{"other base", OP(shop_mov32, R(reg_r2), R(reg_r1)), U},
	};
	for (const Case& c : cases)
	{
		DecodedBlock b = mkblk();
		b.oplist.push_back(OP(shop_readm, R(reg_r5), R(reg_r1), P(), 4));
		b.oplist.push_back(c.mid);
		b.oplist.push_back(OP(shop_readm, R(reg_r6), R(reg_r1), P(), 4));
		b.oplist.push_back(OP(shop_readm, R(reg_r7), R(reg_r1), P(), 4));
		// The first load validates for whichever later load can reuse it; with a cut, the second one does.
		const u8 want[] = {c.second == U ? (u8)V : (u8)N, N, c.second, U};
		expect_roles(c.what, b, want);
	}
	// A RAM check never stands in for the SQ one, nor the reverse.
	DecodedBlock b = mkblk();
	b.oplist.push_back(OP(shop_readm, R(reg_r5), R(reg_r1), P(), 4));
	b.oplist.push_back(OP(shop_writem, P(), R(reg_r1), R(reg_r2), 4));
	b.oplist.push_back(OP(shop_pref, P(), R(reg_r1)));
	const u8 want[] = {N, N, N};
	expect_roles("kind", b, want);
	// Offsets count: after @Rn, @(60,Rn) and 8 bytes at @(-64,Rn) reuse; 8 bytes at 57 does not and checks
	// again, so @(64,Rn) reuses that check.
	DecodedBlock o = mkblk();
	o.oplist.push_back(OP(shop_readm, R(reg_r5), R(reg_r1), P(), 4));
	o.oplist.push_back(OP(shop_readm, R(reg_r6), R(reg_r1), P(), 4, I(60)));
	o.oplist.push_back(OP(shop_readm, DR(0), R(reg_r1), P(), 8, I((u32)-64)));
	o.oplist.push_back(OP(shop_readm, DR(2), R(reg_r1), P(), 8, I(57)));
	o.oplist.push_back(OP(shop_readm, R(reg_r7), R(reg_r1), P(), 4, I(64)));
	const u8 want_o[] = {V, U, U, V, U};
	expect_roles("offsets", o, want_o);
}

// Twelve groups take the twelve flags; a thirteenth gets none.
static void t_flags()
{
	DecodedBlock b = mkblk();
	u8 want[64];
	u32 n = 0;
	for (u32 g = 0; g < 13; g++)
	{
		const u32 r = reg_r0 + g;
		b.oplist.push_back(OP(shop_mov32, R(r), I(0x8C001000 + 0x100 * g)));
		want[n++] = N;
		b.oplist.push_back(OP(shop_readm, R(reg_r14), R(r), P(), 4));
		want[n++] = g < 12 ? V : N;
		b.oplist.push_back(OP(shop_readm, R(reg_r14), R(r), P(), 4, I(4)));
		want[n++] = g < 12 ? U : N;
	}
	expect_roles("flags", b, want);
}

static void test_words()
{
	g_n = 0;
	emit_gr_set(8);
	emit_gr_set(19);
	emit_gr_clear(8);
	emit_gr_clear(19);
	emit_gr_branch(8);
	emit_gr_branch(19);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	CHECK(g_n == want);
	for (u32 i = 0; i < want; i++)
		if (g_code[i] != k_ref[i])
		{
			printf("FAIL words: word %u: %08X, ppu-as %08X\n", i, g_code[i], k_ref[i]);
			exit(1);
		}
	CHECK(GR_BIT0 >= FLAG_LO && GR_BIT0 + GR_BITS <= FLAG_HI && GR_LIMIT == TEST_LIMIT);
}

int main(int argc, char** argv)
{
	const u32 seed = argc > 1 ? (u32)strtoul(argv[1], nullptr, 0) : 1;
	const u32 iters = argc > 2 ? (u32)strtoul(argv[2], nullptr, 0) : 200000;

	// GREUSE_FUZZ_ONLY: mutate.py checks which planted bugs the fuzzer alone catches.
	if (!getenv("GREUSE_FUZZ_ONLY"))
	{
		test_words();
		t_vertex_loop();
		t_cuts();
		t_flags();
		printf("directed tests: ok\n");
	}

	GGen g(seed);
	for (u32 it = 0; it < iters; it++)
	{
		g.pool.clear();
		for (u32 i = 0; i < 6; i++)
			g.pool.push_back(g.near_addr() & ~3u);
		DecodedBlock b = mkblk();
		g.block(b);
		if (b.oplist.size() > GR_MAX_OPS)
			continue;
		u8 kind[GR_MAX_OPS];
		kinds_of(b, kind, (it & 1) ? &g : nullptr);
		Interp states[4];
		for (u32 s = 0; s < 4; s++)
			states[s] = g.state(it * 4 + s);
		plan_and_check(b, kind, states, 4, it * 7);
	}
	printf("fuzz: %u blocks, %u REUSE ops (%u blocks with all 12 flags); %u checks set a flag; at run time %u fast"
	       " (%u past an area's end from a checked address near it), %u cold\n",
	       g_blocks, g_reuse, g_bits_full, g_validate, g_fast, g_edge, g_cold);
	// Every case must be common enough, or the fuzzer proves little about it.
	CHECK(g_reuse > iters && g_fast > iters && g_cold > iters / 2 && g_edge > iters / 1000);
	printf("PASS\n");
	return 0;
}
