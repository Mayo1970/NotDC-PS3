// Host-only check for plan section 7, bundle 8: ps3_jit_mac.h and ps3_jit_bcache.h against ppu-as words, then on a
// PPC64 model: mac.l / mac.w against shil.cpp's reference and 64-bit math, the lookup against blockmanager's mirror.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[512];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_carry.h"
#include "ps3/ps3_jit_div32.h"
#include "ps3/ps3_jit_mac.h"
#include "ps3/ps3_jit_bcache.h"
#include "jit8_words.inc"

// shil.cpp's reference, which writes the macl / mach registers.
static u32 macl, mach;
#define CCALL(x)
#include "mac_ref.inc"

// blockmanager's entry and mirror; the block is the part of DynarecBlock the mirror reads.
typedef void DynarecCodeEntry();
struct DynarecBlock { DynarecCodeEntry* code; u32 addr; u32 lookups; };
#define BM_BLOCKLIST_COUNT 16384
#include "bcache_struct.inc"
static BlockCacheFlatEntry bm_bcache[BM_BLOCKLIST_COUNT];
#include "bcache_mirror.inc"

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

static u64 rotl64(u64 v, u32 n)
{
	n &= 63;
	return n ? (v << n) | (v >> (64 - n)) : v;
}

static u64 mask64(u32 mb, u32 me)
{
	if (mb <= me)
		return (~0ull >> mb) & (~0ull << (63 - me));
	return ~((~0ull >> (me + 1)) & (~0ull << (64 - mb)));
}

static const u64 XER_CA = 0x20000000u;

// The lookup's table sits at this model address; lwz reads big-endian words from it, as on the PPU.
static u64 g_tab_base;

// PPC64 model in 64-bit mode (MSR[SF] = 1, as on the PS3). Only the forms the two headers and the old mac code
// emit; anything else faults. A run ends at the code's end (fell = 1) or at bctr (fell = 0).
struct Cpu
{
	u64 r[32], ctr, xer;
	u32 cr;
	int bad, fell;
	const char* why;

	void fault(const char* w) { if (!bad) { bad = 1; why = w; } }

	u64 load32(u64 ea)
	{
		const u64 off = ea - g_tab_base;
		if (off >= sizeof(bm_bcache) || (off & 3)) { fault("load outside the table"); return 0; }
		const BlockCacheFlatEntry& e = bm_bcache[off >> 3];
		if ((off & 7) == offsetof(BlockCacheFlatEntry, addr))
			return e.addr;
		if ((off & 7) == offsetof(BlockCacheFlatEntry, code))
			return e.code;
		fault("load between the entry's fields");
		return 0;
	}

	void run(const u32* code, u32 n)
	{
		u32 pc = 0;
		fell = 0;
		for (u32 steps = 0; !bad; steps++)
		{
			if (pc == n) { fell = 1; return; }
			if (pc > n || steps > 64) { fault("pc outside the code"); return; }
			const u32 x = code[pc++], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			switch (op)
			{
			case 15: r[d] = (a ? r[a] : 0) + ((u64)simm << 16); break;
			case 24: r[a] = r[d] | (x & 0xFFFF); break;
			case 32: if (!a) { fault("lwz from r0"); return; } r[d] = load32(r[a] + (u64)simm); break;
			case 21:
			{
				if (x & 1) { fault("rlwinm. (record form)"); return; }
				const u32 mb = (x >> 6) & 31, me = (x >> 1) & 31;
				const u64 lo = (u32)r[d];
				r[a] = rotl64((lo << 32) | lo, b) & mask64(mb + 32, me + 32);
				break;
			}
			case 30:
			{
				if (x & 1) { fault("rld record form"); return; }
				const u32 xo = (x >> 2) & 7, sh = b | (((x >> 1) & 1) << 5), mb = ((x >> 6) & 31) | (((x >> 5) & 1) << 5);
				if (xo == 0)
					r[a] = rotl64(r[d], sh) & mask64(mb, 63);
				else if (xo == 3)
				{
					const u64 m = mask64(mb, 63 - sh);
					r[a] = (rotl64(r[d], sh) & m) | (r[a] & ~m);
				}
				else { fault("MD-form other than rldicl, rldimi"); return; }
				break;
			}
			case 16:
			{
				if (x & 3) { fault("bc with AA or LK"); return; }
				const u32 bo = d, bit = (cr >> (31 - a)) & 1;
				if (bo != BO_TRUE && bo != BO_FALSE) { fault("bc with a BO other than true or false"); return; }
				if ((bo == BO_TRUE) == (bit != 0))
					pc = pc - 1 + (u32)((s32)(s16)(x & 0xFFFC) >> 2);
				break;
			}
			case 19:
				// bcctr with BO = always; its BI is not read.
				if ((x & 0xFFE0FFFFu) != 0x4E800420u) { fault("op 19 other than bctr"); return; }
				return;
			case 31:
				x31(x, d, a, b);
				break;
			default:
				fault("unexpected instruction");
				return;
			}
		}
	}

