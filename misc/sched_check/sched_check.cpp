// Host-only check for row JIT SCHEDULE + LIST (ps3/ps3_jit_sched.h) on a PPC64 model: the decoder's sets, address
// kinds against real accesses, and every reordered region (generated and real bodies) against the original order.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

#ifndef SCHED_HEADER
#define SCHED_HEADER "ps3/ps3_jit_sched.h"
#endif
#include SCHED_HEADER
#include "misc/sched_check/ppc_model.h"
#include "misc/sched_check/corpus.inc"

static const u32 CTX = 0x10A1C000, RAM_HI = 0x3051;
static const s64 RAM_LO = (s64)RAM_HI << 16, RAM_END = RAM_LO + (16 << 20) + 4096 + 8;
static const u32 BIAS_HI = (u32)((RAM_LO - 0x8C000000LL) >> 16) & 0xFFFF;
static const SchedEnv ENV = { CTX, RAM_HI, RAM_LO, RAM_END, BIAS_HI, 21 };

static int g_fail, g_checks;
static void fail(const char* what, u32 w, const char* detail)
{
	if (g_fail < 25)
		printf("FAIL %s: word %08X %s\n", what, w, detail);
	g_fail++;
}

static u64 g_seed = 0x2545F4914F6CDD1Dull;
static u64 rnd() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17; return g_seed; }
static u32 rn(u32 n) { return (u32)(rnd() % n); }

static u64 rnd_gpr()
{
	switch (rn(6))
	{
	case 0: return rnd();
	case 1: return (u32)rnd();
	case 2: return (u64)(s64)(s32)rnd();
	case 3: return rnd() & 0xFF;
	case 4: return rnd() | 0xFFFFFFFF00000000ull;
	default: return 0x8C000000u | (rnd() & 0x00FFFFFC);
	}
}
static u64 rnd_fpr()
{
	switch (rn(5))
	{
	case 0: return rnd();
	case 1: return pm::dbits((double)(float)((s32)rnd() / 65536.0));
	case 2: return pm::dbits((double)(s32)rnd());
	case 3: return 0x7FF8000000000000ull | (rnd() & 0xFFFFF);
	default: return pm::dbits((double)pm::fval((u32)rnd()));
	}
}
static void rnd_state(pm::Cpu& c)
{
	c = pm::Cpu();
	for (u32 i = 0; i < 32; i++)
	{
		c.r[i] = rnd_gpr();
		c.f[i] = rnd_fpr();
	}
	c.r[30] = CTX;
	c.r[20] = 0x8C000000u | (rnd() & 0x00FFFFFC);   // 8C PREFIX's checked address: no generated word writes r20
	c.r[21] = (u64)(s64)(s32)(BIAS_HI << 16);         // 8C BASE REG's bias (ENV.base_reg)
	c.r[22] = c.r[20] ^ 3;                             // a swizzled byte address
	c.cr = (u32)rnd();
	c.ca = rn(2); c.so = rn(2); c.ov = rn(2);
	c.ctr = rnd(); c.lr = rnd();
	c.mem_seed = rnd();
}

// --- 1. The decoder's sets against the model ---------------------------------------------------------------

enum { RES_N = 64 + SX_N };
static bool res_in(u32 r, u64 rr, u32 xx) { return r < 64 ? (rr >> r) & 1 : (xx >> (r - 64)) & 1; }
static bool res_eq(const pm::Cpu& a, const pm::Cpu& b, u32 r)
{
	if (r < 32) return a.r[r] == b.r[r];
	if (r < 64) return a.f[r - 32] == b.f[r - 32];
	const u32 x = r - 64;
	if (x < 8) return ((a.cr >> (28 - 4 * x)) & 15) == ((b.cr >> (28 - 4 * x)) & 15);
	switch (x)
	{
	case SX_CA: return a.ca == b.ca;
	case SX_SO: return a.so == b.so && a.ov == b.ov;
	case SX_CTR: return a.ctr == b.ctr;
	case SX_LR: return a.lr == b.lr;
	}
	return true;
}
static void res_perturb(pm::Cpu& c, u32 r)
{
	if (r < 32) { c.r[r] ^= rnd() | 1; return; }
	if (r < 64) { c.f[r - 32] ^= rnd() | 1; return; }
	const u32 x = r - 64;
	if (x < 8) { c.cr ^= (u32)((rn(15) + 1) << (28 - 4 * x)); return; }
	switch (x)
	{
	case SX_CA: c.ca ^= 1; return;
	case SX_SO: c.so ^= 1; c.ov ^= 1; return;
	case SX_CTR: c.ctr ^= rnd() | 1; return;
	case SX_LR: c.lr ^= rnd() | 1; return;
	}
}

