// OPTIMIZATION_PLAN 5.13 A1b: emits one SPU function per DSP program, a drop-in for AicaModel::dsp_step().
// The step body follows dsp_step() in plugs/caiqueAICA/aica_model.cpp in the same order.
#include <stddef.h>
#include "spe_dsp_jit.h"

// SPU instruction formats; every opcode below was checked against spu-as output.
#define RR(op, t, a, b) (((uint32_t)(op) << 21) | ((uint32_t)(b) << 14) | ((uint32_t)(a) << 7) | (uint32_t)(t))
#define RRR(op, t, a, b, c) \
	(((uint32_t)(op) << 28) | ((uint32_t)(t) << 21) | ((uint32_t)(b) << 14) | ((uint32_t)(a) << 7) | (uint32_t)(c))
#define RI7(op, t, a, i) (((uint32_t)(op) << 21) | (((uint32_t)(i) & 0x7F) << 14) | ((uint32_t)(a) << 7) | (uint32_t)(t))
#define RI10(op, t, a, i) (((uint32_t)(op) << 24) | (((uint32_t)(i) & 0x3FF) << 14) | ((uint32_t)(a) << 7) | (uint32_t)(t))
#define RI16(op, t, i) (((uint32_t)(op) << 23) | (((uint32_t)(i) & 0xFFFF) << 7) | (uint32_t)(t))
#define RI18(op, t, i) (((uint32_t)(op) << 25) | (((uint32_t)(i) & 0x3FFFF) << 7) | (uint32_t)(t))

// Long-lived values sit in callee-saved registers, so the C helper calls keep them.
enum
{
	R_ACC = 80, R_FRC, R_Y, R_ADRS, R_MEMVAL, R_PEND, R_S, R_IN,
	R_M4, R_MDEC, R_MDEC1, R_RBL, R_TEMPB, R_MIXB, R_RING, R_FFF,
	R_MIN, R_MAX, R_LDSW, R_STSW, R_B22, R_8000, R_11, R_12,
	R_SINK, R_WM0, R_WM1, R_WM2, R_WM3,
	R_END
};
#define NSAVE (R_END - 80)
#define FRAME (32 + 16 * NSAVE)
#define T0 5   // first temporary; r3/r4 carry the helper arguments

typedef struct
{
	uint32_t *base, *p, *end;
	int full, err, tmp;
} Em;

static void put(Em* e, uint32_t w)
{
	if (e->p < e->end)
		*e->p++ = w;
	else
		e->full = 1;
}
static void chk(Em* e, int ok)
{
	if (!ok)
		e->err = 1;
}
static int tmp(Em* e)
{
	chk(e, e->tmp < 80);
	return e->tmp < 80 ? e->tmp++ : 79;
}