	void x31(u32 x, u32 d, u32 a, u32 b)
	{
		const u32 xo = (x >> 1) & 0x3FF;
		if (x & 1) { fault("record form"); return; }
		switch (xo)
		{
		case 0:
		{
			// cmp: with L = 0 the low words, sign-extended.
			if (d & 2) { fault("cmp with a reserved bit"); return; }
			const s64 va = (d & 1) ? (s64)r[a] : (s64)(s32)(u32)r[a], vb = (d & 1) ? (s64)r[b] : (s64)(s32)(u32)r[b];
			const u32 f = va < vb ? 8 : va > vb ? 4 : 2, sh = 28 - 4 * (d >> 2);
			cr = (cr & ~(0xFu << sh)) | (f << sh);
			return;
		}
		case 467: if (((x >> 11) & 0x3FF) != (9u << 5)) break; ctr = r[d]; return;
		case 986: if (b) break; r[a] = (u64)(s64)(s32)(u32)r[d]; return;
		case 75: r[d] = (u64)(((s64)(s32)(u32)r[a] * (s64)(s32)(u32)r[b]) >> 32); return;
		case 824:
		{
			// srawi: CA when a negative source loses one bits.
			const s64 v = (s32)(u32)r[d];
			r[a] = (u64)(v >> b);
			xer = (v < 0 && b && ((u64)v & ((1ull << b) - 1))) ? xer | XER_CA : xer & ~XER_CA;
			return;
		}
		}
		const u32 xo9 = (x >> 1) & 0x1FF;
		if ((x >> 10) & 1) { fault("OE form"); return; }
		const u64 ra = r[a], rb = r[b], ca = (xer & XER_CA) ? 1 : 0;
		unsigned __int128 s;
		switch (xo9)
		{
		case 266: r[d] = ra + rb; return;
		case 40: r[d] = rb - ra; return;
		case 233: r[d] = ra * rb; return;
		case 235: r[d] = (u64)((s64)(s32)(u32)ra * (s64)(s32)(u32)rb); return;
		case 10:
		case 138:
			s = (unsigned __int128)ra + rb + (xo9 == 138 ? ca : 0);
			r[d] = (u64)s;
			xer = (s >> 64) ? xer | XER_CA : xer & ~XER_CA;
			return;
		default:
			fault("unexpected X-form");
			return;
		}
	}
};

static void fresh(Cpu& c)
{
	c.bad = 0;
	c.why = "";
	for (u32 k = 0; k < 32; k++)
		c.r[k] = rnd64();
	c.cr = (u32)rnd64();
	c.ctr = rnd64();
	c.xer = rnd64() & XER_CA;
}

// Only the listed GPRs may differ, and CR0 and CTR when the flags allow it; XER never.
static bool only_changed(const Cpu& a, const Cpu& b, u32 gprs, bool cr0, bool ctr)
{
	for (u32 k = 0; k < 32; k++)
		if (!((gprs >> k) & 1) && a.r[k] != b.r[k])
			return false;
	const u32 keep = cr0 ? 0x0FFFFFFFu : 0xFFFFFFFFu;
	return (a.cr & keep) == (b.cr & keep) && a.xer == b.xer && (ctr || a.ctr == b.ctr);
}

static u32 rnd_val()
{
	static const u32 edge[] = { 0, 1, 2, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu,
	                            0x0000FFFFu, 0x00010000u, 0x00007FFFu, 0xFFFF8000u, 0xFFFF0000u };
	switch (rnd64() % 4)
	{
	case 0: return edge[rnd64() % (sizeof(edge) / sizeof(edge[0]))];
	case 1: return (u32)(rnd64() % 5) - 2u;
	default: return (u32)rnd64();
	}
}

// The form of ps3_driver.cpp before bundle 8, for the report: rarg5-7 and r11 as it had them.
static void old_mac(bool is_l)
{
	ppc_mullwx(8, 3, 4, 0, 0);
	if (is_l)
		ppc_mulhwx(9, 3, 4, 0);
	else
		ppc_srawix(9, 8, 31, 0);
	ppc_addcx(10, 5, 8, 0, 0);
	ppc_addex(11, 6, 9, 0, 0);
}

