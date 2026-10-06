// Host-only check for plans 5.29 and 5.30: ps3_jit_frchg.h and ps3_jit_pref.h against ppu-as words
// (frpref_words.inc), then on a PPC64 model against the old paths. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[512];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_frchg.h"
#include "ps3/ps3_jit_pref.h"
#include "frpref_words.inc"

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

// The driver's layout: context in r30, xf[n] at 4n, fr[n] at 0x40 + 4n, fpscr and old_fpscr as below; FR0-FR15 in
// f14-f29; the fast path's scratch is r3, r4, f0-f7 and CR0.
static const u32 CTX = 30, XF_OFS = 0, FR_OFS = 0x40, FPSCR_OFS = 0x11C, OLD_OFS = 0x128, CTX_SIZE = 0x140;
static const u32 FR_BIT = 1u << 21, PIN0 = 14;
static const u64 CTX_BASE = 0x20000000ull;

// Power ISA "Load Floating-Point Single": the word to the double format, SNaN payloads kept.
static u64 lfs_conv(u32 w)
{
	const u32 e = (w >> 23) & 0xFF;
	const u64 w01 = (u64)(w >> 30) << 62, low = (u64)(w & 0x3FFFFFFF) << 29, w1 = (w >> 30) & 1;
	if (e > 0 && e < 255)
		return w01 | ((w1 ? 0ull : 7ull) << 59) | low;
	if (e == 255 || (w & 0x7FFFFFFF) == 0)
		return w01 | ((w1 ? 7ull : 0ull) << 59) | low;
	s32 exp = -126;
	u64 frac = (u64)(w & 0x7FFFFF) << 29;
	while (!(frac & (1ull << 52)))
	{
		frac <<= 1;
		exp--;
	}
	return ((u64)(w >> 31) << 63) | ((u64)(exp + 1023) << 52) | (frac & ((1ull << 52) - 1));
}

// Power ISA "Store Floating-Point Single"; undef for a double no lfs can leave in an FPR.
static u32 stfs_conv(u64 f, bool& undef)
{
	const u32 e = (f >> 52) & 0x7FF;
	if (e > 896 || (f << 1) == 0)
		return (u32)((f >> 62) << 30) | (u32)((f >> 29) & 0x3FFFFFFF);
	if (e >= 874)
	{
		s32 exp = (s32)e - 1023;
		u64 frac = (1ull << 52) | (f & ((1ull << 52) - 1));
		while (exp < -126)
		{
			frac >>= 1;
			exp++;
		}
		return (u32)((f >> 63) << 31) | (u32)((frac >> 29) & 0x7FFFFF);
	}
	undef = true;
	return 0;
}

static u64 mask64(u32 mb, u32 me)
{
	if (mb <= me)
		return (~0ull >> mb) & (~0ull << (63 - me));
	return ~((~0ull >> (me + 1)) & (~0ull << (64 - mb)));
}

// PPC64 model of what the headers may emit. It runs from word 0 until pc leaves the code, and reports the word
// index it left at; every dcbt address goes to pf.
struct Cpu
{
	u64 r[32];
	u64 f[32];
	u32 cr;
	std::map<u64, u32> mem;
	std::vector<u64> pf;
	int bad;
	const char* why;

	void fault(const char* w) { if (!bad) { bad = 1; why = w; } }
	u32 ld32(u64 ea)
	{
		if ((ea & 3) || !mem.count(ea)) { fault("load from an unexpected word"); return 0; }
		return mem[ea];
	}
	void st32(u64 ea, u32 v)
	{
		if ((ea & 3) || !mem.count(ea)) { fault("store to an unexpected word"); return; }
		mem[ea] = v;
	}