static u32 g_dec_words, g_dec_modelled;
static void check_word(u32 w)
{
	SchedIns d;
	g_dec_words++;
	if (!sched_decode(w, d))
		return;
	g_dec_modelled++;
	char why[160];
	for (int t = 0; t < 3; t++)
	{
		pm::Cpu base;
		rnd_state(base);
		pm::Cpu a = base;
		g_checks++;
		if (!pm::step(a, w))
		{
			fail("decoder models a word the model does not run", w, "");
			return;
		}
		for (u32 r = 0; r < RES_N; r++)
			if (!res_in(r, d.def_r, d.def_x) && !res_eq(a, base, r))
			{
				snprintf(why, sizeof why, "writes resource %u, not in its def set", r);
				fail("undeclared write", w, why);
			}
		const bool mem = d.load || d.store;
		if (mem != (a.last_kind != 0) || (mem && (d.load != (a.last_kind == 1) || d.size != a.last_size)))
			fail("memory kind or size", w, "");
		if (mem)
		{
			const u64 ea = (d.base ? base.r[d.base] : 0) + (d.indexed ? base.r[d.index] : (u64)(s64)d.disp);
			if (ea != a.last_ea)
				fail("memory address", w, "the decoder's base, index or displacement");
		}
		for (u32 r = 0; r < RES_N; r++)
		{
			if (res_in(r, d.use_r, d.use_x) || r == 64 + SX_FPSCR)
				continue;
			pm::Cpu b = base;
			res_perturb(b, r);
			pm::step(b, w);
			bool same = b.mem == a.mem;
			for (u32 q = 0; q < RES_N && same; q++)
				if (res_in(q, d.def_r, d.def_x) && !res_eq(a, b, q))
					same = false;
			if (!same)
			{
				snprintf(why, sizeof why, "reads resource %u, not in its use set", r);
				fail("undeclared read", w, why);
			}
		}
	}
}

// Words of every form the decoder models, with random fields.
static u32 gen_word()
{
	static const u32 prim[] = { 7, 8, 10, 11, 12, 13, 14, 15, 20, 21, 23, 24, 25, 26, 27, 28, 29, 30, 32, 34, 36, 38, 40,
	                            42, 44, 48, 50, 52, 54, 58, 62, 59, 63, 31, 31, 31, 31, 19 };
	const u32 op = prim[rn(sizeof prim / sizeof prim[0])];
	u32 w = (op << 26) | ((u32)rnd() & 0x03FFFFFF);
	if (op == 31)
	{
		static const u32 xo[] = { 0, 32, 28, 60, 124, 284, 316, 412, 444, 476, 24, 536, 27, 539, 792, 794, 824, 26, 58,
		                          922, 954, 986, 19, 144, 339, 467, 23, 87, 279, 343, 21, 341, 534, 790, 535, 599, 151,
		                          215, 407, 149, 662, 918, 663, 727, 983, 8, 10, 40, 136, 138, 266, 104, 200, 202, 232,
		                          234, 11, 75, 235, 9, 73, 233, 457, 459, 489, 491 };
		w = (w & ~0x7FEu) | (xo[rn(sizeof xo / sizeof xo[0])] << 1);
		const u32 x = (w >> 1) & 0x3FF;
		if (x == 339 || x == 467)
		{
			static const u32 spr[] = { 1, 8, 9 };
			const u32 s = spr[rn(3)];
			w = (w & ~(0x3FFu << 11)) | ((s & 31) << 16) | ((s >> 5) << 11);
		}
	}
	if (op == 19)
	{
		static const u32 xo[] = { 0, 33, 129, 193, 225, 257, 289, 417, 449 };
		w = (w & ~0x7FFu) | (xo[rn(sizeof xo / sizeof xo[0])] << 1);
	}
	if (op == 59 || op == 63)
	{
		static const u32 a5[] = { 18, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 31 };
		static const u32 x10[] = { 0, 32, 12, 14, 15, 40, 72, 136, 264, 814, 815, 846 };
		if (op == 59 || rn(2))
			w = (w & ~0x3Fu) | (a5[rn(12)] << 1) | (rn(8) == 0);
		else
			w = (w & ~0x7FFu) | (x10[rn(12)] << 1);
	}
	if (op == 31 && ((w >> 1) & 0x3FF) == 19 && rn(2))
		w = (w & ~(0xFFFu << 11)) | (1u << 20) | ((0x80u >> rn(8)) << 12);
	return w;
}