static void a_(Em* e, int t, int a, int b) { put(e, RR(0x0C0, t, a, b)); }
static void sf_(Em* e, int t, int a, int b) { put(e, RR(0x040, t, a, b)); }  // t = b - a
static void and_(Em* e, int t, int a, int b) { put(e, RR(0x0C1, t, a, b)); }
static void or_(Em* e, int t, int a, int b) { put(e, RR(0x041, t, a, b)); }
static void xor_(Em* e, int t, int a, int b) { put(e, RR(0x241, t, a, b)); }
static void andc_(Em* e, int t, int a, int b) { put(e, RR(0x2C1, t, a, b)); }  // a & ~b
static void eqv_(Em* e, int t, int a, int b) { put(e, RR(0x249, t, a, b)); }
static void ceq_(Em* e, int t, int a, int b) { put(e, RR(0x3C0, t, a, b)); }
static void cgt_(Em* e, int t, int a, int b) { put(e, RR(0x240, t, a, b)); }
static void clgt_(Em* e, int t, int a, int b) { put(e, RR(0x2C0, t, a, b)); }
static void shl_(Em* e, int t, int a, int b) { put(e, RR(0x05B, t, a, b)); }
static void rotm_(Em* e, int t, int a, int b) { put(e, RR(0x059, t, a, b)); }   // right by -b
static void rotma_(Em* e, int t, int a, int b) { put(e, RR(0x05A, t, a, b)); }  // right by -b, algebraic
static void rotqby_(Em* e, int t, int a, int b) { put(e, RR(0x1DC, t, a, b)); }
static void lqx_(Em* e, int t, int a, int b) { put(e, RR(0x1C4, t, a, b)); }
static void stqx_(Em* e, int t, int a, int b) { put(e, RR(0x144, t, a, b)); }
static void cwx_(Em* e, int t, int a, int b) { put(e, RR(0x1D6, t, a, b)); }
static void chx_(Em* e, int t, int a, int b) { put(e, RR(0x1D5, t, a, b)); }
static void mpy_(Em* e, int t, int a, int b) { put(e, RR(0x3C4, t, a, b)); }
static void clz_(Em* e, int t, int a) { put(e, RR(0x2A5, t, a, 0)); }
static void xshw_(Em* e, int t, int a) { put(e, RR(0x2AE, t, a, 0)); }
static void bi_(Em* e, int a) { put(e, RR(0x1A8, 0, a, 0)); }
static void selb_(Em* e, int t, int a, int b, int c) { put(e, RRR(0x8, t, a, b, c)); }  // c ? b : a, per bit
static void shufb_(Em* e, int t, int a, int b, int c) { put(e, RRR(0xB, t, a, b, c)); }
static void shli_(Em* e, int t, int a, int n)
{
	chk(e, n >= 0 && n < 64);
	put(e, RI7(0x07B, t, a, n));
}
static void rotmi_(Em* e, int t, int a, int n)  // logical right by n
{
	chk(e, n >= 0 && n <= 64);
	put(e, RI7(0x079, t, a, -n));
}
static void rotmai_(Em* e, int t, int a, int n)  // algebraic right by n
{
	chk(e, n >= 0 && n <= 64);
	put(e, RI7(0x07A, t, a, -n));
}
static void rotqbyi_(Em* e, int t, int a, int n) { put(e, RI7(0x1FC, t, a, n & 15)); }
static void cwd_(Em* e, int t, int off, int a)
{
	chk(e, off >= -64 && off < 64);
	put(e, RI7(0x1F6, t, a, off));
}
static void ri10(Em* e, uint32_t op, int t, int a, int i)
{
	chk(e, i >= -512 && i < 512);
	put(e, RI10(op, t, a, i));
}
static void ai_(Em* e, int t, int a, int i) { ri10(e, 0x1C, t, a, i); }
static void sfi_(Em* e, int t, int a, int i) { ri10(e, 0x0C, t, a, i); }  // t = i - a
static void andi_(Em* e, int t, int a, int i) { ri10(e, 0x14, t, a, i); }
static void ori_(Em* e, int t, int a, int i) { ri10(e, 0x04, t, a, i); }
static void cgti_(Em* e, int t, int a, int i) { ri10(e, 0x4C, t, a, i); }
static void lqd_(Em* e, int t, int off, int a)
{
	chk(e, !(off & 15) && off >= -8192 && off < 8192);
	put(e, RI10(0x34, t, a, off >> 4));
}
static void stqd_(Em* e, int t, int off, int a)
{
	chk(e, !(off & 15) && off >= -8192 && off < 8192);
	put(e, RI10(0x24, t, a, off >> 4));
}
static void il_(Em* e, int t, int v)
{
	chk(e, v >= -32768 && v < 32768);
	put(e, RI16(0x081, t, v));
}
static void ilhu_(Em* e, int t, uint32_t v) { put(e, RI16(0x082, t, v)); }
static void iohl_(Em* e, int t, uint32_t v) { put(e, RI16(0x0C1, t, v)); }
static void lqa_(Em* e, int t, uint32_t a)
{
	chk(e, !(a & 15) && a < 0x40000);
	put(e, RI16(0x061, t, a >> 2));
}
static void stqa_(Em* e, int t, uint32_t a)
{
	chk(e, !(a & 15) && a < 0x40000);
	put(e, RI16(0x041, t, a >> 2));
}
static void brasl_(Em* e, uint32_t a)
{
	chk(e, !(a & 3) && a < 0x40000);
	put(e, RI16(0x062, 0, a >> 2));
}
static void ila_(Em* e, int t, uint32_t v)
{
	chk(e, v < 0x40000);
	put(e, RI18(0x021, t, v));
}
static void ldc(Em* e, int t, uint32_t v)
{
	if ((int32_t)v >= -32768 && (int32_t)v < 32768)
		il_(e, t, (int32_t)v);
	else if (v < 0x40000)
		ila_(e, t, v);
	else
	{
		ilhu_(e, t, v >> 16);
		if (v & 0xFFFF)
			iohl_(e, t, v & 0xFFFF);
	}
}
// brnz forward: returns the slot to patch() once the target is known.
static uint32_t brnz_fwd(Em* e, int r)
{
	const uint32_t at = (uint32_t)(e->p - e->base);
	put(e, RI16(0x042, r, 0));
	return at;
}
static void patch(Em* e, uint32_t at)
{
	if (!e->full)
		e->base[at] |= (((uint32_t)(e->p - e->base) - at) & 0xFFFF) << 7;
}