	u32 run(const u32* code, u32 n, u32 stop_at)
	{
		u32 pc = 0, steps = 0;
		while (pc < n && pc != stop_at && !bad)
		{
			if (++steps > 4096) { fault("runaway"); break; }
			const u32 x = code[pc], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 ea = (a ? r[a] : 0) + (u64)simm;
			u32 next = pc + 1;
			switch (op)
			{
			case 10:
			{
				if (x & 0x00600000) { fault("cmpli with L or bit 9 set"); break; }
				const u32 crf = (x >> 23) & 7, va = (u32)r[a], ui = x & 0xFFFF;
				const u32 c = va < ui ? 8 : va > ui ? 4 : 2;
				cr = (cr & ~(0xF0000000u >> (4 * crf))) | (c << (28 - 4 * crf));
				break;
			}
			case 15: r[d] = (a ? r[a] : 0) + ((u64)simm << 16); break;
			case 16:
			{
				if (x & 3) { fault("bc with AA or LK"); break; }
				const u32 bo = d, bi = a;
				if (bo != 4 && bo != 12) { fault("unexpected BO"); break; }
				const bool bit = (cr >> (31 - bi)) & 1;
				if (bit == (bo == 12))
					next = pc + (u32)((s32)(s16)(x & 0xFFFC) >> 2);
				break;
			}
			case 18:
			{
				if (x & 3) { fault("b with AA or LK"); break; }
				const s32 li = (s32)((x & 0x03FFFFFC) << 6) >> 6;
				next = pc + (u32)(li >> 2);
				break;
			}
			case 21:
			{
				if (x & 1) { fault("rlwinm. (record form)"); break; }
				const u32 sh = b, mb = (x >> 6) & 31, me = (x >> 1) & 31;
				const u64 lo = (u32)r[d], dup = (lo << 32) | lo;
				const u64 rot = sh ? (dup << sh) | (dup >> (64 - sh)) : dup;
				r[a] = rot & mask64(mb + 32, me + 32);
				break;
			}
			case 27: r[a] = r[d] ^ ((u64)(x & 0xFFFF) << 16); break;
			case 31:
			{
				const u32 xo = (x >> 1) & 0x3FF;
				if (x & 1) { fault("record form"); break; }
				if (xo == 316)
					r[a] = r[d] ^ r[b];
				else if (xo == 278)
				{
					if (d) { fault("dcbt with a TH hint"); break; }
					pf.push_back((a ? r[a] : 0) + r[b]);
				}
				else
					fault("unexpected X-form");
				break;
			}
			case 32: r[d] = ld32(ea); break;
			case 36: st32(ea, (u32)r[d]); break;
			case 48: f[d] = lfs_conv(ld32(ea)); break;
			case 52:
			{
				bool undef = false;
				const u32 w = stfs_conv(f[d], undef);
				if (undef) { fault("stfs of a double outside the single range"); break; }
				st32(ea, w);
				break;
			}
			case 63:
			{
				if ((x & 1) || ((x >> 1) & 0x3FF) != 72 || a) { fault("unexpected FP op"); break; }
				f[d] = f[b];
				break;
			}
			default:
				fault("unexpected instruction");
				break;
			}
			pc = next;
		}
		return pc;
	}
};

// A single-precision pattern, specials often: zeros, denormals, infinities, quiet and signalling NaNs.
static u32 rnd_single()
{
	const u32 s = (u32)(rnd64() & 0x80000000u);
	switch (rnd64() % 10)
	{
	case 0: return s;
	case 1: return s | (u32)(1 + rnd64() % 0x7FFFFF);
	case 2: return s | 0x7F800000u;
	case 3: return s | 0x7FC00000u | (u32)(rnd64() & 0x3FFFFF);
	case 4: return s | 0x7F800000u | (u32)(1 + rnd64() % 0x3FFFFF);
	default: return (u32)rnd64();
	}
}

static void fresh(Cpu& c)
{
	c.bad = 0;
	c.why = "";
	c.mem.clear();
	c.pf.clear();
	for (u32 k = 0; k < 32; k++)
	{
		c.r[k] = rnd64();
		c.f[k] = lfs_conv(rnd_single());
	}
	c.r[CTX] = CTX_BASE;
	c.cr = (u32)rnd64();
	for (u32 w = 0; w < CTX_SIZE; w += 4)
		c.mem[CTX_BASE + w] = (u32)rnd64();
	for (u32 i = 0; i < 16; i++)
		c.mem[CTX_BASE + XF_OFS + 4 * i] = rnd_single();
}

// Everything but the listed GPRs, FPRs, CR fields and context words must be as before.
static bool only_changed(const Cpu& a, const Cpu& b, u32 gprs, u32 fprs, u32 cr_mask, bool xf_old)
{
	for (u32 k = 0; k < 32; k++)
	{
		if (!((gprs >> k) & 1) && a.r[k] != b.r[k]) return false;
		if (!((fprs >> k) & 1) && a.f[k] != b.f[k]) return false;
	}
	if ((a.cr & ~cr_mask) != (b.cr & ~cr_mask) || a.mem.size() != b.mem.size())
		return false;
	for (const auto& m : a.mem)
	{
		const u64 o = m.first - CTX_BASE;
		const bool listed = xf_old && (o < XF_OFS + 64 || o == OLD_OFS);
		if (!listed && b.mem.at(m.first) != m.second)
			return false;
	}
	return true;
}

// The driver's sequence, the C path stood in for by one word: returns the index of that word (the bne target).
static u32 emit_frchg_seq()
{
	g_n = 0;
	emit_frchg_test(3, 4, CTX, FPSCR_OFS, OLD_OFS);
	ppc_label* full = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	emit_frchg_swap(3, CTX, OLD_OFS, XF_OFS, PIN0);
	ppc_label* done = ppc_CreateLabel();
	ppc_bx(0, 0, 0);
	full->MarkLabel();
	const u32 at = g_n;
	ppc_emit(0);
	done->MarkLabelLong();
	return at;
}

