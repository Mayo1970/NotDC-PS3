// Host-only check for plan 5.38: div32_match.h's SHIL with ps3_jit_div32.h's PPU forms (PPC64 model, junk above
// pinned values) and with the real shil_canonical.h ops, against the SH4 interpreter's division chain.
#include "dc/sh4/rec_v2/decoder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static u32 g_code[512];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_carry.h"
#include "ps3/ps3_jit_div32.h"
#include "dc/sh4/rec_v2/div32_match.h"
#include "div32_words.inc"

void canon_div32u(u32 r1, u32 r2, u32 r3, u32& quo, u32& rem);
void canon_div32s(u32 r1, u32 r2, u32 r3, u32& quo, u32& rem);
u32 canon_div32p2(u32 a, u32 b, u32 T);

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
static u32 rnd32() { return (u32)rnd64(); }

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

// PPC64 model in 64-bit mode (MSR[SF] = 1, as on the PS3): only the forms ps3_jit_div32.h emits, anything else
// faults. What the ISA leaves undefined is junk: divides by 0, divd overflow, the high words of divw/divwu/mullw.
struct Cpu
{
	u64 r[32];
	u32 cr;
	u64 xer;
	int bad;
	const char* why;

	void fault(const char* w) { if (!bad) { bad = 1; why = w; } }

