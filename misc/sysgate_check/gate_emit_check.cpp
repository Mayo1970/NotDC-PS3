// Plan 5.36 PC check: ps3_jit_gate.h and the cycle exit of ps3_driver.cpp against ppu-as words (gate_words.inc),
// then the same words on a PPC64 model: the count, the branch on count < due, and what each path leaves behind.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

#define CODE_WORDS 64
static u32 g_code[CODE_WORDS];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_gate.h"
#include "gate_words.inc"

static int g_fail, g_checks;

static void check(bool ok, const char* what, const char* detail)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 12)
			printf("FAIL %s: %s\n", what, detail);
		g_fail++;
	}
}

static u64 g_seed = 0x2545F4914F6CDD1Dull;
static u64 rnd64()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return g_seed;
}

// The layout of gate_words.s: word indices of the marks and the entries.
enum { W_NU = 0, W_GATE = 2, W_LU = 8, W_LG = 9, W_RV = 18, W_COLD = 19, W_END = 24 };
static const u32 OFS_ELAPSED = 304, OFS_DUE = 308, OFS_PC = 272;
static const s32 TS = 1792;
static const u32 START_PC = 0x8C012345u;

static void b_to(u32 target, u32 lk)
{
	ppc_bx((u32)((s32)(target - g_n) & 0xFFFFFF), 0, lk);
}

// As ps3_driver.cpp emits them: the main loop's GATE count and blt, the RESUME entry, a block's cycle exit.
static void emit_all()
{
	g_n = 0;
	ppc_emit(0x60000000);
	ppc_emit(0x60000000);
	emit_gate_count(30, 11, 12, OFS_ELAPSED, OFS_DUE);
	ppc_bcx(BO_TRUE, BI_CR0_LT, (u32)(s32)(W_NU - (s32)g_n), 0, 0);
	ppc_emit(0x60000000);
	emit_gate_resume(30, 29, TS, 11, 12, OFS_ELAPSED, OFS_DUE, 3, OFS_PC);
	b_to(W_LU, 0);
	ppc_emit(0x60000000);
	ppc_addis(3, 0, START_PC >> 16);
	ppc_ori(3, 3, START_PC & 0xFFFF);
	ppc_emit(0x78000000 | (3 << 21) | (3 << 16) | 0x20);   // clrldi r3,r3,32, as ps3_driver.cpp's ppc_clrldi32
	b_to(W_LG, 1);
	b_to(W_RV, 0);
}

