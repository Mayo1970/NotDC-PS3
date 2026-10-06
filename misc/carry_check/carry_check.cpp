// Host-only check for plan 5.2: ps3_jit_carry.h against ppu-as words (carry_words.inc), then every form on a
// PPC64 model against the SH4 interpreter, with junk above the low word of pinned registers. Not in the PS3 build.
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
#include "carry_words.inc"

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

static const u64 XER_SO = 0x80000000u, XER_OV = 0x40000000u, XER_CA = 0x20000000u;

// PPC64 model in 64-bit mode (MSR[SF] = 1, as on the PS3): CA, OV and CR0 come from 64-bit results. Only the
// forms the header and the pre-5.2 driver emit; anything else faults.
struct Cpu
{
	u64 r[32];
	u32 cr;
	u64 xer;
	int bad;
	const char* why;

	void fault(const char* w) { if (!bad) { bad = 1; why = w; } }

	void cr0(u64 v)
	{
		const u32 f = (s64)v < 0 ? 8 : (s64)v > 0 ? 4 : 2;
		cr = (cr & 0x0FFFFFFFu) | (f << 28) | (u32)((xer & XER_SO) ? 1u << 28 : 0);
	}

	void ov(bool o) { xer = o ? xer | XER_OV | XER_SO : xer & ~XER_OV; }