	void run(const u32* code, u32 n)
	{
		for (u32 pc = 0; pc < n && !bad; pc++)
		{
			const u32 x = code[pc], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			switch (op)
			{
			case 14: r[d] = (a ? r[a] : 0) + (u64)(s64)(s16)(x & 0xFFFF); break;
			case 21:
			{
				if (x & 1) { fault("rlwinm. (record form)"); return; }
				const u32 mb = (x >> 6) & 31, me = (x >> 1) & 31;
				const u64 lo = (u32)r[d], rot = rotl64((lo << 32) | lo, b);
				r[a] = rot & mask64(mb + 32, me + 32);
				break;
			}
			case 30:
			{
				if (x & 1) { fault("rld record form"); return; }
				const u32 xo = (x >> 2) & 7, sh = b | (((x >> 1) & 1) << 5);
				const u32 mb = ((x >> 6) & 31) | (((x >> 5) & 1) << 5);
				if (xo == 0)
					r[a] = rotl64(r[d], sh) & mask64(mb, 63);
				else if (xo == 3)
				{
					const u64 m = mask64(mb, 63 - sh);
					r[a] = (rotl64(r[d], sh) & m) | (r[a] & ~m);
				}
				else
				{
					fault("MD-form other than rldicl / rldimi");
					return;
				}
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
		const u32 xo = (x >> 1) & 0x3FF;
		if (x & 1) { fault("record form"); return; }
		switch (xo)
		{
		case 28:  r[a] = r[d] & r[b]; return;
		case 60:  r[a] = r[d] & ~r[b]; return;
		case 316: r[a] = r[d] ^ r[b]; return;
		case 444: r[a] = r[d] | r[b]; return;
		case 986: if (b) break; r[a] = (u64)(s64)(s32)(u32)r[d]; return;
		case 826:
		case 827:
		{
			const u32 sh = b | ((xo & 1) << 5);
			const u64 v = r[d];
			const bool ca = (s64)v < 0 && sh && (v & ((1ull << sh) - 1)) != 0;
			r[a] = (u64)((s64)v >> sh);
			xer = ca ? xer | XER_CA : xer & ~XER_CA;
			return;
		}
		}
		if ((x >> 10) & 1) { fault("OE form"); return; }
		const u32 xo9 = (x >> 1) & 0x1FF;
		const u64 ra = r[a], rb = r[b];
		switch (xo9)
		{
		case 266: r[d] = ra + rb; return;
		case 40:  r[d] = rb - ra; return;
		case 104: if (b) break; r[d] = 0 - ra; return;
		case 233: r[d] = ra * rb; return;
		case 235: r[d] = (rnd64() << 32) | (u32)((s64)(s32)ra * (s64)(s32)rb); return;
		case 457: r[d] = rb ? ra / rb : rnd64(); return;
		case 489:
			r[d] = (rb == 0 || (ra == 0x8000000000000000ull && rb == ~0ull)) ? rnd64() : (u64)((s64)ra / (s64)rb);
			return;
		case 459:
			r[d] = (rnd64() << 32) | ((u32)rb ? (u32)ra / (u32)rb : (u32)rnd64());
			return;
		case 491:
		{
			const s32 la = (s32)ra, lb = (s32)rb;
			r[d] = (rnd64() << 32) | ((lb == 0 || (la == INT32_MIN && lb == -1)) ? (u32)rnd64() : (u32)(la / lb));
			return;
		}
		}
		fault("unexpected X/XO-form");
	}
};

static void fresh(Cpu& c)
{
	c.bad = 0;
	c.why = "";
	for (u32 k = 0; k < 32; k++)
		c.r[k] = rnd64();
	c.cr = (u32)rnd64();
	c.xer = rnd64() & (XER_SO | XER_OV | XER_CA);
}

// Only the listed GPRs may differ, CR never, and of XER only CA (sradi sets it; no JIT op reads CA across ops).
static bool only_changed(const Cpu& a, const Cpu& b, u32 gprs)
{
	for (u32 k = 0; k < 32; k++)
		if (!((gprs >> k) & 1) && a.r[k] != b.r[k])
			return false;
	return a.cr == b.cr && (a.xer & ~XER_CA) == (b.xer & ~XER_CA);
}

// As GetIntReg: SH4 r0-r15 but r11 live in r14-r28 with junk above the value; r11 is lwz'd into scratch, clean.
static u32 ppc_pin(u32 sh4) { return sh4 == 11 ? 0 : 14 + sh4 - (sh4 > 11); }
static u64 junk_above(u32 v) { return (rnd64() & 0xFFFFFFFF00000000ull) | v; }

// One native op, its operands placed as ps3_driver.cpp does; false on a fault or a stray register change.
struct NativeOp
{
	const char* err;
	char detail[200];
};

// The driver's div32u/div32s: with rs3 (FIXED) sources pinned or in r3-r5, results in r6/r7, scratch r8-r10;
// without (OLD) binop_start's r3/r4 copies, results in r5/r6.
static bool native_div(bool is_unsigned, bool fixed, u32 s1, u32 s2, u32 s3, u32 v1, u32 v2, u32 v3, u32& quo,
                       u32& rem, NativeOp& e)
{
	Cpu c;
	fresh(c);
	u32 a, b, h = 0, q, r, may;
	if (fixed)
	{
		a = s1 == 11 ? 3 : ppc_pin(s1);
		b = s2 == 11 ? 4 : ppc_pin(s2);
		h = s3 == 11 ? 5 : ppc_pin(s3);
		c.r[a] = s1 == 11 ? v1 : junk_above(v1);
		c.r[b] = s2 == 11 ? v2 : junk_above(v2);
		c.r[h] = s3 == 11 ? v3 : junk_above(v3);
		q = 6;
		r = 7;
		may = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10);
	}
	else
	{
		a = 3;
		b = 4;
		c.r[3] = s1 == 11 ? v1 : junk_above(v1);
		c.r[4] = s2 == 11 ? v2 : junk_above(v2);
		q = 5;
		r = 6;
		may = (1u << 5) | (1u << 6);
	}
	const Cpu before = c;
	g_n = 0;
	if (fixed && is_unsigned)
		emit_div32u(q, r, a, b, h, 8, 9);
	else if (fixed)
		emit_div32s(q, r, a, b, h, 8, 9, 10);
	else if (is_unsigned)
		emit_div32u_old(q, r, a, b);
	else
		emit_div32s_old(q, r, a, b);
	c.run(g_code, g_n);
	quo = (u32)c.r[q];
	rem = (u32)c.r[r];
	e.err = c.bad ? c.why : !only_changed(before, c, may) ? "a register outside the form, CR or XER changed" : 0;
	return !e.err;
}

// The driver's div32p2: a and b pinned or in r3/r4, T lwz'd into r5, scratch r6-r9, d pinned (rd) or r10.
static bool native_div32p2(u32 s1, u32 s2, u32 sd, u32 va, u32 vb, u32 vT, u32& out, NativeOp& e)
{
	Cpu c;
	fresh(c);
	const u32 a = s1 == 11 ? 3 : ppc_pin(s1), b = s2 == 11 ? 4 : ppc_pin(s2), d = sd == 11 ? 10 : ppc_pin(sd);
	c.r[a] = s1 == 11 ? va : junk_above(va);
	c.r[b] = s2 == 11 ? vb : junk_above(vb);
	c.r[5] = vT;
	const Cpu before = c;
	g_n = 0;
	emit_div32p2(d, a, b, 5, 6, 7, 8, 9);
	c.run(g_code, g_n);
	out = (u32)c.r[d];
	const u32 may = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << d);
	e.err = c.bad ? c.why : !only_changed(before, c, may) ? "a register outside the form, CR or XER changed" : 0;
	return !e.err;
}

// ---- The reference: the SH4 interpreter (dc/sh4/sh4_cpu_misc.h div0u, div0s, rotcl, div1) ----

struct Sr { u32 Q, M, T; };

static void ref_div1(Sr& sr, u32& rn, u32 rm)
{
	const u32 old_q = sr.Q;
	u32 q = (rn >> 31) & 1;
	const u32 old_rm = rm;
	rn = (rn << 1) | sr.T;
	const u32 old_rn = rn;
	if (old_q == sr.M)
	{
		rn -= old_rm;
		const u32 borrow = (rn > old_rn) ? 1 : 0;
		q = sr.M ? (!q ^ borrow) : (q ^ borrow);
	}
	else
	{
		rn += old_rm;
		const u32 carry = (rn < old_rn) ? 1 : 0;
		q = sr.M ? (!q ^ carry) : (q ^ carry);
	}
	sr.Q = q & 1;
	sr.T = (sr.Q == sr.M) ? 1 : 0;
}

// div0u, or div0s r2,r3; then 32 x (rotcl r1; div1 r2,r3).
static void ref_chain(bool is_unsigned, u32& r1, u32 r2, u32& r3, Sr& sr)
{
	if (is_unsigned)
		sr.Q = sr.M = sr.T = 0;
	else
	{
		sr.Q = r3 >> 31;
		sr.M = r2 >> 31;
		sr.T = sr.Q ^ sr.M;
	}
	for (int i = 0; i < 32; i++)
	{
		const u32 t = sr.T;
		sr.T = r1 >> 31;
		r1 = (r1 << 1) | t;
		ref_div1(sr, r3, r2);
	}
}

// ---- The fast form: div32_emit's SHIL, run with the canonical C ops or with the PPU forms ----

struct Regs { u32 s1, s2, s3; };	// SH4 registers of r1, r2, r3; at most one is r11 (unpinned)

static Regs pick_regs()
{
	Regs g;
	u32 used = 0;
	u32* slot[3] = { &g.s1, &g.s2, &g.s3 };
	for (int k = 0; k < 3; k++)
	{
		u32 v;
		do
			v = (u32)(rnd64() % 16);
		while ((used >> v) & 1);
		used |= 1u << v;
		*slot[k] = v;
	}
	return g;
}

static u32 in(const u32* v, const shil_param& p) { return p.is_imm() ? p._imm : v[p._reg]; }

// v holds SH4 values by Sh4RegType; native runs the div32 ops on the model. False on a model fault.
static bool run_fast(bool is_unsigned, bool fixed, const Regs& g, u32* v, bool native, NativeOp& e)
{
	DecodedBlock b;
	b.oplist.clear();
	div32_emit(b, is_unsigned, fixed, (Sh4RegType)g.s1, (Sh4RegType)g.s2, (Sh4RegType)g.s3);
	for (const shil_opcode& o : b.oplist)
	{
		switch (o.op)
		{
		case shop_and: v[o.rd._reg] = in(v, o.rs1) & in(v, o.rs2); break;
		case shop_or:  v[o.rd._reg] = in(v, o.rs1) | in(v, o.rs2); break;
		case shop_xor: v[o.rd._reg] = in(v, o.rs1) ^ in(v, o.rs2); break;
		case shop_shr: v[o.rd._reg] = in(v, o.rs1) >> (in(v, o.rs2) & 31); break;
		case shop_sar: v[o.rd._reg] = (u32)((s32)in(v, o.rs1) >> (in(v, o.rs2) & 31)); break;
		case shop_div32u:
		case shop_div32s:
		{
			const bool u = o.op == shop_div32u, wide = o.rs3.is_reg();
			u32 quo, rem;
			if (!native)
			{
				if (!wide)
				{
					e.err = "OLD ops have no canonical form";
					return false;
				}
				(u ? canon_div32u : canon_div32s)(v[o.rs1._reg], v[o.rs2._reg], v[o.rs3._reg], quo, rem);
			}
			else if (!native_div(u, wide, o.rs1._reg, o.rs2._reg, wide ? o.rs3._reg : 0, v[o.rs1._reg],
			                     v[o.rs2._reg], wide ? v[o.rs3._reg] : 0, quo, rem, e))
				return false;
			v[o.rd._reg] = quo;
			v[o.rd2._reg] = rem;
			break;
		}
		case shop_div32p2:
		{
			u32 res;
			if (!native)
				res = canon_div32p2(v[o.rs1._reg], v[o.rs2._reg], v[o.rs3._reg]);
			else if (!native_div32p2(o.rs1._reg, o.rs2._reg, o.rd._reg, v[o.rs1._reg], v[o.rs2._reg],
			                         v[o.rs3._reg], res, e))
				return false;
			v[o.rd._reg] = res;
			break;
		}
		default:
			e.err = "unexpected SHIL op";
			return false;
		}
	}
	return true;
}

// ---- Input rows ----

static u32 rnd_val()
{
	static const u32 edge[] = { 0, 1, 2, 3, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu,
	                            0xFFFFFFFFu, 0x0000FFFFu, 0x00010000u, 0x7FFF0000u, 0xFFFF0001u };
	switch (rnd64() % 4)
	{
	case 0: return edge[rnd64() % (sizeof(edge) / sizeof(edge[0]))];
	case 1: return rnd32() >> (rnd64() % 32);
	default: return rnd32();
	}
}

// A signed value of 1-32 random bits, either sign.
static s32 rnd_s32()
{
	const u32 bits = 1 + (u32)(rnd64() % 32);
	s32 v = (s32)(bits == 32 ? rnd32() : rnd32() >> (32 - bits));
	return (rnd64() & 1) ? (s32)(0u - (u32)v) : v;
}

struct Case { u32 r1, r2, r3; };

enum Row { ROW_U32, ROW_U64, ROW_S_IDIOM, ROW_S_POS, ROW_S64, ROW_COUNT };
static const char* const k_row[] = {
	"unsigned 32/32 (r3 = 0)", "unsigned 64/32 (r3 < r2)", "signed, SH manual idiom",
	"signed, dividend >= 0, divisor > 0", "signed 64/32, quotient fits s32" };

static Case gen(Row row)
{
	Case k;
	switch (row)
	{
	case ROW_U32:
		k.r1 = rnd_val();
		k.r2 = rnd_val();
		if (!k.r2)
			k.r2 = 1;
		k.r3 = 0;
		break;
	case ROW_U64:
		k.r2 = rnd_val();
		if (!k.r2)
			k.r2 = 1;
		k.r3 = (rnd64() % 4 == 0) ? k.r2 - 1 : (u32)(rnd64() % k.r2);
		k.r1 = rnd_val();
		break;
	case ROW_S_IDIOM:
	{
		// SH manual: the dividend sign-extended into r3, minus 1 when negative (one's complement); not INT_MIN / -1.
		s32 dvd, dvs;
		do
		{
			dvd = (rnd64() % 8 == 0) ? (s32)rnd_val() : rnd_s32();
			dvs = (rnd64() % 8 == 0) ? (s32)rnd_val() : rnd_s32();
		} while (dvs == 0 || (dvd == INT32_MIN && dvs == -1));
		k.r3 = dvd < 0 ? 0xFFFFFFFFu : 0;
		k.r1 = (u32)dvd - (dvd < 0 ? 1u : 0u);
		k.r2 = (u32)dvs;
		break;
	}
	case ROW_S_POS:
		k.r1 = rnd32() >> (1 + rnd64() % 31);
		do
			k.r2 = rnd32() >> (1 + rnd64() % 31);
		while (!k.r2);
		k.r3 = 0;
		break;
	default:
	{
		// A truncating quotient that fits, a remainder smaller than the divisor with the dividend's sign.
		s64 x;
		s32 dvs;
		do
		{
			const s32 q = (rnd64() % 8 == 0) ? (s32)rnd_val() : rnd_s32();
			dvs = rnd_s32();
			if (!dvs)
				continue;
			const u64 mag = (u64)(dvs < 0 ? -(s64)dvs : dvs);
			const s64 prod = (s64)q * dvs, rr = (s64)(rnd64() % mag);
			x = prod < 0 || (prod == 0 && (rnd64() & 1)) ? prod - rr : prod + rr;
			break;
		} while (true);
		if (x < 0)
			x--;
		k.r1 = (u32)(u64)x;
		k.r3 = (u32)((u64)x >> 32);
		k.r2 = (u32)dvs;
		break;
	}
	}
	return k;
}

static bool row_signed(Row row) { return row >= ROW_S_IDIOM; }

// Flycast's tests/src/div32_test.cpp cases (n1, n2, n3 = r1, r2, r3).
static const s32 k_fly_s[][3] = {
	{ 0, 1, 0 }, { 1, 1, 0 }, { 2, 1, 0 }, { 4, 2, 0 }, { 5, 2, 0 }, { 1000, 100, 0 }, { 1001, 100, 0 },
	{ 1099, 100, 0 }, { 1100, 100, 0 }, { 37, 5, 0 }, { -37, 5, -1 }, { -37, -5, -1 }, { 37, -5, 0 },
	{ 42, 5, 0 }, { 42, -5, 0 }, { -42, 5, -1 }, { -42, -5, -1 }, { 5, 7, 0 }, { 5, -7, 0 }, { -5, 7, -1 },
	{ -5, -7, -1 }, { -1846, -1643, -1 }, { -496138, -1042, -1 }, { -416263, -1037, -1 },
	{ -270831, -13276, -1 }, { -3338802, -7266, -1 }, { -3106, -354865, -1 }, { -4446, -4095, -1 },
	{ -64, -8, -1 }, { -72, -8, -1 }, { 217781009, -45, 0 }, { 1858552, -8, 0 }, { 64, -8, 0 }, { -64, 8, -1 },
	{ -9415081, 130765, -1 }, { -3639715, 78, -1 }, { -11361399, 107183, -1 } };
static const u32 k_fly_u[][3] = {
	{ 0, 1, 0 }, { 1, 1, 0 }, { 2, 1, 0 }, { 4, 2, 0 }, { 5, 2, 0 }, { 1000, 100, 0 }, { 1001, 100, 0 },
	{ 1099, 100, 0 }, { 1100, 100, 0 }, { 1964671145, 123383161, 0 }, { 1867228769, 653467280, 0 },
	{ 1523687288, 32181601, 0 }, { 3499805483u, 1401792939, 29611 }, { 0x80000000u, 0x80000000u, 0 } };

// Signed boundaries, through the SH manual idiom (dividend, divisor); INT_MIN / 1 is where sar was wrong.
static const s32 k_edge_s[][2] = {
	{ INT32_MIN, 1 }, { INT32_MIN, 2 }, { INT32_MIN, -2 }, { INT32_MIN, INT32_MIN }, { INT32_MIN, INT32_MAX },
	{ INT32_MAX, -1 }, { INT32_MAX, 1 }, { INT32_MAX, INT32_MIN }, { -1, 1 }, { -1, -1 }, { 1, -1 }, { 0, -1 },
	{ 0, INT32_MIN }, { -1, INT32_MIN }, { INT32_MIN + 1, -1 } };

// One case through the chain and a fast form: 0 = same r1, r3 and T, 1 = differ, 2 = a model fault.
static int compare(bool is_unsigned, const Case& k, bool fixed, bool native, char* d, size_t dl)
{
	u32 c1 = k.r1, c3 = k.r3;
	Sr sr;
	ref_chain(is_unsigned, c1, k.r2, c3, sr);

	const Regs g = pick_regs();
	u32 v[sh4_reg_count];
	for (u32 i = 0; i < (u32)sh4_reg_count; i++)
		v[i] = rnd32();
	v[g.s1] = k.r1;
	v[g.s2] = k.r2;
	v[g.s3] = k.r3;
	v[reg_sr_T] = rnd32() & 1;
	NativeOp e = { 0, "" };
	if (!run_fast(is_unsigned, fixed, g, v, native, e))
	{
		snprintf(d, dl, "%s", e.err);
		return 2;
	}
	snprintf(d, dl, "%s r1=%08X r2=%08X r3=%08X (regs r%u r%u r%u): got r1=%08X r3=%08X T=%u, chain r1=%08X "
	         "r3=%08X T=%u", is_unsigned ? "div0u" : "div0s", k.r1, k.r2, k.r3, g.s1, g.s2, g.s3, v[g.s1], v[g.s3],
	         v[reg_sr_T], c1, c3, sr.T);
	return v[g.s1] == c1 && v[g.s3] == c3 && v[reg_sr_T] == sr.T ? 0 : 1;
}

static void test_rows(u32 iters)
{
	for (int row = 0; row < ROW_COUNT; row++)
		for (int native = 0; native < 2; native++)
		{
			u32 wrong = 0;
			for (u32 t = 0; t < iters; t++)
			{
				const Case k = gen((Row)row);
				char d[300];
				const int r = compare(!row_signed((Row)row), k, true, native, d, sizeof(d));
				wrong += r != 0;
				char what[96];
				snprintf(what, sizeof(what), "FIXED %s, %s", native ? "native" : "canonical", k_row[row]);
				check(r == 0, what, d);
			}
			printf("  FIXED %-9s %-36s wrong %u of %u\n", native ? "native" : "canonical", k_row[row], wrong, iters);
		}
	for (int native = 0; native < 2; native++)
	{
		for (const auto& f : k_fly_s)
		{
			char d[300];
			const Case k = { (u32)f[0], (u32)f[1], (u32)f[2] };
			check(compare(false, k, true, native, d, sizeof(d)) == 0, "FIXED, Flycast div32s case", d);
		}
		for (const auto& f : k_fly_u)
		{
			char d[300];
			const Case k = { f[0], f[1], f[2] };
			check(compare(true, k, true, native, d, sizeof(d)) == 0, "FIXED, Flycast div32u case", d);
		}
		for (const auto& f : k_edge_s)
		{
			char d[300];
			const Case k = { (u32)f[0] - (f[0] < 0 ? 1u : 0u), (u32)f[1], f[0] < 0 ? 0xFFFFFFFFu : 0 };
			check(compare(false, k, true, native, d, sizeof(d)) == 0, "FIXED, signed boundary case", d);
		}
	}
}

// Report only: inputs where the SH4 manual leaves the result unchecked (overflow, a divisor of 0).
static void report_outside(u32 iters)
{
	static const char* const name[] = { "unsigned, r3 >= r2 (quotient overflows)", "unsigned, divisor 0",
	                                    "signed, divisor 0", "signed, any r1 and r3" };
	printf("FIXED native outside the checked rows, wrong r1, r3 or T against the chain:\n");
	for (int row = 0; row < 4; row++)
	{
		u32 wrong = 0;
		for (u32 t = 0; t < iters; t++)
		{
			Case k = { rnd_val(), rnd_val(), rnd_val() };
			if (row == 0)
			{
				if (!k.r2)
					k.r2 = 1;
				k.r3 = k.r2 + (u32)(rnd64() % ((u64)0x100000000ull - k.r2));
			}
			else if (row <= 2)
				k.r2 = 0;
			char d[300];
			wrong += compare(row <= 1, k, true, true, d, sizeof(d)) != 0;
		}
		printf("  %-40s %5.1f%%\n", name[row], 100.0 * wrong / iters);
	}
	char d[300];
	const Case k = { 0x7FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
	printf("  %-40s %s\n", "signed idiom, INT_MIN / -1", compare(false, k, true, true, d, sizeof(d)) ? "wrong" : "right");
}

// Report only: the OLD forms on the same rows.
static void report_old(u32 iters)
{
	printf("OLD forms (before 5.38), wrong r1, r3 or T against the chain:\n");
	for (int row = 0; row < ROW_COUNT; row++)
	{
		u32 wrong = 0, faults = 0;
		for (u32 t = 0; t < iters; t++)
		{
			const Case k = gen((Row)row);
			char d[300];
			const int r = compare(!row_signed((Row)row), k, false, true, d, sizeof(d));
			wrong += r == 1;
			faults += r == 2;
		}
		printf("  %-36s %5.1f%%%s\n", k_row[row], 100.0 * wrong / iters, faults ? " (model faults)" : "");
	}
}

// Every input, quotients that overflow and divisors of 0 too: the PPU forms against the real canonical ops.
static void test_native_vs_canonical(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const bool u = t & 1;
		const Regs g = pick_regs();
		const u32 v1 = rnd_val(), v2 = (rnd64() % 8 == 0) ? 0 : rnd_val(), v3 = rnd_val();
		u32 nq, nr, cq, cr;
		NativeOp e = { 0, "" };
		const bool ok = native_div(u, true, g.s1, g.s2, g.s3, v1, v2, v3, nq, nr, e);
		(u ? canon_div32u : canon_div32s)(v1, v2, v3, cq, cr);
		char d[200];
		snprintf(d, sizeof(d), "%s r1=%08X r2=%08X r3=%08X: native %08X %08X, canonical %08X %08X%s%s",
		         u ? "div32u" : "div32s", v1, v2, v3, nq, nr, cq, cr, e.err ? ": " : "", e.err ? e.err : "");
		check(ok && nq == cq && nr == cr, "native vs canonical", d);
	}
	static const u32 k_t[] = { 0, 1, 0x80000000u, 0x80000001u };
	for (u32 t = 0; t < iters; t++)
	{
		const Regs g = pick_regs();
		const u32 a = rnd_val(), b = rnd_val(), T = (rnd64() % 5) ? k_t[rnd64() % 4] : rnd32();
		const u32 sd = (rnd64() % 4) ? g.s1 : g.s3;	// rd = rs1 in the decoder's ops; also another register
		u32 nres;
		NativeOp e = { 0, "" };
		const bool ok = native_div32p2(g.s1, g.s2, sd, a, b, T, nres, e);
		const u32 cres = canon_div32p2(a, b, T);
		char d[200];
		snprintf(d, sizeof(d), "div32p2 a=%08X b=%08X T=%08X: native %08X, canonical %08X%s%s", a, b, T, nres, cres,
		         e.err ? ": " : "", e.err ? e.err : "");
		check(ok && nres == cres, "native vs canonical", d);
		// With T 0/1 (all OLD sequences) div32p2 is still the old a - (T ? 0 : b).
		if (T <= 1)
			check(cres == (T ? a : a - b), "div32p2 old semantics", d);
	}
}

static void test_words()
{
	g_n = 0;
	emit_div32u(6, 7, 14, 15, 16, 8, 9);
	emit_div32u(6, 7, 3, 4, 5, 8, 9);
	emit_div32s(6, 7, 14, 15, 16, 8, 9, 10);
	emit_div32s(6, 7, 3, 4, 5, 8, 9, 10);
	emit_div32p2(16, 16, 15, 5, 6, 7, 8, 9);
	emit_div32p2(10, 3, 4, 5, 6, 7, 8, 9);
	emit_div32u_old(5, 6, 3, 4);
	emit_div32s_old(5, 6, 3, 4);
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
	// "model" skips the words, so mutate.py can tell what the model alone catches.
	if (argc <= 2 || strcmp(argv[2], "model") != 0)
		test_words();
	test_native_vs_canonical(iters);
	test_rows(iters);
	report_old(iters);
	report_outside(iters);
	printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