// Word, byte and halfword at a constant LS address, into the preferred slot.
static void ldw(Em* e, int t, uint32_t a)
{
	lqa_(e, t, a & ~15u);
	if (a & 15)
		rotqbyi_(e, t, t, a & 15);
}
static void stw(Em* e, int v, uint32_t a)
{
	const int q = tmp(e);
	lqa_(e, q, a & ~15u);
	shufb_(e, q, v, q, R_WM0 + ((a >> 2) & 3));
	stqa_(e, q, a & ~15u);
}
static void ldb(Em* e, int t, uint32_t a)
{
	lqa_(e, t, a & ~15u);
	if (((a - 3) & 15) != 0)
		rotqbyi_(e, t, t, (a - 3) & 15);
	andi_(e, t, t, 0xFF);
}
static void ldh(Em* e, int t, uint32_t a)
{
	lqa_(e, t, a & ~15u);
	if (a & 15)
		rotqbyi_(e, t, t, a & 15);
	rotmi_(e, t, t, 16);
}

// TEMP[(i + MDEC_CT) & 0x7F]; TEMP is 16-aligned, so the low offset bits are the rotate count.
static int temp_off(Em* e, unsigned i)
{
	const int t = tmp(e);
	ai_(e, t, R_M4, (int)(i * 4));
	andi_(e, t, t, 0x1FC);
	return t;
}
static int temp_rd(Em* e, unsigned i)
{
	const int t = temp_off(e, i), q = tmp(e);
	lqx_(e, q, R_TEMPB, t);
	rotqby_(e, q, q, t);
	return q;
}
static void temp_wr(Em* e, unsigned i, int v)
{
	const int t = temp_off(e, i), q = tmp(e), m = tmp(e);
	lqx_(e, q, R_TEMPB, t);
	cwx_(e, m, R_TEMPB, t);
	shufb_(e, q, v, q, m);
	stqx_(e, q, R_TEMPB, t);
}

// The ring holds DC byte order: byte o is the low byte of the 16-bit word.
static int ring_ld(Em* e, int o)
{
	const int q = tmp(e);
	lqx_(e, q, R_RING, o);
	rotqby_(e, q, q, o);
	shufb_(e, q, q, q, R_LDSW);
	return q;
}
static void ring_st(Em* e, int o, int v)
{
	const int s = tmp(e), q = tmp(e), m = tmp(e);
	shufb_(e, s, v, v, R_STSW);
	lqx_(e, q, R_RING, o);
	chx_(e, m, R_RING, o);
	shufb_(e, q, s, q, m);
	stqx_(e, q, R_RING, o);
}

// coll_claim[ck] as a mask (bool is one byte)
static int coll_mask(Em* e, const DspJitLayout* L, unsigned ck)
{
	const int c = tmp(e);
	ldb(e, c, L->coll_claim + ck);
	cgti_(e, c, c, 0);
	return c;
}