// --- 2. Regions: generated and real bodies, original against reordered ----------------------------------------

static u32 g_regions_run, g_words_moved_total;

// Anchors of a body as sched_range sees them: words it does not model, branch targets and joins.
static void anchors(const u32* w, u32 n, const std::vector<u32>& joins, std::vector<u8>& start, std::vector<u8>& bar)
{
	start.assign(n, 0);
	bar.assign(n, 0);
	for (u32 j : joins)
		if (j < n)
			start[j] = 1;
	SchedIns d;
	for (u32 i = 0; i < n; i++)
	{
		bar[i] = !sched_decode(w[i], d);
		const u32* t = sched_target(w + i);
		if (t && t >= w && t < w + n)
			start[t - w] = 1;
	}
}

// Runs [a, b) of a segment on the model from state c; checks each access against the tracker's span.
static bool run_seg(pm::Cpu& c, const u32* w, u32 a, u32 b, bool spans)
{
	SchedPtr ptr[32];
	for (u32 k = 0; k < 32; k++)
		ptr[k] = SchedPtr{ SP_UNK, 0, 0 };
	ptr[30] = SchedPtr{ SP_ABS, (s64)CTX, (s64)CTX };
	for (u32 i = a; i < b; i++)
	{
		SchedIns d;
		sched_decode(w[i], d);
		s64 lo = 0, hi = 0;
		const bool known = spans && (d.load || d.store) && sched_span(d, ptr[d.base], ENV, lo, hi);
		if (!pm::step(c, w[i]))
		{
			fail("model cannot run a modelled word", w[i], "");
			return false;
		}
		if (known && ((s64)c.last_ea < lo || (s64)c.last_ea + (s64)c.last_size > hi))
		{
			char why[120];
			snprintf(why, sizeof why, "address %llX outside the span %llX-%llX", (unsigned long long)c.last_ea,
			         (unsigned long long)lo, (unsigned long long)hi);
			fail("address kind", w[i], why);
		}
		sched_track(d, ptr, ENV);
	}
	return true;
}

static bool same_state(const pm::Cpu& a, const pm::Cpu& b)
{
	for (u32 i = 0; i < 32; i++)
		if (a.r[i] != b.r[i] || a.f[i] != b.f[i])
			return false;
	return a.cr == b.cr && a.ca == b.ca && a.so == b.so && a.ov == b.ov && a.ctr == b.ctr && a.lr == b.lr && a.mem == b.mem;
}

static SchedWork g_sw;
static SchedStats g_real;   // the scheduler's own estimate on the real block bodies
static bool run_whole(pm::Cpu& c, const u32* w, u32 n, u64 seed);

// Schedules the body w[0..n) with these joins, then compares every segment between anchors on the model; whole:
// also the whole body, calls included (generated bodies).
static void check_body(const u32* w, u32 n, const std::vector<u32>& joins, u32 states, const char* what,
                       bool whole = false)
{
	std::vector<u32> s(w, w + n);
	std::vector<const void*> jp;
	for (u32 j : joins)
		jp.push_back(&s[0] + j);
	SchedStats st = {};
	sched_range(g_sw, &s[0], &s[0] + n, jp.empty() ? 0 : &jp[0], (u32)jp.size(), ENV, st);
	g_words_moved_total += st.moved;
	if (!whole)
	{
		g_real.regions += st.regions;
		g_real.words += st.words;
		g_real.moved += st.moved;
		g_real.cyc_before += st.cyc_before;
		g_real.cyc_after += st.cyc_after;
	}
	std::vector<u8> start, bar;
	anchors(w, n, joins, start, bar);
	u32 i = 0;
	while (i < n)
	{
		if (bar[i])
		{
			if (s[i] != w[i])
				fail("an anchor moved", w[i], what);
			i++;
			continue;
		}
		u32 j = i + 1;
		while (j < n && !bar[j] && !start[j])
			j++;
		std::vector<u32> x(w + i, w + j), y(s.begin() + i, s.begin() + j);
		std::sort(x.begin(), x.end());
		std::sort(y.begin(), y.end());
		if (x != y)
			fail("a word crossed an anchor", w[i], what);
		else
			for (u32 t = 0; t < states; t++)
			{
				pm::Cpu c0;
				rnd_state(c0);
				pm::Cpu c1 = c0, c2 = c0;
				g_checks++;
				if (!run_seg(c1, w, i, j, true) || !run_seg(c2, &s[0], i, j, false))
					break;
				if (!same_state(c1, c2))
				{
					char why[96];
					snprintf(why, sizeof why, "%s: segment of %u words at %u", what, j - i, i);
					fail("reordered segment differs", w[i], why);
					break;
				}
			}
		g_regions_run++;
		i = j;
	}
	for (u32 t = 0; whole && t < states; t++)
	{
		pm::Cpu c0;
		rnd_state(c0);
		pm::Cpu c1 = c0, c2 = c0;
		const u64 seed = rnd() | 1;
		g_checks++;
		if (!run_whole(c1, w, n, seed) || !run_whole(c2, &s[0], n, seed))
			break;
		if (!same_state(c1, c2))
		{
			fail("reordered body differs (calls included)", w[0], what);
			break;
		}
	}
}

