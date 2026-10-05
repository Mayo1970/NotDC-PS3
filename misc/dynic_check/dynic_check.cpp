// Host-only check for plan 5.3: ps3_jit_dynic.h against ppu-as words (dynic_words.inc), then the whole flow on a
// PPC64 model: unfilled site -> stub -> C fill -> guard hits and misses. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

#define CODE_WORDS 8192
static u32 g_code[CODE_WORDS];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_dynic.h"
#include "dynic_words.inc"

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

// A pointer at a byte offset from the emit position: the header only takes displacements from it.
static const void* at(s64 ofs) { return (const void*)((uintptr_t)emit_GetCCPtr() + (uintptr_t)ofs); }

static void test_words()
{
	g_n = 0;
	emit_dynic_site(at(-0x40000));
	emit_dynic_site(at(0x123450));
	emit_dynic_guard(4, 31, 0x8C010000u, at(12 + 0x1000));
	emit_dynic_guard(4, 31, 0x0C15A660u, at(12 - 0x2000));
	emit_dynic_guard(4, 31, 0xFFFFFFFFu, at(12 + 0x1FFFFFC));
	emit_dynic_guard(4, 31, 0x00000000u, at(12 - 0x2000000));
	emit_dynic_guard(6, 30, 0x12345678u, at(12 + 0x10));
	emit_dynic_miss_count(5, 6, (const u32*)(uintptr_t)0x10B23450u);
	emit_dynic_miss_count(5, 6, (const u32*)(uintptr_t)0x10B2FFF0u);
	emit_dynic_miss_count(5, 6, (const u32*)(uintptr_t)0x10B27FFCu);
	emit_dynic_miss_count(7, 8, (const u32*)(uintptr_t)0x00001000u);
	emit_dynic_stub_args(3, 4);
	emit_dynic_stub_args(5, 6);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// Pseudo-instructions (primary opcode 0, which the header never emits) that end a run or stand for the C fill.
static const u32 MK_FILL = 1, MK_GENERIC = 2, MK_TARGET = 3;

// Layout: the stub, the fill marker, low targets, sites, high targets (so b goes both ways). The miss counter lies
// below 2 GB.
static const u32 STUB = 16, FILL = 64, TGT = 128, TGT_N = 64, SITE_LO = 512, TGT_HI = CODE_WORDS - TGT_N;
static const u32 SITE_SPAN = TGT_HI - SITE_LO - 16;
static const u64 CTR_ADDR = 0x10B2FFF0u;
static const u32 A0 = 3, A1 = 4, T1 = 5, T2 = 6, DJUMP = 31;

static u64 code_addr(u32 idx) { return (u64)(uintptr_t)&g_code[idx]; }

static u64 mask64(u32 mb, u32 me)
{
	if (mb <= me)
		return (~0ull >> mb) & (~0ull << (63 - me));
	return ~((~0ull >> (me + 1)) & (~0ull << (64 - mb)));
}

static u64 rotl64(u64 x, u32 n) { return n ? (x << n) | (x >> (64 - n)) : x; }

struct Cpu;
static void c_fill(Cpu& c);

// PPC64 model of what the site, the guard, the counter and the stub may use.
struct Cpu
{
	u64 r[32], lr, ctr;
	u32 cr;			// CR0 in bits 31..28, IBM order
	std::map<u64, u32> mem;
	int fills;
	const char* why;

	void bad(const char* w) { if (!why) why = w; }

	// Runs from code index start to a marker; returns its index, or -1 with why set.
	int run(u32 start)
	{
		u64 pc = code_addr(start);
		for (int step = 0; step < 64 && !why; step++)
		{
			const u64 off = pc - code_addr(0);
			if ((pc & 3) || off >= sizeof(g_code)) { bad("pc outside the code"); return -1; }
			const u32 idx = (u32)(off >> 2), x = g_code[idx];
			if (x == MK_FILL)
			{
				c_fill(*this);
				pc = lr;
				continue;
			}
			if (x == MK_GENERIC || x == MK_TARGET)
				return (int)idx;
			const u32 op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 uimm = x & 0xFFFF;
			u64 next = pc + 4;
			switch (op)
			{
			case 18:
			{
				if (x & 2) { bad("absolute branch"); return -1; }
				if (x & 1) lr = pc + 4;
				next = pc + (u64)(s64)((s32)((x & 0x03FFFFFCu) << 6) >> 6);
				break;
			}
			case 16:
			{
				if (x & 3) { bad("bc with AA or LK"); return -1; }
				const bool bit = (cr >> (31 - a)) & 1;
				bool taken;
				if (d == 12) taken = bit;
				else if (d == 4) taken = !bit;
				else if (d == 20) taken = true;
				else { bad("unexpected BO"); return -1; }
				if (taken)
					next = pc + (u64)(s64)(s16)(x & 0xFFFC);
				break;
			}
			case 19:
				if (((x >> 1) & 0x3FF) != 528 || d != 20 || (x & 1)) { bad("unexpected XL-form"); return -1; }
				next = ctr & ~3ull;
				break;
			case 31:
			{
				const u32 xo = (x >> 1) & 0x3FF, spr = (x >> 11) & 0x3FF;
				if (xo == 339 && spr == 256) r[d] = lr;
				else if (xo == 339 && spr == 288) r[d] = ctr;
				else if (xo == 467 && spr == 288) ctr = r[d];
				else if (xo == 467 && spr == 256) lr = r[d];
				else { bad("unexpected X-form"); return -1; }
				break;
			}
			case 14: r[d] = (a ? r[a] : 0) + (u64)simm; break;
			case 15: r[d] = (a ? r[a] : 0) + ((u64)simm << 16); break;
			case 24: r[a] = r[d] | uimm; break;
			case 26: r[a] = r[d] ^ uimm; break;
			case 27: r[a] = r[d] ^ (uimm << 16); break;
			case 10:
			{
				const u32 f = (x >> 23) & 7;
				const u64 v = ((x >> 21) & 1) ? r[a] : (u64)(u32)r[a];
				const u32 c = v < uimm ? 8 : v > uimm ? 4 : 2;	// SO stays 0: XER[SO] is never set here
				cr = (cr & ~(0xFu << (28 - 4 * f))) | (c << (28 - 4 * f));
				break;
			}
			case 30:
			{
				if (((x >> 2) & 7) != 0 || (x & 1)) { bad("rld* other than rldicl"); return -1; }
				const u32 sh = b | (((x >> 1) & 1) << 5), mb = ((x >> 6) & 31) | (((x >> 5) & 1) << 5);
				r[a] = rotl64(r[d], sh) & mask64(mb, 63);
				break;
			}
			case 32:
			case 36:
			{
				const u64 ea = (a ? r[a] : 0) + (u64)simm;
				if (!mem.count(ea)) { bad(op == 32 ? "load from an unexpected word" : "store to an unexpected word"); return -1; }
				if (op == 32) r[d] = mem[ea];
				else mem[ea] = (u32)r[d];
				break;
			}
			default:
				bad("unexpected instruction");
				return -1;
			}
			pc = next;
		}
		if (!why)
			bad("no marker within 64 steps");
		return -1;
	}
};

// The fill as the driver's ngen_LinkBlock_Dynamic_IC, with rdv_FindOrCompile as a pc -> target table. A cache
// clear in the compile is a site overwritten (clobber 1) or rebuilt by another block (clobber 2) first.
static u32 g_blk_pc[TGT_N], g_blk_n, g_fill_clobber, g_fill_site_want, g_patched;

static u32 blk_index(u32 i) { return (i & 1) ? TGT_HI + i : TGT + i; }

static u32 find_or_compile(u32 pc)
{
	for (u32 i = 0; i < g_blk_n; i++)
		if (g_blk_pc[i] == pc)
			return blk_index(i);
	if (g_blk_n == TGT_N)
		g_blk_n = 0;
	g_blk_pc[g_blk_n] = pc;
	return blk_index(g_blk_n++);
}

static void c_fill(Cpu& c)
{
	c.fills++;
	if (c.r[A0] >> 32)
		c.bad("pc argument not zero-extended");
	if (c.r[A1] != code_addr(g_fill_site_want))
		c.bad("site argument is not slot 0");
	if (c.lr != code_addr(STUB + 4))
		c.bad("fill not called from the stub");
	if (c.why)
		return;
	const u32 pc = (u32)c.r[A0];
	u32* site = &g_code[g_fill_site_want];
	const u32 tgt = find_or_compile(pc);
	if (g_fill_clobber == 1)
		for (u32 k = 0; k < 8; k++)
			site[k] = 0x7C000378u | ((u32)rnd64() & 0x03FFF800u);	// or rA,rS,rB: never a bl
	else if (g_fill_clobber == 2)
	{
		const u32 save = g_n;
		g_n = g_fill_site_want;
		emit_dynic_site(&g_code[STUB]);
		g_n = save;
	}
	if (dynic_site_live(site, &g_code[STUB]))
	{
		const u32 save = g_n;
		g_n = g_fill_site_want;
		emit_dynic_guard(A1, DJUMP, pc, &g_code[tgt]);
		if (g_n != g_fill_site_want + 4)
			c.bad("guard is not 4 words");
		g_n = save;
		g_patched++;
	}
	c.r[A0] = code_addr(tgt);
	// ELFv1 volatiles come back as junk: r0, r4-r12, CTR, CR0, CR1, CR5-CR7.
	c.r[0] = rnd64();
	for (int i = 4; i <= 12; i++)
		c.r[i] = rnd64();
	c.ctr = rnd64();
	c.cr = (c.cr & 0x00FFF000u) | ((u32)rnd64() & 0xFF000FFFu);
}

// Stub at STUB: the header's head, then the driver's ppc_call_and_jump_c (bl fill, mtctr r3, bctr).
static void build_stub()
{
	memset(g_code, 0, sizeof(g_code));
	g_n = STUB;
	emit_dynic_stub_args(A0, A1);
	ppc_emit(0x48000001u | ((u32)((FILL - g_n) * 4) & 0x03FFFFFCu));
	ppc_mtspr(ppc_spr_ctr, 3);
	ppc_bcctrx(BO_ALWAYS, BI_CR0_EQ, 0);
	g_code[FILL] = MK_FILL;
	for (u32 i = 0; i < TGT_N; i++)
		g_code[blk_index(i)] = MK_TARGET;
	g_blk_n = 0;
}

// `mr a0,djump`, the site and the counter, as ngen_End emits them; the generic path is a marker. Returns slot 0.
static u32 build_site(u32 at_idx)
{
	g_n = at_idx;
	ppc_ori(A0, DJUMP, 0);
	const u32 s = g_n;
	emit_dynic_site(&g_code[STUB]);
	emit_dynic_miss_count(T1, T2, (const u32*)(uintptr_t)CTR_ADDR);
	g_code[g_n++] = MK_GENERIC;
	return s;
}

static void fresh_cpu(Cpu& c, u32 count)
{
	for (int i = 0; i < 32; i++)
		c.r[i] = rnd64();
	c.lr = rnd64();
	c.ctr = rnd64();
	c.cr = (u32)rnd64();
	c.mem.clear();
	c.mem[CTR_ADDR] = count;
	c.fills = 0;
	c.why = 0;
}

static u64 junk_high(u32 kind)
{
	return kind == 0 ? 0 : kind == 1 ? 0xFFFFFFFF00000000ull : rnd64() << 32;
}

// Every GPR but those in `may`, LR (only for a fill) and the counter word must be as before.
static void check_kept(const Cpu& b, const Cpu& c, u32 may, const char* what)
{
	char d[96];
	for (int i = 0; i < 32; i++)
		if (!((may >> i) & 1) && b.r[i] != c.r[i])
		{
			snprintf(d, sizeof(d), "r%d changed", i);
			check(false, what, d);
		}
	check(c.mem.size() == 1, what, "touched other words");
}

static void one_case(u32 site_at, u32 pc0, u32 pc1, u32 jk, const char* what)
{
	build_stub();
	const u32 s = build_site(site_at);
	g_fill_site_want = s;
	g_fill_clobber = 0;
	const u32 count0 = (u32)rnd64();
	char d[160];

	// Unfilled: slot 0 -> stub -> fill -> target of pc0; the fill writes the guard.
	Cpu c;
	fresh_cpu(c, count0);
	c.r[DJUMP] = junk_high(jk) | pc0;
	Cpu b = c;
	u32 patched0 = g_patched;
	int m = c.run(s - 1);
	snprintf(d, sizeof(d), "%s: unfilled run: %s", what, c.why ? c.why : "wrong end");
	check(m == (int)find_or_compile(pc0) && c.fills == 1 && g_patched == patched0 + 1, what, d);
	if (m < 0)
		return;
	check(c.mem[CTR_ADDR] == count0, what, "unfilled run counted a miss");
	check_kept(b, c, (1u << 0) | (0x1FFFu & ~7u), what);
	check(!dynic_site_live(&g_code[s], &g_code[STUB]), what, "filled site still live");

	// Hit: the same low word under another junk high word goes straight to the target.
	for (u32 k = 0; k < 3; k++)
	{
		fresh_cpu(c, count0);
		c.r[DJUMP] = junk_high(k) | pc0;
		b = c;
		m = c.run(s - 1);
		snprintf(d, sizeof(d), "%s: hit: %s", what, c.why ? c.why : "wrong end");
		check(m == (int)find_or_compile(pc0) && c.fills == 0, what, d);
		check(c.r[A0] == c.r[DJUMP], what, "hit: a0 is not djump");
		check(c.lr == b.lr, what, "hit: LR changed (a stray bl unbalances the PPE link stack)");
		check(c.mem[CTR_ADDR] == count0, what, "hit counted a miss");
		check_kept(b, c, (1u << A0) | (1u << A1), what);
		check((c.cr & 0x0FFFFFFFu) == (b.cr & 0x0FFFFFFFu), what, "hit: CR1-CR7 changed");
	}

	// Miss: another low word goes to the generic path with a0 = djump, one more miss counted.
	fresh_cpu(c, count0);
	c.r[DJUMP] = junk_high(jk) | pc1;
	b = c;
	m = c.run(s - 1);
	snprintf(d, sizeof(d), "%s: miss %08X: %s", what, pc1, c.why ? c.why : "wrong end");
	check(m == (int)(s + 8) && c.fills == 0, what, d);
	check(c.r[A0] == c.r[DJUMP], what, "miss: a0 is not djump");
	check(c.lr == b.lr, what, "miss: LR changed");
	check(c.mem[CTR_ADDR] == count0 + 1, what, "miss not counted once");
	check_kept(b, c, (1u << A0) | (1u << A1) | (1u << T1) | (1u << T2), what);
	check((c.cr & 0x0FFFFFFFu) == (b.cr & 0x0FFFFFFFu), what, "miss: CR1-CR7 changed");
}

static void test_flow()
{
	const u32 pcs[] = { 0x8C010000u, 0x0C15A660u, 0xFFFFFFFFu, 0x00000000u, 0x8C0123A4u, 0xAC00FFFEu, 0x0000FFFFu,
	                    0xFFFF0000u, 0x80000000u, 0x7FFFFFFFu };
	char what[96];
	u32 n = 0;
	for (u32 pc0 : pcs)
		for (u32 jk = 0; jk < 3; jk++)
		{
			// Near misses: each single bit, the halves swapped, then random words.
			for (u32 bit = 0; bit < 32; bit++)
			{
				snprintf(what, sizeof(what), "pc %08X bit %u junk %u", pc0, bit, jk);
				one_case(SITE_LO + (n++ % 64) * 37, pc0, pc0 ^ (1u << bit), jk, what);
			}
			snprintf(what, sizeof(what), "pc %08X halves swapped", pc0);
			const u32 sw = (pc0 << 16) | (pc0 >> 16);
			one_case(SITE_LO + (n++ % 64) * 37, pc0, sw != pc0 ? sw : pc0 ^ 0x00010001u, jk, what);
			for (u32 k = 0; k < 40; k++)
			{
				u32 pc1 = (u32)rnd64();
				if (pc1 == pc0)
					pc1 ^= 0x80;
				snprintf(what, sizeof(what), "pc %08X random miss", pc0);
				one_case(SITE_LO + (n++ % 64) * 37, pc0, pc1, jk, what);
			}
		}
	for (u32 k = 0; k < 20000; k++)
	{
		const u32 pc0 = (u32)rnd64();
		u32 pc1 = (u32)rnd64();
		if (pc1 == pc0)
			pc1 ^= 1;
		snprintf(what, sizeof(what), "random pc %08X", pc0);
		one_case(SITE_LO + (u32)(rnd64() % SITE_SPAN), pc0, pc1, k % 3, what);
	}
}

// A cache clear inside the fill: an overwritten site is left alone; a site rebuilt at the address is filled.
static void test_clear_in_fill()
{
	char d[128];
	for (u32 k = 0; k < 2000; k++)
	{
		const u32 pc0 = (u32)rnd64();
		const u32 clobber = 1 + (k & 1);
		build_stub();
		const u32 s = build_site(SITE_LO + (u32)(rnd64() % 4096));
		g_fill_site_want = s;
		g_fill_clobber = clobber;
		Cpu c;
		fresh_cpu(c, 7);
		c.r[DJUMP] = junk_high(k % 3) | pc0;
		const u32 patched0 = g_patched;
		const int m = c.run(s - 1);
		snprintf(d, sizeof(d), "clobber %u pc %08X: %s", clobber, pc0, c.why ? c.why : "wrong end");
		check(m == (int)find_or_compile(pc0) && c.fills == 1, "clear in fill", d);
		check(g_patched == patched0 + (clobber == 2 ? 1 : 0), "clear in fill",
		      clobber == 2 ? "rebuilt site not filled" : "overwritten site was patched");
		g_fill_clobber = 0;
	}
}

// Liveness names the address: a fresh site is live, a filled one or a copy elsewhere is not.
static void test_live()
{
	build_stub();
	for (u32 k = 0; k < 4000; k++)
	{
		const u32 s = SITE_LO + (u32)(rnd64() % 4096);
		g_n = s;
		emit_dynic_site(&g_code[STUB]);
		check(dynic_site_live(&g_code[s], &g_code[STUB]), "live", "fresh site not live");
		const u32 s2 = s + 4 + (u32)(rnd64() % 64);
		memcpy(&g_code[s2], &g_code[s], 16);
		check(!dynic_site_live(&g_code[s2], &g_code[STUB]), "live", "copied site live at another address");
		g_n = s;
		emit_dynic_guard(A1, DJUMP, (u32)rnd64(), &g_code[TGT]);
		check(!dynic_site_live(&g_code[s], &g_code[STUB]), "live", "filled site live");
	}
}

int main()
{
	test_words();
	const int words_fail = g_fail;
	test_flow();
	test_clear_in_fill();
	test_live();
	printf("%s: %d checks, %d failures (words %d, model %d)\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail,
	       words_fail, g_fail - words_fail);
	return g_fail ? 1 : 0;
}