// One mac.l or mac.w with S clear. The driver's registers: a r3, b r4, MACL r5, MACH r6, SR r7, x r8, lo r9, hi
// r10; alt moves the scratch registers. mac.w's operands are sign-extended words, as its reads leave them.
static int run_mac(bool is_l, u32 a, u32 b, u32 l, u32 h, bool clean, bool alt, bool old, char* d_out, size_t d_len)
{
	const u32 X = alt ? 12 : 8, LO = alt ? 0 : 9, HI = alt ? 11 : 10;
	if (!is_l)
	{
		a = (u32)(s32)(s16)a;
		b = (u32)(s32)(s16)b;
	}
	Cpu c;
	fresh(c);
	const u64 junk = clean ? 0 : ~0ull;
	c.r[3] = (rnd64() & junk & 0xFFFFFFFF00000000ull) | a;
	c.r[4] = (rnd64() & junk & 0xFFFFFFFF00000000ull) | b;
	c.r[5] = (rnd64() & junk & 0xFFFFFFFF00000000ull) | l;
	c.r[6] = (rnd64() & junk & 0xFFFFFFFF00000000ull) | h;
	const Cpu before = c;

	g_n = 0;
	if (old)
		old_mac(is_l);
	else
		emit_mac_acc(LO, HI, 3, 4, 5, 6, X);
	c.run(g_code, g_n);

	const u64 want = (((u64)h << 32) | l) + (u64)((s64)(s32)a * (s64)(s32)b);
	macl = l;
	mach = h;
	if (is_l)
		sh4_mac_l(a, b, l, h, 0);
	else
		sh4_mac_w(a, b, l, h, 0);
	const u64 ref = ((u64)mach << 32) | macl;
	const u32 got_lo = (u32)c.r[old ? 10 : LO];
	const u64 got_hi = old ? (u32)c.r[11] : c.r[HI];
	snprintf(d_out, d_len, "mac.%c a=%08X b=%08X MAC=%08X:%08X: got %016llX:%08X want %016llX (shil.cpp %016llX)",
	         is_l ? 'l' : 'w', a, b, h, l, (unsigned long long)got_hi, got_lo, (unsigned long long)want,
	         (unsigned long long)ref);
	if (c.bad)
	{
		snprintf(d_out, d_len, "mac: %s", c.why);
		return 1;
	}
	if (!c.fell || ref != want || got_lo != (u32)want || got_hi != (want >> 32))
		return 1;
	return old || only_changed(before, c, (1u << X) | (1u << LO) | (1u << HI), false, false) ? 0 : 2;
}

// Flycast's tests/src/sh4_ops.h mac cases: the accumulator after each op, from a clear MAC.
static void test_mac_flycast()
{
	static const struct { bool is_l; u32 a, b; u64 mac; } k[] = {
		{ true, 4, 3, 12ull }, { true, (u32)-5, 7, (u64)-23ll },
		{ false, (u16)-7, 3, (u64)-21ll }, { false, 5, 7, 14ull },
	};
	u64 acc = 0;
	for (u32 i = 0; i < 4; i++)
	{
		if (i == 2)
			acc = 0;
		char d[240];
		const int r = run_mac(k[i].is_l, k[i].a, k[i].b, (u32)acc, (u32)(acc >> 32), true, false, false, d, sizeof(d));
		const u32 a = k[i].is_l ? k[i].a : (u32)(s32)(s16)k[i].a, b = k[i].is_l ? k[i].b : (u32)(s32)(s16)k[i].b;
		acc += (u64)((s64)(s32)a * (s64)(s32)b);
		check(r == 0 && acc == k[i].mac, "mac flycast", d);
	}
}

static void test_mac(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		char d[240];
		const int r = run_mac(t & 1, rnd_val(), rnd_val(), rnd_val(), rnd_val(), (t / 2) % 4 == 0, (t / 8) % 2 != 0, false,
		                      d, sizeof(d));
		check(r == 0, "mac", r == 2 ? "a register outside the form, CR or XER changed" : d);
	}
}

// Report only: how often the form before bundle 8 was wrong on the same model.
static void report_old(u32 iters)
{
	static const struct { bool is_l; u32 a, b; u64 mac; } k[] = {
		{ true, 4, 3, 0 }, { true, (u32)-5, 7, 12 }, { false, (u16)-7, 3, 0 }, { false, 5, 7, (u64)-21ll },
	};
	u32 fly = 0;
	for (u32 i = 0; i < 4; i++)
	{
		char d[240];
		fly += run_mac(k[i].is_l, k[i].a, k[i].b, (u32)k[i].mac, (u32)(k[i].mac >> 32), true, false, true, d, sizeof(d)) != 0;
	}
	printf("form before bundle 8, wrong results: Flycast's 4 cases %u", fly);
	for (u32 w = 0; w < 2; w++)
	{
		u32 wrong = 0;
		for (u32 t = 0; t < iters; t++)
		{
			char d[240];
			wrong += run_mac(w == 0, rnd_val(), rnd_val(), rnd_val(), rnd_val(), true, false, true, d, sizeof(d)) != 0;
		}
		printf(", mac.%c %.1f%%", w ? 'w' : 'l', 100.0 * wrong / iters);
	}
	printf("\n");
}

