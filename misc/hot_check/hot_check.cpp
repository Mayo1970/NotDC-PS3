// Host-only check for plan 5.18: hot_classify on ppu-as encodings and on emitted sequences, and the code of
// emit_hot_cold_count on a small PPC64 model. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[4096];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_hot.h"

struct Case { u32 w; const char* text; const char* classes; };
#include "hot_words.inc"

static int g_fail, g_checks;
static const u32 NOSKIP = 0xFFFFFFF0u;

static void check(bool ok, const char* what, const char* detail)
{
	g_checks++;
	if (!ok)
	{
		g_fail++;
		printf("FAIL %s: %s\n", what, detail);
	}
}

static int cls_index(const std::string& n)
{
	for (int c = 0; c < HC_COUNT; c++)
		if (n == k_hc_name[c])
			return c;
	return -1;
}

static void compare(const char* what, const u16* got, const u32* want)
{
	for (u32 c = 0; c < HC_COUNT; c++)
	{
		char d[128];
		snprintf(d, sizeof(d), "%s = %u, want %u", k_hc_name[c], got[c], want[c]);
		check(got[c] == want[c], what, d);
	}
}

// Every assembled case alone: insn 1 plus the classes listed in hot_cases.s.
static void test_cases()
{
	for (const Case& k : k_cases)
	{
		u32 want[HC_COUNT] = { 0 };
		want[HC_INSN] = 1;
		std::string s = k.classes;
		size_t p = 0;
		while (p < s.size())
		{
			size_t q = s.find(' ', p);
			if (q == std::string::npos)
				q = s.size();
			if (q > p)
			{
				const int c = cls_index(s.substr(p, q - p));
				if (c < 0)
				{
					printf("bad class name in %s\n", k.text);
					exit(2);
				}
				want[c]++;
			}
			p = q + 1;
		}
		u16 got[HC_COUNT];
		hot_classify(&k.w, 1, NOSKIP, 0x10000, 0x20000, 0x01000000, 0x01600000, got);
		compare(k.text, got, want);
	}
}

static void nops(u32 n)
{
	for (u32 i = 0; i < n; i++)
		ppc_ori(0, 0, 0);
}

// Store and load pairs on the context base r30 (and one on r4): lhs and lhs_if counts.
static void lhs_case(const char* name, void (*build)(), u32 lhs, u32 lhs_if)
{
	g_n = 0;
	build();
	u16 got[HC_COUNT];
	hot_classify(g_code, g_n, NOSKIP, 0x10000, 0x10000 + g_n * 4, 0x01000000, 0x01600000, got);
	char d[128];
	snprintf(d, sizeof(d), "lhs %u lhs_if %u, want %u %u", got[HC_LHS], got[HC_LHS_IF], lhs, lhs_if);
	check(got[HC_LHS] == lhs && got[HC_LHS_IF] == lhs_if, name, d);
}