static void test_words()
{
	emit_frchg_seq();
	emit_pref_dcbt(4, 3, 0x1234);
	emit_pref_dcbt(6, 5, 0x7EFF);
	emit_pref_dcbt(12, 31, 0x0001);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// FR alone changed: the new code must leave what the old flush, ChangeFP() swap and reload leave in f14-f29,
// xf[] and old_fpscr, and touch nothing else but its scratch (the fr[] slots stay as they were).
static void test_frchg_fast(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		Cpu c;
		fresh(c);
		const u32 old = (u32)rnd64();
		c.mem[CTX_BASE + OLD_OFS] = old;
		c.mem[CTX_BASE + FPSCR_OFS] = old ^ FR_BIT;
		u32 xf_new[16];
		u64 fr_new[16];
		for (u32 i = 0; i < 16; i++)
		{
			bool undef = false;
			xf_new[i] = stfs_conv(c.f[PIN0 + i], undef);
			fr_new[i] = lfs_conv(c.mem[CTX_BASE + XF_OFS + 4 * i]);
		}
		const u32 full_at = emit_frchg_seq();
		Cpu before = c;
		const u32 end = c.run(g_code, g_n, full_at);
		bool same = end == g_n && c.mem[CTX_BASE + OLD_OFS] == (old ^ FR_BIT) && c.pf.empty();
		for (u32 i = 0; i < 16; i++)
			same = same && c.f[PIN0 + i] == fr_new[i] && c.mem[CTX_BASE + XF_OFS + 4 * i] == xf_new[i];
		char d[96];
		snprintf(d, sizeof(d), "old=%08X: left at %u of %u (C path at %u)", old, end, g_n, full_at);
		check(!c.bad && same && only_changed(before, c, (1u << 3) | (1u << 4), 0xFFu | (0xFFFFu << PIN0), 0xF0000000u, true),
		      "frchg fast", c.bad ? c.why : d);
	}
}

// Anything else (no change, or FR with other bits, or other bits alone): straight to the C path with only r3, r4
// and CR0 changed, so UpdateFPSCR() sees the state it saw before.
static void test_frchg_full(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		Cpu c;
		fresh(c);
		const u32 old = (u32)rnd64();
		u32 diff;
		do
			diff = (t % 3) == 0 ? 0 : (t % 3) == 1 ? FR_BIT | (1u << (rnd64() % 32)) : (u32)rnd64();
		while (diff == FR_BIT);
		c.mem[CTX_BASE + OLD_OFS] = old;
		c.mem[CTX_BASE + FPSCR_OFS] = old ^ diff;
		const u32 full_at = emit_frchg_seq();
		Cpu before = c;
		const u32 end = c.run(g_code, g_n, full_at);
		char d[96];
		snprintf(d, sizeof(d), "old=%08X diff=%08X: left at %u, C path at %u", old, diff, end, full_at);
		const bool kept = only_changed(before, c, (1u << 3) | (1u << 4), 0, 0xF0000000u, false);
		check(!c.bad && end == full_at && c.pf.empty() && kept, "frchg full", c.bad ? c.why : d);
	}
}

// pref: one dcbt at mem_b.data + (addr & 0xFFFFFF) for any address, junk high word included; only t changes.
static void test_pref(u32 iters)
{
	static const u32 regs[][2] = { { 4, 3 }, { 6, 5 }, { 12, 31 }, { 3, 3 } };
	for (u32 t = 0; t < iters; t++)
	{
		const u32 tr = regs[t % 4][0], ar = regs[t % 4][1];
		const u32 hi = 1 + (u32)(rnd64() % 0x7EFF);
		Cpu c;
		fresh(c);
		if (t & 4)
			c.r[ar] = (u32)c.r[ar];
		const u64 want = ((u64)hi << 16) + (c.r[ar] & 0xFFFFFF);
		g_n = 0;
		emit_pref_dcbt(tr, ar, hi);
		Cpu before = c;
		c.run(g_code, g_n, ~0u);
		char d[128];
		snprintf(d, sizeof(d), "r%u from r%u=%016llX hi=%04X: %zu prefetches, first %016llX want %016llX", tr, ar,
		         (unsigned long long)before.r[ar], hi, c.pf.size(), (unsigned long long)(c.pf.empty() ? 0 : c.pf[0]),
		         (unsigned long long)want);
		check(!c.bad && c.pf.size() == 1 && c.pf[0] == want && only_changed(before, c, 1u << tr, 0, 0, false),
		      "pref", c.bad ? c.why : d);
	}
}

int main(int argc, char** argv)
{
	const u32 iters = argc > 1 ? (u32)atoi(argv[1]) : 200000;
	const bool model_only = argc > 2 && !strcmp(argv[2], "model");
	if (!model_only)
		test_words();
	test_frchg_fast(iters);
	test_frchg_full(iters);
	test_pref(iters);
	printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