// dsp_unpack(f) of dsp_float.h
static int unpack(Em* e, int f)
{
	const int t1 = tmp(e), x = tmp(e), big = tmp(e), x2 = tmp(e), m = tmp(e), t2 = tmp(e), sg = tmp(e), b22 = tmp(e),
	          v = tmp(e);
	shli_(e, t1, f, 17);
	rotmi_(e, x, t1, 28);           // exponent
	cgti_(e, big, x, 11);
	selb_(e, x2, x, R_11, big);     // exponents above 11 act as 11
	sfi_(e, x2, x2, 0);             // rotma shifts right by minus the count
	shli_(e, m, f, 21);
	rotmi_(e, m, m, 10);            // mantissa << 11
	shli_(e, t2, f, 16);
	rotmai_(e, sg, t2, 31);         // sign word
	eqv_(e, b22, sg, big);          // bit 22: the sign above 11, else its inverse
	selb_(e, v, m, sg, R_MIN);      // bits 31..23 = sign (the 24-bit sign extension)
	selb_(e, v, v, b22, R_B22);
	rotma_(e, v, v, x2);
	return v;
}

// dsp_pack(S) of dsp_float.h: exponent = min(clz of (S ^ S << 1) in 24 bits, 12)
static int pack(Em* e, int S)
{
	const int t1 = tmp(e), t2 = tmp(e), u = tmp(e), x = tmp(e), b = tmp(e), sh = tmp(e), b2 = tmp(e), mm = tmp(e),
	          xe = tmp(e), r = tmp(e), sg = tmp(e), v = tmp(e);
	shli_(e, t1, S, 1);
	xor_(e, t2, S, t1);
	shli_(e, u, t2, 8);
	clz_(e, x, u);
	cgti_(e, b, x, 12);
	selb_(e, x, x, R_12, b);
	cgti_(e, b2, x, 11);
	selb_(e, sh, x, R_11, b2);      // exponent 12 keeps S << 11, like exponent 11
	ai_(e, sh, sh, 10);
	shl_(e, mm, S, sh);
	rotmi_(e, mm, mm, 21);          // mantissa = bits 21..11 of S << min(exponent, 11)
	shli_(e, xe, x, 11);
	or_(e, r, mm, xe);
	rotmai_(e, sg, S, 8);
	selb_(e, v, r, sg, R_8000);     // bit 15 = bit 23 of S
	return v;
}

// The interpreter's ADDR before the RBP add: MADRS (+ADRS) (+1), then ring or table mask.
static int mem_addr(Em* e, const DspJitLayout* L, const dsp_op_t* m)
{
	const int a = tmp(e);
	lqa_(e, a, L->madrs + 16 * m->MASA);
	if (m->ADREB)
		a_(e, a, a, R_ADRS);
	if (!m->TABLE)
	{
		a_(e, a, a, m->NXADR ? R_MDEC1 : R_MDEC);
		and_(e, a, a, R_RBL);
	}
	else
	{
		if (m->NXADR)
			ai_(e, a, a, 1);
		shli_(e, a, a, 16);
		rotmi_(e, a, a, 16);
	}
	return a;
}

typedef struct
{
	const DspJitLayout* L;
	const dsp_op_t* prog;
	int ring_in_ls;
	int8_t rd_src[128];   // the read issued at step s uses the fields of this step (s, or the even step before); -1 none
	uint8_t need_acc[128];
	DspJitInfo* info;
} Plan;

static int slow(const Plan* P, const dsp_op_t* m) { return m->TABLE || !P->ring_in_ls; }

static void emit_inputs(Em* e, const Plan* P, unsigned ira)
{
	if (ira <= 0x1F)
		ldw(e, R_IN, P->L->mems + 4 * ira);   // MEMS is always sign-extended 24-bit
	else if (ira <= 0x2F)
	{
		// MIXS_bank is 16-aligned; its values are 20-bit, so << 4 stays in 24 bits
		const unsigned off = 4 * (ira - 0x20);
		const int q = tmp(e);
		lqd_(e, q, (int)(off & ~15u), R_MIXB);
		if (off & 15)
			rotqbyi_(e, q, q, off & 15);
		shli_(e, R_IN, q, 4);
	}
	else if (ira <= 0x31)
	{
		const int q = tmp(e);
		ldw(e, q, P->L->exts + 4 * (ira - 0x30));
		shli_(e, q, q, 16);
		rotmai_(e, R_IN, q, 8);   // sext24(EXTS << 8)
	}
	else
		il_(e, R_IN, 0);
}