// Generated bodies: few registers, so words depend on each other; context, RAM, absolute and store-queue bases.
static u32 gen_reg() { static const u32 g[] = { 0, 3, 4, 5, 6, 11, 12, 14, 15, 16 }; return g[rn(10)]; }
static u32 gen_freg() { static const u32 f[] = { 0, 1, 2, 3, 14, 15, 16, 17 }; return f[rn(8)]; }

static void gen_body(std::vector<u32>& w)
{
	const u32 n = 4 + rn(120);
	while (w.size() < n)
	{
		const u32 d = gen_reg(), a = gen_reg(), b = gen_reg(), f = gen_freg(), g = gen_freg(), h = gen_freg();
		switch (rn(33))
		{
		case 0: w.push_back(0x38000000 | d << 21 | a << 16 | (rnd() & 0xFFFF)); break;                  // addi
		case 1: w.push_back(0x7C000214 | d << 21 | a << 16 | b << 11); break;                           // add
		case 2: w.push_back(0x54000000 | a << 21 | d << 16 | ((u32)rnd() & 0xFFFE)); break;             // rlwinm
		case 3: w.push_back(0x28000000 | rn(3) << 23 | a << 16 | (rnd() & 0xFF)); break;               // cmpli
		case 4: w.push_back(0x80000000 | d << 21 | 30 << 16 | rn(16) * 4); break;                       // lwz ctx
		case 5: w.push_back(0x90000000 | d << 21 | 30 << 16 | rn(16) * 4); break;                       // stw ctx
		case 6: w.push_back(0xC0000000 | f << 21 | 30 << 16 | rn(16) * 4); break;                       // lfs ctx
		case 7: w.push_back(0xD0000000 | f << 21 | 30 << 16 | rn(16) * 4); break;                       // stfs ctx
		case 8:                                                                                         // RAM pointer
			w.push_back(0x54000000 | a << 21 | d << 16 | 8 << 6 | 31 << 1);
			w.push_back(0x3C000000 | d << 21 | d << 16 | RAM_HI);
			if (rn(2)) w.push_back(0x68000000 | d << 21 | d << 16 | rn(4));
			break;
		case 9: w.push_back(0x80000000 | b << 21 | d << 16 | rn(4) * 4); break;                         // lwz any
		case 10: w.push_back(0x90000000 | b << 21 | d << 16 | rn(4) * 4); break;                        // stw any
		case 11: w.push_back(0x88000000 | b << 21 | d << 16 | rn(8)); break;                            // lbz any
		case 12: w.push_back(0x98000000 | b << 21 | d << 16 | rn(8)); break;                            // stb any
		case 13: w.push_back(0xEC00002A | f << 21 | g << 16 | h << 11); break;                         // fadds
		case 14: w.push_back(0xEC000032 | f << 21 | g << 16 | h << 6); break;                          // fmuls
		case 15: w.push_back(0xEC00003A | f << 21 | g << 16 | h << 11 | gen_freg() << 6); break;      // fmadds
		case 16: w.push_back(0x7C000774 | a << 21 | d << 16); break;                                    // extsb
		case 17: w.push_back(0x3C000000 | d << 21 | (0x1180 + rn(4))); w.push_back(0x60000000 | d << 21 | d << 16 | rn(64) * 4); break;   // abs
		case 18:                                                                                        // SQ pointer
			w.push_back(0x54000000 | a << 21 | d << 16 | 26 << 6 | 29 << 1);
			w.push_back(0x3C000000 | d << 21 | d << 16 | 0x11E5);
			break;
		case 19: w.push_back(0x30000000 | d << 21 | a << 16 | (rnd() & 0xFFFF)); break;                 // addic
		case 20: w.push_back(0x7C000114 | d << 21 | a << 16 | b << 11); break;                          // adde
		case 21: w.push_back(0x7C000378 | a << 21 | d << 16 | b << 11 | rn(2)); break;                  // or(.)
		case 22: w.push_back(0x4C000242 | (rn(12) + 8) << 21 | (rn(12) + 8) << 16 | (rn(12) + 8) << 11); break;   // creqv
		case 23: w.push_back(0x7C100026 | d << 21 | (0x80u >> rn(3)) << 12); break;                    // mfocrf
		case 24: w.push_back(0xFC00001E | f << 21 | g << 11); break;                                    // fctiwz
		case 25:                                                                                        // a RAM index of any mask
			w.push_back(0x54000000 | a << 21 | d << 16 | rn(32) << 6 | 31 << 1);
			w.push_back(0x3C000000 | d << 21 | d << 16 | RAM_HI);
			break;
		case 26: w.push_back(0x48000001 | (rn(1024) << 2)); break;                                     // bl: a call
		case 27: w.push_back(rn(2) ? 0x7C0004AC : 0x7C001A2C); break;                                   // sync, dcbt
		case 28:                                                                                        // near the context
			w.push_back(0x3C000000 | d << 21 | ((CTX + 0x8000) >> 16));
			w.push_back(0x38000000 | d << 21 | d << 16 | ((CTX + (rn(2) ? rn(16) * 4 : 0)) & 0xFFFF));
			w.push_back(0x60000000 | d << 21 | d << 16 | (rn(2) ? 0x4000 : 0) | rn(16) * 4);
			w.push_back((rn(2) ? 0x90000000 : 0x80000000) | b << 21 | d << 16 | rn(4) * 4);
			break;
		case 29:                                                                                        // RAM, any xori
			w.push_back(0x54000000 | a << 21 | d << 16 | 8 << 6 | 31 << 1);
			w.push_back(0x3C000000 | d << 21 | d << 16 | RAM_HI);
			w.push_back((rn(3) ? 0x68000000 : 0x6C000000) | d << 21 | d << 16 | (rn(2) ? rn(4) : (u32)rnd() & 0xFFFF));
			w.push_back((rn(2) ? 0x90000000 : 0x88000000) | b << 21 | d << 16 | rn(4) * 4);
			break;
		case 30:                                                                                        // 8C PREFIX past its guard
			w.push_back(0x3C000000 | d << 21 | 20 << 16 | BIAS_HI);
			if (rn(2)) w.push_back(0x68000000 | d << 21 | d << 16 | rn(4));
			w.push_back((rn(2) ? 0x90000000 : 0x80000000) | b << 21 | d << 16 | rn(4) * 4);
			break;
		case 31:                                                                                        // 8C BASE REG past its guard
		{
			// Words at r20, bytes at r22 (r20 ^ 3); lwzx, stwx, lbzx, stbx. The base register as the base is no RAM.
			static const u32 xo[] = { 0x7C00002E, 0x7C00012E, 0x7C0000AE, 0x7C0001AE };
			const u32 k = rn(5);
			if (k == 4)
				w.push_back(0x7C00002E | b << 21 | 21 << 16 | a << 11);
			else
				w.push_back(xo[k] | b << 21 | (k < 2 ? 20 : 22) << 16 | 21 << 11);
			break;
		}
		default: w.push_back(0x7C0007AE | f << 21 | a << 16 | b << 11); break;                          // stfiwx
		}
	}
	w.resize(n);
}