	void run(const u32* code, u32 n)
	{
		for (u32 pc = 0; pc < n && !bad; pc++)
		{
			const u32 x = code[pc], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 uimm = x & 0xFFFF;
			switch (op)
			{
			case 14: r[d] = (a ? r[a] : 0) + (u64)simm; break;
			case 15: r[d] = (a ? r[a] : 0) + ((u64)simm << 16); break;
			case 24: r[a] = r[d] | uimm; break;
			case 26: r[a] = r[d] ^ uimm; break;
			case 20:
			case 21:
			{
				if (x & 1) { fault("rlwinm. / rlwimi. (record form)"); return; }
				const u32 mb = (x >> 6) & 31, me = (x >> 1) & 31;
				const u64 lo = (u32)r[d], rot = rotl64((lo << 32) | lo, b), m = mask64(mb + 32, me + 32);
				r[a] = op == 21 ? rot & m : (rot & m) | (r[a] & ~m);
				break;
			}
			case 30:
			{
				if (x & 1) { fault("rld record form"); return; }
				if ((x >> 2) & 7) { fault("MD-form other than rldicl"); return; }
				const u32 sh = b | (((x >> 1) & 1) << 5), mb = ((x >> 6) & 31) | (((x >> 5) & 1) << 5);
				r[a] = rotl64(r[d], sh) & mask64(mb, 63);
				break;
			}
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
		const u32 xo = (x >> 1) & 0x3FF, rc = x & 1;
		if (rc && xo != 28) { fault("record form"); return; }
		switch (xo)
		{
		case 28:  r[a] = r[d] & r[b]; if (rc) cr0(r[a]); return;
		case 60:  r[a] = r[d] & ~r[b]; return;
		case 316: r[a] = r[d] ^ r[b]; return;
		case 444: r[a] = r[d] | r[b]; return;
		case 986: if (b) break; r[a] = (u64)(s64)(s32)(u32)r[d]; return;
		case 339: if (((x >> 11) & 0x3FF) != 32) break; r[d] = (u32)xer; return;
		case 467: if (((x >> 11) & 0x3FF) != 32) break; xer = (u32)r[d]; return;
		}
		const u32 oe = (x >> 10) & 1, xo9 = (x >> 1) & 0x1FF;
		const u64 ra = r[a], rb = r[b], ca = (xer & XER_CA) ? 1 : 0;
		unsigned __int128 s;
		switch (xo9)
		{
		case 266:
			r[d] = ra + rb;
			if (oe) ov((((ra ^ r[d]) & (rb ^ r[d])) >> 63) != 0);
			return;
		case 40:
			r[d] = rb - ra;
			if (oe) ov((((~ra ^ r[d]) & (rb ^ r[d])) >> 63) != 0);
			return;
		case 104:
			if (b || oe) { fault("unexpected neg form"); return; }
			r[d] = 0 - ra;
			return;
		case 136:
		case 138:
		case 200:
			if (oe || (xo9 == 200 && b)) { fault("unexpected adde/subfe/subfze form"); return; }
			s = xo9 == 138 ? (unsigned __int128)ra + rb + ca
			  : xo9 == 136 ? (unsigned __int128)~ra + rb + ca : (unsigned __int128)~ra + ca;
			r[d] = (u64)s;
			xer = (s >> 64) ? xer | XER_CA : xer & ~XER_CA;
			return;
		default:
			fault("unexpected X-form");
			return;
		}
	}
};

// The SH4 interpreter (dc/sh4/sh4_cpu_misc.h) is the reference; Rm is passed by value, as it reads it first.
static void ref_addc(u32& rn, u32 rm, u32& T)
{
	const u32 tmp0 = rn, tmp1 = rn + rm;
	rn = tmp1 + T;
	T = (tmp0 > tmp1) || (tmp1 > rn);
}
static void ref_subc(u32& rn, u32 rm, u32& T)
{
	const u32 tmp0 = rn, tmp1 = rn - rm;
	rn = tmp1 - T;
	T = (tmp0 < tmp1) || (tmp1 < rn);
}
static void ref_negc(u32& rn, u32 rm, u32& T)
{
	const u32 tmp = 0 - rm;
	rn = tmp - T;
	T = (0 < tmp) || (tmp < rn);
}
static void ref_addv(u32& rn, u32 rm, u32& T)
{
	const s32 dest = (s32)rn, src = (s32)rm, ans = (s32)(rn + rm);
	rn = (u32)ans;
	T = (dest >= 0 && src >= 0 && ans < 0) || (dest < 0 && src < 0 && ans >= 0);
}
static void ref_subv(u32& rn, u32 rm, u32& T)
{
	const s32 dest = (s32)rn, src = (s32)rm, ans = (s32)(rn - rm);
	rn = (u32)ans;
	T = (dest >= 0 && src < 0 && ans < 0) || (dest < 0 && src >= 0 && ans >= 0);
}
// SR: Q is bit 8, M bit 9.
static void ref_div1(u32& rn, u32 rm, u32& sr, u32& T)
{
	const u32 old_q = (sr >> 8) & 1, M = (sr >> 9) & 1;
	u32 q = (rn >> 31) & 1;
	rn = (rn << 1) | T;
	const u32 old_rn = rn;
	if (old_q == M)
	{
		rn -= rm;
		const u32 borrow = (rn > old_rn) ? 1 : 0;
		q = M ? (!q ^ borrow) : (q ^ borrow);
	}
	else
	{
		rn += rm;
		const u32 carry = (rn < old_rn) ? 1 : 0;
		q = M ? (!q ^ carry) : (q ^ carry);
	}
	sr = (sr & ~(1u << 8)) | ((q & 1) << 8);
	T = ((q & 1) == M) ? 1 : 0;
}
static u32 ref_cmpstr(u32 rn, u32 rm)
{
	const u32 t = rn ^ rm;
	return ((t >> 24) & 0xFF) && ((t >> 16) & 0xFF) && ((t >> 8) & 0xFF) && (t & 0xFF) ? 0 : 1;
}
static u32 ref_swaplb(u32 rm)
{
	return (rm & 0xFFFF0000) | ((rm & 0xFF) << 8) | ((rm >> 8) & 0xFF);
}

// Pre-5.2 driver forms, T in register t instead of the sr_T slot (the old div1 copies it from u); for the report.
static void old_carry(u32 op, u32 d, u32 a, u32 b, u32 t)
{
	if (op <= 2)
	{
		if (op)
			ppc_xori(t, t, 1);
		ppc_rlwinmx(t, t, 29, 2, 2, 0);
		ppc_mtxer(t);
		if (op == 0)
			ppc_addex(d, a, b, 0, 0);
		else if (op == 1)
			ppc_subfex(d, b, a, 0, 0);
		else
			ppc_subfzex(d, a, 0, 0);
		ppc_mfxer(t);
		ppc_rlwinmx(t, t, 3, 31, 31, 0);
		if (op)
			ppc_xori(t, t, 1);
		return;
	}
	if (op == 3)
		ppc_addx(d, a, b, 1, 0);
	else
		ppc_subfx(d, b, a, 1, 0);
	ppc_mfxer(t);
	ppc_rlwinmx(t, t, 2, 31, 31, 0);
}
static void old_div1(u32 A, u32 B, u32 u)
{
	const u32 t0 = 3, t1 = 4, t2 = 5, t3 = 6, t4 = 0, t5 = 7;
	ppc_rlwinmx(t1, t0, 23, 31, 31, 0);
	ppc_rlwinmx(t2, t0, 24, 31, 31, 0);
	ppc_xorx(t5, t2, t1, 0);
	ppc_addi(t4, t5, -1);
	ppc_xorx(t4, B, t4, 0);
	ppc_xori(t5, t5, 1);
	ppc_rlwinmx(t5, t5, 29, 2, 2, 0);
	ppc_mtxer(t5);
	ppc_rlwinmx(t3, A, 1, 31, 31, 0);
	ppc_xorx(t3, t3, t2, 0);
	ppc_ori(t2, u, 0);
	ppc_rlwinmx(A, A, 1, 0, 30, 0);
	ppc_orx(A, A, t2, 0);
	ppc_addex(A, A, t4, 0, 0);
	ppc_mfxer(t2);
	ppc_rlwinmx(t2, t2, 3, 31, 31, 0);
	ppc_xorx(t3, t3, t2, 0);
	ppc_xori(t3, t3, 1);
	ppc_xorx(t2, t3, t1, 0);
	ppc_xori(t2, t2, 1);
	ppc_rlwimix(t0, t3, 8, 23, 23, 0);
}

// Edge values often: carries, borrows and overflows at the 32-bit boundaries.
static u32 rnd_val()
{
	static const u32 edge[] = { 0, 1, 2, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu,
	                            0x0000FFFFu, 0x00010000u, 0x01010101u, 0x80808080u, 0xFF00FF00u, 0x00FF00FFu };
	switch (rnd64() % 4)
	{
	case 0: return edge[rnd64() % (sizeof(edge) / sizeof(edge[0]))];
	case 1: return (u32)(rnd64() % 5) - 2u;
	default: return (u32)rnd64();
	}
}

// A pinned register holds its value with junk above it; an unpinned one is lwz'd into scratch, upper half 0.
static u64 pinned_val(u32 v, bool clean) { return (clean ? 0 : rnd64() & 0xFFFFFFFF00000000ull) | v; }

// As the driver: SH4 r0-r15 but r11 pinned in r14-r28; Rn/Rm r11 means the scratch the driver loads.
struct Pick { u32 rn, rm; bool rn_pin, rm_pin; };
static Pick pick_regs(u32 mode)
{
	Pick p;
	const u32 i = 14 + (u32)(rnd64() % 15);
	u32 j = 14 + (u32)(rnd64() % 14);
	if (j >= i)
		j++;
	p.rn_pin = mode != 3 && mode != 4;
	p.rm_pin = mode != 2 && mode != 4;
	p.rn = i;
	p.rm = mode == 1 ? i : j;
	return p;
}

static void fresh(Cpu& c)
{
	c.bad = 0;
	c.why = "";
	for (u32 k = 0; k < 32; k++)
		c.r[k] = rnd64();
	c.cr = (u32)rnd64();
	c.xer = rnd64() & (XER_SO | XER_OV | XER_CA);
}

// Only the listed GPRs (and CR0 when cr0 is set) may differ; XER never.
static bool only_changed(const Cpu& a, const Cpu& b, u32 gprs, bool cr0)
{
	for (u32 k = 0; k < 32; k++)
		if (!((gprs >> k) & 1) && a.r[k] != b.r[k])
			return false;
	const u32 keep = cr0 ? 0x0FFFFFFFu : 0xFFFFFFFFu;
	return (a.cr & keep) == (b.cr & keep) && a.xer == b.xer;
}

static const char* const k_names[] = { "addc", "subc", "negc", "addv", "subv" };

// One case of op 0-4 in mode 0-4: d, a, b as the driver picks them, T in r5, x r3, y r4. The result: 0 right.
static int run_carry(u32 op, u32 mode, bool clean, bool old, char* d_out, size_t d_len)
{
	const Pick p = pick_regs(mode);
	u32 rn = rnd_val(), rm = rnd_val(), T = (u32)(rnd64() & 1);
	if (p.rn == p.rm && p.rn_pin && p.rm_pin)
		rm = rn;
	Cpu c;
	fresh(c);
	u32 dreg, areg, breg = 0;
	if (op == 2)
	{
		// negc: rd = Rn, rs1 = Rm; Rn may be Rm.
		areg = p.rm_pin ? p.rm : 3;
		dreg = p.rn_pin ? p.rn : 6;
		c.r[areg] = pinned_val(rm, clean || !p.rm_pin);
		if (p.rn_pin && p.rn != p.rm)
			c.r[dreg] = pinned_val(rn, clean);
		if (mode == 1)
			rn = rm;
	}
	else
	{
		// rd = rs1 = Rn, rs2 = Rm.
		areg = p.rn_pin ? p.rn : 3;
		breg = p.rm_pin ? p.rm : 4;
		dreg = p.rn_pin ? p.rn : 6;
		if (mode == 4)
			rm = rn;
		c.r[areg] = pinned_val(rn, clean || !p.rn_pin);
		if (breg != areg)
			c.r[breg] = pinned_val(rm, clean || !p.rm_pin);
	}
	if (op <= 2)
		c.r[5] = T;
	Cpu before = c;

	g_n = 0;
	if (old)
		old_carry(op, dreg, areg, breg, 5);
	else if (op == 0)
		emit_carry_addc(dreg, areg, breg, 5, 3, 4);
	else if (op == 1)
		emit_carry_subc(dreg, areg, breg, 5, 3, 4);
	else if (op == 2)
		emit_carry_negc(dreg, areg, 5, 3);
	else
		emit_carry_ovf(dreg, areg, breg, 5, 3, 4, op == 4);
	c.run(g_code, g_n);

	u32 want = rn, wantT = T;
	switch (op)
	{
	case 0: ref_addc(want, rm, wantT); break;
	case 1: ref_subc(want, rm, wantT); break;
	case 2: ref_negc(want, rm, wantT); break;
	case 3: ref_addv(want, rm, wantT); break;
	default: ref_subv(want, rm, wantT); break;
	}
	snprintf(d_out, d_len, "%s mode %u r%u=r%u,r%u Rn=%08X Rm=%08X T=%u: got %08X T=%016llX want %08X T=%u", k_names[op],
	         mode, dreg, areg, breg, rn, rm, T, (u32)c.r[dreg], (unsigned long long)c.r[5], want, wantT);
	if (c.bad)
	{
		snprintf(d_out, d_len, "%s: %s", k_names[op], c.why);
		return 1;
	}
	const u32 may = (1u << dreg) | (1u << 5) | (1u << 3) | (1u << 4);
	if ((u32)c.r[dreg] != want || c.r[5] != wantT)
		return 1;
	return old || only_changed(before, c, may, false) ? 0 : 2;
}

static int run_div1(u32 mode, bool clean, bool old, char* d_out, size_t d_len)
{
	const Pick p = pick_regs(mode);
	u32 rn = rnd_val(), rm = rnd_val(), T = (u32)(rnd64() & 1), sr = (u32)rnd64();
	if (mode == 1 || mode == 4)
		rm = rn;
	Cpu c;
	fresh(c);
	// As the driver: A = Rn pinned or r10, B = Rm pinned or r9, s r3, t r5, m r4, q r6, x r8, y r7.
	const u32 A = p.rn_pin ? p.rn : 10, B = p.rm_pin ? p.rm : 9;
	c.r[A] = pinned_val(rn, clean || !p.rn_pin);
	if (B != A)
		c.r[B] = pinned_val(rm, clean || !p.rm_pin);
	c.r[3] = sr;
	if (old)
		c.r[11] = T;
	else
		c.r[5] = T;
	Cpu before = c;

	g_n = 0;
	if (old)
		old_div1(A, B, 11);
	else
		emit_carry_div1(A, B, 3, 5, 4, 6, 8, 7);
	c.run(g_code, g_n);

	u32 want = rn, wantT = T, wantsr = sr;
	ref_div1(want, rm, wantsr, wantT);
	snprintf(d_out, d_len, "div1 mode %u A r%u B r%u Rn=%08X Rm=%08X T=%u SR=%08X: got %08X T=%016llX SR=%08X want "
	         "%08X T=%u SR=%08X", mode, A, B, rn, rm, T, sr, (u32)c.r[A], (unsigned long long)c.r[5], (u32)c.r[3], want,
	         wantT, wantsr);
	if (c.bad)
	{
		snprintf(d_out, d_len, "div1: %s", c.why);
		return 1;
	}
	if ((u32)c.r[A] != want || c.r[5] != wantT || (u32)c.r[3] != wantsr)
		return 1;
	const u32 may = (1u << A) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
	return old || only_changed(before, c, may, false) ? 0 : 2;
}

static void test_carry(u32 iters)
{
	for (u32 op = 0; op < 5; op++)
		for (u32 t = 0; t < iters; t++)
		{
			char d[200];
			const int r = run_carry(op, t % 5, (t / 5) % 4 == 0, false, d, sizeof(d));
			check(r == 0, k_names[op], r == 2 ? "a register outside the form, CR or XER changed" : d);
		}
	for (u32 t = 0; t < iters; t++)
	{
		char d[240];
		const int r = run_div1(t % 5, (t / 5) % 4 == 0, false, d, sizeof(d));
		check(r == 0, "div1", r == 2 ? "a register outside the form, CR or XER changed" : d);
	}
}

// A byte of Rn often equal to Rm's, in each lane.
static void test_cmpstr(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const u32 mode = t % 5;
		const Pick p = pick_regs(mode);
		u32 rn = rnd_val(), rm = rnd_val();
		for (u32 k = 0; k < 4; k++)
			if (rnd64() % 3 == 0)
				rm = (rm & ~(0xFFu << (8 * k))) | (rn & (0xFFu << (8 * k)));
		if (mode == 1 || mode == 4)
			rm = rn;
		Cpu c;
		fresh(c);
		const u32 a = p.rn_pin ? p.rn : 3, b = p.rm_pin ? p.rm : 4;
		c.r[a] = pinned_val(rn, !p.rn_pin);
		if (b != a)
			c.r[b] = pinned_val(rm, !p.rm_pin);
		Cpu before = c;
		g_n = 0;
		emit_carry_cmpstr(a, b, 5, 6, 0);
		c.run(g_code, g_n);
		const u32 want = ref_cmpstr(rn, rm), got = (c.cr >> 30) & 1;
		char d[160];
		snprintf(d, sizeof(d), "mode %u Rn=%08X Rm=%08X: CR0 %X want GT=%u", mode, rn, rm, c.cr >> 28, want);
		check(!c.bad && got == want && !(c.cr >> 31) && only_changed(before, c, (1u << 5) | (1u << 6) | 1u, true),
		      "cmpstr", c.bad ? c.why : d);
	}
}

static void test_swaplb(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const bool pin = t & 1;
		const u32 s = pin ? 14 + (u32)(rnd64() % 15) : 3, v = rnd_val();
		Cpu c;
		fresh(c);
		c.r[s] = pinned_val(v, !pin);
		Cpu before = c;
		g_n = 0;
		emit_carry_swaplb(4, s);
		c.run(g_code, g_n);
		char d[96];
		snprintf(d, sizeof(d), "r%u=%08X: got %08X want %08X", s, v, (u32)c.r[4], ref_swaplb(v));
		check(!c.bad && (u32)c.r[4] == ref_swaplb(v) && only_changed(before, c, 1u << 4, false), "swaplb",
		      c.bad ? c.why : d);
	}
}