static void emit_iwt(Em* e, const Plan* P, unsigned s)
{
	const int nofl = s >= 2 ? (int)P->prog[s - 2].NOFL : -1;   // -1: from the last sample (nofl_pipe)
	int vn = -1, vu = -1, v;
	if (nofl != 0)
	{
		vn = tmp(e);
		shli_(e, vn, R_MEMVAL, 16);
		rotmai_(e, vn, vn, 8);
	}
	if (nofl != 1)
		vu = unpack(e, R_MEMVAL);
	if (nofl < 0)
	{
		const int m = tmp(e);
		v = tmp(e);
		ldw(e, m, P->L->ctx + (s == 0 ? offsetof(DspJitCtx, nofl0) : offsetof(DspJitCtx, nofl1)));
		selb_(e, v, vu, vn, m);
	}
	else
		v = nofl ? vn : vu;
	stw(e, v, P->L->mems + 4 * P->prog[s].IWA);
}

static void emit_shifted(Em* e, unsigned shift)
{
	if (shift <= 1)
	{
		int x = R_ACC;
		if (shift == 1)
		{
			x = tmp(e);
			a_(e, x, R_ACC, R_ACC);
		}
		const int m1 = tmp(e), t = tmp(e), m2 = tmp(e);
		cgt_(e, m1, x, R_MAX);
		selb_(e, t, x, R_MAX, m1);
		cgt_(e, m2, R_MIN, t);
		selb_(e, R_S, t, R_MIN, m2);
	}
	else
	{
		const int t = tmp(e);
		shli_(e, t, R_ACC, shift == 2 ? 9 : 8);
		rotmai_(e, R_S, t, 8);
	}
}

// ACC = sext26(floor(X * Y / 4096) + B), the product split as X = xh * 4096 + xl so both mpy fit 16 x 16
static void emit_acc(Em* e, const Plan* P, unsigned s)
{
	const dsp_op_t* in = &P->prog[s];
	int tr = -1, Y;
	if (!in->XSEL || (!in->ZERO && !in->BSEL))
		tr = temp_rd(e, in->TRA);
	const int X = in->XSEL ? R_IN : tr;
	switch (in->YSEL)
	{
	case 0:
		Y = R_FRC;
		break;
	case 1:
		Y = tmp(e);
		lqa_(e, Y, P->L->coef + 16 * s);
		break;
	case 2:
		Y = tmp(e);
		rotmai_(e, Y, R_Y, 11);
		break;
	default:
		Y = tmp(e);
		rotmai_(e, Y, R_Y, 4);
		and_(e, Y, Y, R_FFF);
		break;
	}
	const int xh = tmp(e), xl = tmp(e), p1 = tmp(e), p2 = tmp(e);
	rotmai_(e, xh, X, 12);
	and_(e, xl, X, R_FFF);
	mpy_(e, p1, xh, Y);
	mpy_(e, p2, xl, Y);
	rotmai_(e, p2, p2, 12);
	a_(e, p1, p1, p2);
	if (!in->ZERO)
	{
		const int B = in->BSEL ? R_ACC : tr;
		if (in->NEGB)
			sf_(e, p1, B, p1);
		else
			a_(e, p1, p1, B);
	}
	shli_(e, p1, p1, 6);
	rotmai_(e, R_ACC, p1, 6);
}