// A whole generated body on the model: a call (bl) returns a pointer into the context in r3 and changes the
// volatile registers; sync and dcbt do nothing. The same values for both orders.
static bool run_whole(pm::Cpu& c, const u32* w, u32 n, u64 seed)
{
	u64 s = seed;
	for (u32 i = 0; i < n; i++)
	{
		if ((w[i] >> 26) == 18)
		{
			for (u32 k = 0; k <= 12; k++)
				if (k != 1 && k != 2)
				{
					s ^= s << 13; s ^= s >> 7; s ^= s << 17;
					c.r[k] = s;
				}
			c.r[3] = CTX + 4 * (s % 16);
			for (u32 k = 0; k < 14; k++)
				c.f[k] = s * (k + 3);
			c.cr = (c.cr & 0x00FFF000u) | ((u32)s & 0xFF000FFFu);
			c.ca = (u32)(s >> 7) & 1;
			continue;
		}
		if (w[i] == 0x7C0004AC || w[i] == 0x7C001A2C)
			continue;
		if (!pm::step(c, w[i]))
		{
			fail("model cannot run a modelled word", w[i], "whole body");
			return false;
		}
	}
	return true;
}

// Directed: an address kind known before a call or a join must not survive it, as the call or the cold path that
// joins can leave a context pointer there. The store through r3 must stay before the context load.
static void check_directed()
{
	const u32 ram = 0x54000000 | 14 << 21 | 3 << 16 | 8 << 6 | 31 << 1, base = 0x3C000000 | 3 << 21 | 3 << 16 | RAM_HI;
	const u32 tail[] = { 0x7CA529D2, 0x90A30000, 0x80DE0000, 0x7CE63214, 0x7D073A14 };   // mulld, stw, lwz, add, add
	for (int v = 0; v < 3; v++)
	{
		std::vector<u32> w = { ram, base };
		if (v == 0)
			w.push_back(0x48000041);   // bl
		for (u32 t : tail)
			w.push_back(t);
		std::vector<const void*> jp;
		if (v == 1)
			jp.push_back(&w[2]);
		SchedStats st = {};
		sched_range(g_sw, &w[0], &w[0] + w.size(), jp.empty() ? 0 : &jp[0], (u32)jp.size(), ENV, st);
		u32 at_st = 0, at_ld = 0;
		for (u32 i = 0; i < w.size(); i++)
		{
			if (w[i] == 0x90A30000) at_st = i;
			if (w[i] == 0x80DE0000) at_ld = i;
		}
		g_checks++;
		// v 2: no call, no join: r3 is a RAM pointer, so the load may and should go first.
		if (v < 2 && at_ld < at_st)
			fail("a known address survived a call or a join", 0x90A30000, v ? "join" : "call");
		if (v == 2 && at_ld > at_st)
			fail("a RAM store kept a context load behind it", 0x90A30000, "no call, no join");
	}
}