static void test_words()
{
	emit_all();
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want && g_n == W_END, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// PPC64 model of the instructions above; big-endian context memory at CTX.
static const u64 CTX = 0x10000000ull;

struct Cpu
{
	u64 r[32], lr;
	u32 cr;
	u8 mem[512];
	const char* why;

	void bad(const char* w) { if (!why) why = w; }
	u8* at(u64 a, u32 n)
	{
		if (a < CTX || a + n > CTX + sizeof(mem))
		{
			bad("memory access outside the context");
			return 0;
		}
		return &mem[a - CTX];
	}
	u32 ld4(u64 a)
	{
		u8* p = at(a, 4);
		return p ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3] : 0;
	}
	void st4(u64 a, u32 v)
	{
		u8* p = at(a, 4);
		if (p) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
	}
	bool cond(u32 bo, u32 bi) const
	{
		if (bo & 0x10)
			return true;
		const bool bit = (cr >> (31 - bi)) & 1;
		return (bo & 8) ? bit : !bit;
	}

	// Runs from word start until a mark word (stop set) is reached; returns it, or -1 with why set.
	int run(u32 start, u64 stops, int limit = 64)
	{
		u32 idx = start;
		while (!why && limit-- > 0)
		{
			if (idx >= W_END) { bad("ran off the code"); return -1; }
			if (idx != start && ((stops >> idx) & 1))
				return (int)idx;
			const u32 x = g_code[idx], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 here = (u64)(uintptr_t)&g_code[idx];
			idx++;
			switch (op)
			{
			case 14: r[d] = (a ? r[a] : 0) + (u64)simm; break;                       // addi
			case 15: r[d] = (a ? r[a] : 0) + (u64)(simm << 16); break;               // addis
			case 24: r[a] = r[d] | (x & 0xFFFF); break;                              // ori
			case 30:                                                                 // clrldi ra,rs,32 only
				if (x != (0x78000020u | (d << 21) | (a << 16)))
				{ bad("rldicl form"); return -1; }
				r[a] = r[d] & 0xFFFFFFFFull;
				break;
			case 32: r[d] = ld4((a ? r[a] : 0) + (u64)simm); break;                  // lwz
			case 36: st4((a ? r[a] : 0) + (u64)simm, (u32)r[d]); break;              // stw
			case 31:
				if (((x >> 1) & 0x3FF) == 0 && !(x & (1u << 21)) && !(x & 1))       // cmpw crf,ra,rb
				{
					const u32 crf = d >> 2;
					const s32 va = (s32)r[a], vb = (s32)r[b];
					const u32 c = va < vb ? 8 : va > vb ? 4 : 2;
					cr = (cr & ~(0xF0000000u >> (4 * crf))) | (c << (28 - 4 * crf));
					break;
				}
				bad("op 31");
				return -1;
			case 16:                                                                 // bc
			{
				if (x & 3) { bad("bc form"); return -1; }
				if (cond(d, a))
				{
					const s32 disp = (s32)(s16)(x & 0xFFFC);
					idx = (u32)((s32)(idx - 1) + disp / 4);
				}
				break;
			}
			case 18:                                                                 // b, bl
			{
				if (x & 2) { bad("absolute branch"); return -1; }
				s32 disp = (s32)(x & 0x03FFFFFC);
				if (disp & 0x02000000) disp -= 0x04000000;
				if (x & 1) lr = here + 4;
				idx = (u32)((s32)(idx - 1) + disp / 4);
				break;
			}
			case 19:                                                                 // bclr
				if (((x >> 1) & 0x3FF) == 16 && !(x & 1) && b == 0)
				{
					if (cond(d, a))
					{
						const u64 base = (u64)(uintptr_t)g_code;
						if (lr < base || lr >= base + W_END * 4 || (lr & 3)) { bad("blr outside the code"); return -1; }
						idx = (u32)((lr - base) >> 2);
					}
					break;
				}
				bad("op 19");
				return -1;
			default:
				bad("opcode outside the model");
				return -1;
			}
		}
		if (!why)
			bad("no mark reached");
		return -1;
	}
};

// Counts and dues around the signed edges, and plain random ones.
static s32 pick()
{
	static const s32 edge[] = { 0, 1, -1, 2, 3, 4, 7, 8, 0x7FFFFFFF, (s32)0x80000000, 0x7FFFFFFE, -2 };
	const u32 k = (u32)(rnd64() % 3);
	if (k == 0)
		return edge[rnd64() % (sizeof(edge) / sizeof(edge[0]))];
	if (k == 1)
		return (s32)(rnd64() % 17) - 4;
	return (s32)rnd64();
}

static void setup(Cpu& c, s32 elapsed, s32 due, u64 r0[32], u32& cr0, u64& lr0)
{
	memset(&c, 0, sizeof(c));
	for (u32 i = 0; i < sizeof(c.mem); i++)
		c.mem[i] = (u8)rnd64();
	for (u32 i = 0; i < 32; i++)
		c.r[i] = r0[i] = rnd64();
	c.r[30] = r0[30] = CTX;
	c.r[29] = r0[29] = (u64)(s64)(s32)(rnd64() % 4000) - 2000;
	c.st4(CTX + OFS_ELAPSED, (u32)elapsed);
	c.st4(CTX + OFS_DUE, (u32)due);
	c.cr = cr0 = (u32)rnd64();
	c.lr = lr0 = rnd64();
}