// Pass 0 (MWT) then pass 1 (the read), as dsp_step(). Only the collision bits are runtime data.
static void emit_mem(Em* e, const Plan* P, unsigned s)
{
	const DspJitLayout* L = P->L;
	const dsp_op_t* in = &P->prog[s];
	const int even = !(s & 1);
	if (in->MWT)
	{
		int v;
		if (in->NOFL)
		{
			v = tmp(e);
			rotmai_(e, v, R_S, 8);
		}
		else
			v = pack(e, R_S);
		const int a = mem_addr(e, L, in);
		if (slow(P, in))
		{
			uint32_t br = 0;
			if (even)
				br = brnz_fwd(e, coll_mask(e, L, ((s + 14) >> 1) & 63));
			ori_(e, 3, a, 0);
			ori_(e, 4, v, 0);
			brasl_(e, L->wr_fn);
			if (even)
				patch(e, br);
			P->info->slow++;
		}
		else
		{
			const int o = tmp(e);
			shli_(e, o, a, 1);
			if (even)   // a channel owns this slot: the store goes to the spare quadword
				selb_(e, o, o, R_SINK, coll_mask(e, L, ((s + 14) >> 1) & 63));
			ring_st(e, o, v);
		}
	}
	const int r = P->rd_src[s];
	if (r < 0)
		return;
	const dsp_op_t* m = &P->prog[r];
	const int deferred = r != (int)s;
	const unsigned ck = ((unsigned)(r + 14) >> 1) & 63;
	if (slow(P, m))
	{
		// A collision skips the memory read, as in dsp_step (a cache fetch could evict a written line)
		uint32_t br = 0;
		if (deferred)
		{
			const int cw = tmp(e);
			ldh(e, cw, L->coll_word + 2 * ck);
			ori_(e, R_PEND, cw, 0);
			br = brnz_fwd(e, coll_mask(e, L, ck));
		}
		ori_(e, 3, mem_addr(e, L, m), 0);
		brasl_(e, L->rd_fn);
		ori_(e, R_PEND, 3, 0);
		if (deferred)
			patch(e, br);
		P->info->slow++;
	}
	else
	{
		const int a = mem_addr(e, L, m), o = tmp(e);
		shli_(e, o, a, 1);
		const int v = ring_ld(e, o);
		if (deferred)
		{
			const int cw = tmp(e);
			ldh(e, cw, L->coll_word + 2 * ck);
			selb_(e, R_PEND, v, cw, coll_mask(e, L, ck));
		}
		else
			ori_(e, R_PEND, v, 0);
	}
}

static void emit_step(Em* e, const Plan* P, unsigned s)
{
	const DspJitLayout* L = P->L;
	const dsp_op_t* in = &P->prog[s];
	const int needs_s = in->TWT || in->FRCL || in->EWT || in->MWT || (in->ADRL && in->SHIFT == 3);
	const int need_in = (P->need_acc[s] && in->XSEL) || in->YRL || (in->ADRL && in->SHIFT != 3);
	e->tmp = T0;

	// A read lands two steps after it was issued; step 1 takes one from the last sample.
	if (s == 1)
	{
		const int m = tmp(e), v = tmp(e);
		lqa_(e, m, L->ctx + offsetof(DspJitCtx, pend_m));
		lqa_(e, v, L->ctx + offsetof(DspJitCtx, pend_v));
		selb_(e, R_MEMVAL, R_MEMVAL, v, m);
	}
	else if (s >= 3 && P->rd_src[s - 2] >= 0)
		ori_(e, R_MEMVAL, R_PEND, 0);

	if (need_in)
		emit_inputs(e, P, in->IRA);
	if (in->IWT)
		emit_iwt(e, P, s);
	if (needs_s)
		emit_shifted(e, in->SHIFT);
	if (P->need_acc[s])
		emit_acc(e, P, s);
	if (in->YRL)
		ori_(e, R_Y, R_IN, 0);
	if (in->TWT)
		temp_wr(e, in->TWA, R_S);
	if (in->FRCL)
	{
		if (in->SHIFT == 3)
			and_(e, R_FRC, R_S, R_FFF);
		else
			rotmai_(e, R_FRC, R_S, 11);
	}
	emit_mem(e, P, s);
	if (in->ADRL)
	{
		if (in->SHIFT == 3)
			rotmai_(e, R_ADRS, R_S, 12);
		else
			rotmai_(e, R_ADRS, R_IN, 16);
	}
	if (in->EWT)
	{
		const int v = tmp(e);
		rotmai_(e, v, R_S, 8);
		stw(e, v, L->efreg + 4 * in->EWA);
	}
}