int main(int argc, char** argv)
{
	check_directed();
	const u32 regions = argc > 1 ? (u32)atoi(argv[1]) : 20000;
	if (argc > 2)
		g_seed ^= (u64)atoi(argv[2]) * 0x9E3779B97F4A7C15ull;

	// 1. Real words, then generated ones.
	for (u32 i = 0; i < sizeof k_words / sizeof k_words[0]; i++)
		check_word(k_words[i]);
	for (u32 i = 0; i < 40000; i++)
		check_word(gen_word());
	printf("decoder: %u words, %u modelled\n", g_dec_words, g_dec_modelled);

	// 2. Generated bodies, some with joins.
	for (u32 k = 0; k < regions; k++)
	{
		std::vector<u32> w;
		gen_body(w);
		std::vector<u32> joins;
		for (u32 t = rn(3); t; t--)
			joins.push_back(rn((u32)w.size()));
		check_body(&w[0], (u32)w.size(), joins, 2, "generated", true);
	}

	// 3. Real block bodies (the hot part); joins are where the cold code's branches come back to.
	u32 real = 0;
	for (u32 b = 0; b < sizeof k_blk_index / sizeof k_blk_index[0]; b++)
	{
		const u32* w = k_blk_words + k_blk_index[b][0];
		const u32 hot = k_blk_index[b][1], total = k_blk_index[b][2];
		std::vector<u32> joins;
		for (u32 i = hot; i < total; i++)
		{
			const u32* t = sched_target(w + i);
			if (t && t >= w && t < w + hot && (w[i] >> 26) == 18)
				joins.push_back((u32)(t - w));
		}
		check_body(w, hot, joins, 3, "real block");
		real++;
	}

	printf("regions: %u segments run, %u words moved, %u real blocks, %d checks\n", g_regions_run, g_words_moved_total,
	       real, g_checks);
	printf("real bodies: %u regions, %u words, %u moved, estimate %u -> %u cycles (%.1f%%)\n", g_real.regions,
	       g_real.words, g_real.moved, g_real.cyc_before, g_real.cyc_after,
	       g_real.cyc_before ? 100.0 * (1.0 - (double)g_real.cyc_after / g_real.cyc_before) : 0.0);
	if (g_words_moved_total < 1000)
		fail("the scheduler moved almost nothing", 0, "");
	if (g_fail)
	{
		printf("FAIL: %d\n", g_fail);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