static void test_lhs()
{
	lhs_case("stw/lwz", [] { ppc_stw(3, 30, 16); ppc_lwz(4, 30, 16); }, 1, 0);
	lhs_case("stfs/lwz", [] { ppc_stfs(14, 30, 16); ppc_lwz(4, 30, 16); }, 1, 1);
	lhs_case("stw/lfs", [] { ppc_stw(3, 30, 16); ppc_lfs(14, 30, 16); }, 1, 1);
	lhs_case("stfs/lfs", [] { ppc_stfs(14, 30, 16); ppc_lfs(15, 30, 16); }, 1, 0);
	lhs_case("distance 24", [] { ppc_stw(3, 30, 16); nops(23); ppc_lwz(4, 30, 16); }, 1, 0);
	lhs_case("distance 25", [] { ppc_stw(3, 30, 16); nops(24); ppc_lwz(4, 30, 16); }, 0, 0);
	lhs_case("next word", [] { ppc_stw(3, 30, 16); ppc_lwz(4, 30, 20); }, 0, 0);
	lhs_case("word below", [] { ppc_stw(3, 30, 16); ppc_lwz(4, 30, 12); }, 0, 0);
	lhs_case("stfd covers +4", [] { ppc_stfd(1, 30, 16); ppc_lwz(4, 30, 20); }, 1, 1);
	lhs_case("stb in the word", [] { ppc_stb(3, 30, 19); ppc_lwz(4, 30, 16); }, 1, 0);
	lhs_case("lbz after stw", [] { ppc_stw(3, 30, 16); ppc_lbz(4, 30, 18); }, 1, 0);
	lhs_case("std then lwz", [] { ppc_emit(0xF8000000 | (3 << 21) | (30 << 16) | 16); ppc_lwz(4, 30, 20); }, 1, 0);
	lhs_case("other base", [] { ppc_stw(3, 4, 16); ppc_lwz(5, 4, 16); }, 0, 0);
	lhs_case("two loads", [] { ppc_stw(3, 30, 16); ppc_lwz(4, 30, 16); ppc_lwz(5, 30, 16); }, 2, 0);
	lhs_case("load first", [] { ppc_lwz(4, 30, 16); ppc_stw(3, 30, 16); }, 0, 0);
	lhs_case("latest store wins", [] { ppc_stw(3, 30, 16); ppc_stfs(1, 30, 16); ppc_lwz(4, 30, 16); }, 1, 1);
	lhs_case("negative offsets", [] { ppc_stw(3, 30, (u32)-8); ppc_lwz(4, 30, (u32)-8); }, 1, 0);
	lhs_case("lwa is 4 bytes", [] { ppc_stw(3, 30, 24); ppc_emit(0xE8000002 | (4 << 21) | (30 << 16) | 20); }, 0, 0);
	lhs_case("lwa hit", [] { ppc_stw(3, 30, 24); ppc_emit(0xE8000002 | (4 << 21) | (30 << 16) | 24); }, 1, 0);
	lhs_case("ring of 24", [] {
		for (u32 k = 0; k < 30; k++)
			ppc_stw(3, 30, 0x100 + 4 * k);
		ppc_lwz(4, 30, 0x100);
		ppc_lwz(4, 30, 0x100 + 4 * 29);
	}, 1, 0);
}

// The probe's 4 counter words are left out of every class.
static void test_skip()
{
	g_n = 0;
	ppc_ori(0, 0, 0);
	ppc_addis(4, 0, 0x5000);
	ppc_lwz(3, 4, 0x10);
	ppc_addi(3, 3, 1);
	ppc_stw(3, 4, 0x10);
	ppc_lwz(5, 30, 8);
	u16 got[HC_COUNT];
	hot_classify(g_code, g_n, 1, 0x10000, 0x10000 + g_n * 4, 0x01000000, 0x01600000, got);
	u32 want[HC_COUNT] = { 0 };
	want[HC_INSN] = 2;
	want[HC_LD] = 1;
	want[HC_CTX] = 1;
	compare("probe skip", got, want);
}

static u32 bc_word(u32 bo, u32 bi, s32 disp, u32 lk) { return 0x40000000 | (bo << 21) | (bi << 16) | ((u32)disp & 0xFFFC) | lk; }
static u32 b_word(s32 disp, u32 lk) { return 0x48000000 | ((u32)disp & 0x03FFFFFC) | lk; }

static void branch_case(const char* name, u32 w, uintptr_t at, int cls, u32 n)
{
	u16 got[HC_COUNT];
	hot_classify(&w, 1, NOSKIP, at, 0x01001100, 0x01000000, 0x01600000, got);
	u32 want[HC_COUNT] = { 0 };
	want[HC_INSN] = 1;
	if (cls >= 0)
		want[cls] = n;
	if (cls == HC_BCCOLD)
		want[HC_BC] = 1;
	compare(name, got, want);
}