int dspjit_compile(const DspJitLayout* L, const dsp_op_t* prog, uint32_t rbl, uint32_t ring_bytes,
                   uint32_t* buf, uint32_t cap, DspJitInfo* info)
{
	Plan P;
	Em E = {buf, buf, buf + cap, 0, 0, T0};
	Em* e = &E;
	int s, even_pend = -1;
	const uint32_t ctx = L->ctx;

	P.L = L;
	P.prog = prog;
	P.ring_in_ls = (rbl + 1) * 2 <= ring_bytes;
	P.info = info;
	info->words = info->live = info->slow = 0;
	info->has_slow = 0;
	// dsp_step's read selection: odd MRD, or the even MRD of the step before when the odd step has no MRD
	for (s = 0; s < 128; s++)
	{
		P.rd_src[s] = -1;
		if (s & 1)
		{
			if (prog[s].MRD && !prog[s].MWT)
				P.rd_src[s] = (int8_t)s;
			else if (!prog[s].MRD && even_pend >= 0)
				P.rd_src[s] = (int8_t)even_pend;
			even_pend = -1;
		}
		else if (prog[s].MRD && !prog[s].MWT)
			even_pend = s;
	}
	// ACC of step s is read only by step s + 1 (SHIFTED or B); step 127's is the next sample's input
	for (s = 0; s < 128; s++)
	{
		const dsp_op_t* n = &prog[s == 127 ? 0 : s + 1];
		const int uses = n->TWT || n->FRCL || n->EWT || n->MWT || (n->ADRL && n->SHIFT == 3) || (!n->ZERO && n->BSEL);
		P.need_acc[s] = s == 127 || uses;
	}
	for (s = 0; s < 128; s++)
		if ((prog[s].MWT || P.rd_src[s] >= 0) &&
		    ((prog[s].MWT && slow(&P, &prog[s])) || (P.rd_src[s] >= 0 && slow(&P, &prog[P.rd_src[s]]))))
			info->has_slow = 1;

	// Prologue: LR into the caller's frame, callee-saved registers, back chain (SPU ABI, as spu-gcc emits)
	stqd_(e, 0, 16, 1);
	for (s = 0; s < NSAVE; s++)
		stqd_(e, 80 + s, -16 * (s + 1), 1);
	stqd_(e, 1, -FRAME, 1);
	ai_(e, 1, 1, -FRAME);
	lqa_(e, R_ACC, ctx + offsetof(DspJitCtx, acc));
	lqa_(e, R_FRC, ctx + offsetof(DspJitCtx, frc));
	lqa_(e, R_Y, ctx + offsetof(DspJitCtx, y));
	lqa_(e, R_ADRS, ctx + offsetof(DspJitCtx, adrs));
	lqa_(e, R_MEMVAL, ctx + offsetof(DspJitCtx, memval));
	lqa_(e, R_MDEC, ctx + offsetof(DspJitCtx, mdec));
	lqa_(e, R_M4, ctx + offsetof(DspJitCtx, m4));
	lqa_(e, R_MIXB, ctx + offsetof(DspJitCtx, mixb));
	ai_(e, R_MDEC1, R_MDEC, 1);
	ldc(e, R_RBL, rbl);
	ldc(e, R_TEMPB, L->temp);
	ldc(e, R_RING, L->ring);
	il_(e, R_FFF, 0xFFF);
	ilhu_(e, R_MIN, 0xFF80);          // -0x800000, also the unpack sign mask
	ldc(e, R_MAX, 0x7FFFFF);
	ldc(e, R_LDSW, 0x80800100);       // word = (byte 1 << 8) | byte 0
	ldc(e, R_STSW, 0x00010302);       // bytes 2, 3 = low, high: DC order for chx
	ilhu_(e, R_B22, 0x0040);
	ila_(e, R_8000, 0x8000);
	il_(e, R_11, 11);
	il_(e, R_12, 12);
	ldc(e, R_SINK, ring_bytes);
	cwd_(e, R_WM0, 0, 1);             // word insert masks; the stack pointer is 16-aligned
	cwd_(e, R_WM1, 4, 1);
	cwd_(e, R_WM2, 8, 1);
	cwd_(e, R_WM3, 12, 1);

	for (s = 0; s < 128; s++)
	{
		uint32_t* const p0 = e->p;
		emit_step(e, &P, (unsigned)s);
		if (e->p != p0)
			info->live++;
	}

	stqa_(e, R_ACC, ctx + offsetof(DspJitCtx, acc));
	stqa_(e, R_FRC, ctx + offsetof(DspJitCtx, frc));
	stqa_(e, R_Y, ctx + offsetof(DspJitCtx, y));
	stqa_(e, R_ADRS, ctx + offsetof(DspJitCtx, adrs));
	stqa_(e, R_MEMVAL, ctx + offsetof(DspJitCtx, memval));
	info->pend_out = P.rd_src[127] >= 0;
	if (info->pend_out)
		stqa_(e, R_PEND, ctx + offsetof(DspJitCtx, pend_out));
	ai_(e, 1, 1, FRAME);
	lqd_(e, 0, 16, 1);
	for (s = 0; s < NSAVE; s++)
		lqd_(e, 80 + s, -16 * (s + 1), 1);
	bi_(e, 0);

	info->nofl126 = (uint8_t)prog[126].NOFL;
	info->nofl127 = (uint8_t)prog[127].NOFL;
	info->words = (uint32_t)(e->p - buf);
	if (e->err)
		return -2;
	return e->full ? -1 : 0;
}