// Buckets: empty, or a block whose pc has the bucket's index in bits 2-15.
static void fill_table()
{
	static DynarecBlock empty = { 0, 0xFFFFFFFFu, 0 };
	for (u32 i = 0; i < BM_BLOCKLIST_COUNT; i++)
	{
		DynarecBlock blk;
		blk.addr = ((u32)rnd64() & 0xFFFF0002u) | (i << 2);
		blk.code = (DynarecCodeEntry*)(uintptr_t)(0x00100000u + ((u32)rnd64() & 0x017FFFFCu));
		blk.lookups = 0;
		bcache_mirror(i, rnd64() % 4 ? &blk : &empty);
	}
}

static void test_bcache(u32 iters)
{
	static const u64 bases[] = { 0x10B28000u, 0x10B27FE0u, 0x00123400u, 0x7FFF7FE0u };
	for (u32 t = 0; t < iters; t++)
	{
		if (t % 4096 == 0)
			fill_table();
		g_tab_base = bases[t % 4];
		const u32 idx = (u32)rnd64() % BM_BLOCKLIST_COUNT;
		const BlockCacheFlatEntry e = bm_bcache[idx];
		// The probe: the bucket's own pc, or one that shares its index: other high bits, one bit above, or bit 1.
		u32 pc = e.addr;
		const u32 kind = (u32)rnd64() % 4;
		if (kind == 1 || e.addr == 0xFFFFFFFFu)
			pc = ((u32)rnd64() & 0xFFFF0002u) | (idx << 2);
		else if (kind == 2)
			pc = e.addr ^ (1u << (16 + (u32)rnd64() % 16));
		else if (kind == 3)
			pc = e.addr ^ 2;
		const bool hit = pc == e.addr;

		Cpu c;
		fresh(c);
		const u32 PC = t & 8 ? 31 : 3, I = t & 16 ? 0 : 4, P = 5, V = t & 32 ? 7 : 6;
		c.r[PC] = (t & 1 ? rnd64() & 0xFFFFFFFF00000000ull : 0) | pc;
		const Cpu before = c;
		g_n = 0;
		emit_bcache_lookup(PC, (const void*)(uintptr_t)g_tab_base, I, P, V);
		c.run(g_code, g_n);

		char d[200];
		snprintf(d, sizeof(d), "base %08llX pc %08X bucket %u {%08X, %08X}: %s, ctr %016llX", (unsigned long long)g_tab_base, pc,
		         idx, e.addr, e.code, c.fell ? "miss" : "hit", (unsigned long long)c.ctr);
		bool ok = !c.bad && only_changed(before, c, (1u << I) | (1u << P) | (1u << V), true, hit);
		if (hit)
			ok = ok && !c.fell && c.ctr == e.code;
		else
			ok = ok && c.fell;
		check(ok, "bcache", c.bad ? c.why : d);
	}
}

// The mirror keeps the block's pc and code; a code address fits the u32.
static void test_mirror()
{
	DynarecBlock blk = { (DynarecCodeEntry*)(uintptr_t)0x01A2B3C4u, 0x8C0123A4u, 7 };
	bcache_mirror(5, &blk);
	check(bm_bcache[5].addr == 0x8C0123A4u && bm_bcache[5].code == 0x01A2B3C4u, "mirror", "entry differs from the block");
	check(sizeof(BlockCacheFlatEntry) == 8 && offsetof(BlockCacheFlatEntry, addr) == 0 &&
	      offsetof(BlockCacheFlatEntry, code) == 4, "mirror", "entry layout");
}

static void test_words()
{
	g_n = 0;
	emit_mac_acc(9, 10, 3, 4, 5, 6, 8);
	emit_mac_acc(0, 11, 14, 28, 5, 6, 12);
	emit_bcache_lookup(3, (const void*)(uintptr_t)0x10B28000u, 4, 5, 6);
	emit_bcache_lookup(3, (const void*)(uintptr_t)0x10B27FE0u, 4, 5, 6);
	emit_bcache_lookup(31, (const void*)(uintptr_t)0x00123400u, 0, 5, 7);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

int main(int argc, char** argv)
{
	const u32 iters = argc > 1 ? (u32)atoi(argv[1]) : 200000;
	// A second argument "model" skips the words, so mutate.py can tell what the model alone catches.
	if (argc <= 2 || strcmp(argv[2], "model") != 0)
		test_words();
	test_mac_flycast();
	test_mac(iters);
	test_mirror();
	test_bcache(iters);
	if (argc > 2 && strcmp(argv[2], "old") == 0)
		report_old(iters);
	printf("%s: emit: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