static void test_model(int rounds)
{
	emit_all();
	const u64 base = (u64)(uintptr_t)g_code;
	char d[160];
	for (int round = 0; round < rounds; round++)
	{
		const s32 elapsed = pick(), due = pick();
		const bool fast = (s32)((u32)elapsed + 1u) < due;
		u64 r0[32], lr0;
		u32 cr0;
		Cpu c;

		// GATE: the count after loop_do_update_write's next_pc store and cycle add.
		setup(c, elapsed, due, r0, cr0, lr0);
		u8 mem0[sizeof(c.mem)];
		memcpy(mem0, c.mem, sizeof(mem0));
		int stop = c.run(W_GATE, (1ull << W_NU) | (1ull << W_LU));
		snprintf(d, sizeof(d), "elapsed %d due %d: stop %d %s", elapsed, due, stop, c.why ? c.why : "");
		check(stop == (fast ? W_NU : W_LU), "gate path", d);
		check(c.ld4(CTX + OFS_ELAPSED) == (u32)elapsed + 1u, "gate count", d);
		check(c.ld4(CTX + OFS_DUE) == (u32)due, "gate due", d);
		bool same = true;
		for (u32 i = 0; i < sizeof(mem0); i++)
			if (i < OFS_ELAPSED || i >= OFS_ELAPSED + 4)
				same = same && c.mem[i] == mem0[i];
		check(same, "gate memory", d);
		for (u32 i = 0; i < 32; i++)
			if (i != 11 && i != 12)
			{
				snprintf(d, sizeof(d), "gate: r%u changed", i);
				check(c.r[i] == r0[i], "gate gpr", d);
			}
		check((c.cr & 0x0FFFFFFFu) == (cr0 & 0x0FFFFFFFu), "gate cr", "cr1-cr7 changed");
		check(c.lr == lr0, "gate lr", "LR changed");

		// + RESUME: a block's cycle exit, through loop_gate and back, or on to the update.
		setup(c, elapsed, due, r0, cr0, lr0);
		memcpy(mem0, c.mem, sizeof(mem0));
		stop = c.run(W_COLD, (1ull << W_RV) | (1ull << W_LU));
		snprintf(d, sizeof(d), "elapsed %d due %d: stop %d %s", elapsed, due, stop, c.why ? c.why : "");
		check(stop == (fast ? W_RV : W_LU), "resume path", d);
		check(c.r[3] == START_PC, "resume pc", "r3 is not the zero-extended block pc");
		check(c.r[29] == r0[29] + (u64)(s64)TS, "resume cycles", "r29 is not the old value plus the timeslice");
		check(c.ld4(CTX + OFS_ELAPSED) == (u32)elapsed + 1u, "resume count", d);
		check(c.lr == base + (W_COLD + 4) * 4, "resume lr", "LR is not the word after the bl");
		const u32 pc_mem = c.ld4(CTX + OFS_PC);
		check(fast ? pc_mem == (u32)(mem0[OFS_PC] << 24 | mem0[OFS_PC + 1] << 16 | mem0[OFS_PC + 2] << 8 |
		                             mem0[OFS_PC + 3]) : pc_mem == START_PC, "resume next_pc", d);
		for (u32 i = 0; i < 32; i++)
			if (i != 3 && i != 11 && i != 12 && i != 29)
			{
				snprintf(d, sizeof(d), "resume: r%u changed", i);
				check(c.r[i] == r0[i], "resume gpr", d);
			}
		check((c.cr & 0x0FFFFFFFu) == (cr0 & 0x0FFFFFFFu), "resume cr", "cr1-cr7 changed");
	}
}

int main(int argc, char** argv)
{
	const int rounds = argc > 1 ? atoi(argv[1]) : 20000;
	test_words();
	test_model(rounds);
	printf("%s: %d emit checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
