// Host-only check for plan section 7, bundle 8: the decoder's JIT RM 2/3, JIT PR MOVES and JIT IDLE RULE code and
// SetFloatStatusReg()'s PS3 branch, cut out by cut.py, on mocks. Row OFF must be the code before the bundle.
#include <algorithm>
#include <cstdint>
#include <cstdio>

typedef uint32_t u32;
using std::min;

enum BlockEndType { BET_StaticJump = 0, BET_DynamicRet = 12, BET_Cond_0 = 16, BET_Cond_1 = 17 };

struct DecodedBlock
{
	u32 start, cycles, opcodes, BranchBlock;
	BlockEndType BlockType;
	bool idle_loop;
};
static DecodedBlock block;
static struct
{
	struct { bool FPR64, FSZ64, RoundToZero; } cpu;
	struct { bool has_readm, has_writem, has_fpu; } info;
} state;

static int g_pr_moves, g_rm_any, g_idle_rule, g_fpu_dn, g_verify_fails;
static bool g_pure;
extern "C" int get_jit_pr_moves_preset() { return g_pr_moves; }
extern "C" int get_jit_rm_any_preset() { return g_rm_any; }
extern "C" int get_jit_idle_rule_preset() { return g_idle_rule; }
extern "C" int get_fpu_dn_preset() { return g_fpu_dn; }
static u32 g_dec_pr_moves, g_dec_rule_blocks, g_dec_rm23_blocks;
static bool idle_turn_is_pure(const DecodedBlock&) { return g_pure; }
// The shape of types.h's macro: an if with a block, so an else after it does not parse.
#define verify(x) if((x)==false){ g_verify_fails++; }

#include "pr_free.inc"

// The gate as dec_generic has it: false = interpreter fallback.
static bool pr_gate(u32 op)
{
#include "pr_gate.inc"
	return true;
}

static void cycle_new(u32 max_cycles)
{
#include "cycle.inc"
}

static void cycle_old(u32 max_cycles)
{
#include "ref_cycle_old.inc"
}

struct fpscr_t { u32 RM, DN, SZ; };
static void rm_setup(fpscr_t fpu_cfg)
{
	auto& cpu = state.cpu;
#include "rm.inc"
}

static fpscr_t fpscr;
static int g_field7, g_field7_sets;
static void host_field7(int v) { g_field7 = v; g_field7_sets++; }
static void host_fpscr()
{
#include "fpscr_host.inc"
}

static int g_fail, g_checks;
static void check(bool ok, const char* what, u32 a, u32 b, u32 c)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 12)
			printf("FAIL %s: %08X %08X %08X\n", what, a, b, c);
		g_fail++;
	}
}

// The op names of dc/sh4/sh4_opcode_list.cpp: every fmov form, fsts and flds; and Flycast's list without fneg.
static bool want_pr_free(u32 op)
{
	const u32 nm = op & 0xF00F;
	return (nm >= 0xF006 && nm <= 0xF00C) || (op & 0xF0FF) == 0xF00D || (op & 0xF0FF) == 0xF01D;
}
static bool flycast_native(u32 op)
{
	return !(((op & 0xf) < 6 || (op & 0xf) > 0xc) && (op & 0xef) != 0x0d && (op & 0xff) != 0x4d);
}

static void test_pr()
{
	for (u32 op = 0xF000; op <= 0xFFFF; op++)
	{
		check(dec_is_pr_free(op) == want_pr_free(op), "pr_free list", op, dec_is_pr_free(op), 0);
		check(dec_is_pr_free(op) == (flycast_native(op) && (op & 0xFF) != 0x4D), "pr_free against Flycast", op, 0, 0);
		for (int pr = 0; pr < 2; pr++)
			for (g_pr_moves = 0; g_pr_moves < 2; g_pr_moves++)
			{
				state.cpu.FPR64 = pr != 0;
				const u32 n0 = g_dec_pr_moves;
				const bool native = pr_gate(op);
				// PR=0: every op native, as before. PR=1: only the row's ops, and each one counted.
				const bool want = !pr || (g_pr_moves && want_pr_free(op));
				check(native == want && g_dec_pr_moves - n0 == (u32)(pr && want), "pr gate", op, (u32)pr, (u32)g_pr_moves);
			}
	}
}

// Reicast's rule (libswirl/hw/sh4/dyna/decoder.cpp), on the same block.
static u32 reicast_cycles(u32 cycles, u32 max_cycles)
{
	if (state.info.has_readm && !state.info.has_writem && !state.info.has_fpu && block.opcodes < 6)
	{
		if (block.BlockType == BET_Cond_0 || (block.BlockType == BET_Cond_1 && block.BranchBlock <= block.start))
			cycles *= 3;
		if (block.BranchBlock == block.start)
			cycles *= 10;
	}
	if ((block.start & 0x1FFF0000) == 0x0C000000)
		cycles *= (block.start & 0x8000) ? 15 : 5;
	return min(cycles, max_cycles);
}