// at 0x01001000, cold area from 0x01001100, code cache 0x01000000-0x01600000.
static void test_branches()
{
	const uintptr_t at = 0x01001000;
	branch_case("bc into the body", bc_word(12, 2, 0x80, 0), at, HC_BC, 1);
	branch_case("bc to the cold start", bc_word(12, 2, 0x100, 0), at, HC_BCCOLD, 1);
	branch_case("bc past the cold start", bc_word(4, 0, 0x200, 0), at, HC_BCCOLD, 1);
	branch_case("bc backward", bc_word(4, 2, -0x40, 0), at, HC_BC, 1);
	branch_case("bc always", bc_word(20, 0, 0x100, 0), at, -1, 0);
	branch_case("b forward", b_word(0x200, 0), at, -1, 0);
	branch_case("bl stub", b_word(0x01000020 - (s32)at, 1), at, HC_STUB, 1);
	branch_case("bl last cache word", b_word(0x015FFFFC - (s32)at, 1), at, HC_STUB, 1);
	branch_case("bl cache end", b_word(0x01600000 - (s32)at, 1), at, HC_CALLC, 1);
	branch_case("bl below the cache", b_word(0x00F00000 - (s32)at, 1), at, HC_CALLC, 1);
	// ppu-as writes a one-field mtcrf as mtocrf; the plain form with one field is not microcode either.
	branch_case("mtcrf one field", 0x7C000120 | (3 << 21) | (0x80 << 12), at, -1, 0);
	branch_case("mtcrf two fields", 0x7C000120 | (3 << 21) | (0xC0 << 12), at, HC_MCODE, 1);
}

// Small PPC64 model of what emit_hot_cold_count may emit: addis, lwz, nor, neg, stw.
static void count_case(u32 kind, u32 addr, u32 start, u64 seed)
{
	char what[96];
	snprintf(what, sizeof(what), "cold count kind %u at %08X from %08X", kind, addr, start);
	g_n = 0;
	emit_hot_cold_count((u32*)(uintptr_t)addr, kind);

	u64 r[32], r0[32];
	for (int i = 0; i < 32; i++)
	{
		seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
		r[i] = r0[i] = seed;
	}
	u32 mem = start;
	bool ok = true;
	for (u32 k = 0; k < g_n && ok; k++)
	{
		const u32 x = g_code[k], op = x >> 26, rd = (x >> 21) & 31, ra = (x >> 16) & 31, rb = (x >> 11) & 31;
		const u64 base = ra ? r[ra] : 0;
		const u64 ea = base + (u64)(s64)(s16)(x & 0xFFFF);
		if (op == 15)
			r[rd] = base + ((u64)(s64)(s16)(x & 0xFFFF) << 16);
		else if (op == 32 || op == 36)
		{
			if (ea != addr)
			{
				check(false, what, "load or store at the wrong address");
				ok = false;
			}
			else if (op == 32)
				r[rd] = mem;
			else
				mem = (u32)r[rd];
		}
		else if (op == 31 && ((x >> 1) & 0x3FF) == 124 && !(x & 1))
			r[ra] = ~(r[rd] | r[rb]);
		else if (op == 31 && ((x >> 1) & 0x1FF) == 104 && !(x & 0x401) && rb == 0)
			r[rd] = (u64)0 - r[ra];
		else
		{
			check(false, what, "unexpected instruction (or a record / overflow form)");
			ok = false;
		}
	}
	if (!ok)
		return;
	check(mem == start + 1, what, "counter not incremented by one");
	const u32 base_reg = (kind == 2) ? 5 : 4;
	for (int i = 0; i < 32; i++)
		if (i != 0 && i != (int)base_reg && r[i] != r0[i])
		{
			char d[64];
			snprintf(d, sizeof(d), "r%d changed", i);
			check(false, what, d);
		}
	check(g_n == 5, what, "not 5 instructions");
}

static void test_count()
{
	g_n = 0;
	emit_hot_cold_count((u32*)(uintptr_t)0x51B18000u, 0);
	emit_hot_cold_count((u32*)(uintptr_t)0x51B17FFCu, 2);
	check(g_n == 10 && !memcmp(g_code, k_count_ref, sizeof(k_count_ref)), "cold count words", "differ from ppu-as");

	const u32 addrs[] = { 0x51B18000u, 0x51B17FFCu, 0x00010000u, 0x0001FFFCu, 0x12348000u, 0x7FFEFFFCu, 0x00008000u };
	const u32 starts[] = { 0, 1, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x12345678u };
	u64 seed = 88172645463325252ull;
	for (u32 kind = 0; kind < 4; kind++)
		for (u32 a : addrs)
			for (u32 s : starts)
				count_case(kind, a, s, seed += 0x9E3779B97F4A7C15ull);
}

int main()
{
	test_cases();
	test_lhs();
	test_skip();
	test_branches();
	test_count();
	printf("%s: %d checks, %d failures (%u assembled cases)\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail,
	       (u32)(sizeof(k_cases) / sizeof(k_cases[0])));
	return g_fail ? 1 : 0;
}