const int32_t dspjit_form_imm[DJF_COUNT][2] = {
	{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
	{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
	{0, 0}, {0, 0},
	{0, 63}, {0, 63}, {0, 63}, {0, 15}, {-64, 63},
	{-512, 511}, {-512, 511}, {-512, 511}, {-512, 511}, {-512, 511},
	{-32768, 32767}, {0, 65535}, {0, 65535}, {0, 0x3FFFF},
};

int dspjit_probe(uint32_t* buf, uint32_t cap, int form, uint32_t in_ls, uint32_t out_ls, int imm)
{
	Em E = {buf, buf, buf + cap, 0, 0, T0};
	Em* e = &E;
	const int t = 13, a = 10, b = 11, c = 12;
	lqa_(e, a, in_ls);
	lqa_(e, b, in_ls + 16);
	lqa_(e, c, in_ls + 32);
	switch (form)
	{
	case DJF_A: a_(e, t, a, b); break;
	case DJF_SF: sf_(e, t, a, b); break;
	case DJF_AND: and_(e, t, a, b); break;
	case DJF_OR: or_(e, t, a, b); break;
	case DJF_XOR: xor_(e, t, a, b); break;
	case DJF_ANDC: andc_(e, t, a, b); break;
	case DJF_EQV: eqv_(e, t, a, b); break;
	case DJF_CEQ: ceq_(e, t, a, b); break;
	case DJF_CGT: cgt_(e, t, a, b); break;
	case DJF_CLGT: clgt_(e, t, a, b); break;
	case DJF_SHL: shl_(e, t, a, b); break;
	case DJF_ROTM: rotm_(e, t, a, b); break;
	case DJF_ROTMA: rotma_(e, t, a, b); break;
	case DJF_ROTQBY: rotqby_(e, t, a, b); break;
	case DJF_CWX: cwx_(e, t, a, b); break;
	case DJF_CHX: chx_(e, t, a, b); break;
	case DJF_MPY: mpy_(e, t, a, b); break;
	case DJF_CLZ: clz_(e, t, a); break;
	case DJF_XSHW: xshw_(e, t, a); break;
	case DJF_SELB: selb_(e, t, a, b, c); break;
	case DJF_SHUFB: shufb_(e, t, a, b, c); break;
	case DJF_SHLI: shli_(e, t, a, imm); break;
	case DJF_ROTMI: rotmi_(e, t, a, imm); break;
	case DJF_ROTMAI: rotmai_(e, t, a, imm); break;
	case DJF_ROTQBYI: rotqbyi_(e, t, a, imm); break;
	case DJF_CWD: cwd_(e, t, imm, a); break;
	case DJF_AI: ai_(e, t, a, imm); break;
	case DJF_SFI: sfi_(e, t, a, imm); break;
	case DJF_ANDI: andi_(e, t, a, imm); break;
	case DJF_ORI: ori_(e, t, a, imm); break;
	case DJF_CGTI: cgti_(e, t, a, imm); break;
	case DJF_IL: il_(e, t, imm); break;
	case DJF_ILHU: ilhu_(e, t, (uint32_t)imm); break;
	case DJF_IOHL:
		ori_(e, t, a, 0);
		iohl_(e, t, (uint32_t)imm);
		break;
	case DJF_ILA: ila_(e, t, (uint32_t)imm); break;
	default: e->err = 1; break;
	}
	stqa_(e, t, out_ls);
	bi_(e, 0);
	if (e->err || e->full)
		return -1;
	return (int)(e->p - buf);
}