static void test_cycles()
{
	static const u32 starts[] = { 0x8C010000u, 0x8C19CC68u, 0x8C000100u, 0x8C008100u, 0x0C00F000u, 0xAC004000u };
	static const BlockEndType types[] = { BET_Cond_0, BET_Cond_1, BET_StaticJump, BET_DynamicRet };
	static const u32 plains[] = { 1, 3, 5, 8, 70, 223, 224, 230 };
	const u32 max_cycles = 224;
	for (u32 flags = 0; flags < 16; flags++)
	for (u32 opcodes = 1; opcodes <= 7; opcodes++)
	for (u32 ti = 0; ti < 4; ti++)
	for (u32 rel = 0; rel < 3; rel++)
	for (u32 si = 0; si < 6; si++)
	for (u32 pi = 0; pi < 8; pi++)
	for (g_idle_rule = 0; g_idle_rule < 3; g_idle_rule++)
	{
		state.info.has_readm = flags & 1;
		state.info.has_writem = (flags & 2) != 0;
		state.info.has_fpu = (flags & 4) != 0;
		g_pure = (flags & 8) != 0;
		DecodedBlock in;
		in.start = starts[si];
		in.cycles = plains[pi];
		in.opcodes = opcodes;
		in.BlockType = types[ti];
		in.BranchBlock = rel == 0 ? in.start - 16 : rel == 1 ? in.start : in.start + 16;
		in.idle_loop = false;

		block = in;
		cycle_old(max_cycles);
		const DecodedBlock old = block;

		block = in;
		const u32 n0 = g_dec_rule_blocks;
		cycle_new(max_cycles);
		const u32 want = g_idle_rule == 0 ? old.cycles : g_idle_rule == 1 ? reicast_cycles(in.cycles, max_cycles)
		                                                                 : min(in.cycles, max_cycles);
		const u32 key = (flags << 24) | (opcodes << 16) | (ti << 12) | (rel << 8) | (si << 4) | pi;
		check(block.cycles == want, "cycles", key, block.cycles, want);
		// JIT IDLE SKIP's flag does not follow the rule.
		check(block.idle_loop == old.idle_loop, "idle_loop", key, block.idle_loop, old.idle_loop);
		check(g_dec_rule_blocks - n0 == (u32)(block.cycles > in.cycles), "billed count", key, block.cycles, in.cycles);
		check(block.start == in.start && block.opcodes == in.opcodes && block.BranchBlock == in.BranchBlock &&
		      block.BlockType == in.BlockType, "block kept", key, 0, 0);
	}
	// The rule's own cases: the Looney Tunes loop tail (P71) at each level, and a wait loop on itself.
	static const struct { BlockEndType t; int rel; u32 want[3]; } k[] = {
		{ BET_Cond_0, 0, { 90, 9, 3 } }, { BET_Cond_0, 2, { 90, 9, 3 } }, { BET_Cond_1, 0, { 90, 9, 3 } },
		{ BET_Cond_1, 2, { 90, 3, 3 } }, { BET_Cond_0, 1, { 224, 90, 3 } },
	};
	for (u32 i = 0; i < 5; i++)
		for (g_idle_rule = 0; g_idle_rule < 3; g_idle_rule++)
		{
			state.info.has_readm = true;
			state.info.has_writem = state.info.has_fpu = false;
			block.start = 0x8C19CC68u;
			block.cycles = 3;
			block.opcodes = 3;
			block.BlockType = k[i].t;
			block.BranchBlock = block.start + (k[i].rel - 1) * 16;
			cycle_new(max_cycles);
			check(block.cycles == k[i].want[g_idle_rule], "rule case", i, (u32)g_idle_rule, block.cycles);
		}
}

static void test_rm()
{
	for (g_rm_any = 0; g_rm_any < 2; g_rm_any++)
		for (u32 rm = 0; rm < 4; rm++)
		{
			g_verify_fails = 0;
			const u32 n0 = g_dec_rm23_blocks;
			state.cpu.RoundToZero = rm != 1;
			rm_setup({ rm, 0, 0 });
			// VERIFY stops at RM 2 and 3, as before; ACCEPT compiles them as round to nearest and counts them.
			check(state.cpu.RoundToZero == (rm == 1), "RoundToZero", rm, (u32)g_rm_any, 0);
			check(g_verify_fails == (!g_rm_any && rm >= 2), "RM verify", rm, (u32)g_rm_any, (u32)g_verify_fails);
			check(g_dec_rm23_blocks - n0 == (u32)(g_rm_any && rm >= 2), "RM count", rm, (u32)g_rm_any, 0);
		}
}

static void test_host_fpscr()
{
	for (g_fpu_dn = 0; g_fpu_dn < 2; g_fpu_dn++)
		for (u32 rm = 0; rm < 4; rm++)
			for (u32 dn = 0; dn < 2; dn++)
			{
				fpscr.RM = rm;
				fpscr.DN = dn;
				g_field7 = -1;
				g_field7_sets = 0;
				host_fpscr();
				// Field 7 = XE, NI, RN: toward zero for RM 1, else nearest; NI only with the row and DN.
				const int want = (rm == 1 ? 1 : 0) | (g_fpu_dn && dn ? 4 : 0);
				check(g_field7 == want && g_field7_sets == 1, "host FPSCR", rm, dn, (u32)g_field7);
			}
}

int main()
{
	test_pr();
	test_cycles();
	test_rm();
	test_host_fpscr();
	printf("%s: decoder: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