// Report only: how often the pre-5.2 forms were wrong on the same model, with clean and with junk upper halves.
static void report_old(u32 iters)
{
	printf("pre-5.2 forms, wrong results (clean upper halves / junk above pinned values):\n");
	for (u32 op = 0; op < 6; op++)
	{
		u32 wrong[2] = { 0, 0 };
		for (u32 k = 0; k < 2; k++)
			for (u32 t = 0; t < iters; t++)
			{
				char d[240];
				const int r = op < 5 ? run_carry(op, t % 5, k == 0, true, d, sizeof(d))
				                     : run_div1(t % 5, k == 0, true, d, sizeof(d));
				wrong[k] += r != 0;
			}
		printf("  %-4s %5.1f%% / %5.1f%%\n", op < 5 ? k_names[op] : "div1", 100.0 * wrong[0] / iters,
		       100.0 * wrong[1] / iters);
	}
}

static void test_words()
{
	g_n = 0;
	emit_carry_addc(14, 14, 15, 5, 3, 4);
	emit_carry_addc(6, 3, 4, 5, 3, 4);
	emit_carry_subc(14, 14, 15, 5, 3, 4);
	emit_carry_subc(6, 3, 28, 5, 3, 4);
	emit_carry_negc(14, 15, 5, 3);
	emit_carry_negc(6, 3, 5, 3);
	emit_carry_ovf(14, 14, 15, 5, 3, 4, false);
	emit_carry_ovf(6, 3, 28, 5, 3, 4, true);
	emit_carry_div1(14, 15, 3, 5, 4, 6, 8, 7);
	emit_carry_div1(10, 9, 3, 5, 4, 6, 8, 7);
	emit_carry_cmpstr(14, 15, 5, 6, 0);
	emit_carry_cmpstr(3, 4, 5, 6, 0);
	emit_carry_swaplb(4, 14);
	emit_carry_swaplb(4, 3);
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
	test_carry(iters);
	test_cmpstr(iters);
	test_swaplb(iters);
	if (argc > 2 && strcmp(argv[2], "old") == 0)
		report_old(iters);
	printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
