// Host-only check for P6 JIT EXIT: ps3_jit_exit.h against ppu-as words (exit_words.inc), then the main loop's
// prologue, a clobber and the exit epilogue on a PPC64 model: every callee-saved register comes back. Not in the build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

#define CODE_WORDS 1024
static u32 g_code[CODE_WORDS];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_exit.h"
#include "exit_words.inc"

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

static u64 g_seed = 88172645463325252ull;
static u64 rnd64()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return g_seed;
}

// The frame of ps3_driver.cpp's ngen_mainloop with JIT EXIT on.
static const u32 GPR_OFS = 112, FPR_OFS = GPR_OFS + 19 * 8, CR_OFS = FPR_OFS + 18 * 8;
static const u32 FRAME = (CR_OFS + 8 + 15) & ~15u;

static void test_words()
{
	g_n = 0;
	emit_exit_saves(FPR_OFS, CR_OFS);
	emit_exit_epilogue(GPR_OFS, FPR_OFS, CR_OFS, FRAME);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// PPC64 model of what the prologue and the epilogue use. Big-endian memory, as on the PPE.
static const u64 STACK_BASE = 0x40000000ull, STACK_SIZE = 0x10000;
static const u32 MK_RET = 1;   // primary opcode 0: where the epilogue's blr must land

struct Cpu
{
	u64 r[32], f[32], lr;
	u32 cr;
	u8 stack[STACK_SIZE];
	const char* why;

	void bad(const char* w) { if (!why) why = w; }
	u8* at(u64 a, u32 n)
	{
		if (a < STACK_BASE || a + n > STACK_BASE + STACK_SIZE)
		{
			bad("memory access outside the stack");
			return 0;
		}
		return &stack[a - STACK_BASE];
	}
	u64 ld(u64 a, u32 n)
	{
		u8* p = at(a, n);
		u64 v = 0;
		for (u32 i = 0; p && i < n; i++)
			v = (v << 8) | p[i];
		return v;
	}
	void st(u64 a, u32 n, u64 v)
	{
		u8* p = at(a, n);
		for (u32 i = 0; p && i < n; i++)
			p[i] = (u8)(v >> (8 * (n - 1 - i)));
	}

	// Runs code words [start, end) or until a blr; returns the index it stopped at, or -1 with why set.
	int run(u32 start, u32 end)
	{
		for (u32 idx = start; idx < end && !why; )
		{
			const u32 x = g_code[idx], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 ea = (a ? r[a] : 0) + (u64)simm, ds = (a ? r[a] : 0) + (u64)(s64)(s16)(x & 0xFFFC);
			idx++;
			switch (op)
			{
			case 14: r[d] = (a ? r[a] : 0) + (u64)simm; break;                       // addi
			case 32: r[d] = ld(ea, 4); break;                                        // lwz
			case 36: st(ea, 4, r[d]); break;                                         // stw
			case 50: f[d] = ld(ea, 8); break;                                        // lfd
			case 54: st(ea, 8, f[d]); break;                                         // stfd
			case 58:                                                                 // ld
				if (x & 3) { bad("ld form"); return -1; }
				r[d] = ld(ds, 8);
				break;
			case 62:                                                                 // std, stdu
				if ((x & 3) == 0) st(ds, 8, r[d]);
				else if ((x & 3) == 1) { st(ds, 8, r[d]); r[a] = ds; }
				else { bad("std form"); return -1; }
				break;
			case 19:
				if (x == 0x4E800020u)                                                // blr
				{
					const u64 base = (u64)(uintptr_t)g_code;
					if (lr < base || lr >= base + sizeof(g_code) || (lr & 3)) { bad("blr outside the code"); return -1; }
					return (int)((lr - base) >> 2);
				}
				bad("op 19");
				return -1;
			case 31:
			{
				const u32 xo = (x >> 1) & 0x3FF, spr = ((x >> 16) & 31) | (((x >> 11) & 31) << 5);
				if (xo == 19 && !(x & (1u << 20))) r[d] = cr;                        // mfcr
				else if (xo == 144 && !(x & (1u << 20)))                             // mtcrf
				{
					const u32 fxm = (x >> 12) & 0xFF;
					for (u32 fld = 0; fld < 8; fld++)
						if (fxm & (0x80u >> fld))
						{
							const u32 m = 0xF0000000u >> (4 * fld);
							cr = (cr & ~m) | ((u32)r[d] & m);
						}
				}
				else if (xo == 339 && spr == 8) r[d] = lr;                           // mflr
				else if (xo == 467 && spr == 8) lr = r[d];                           // mtlr
				else { bad("op 31"); return -1; }
				break;
			}
			default:
				bad("opcode outside the model");
				return -1;
			}
		}
		return (int)end;
	}
};

static Cpu g_cpu;

// The prologue of ngen_mainloop as it stands, then emit_exit_saves; a clobber; the epilogue back to the C caller.
static void test_model(int rounds)
{
	for (int round = 0; round < rounds; round++)
	{
		Cpu& c = g_cpu;
		memset(&c, 0, sizeof(c));
		g_n = 0;
		const u32 ret = g_n;
		ppc_emit(MK_RET);
		const u32 pro = g_n;
		ppc_mfspr(ppc_r0, ppc_spr_lr);
		ppc_emit(0xF8000000 | (ppc_r0 << 21) | (ppc_sp << 16) | 16);                         // std r0,16(r1)
		ppc_emit(0xF8000001 | (ppc_sp << 21) | (ppc_sp << 16) | ((u32)(-(s32)FRAME) & 0xFFFC));   // stdu
		for (u32 i = 0; i < 19; i++)
			ppc_emit(0xF8000000 | ((ppc_r13 + i) << 21) | (ppc_sp << 16) | (GPR_OFS + i * 8));   // std
		emit_exit_saves(FPR_OFS, CR_OFS);
		const u32 pro_end = g_n;
		emit_exit_epilogue(GPR_OFS, FPR_OFS, CR_OFS, FRAME);
		const u32 epi_end = g_n;

		u64 r0[32], f0[32];
		for (u32 i = 0; i < 32; i++)
		{
			c.r[i] = r0[i] = rnd64();
			c.f[i] = f0[i] = rnd64();
		}
		const u64 sp0 = STACK_BASE + STACK_SIZE - 0x1000 - (rnd64() % 64) * 16;
		c.r[1] = r0[1] = sp0;
		const u32 cr0 = (u32)rnd64();
		c.cr = cr0;
		const u64 lr0 = (u64)(uintptr_t)&g_code[ret];
		c.lr = lr0;

		check(c.run(pro, pro_end) == (int)pro_end && !c.why, "model prologue", c.why ? c.why : "stopped early");
		// The loop runs: every register but r1 is the JIT's, and so is the CR.
		for (u32 i = 0; i < 32; i++)
		{
			if (i != 1)
				c.r[i] = rnd64();
			c.f[i] = rnd64();
		}
		c.cr = (u32)rnd64();
		c.lr = rnd64();
		const int stop = c.run(pro_end, epi_end);
		check(stop == (int)ret && !c.why, "model epilogue", c.why ? c.why : "blr did not reach the C caller");
		char d[96];
		check(c.r[1] == sp0, "model sp", "the frame is not popped");
		check(c.lr == lr0, "model lr", "LR is not the caller's");
		for (u32 i = 13; i < 32; i++)
		{
			snprintf(d, sizeof(d), "r%u %016llX, at entry %016llX", i, (unsigned long long)c.r[i],
			         (unsigned long long)r0[i]);
			check(c.r[i] == r0[i], "model gpr", d);
		}
		for (u32 i = 14; i < 32; i++)
		{
			snprintf(d, sizeof(d), "f%u differs from entry", i);
			check(c.f[i] == f0[i], "model fpr", d);
		}
		snprintf(d, sizeof(d), "cr %08X, at entry %08X", c.cr, cr0);
		check((c.cr & 0x00FFF000u) == (cr0 & 0x00FFF000u), "model cr2-cr4", d);
	}
}

int main(int argc, char** argv)
{
	const int rounds = argc > 1 ? atoi(argv[1]) : 2000;
	test_words();
	test_model(rounds);
	printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
