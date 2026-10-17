#pragma once
// Row JIT SCHEDULE + LIST: list scheduling of a block body for the in-order PPE, between anchors (branches, targets,
// cold-path joins, words it does not model), keeping every dependency. misc/sched_check tests it on a PPC64 model.

#define SCHED_MAX 96      // words a region; a longer run is cut
#define SCHED_BODY 8192   // words a body; a longer one is left as it is
#define SCHED_LHS 12      // a load after a store to the same bytes: kept this many cycles apart if it can be

// Resource bits. Word r: GPRs 0-31, FPRs 32-63. Word x: CR fields 0-7, CA, SO/OV, CTR, LR, the FPSCR mode.
enum { SX_CA = 8, SX_SO = 9, SX_CTR = 10, SX_LR = 11, SX_FPSCR = 12, SX_N = 13 };
enum { SU_FX, SU_LS, SU_FP };

struct SchedIns
{
	u32 w;
	u64 use_r, def_r;
	u32 use_x, def_x;
	u8 load, store;      // a memory access of `size` bytes at rA + disp, or at rA + rB when indexed
	u8 size, base, index, indexed;
	s32 disp;
	u8 lat, unit, busy;  // result latency; busy: cycles a non-pipelined op holds its unit
	bool barrier;        // not modelled: stays in place, nothing moves across it
};

// What a GPR holds as an address: unknown, an exact value, a RAM index (24 bits), a RAM pointer, a store-queue
// index (6 bits), or a range of 64 bytes.
enum { SP_UNK, SP_ABS, SP_RAMIDX, SP_RAM, SP_SQIDX, SP_RANGE };
struct SchedPtr { u8 k; s64 lo, hi; };

struct SchedEnv
{
	u32 ctx;              // r30's value
	u32 ram_hi;           // the addis immediate of the fastmem RAM base
	s64 ram_lo, ram_end;  // the host RAM block, its pad included
	u32 bias_hi;          // 8C PREFIX's addis immediate in this block (RAM base - prefix << 24), 0 = none
};

struct SchedStats { u32 regions, words, moved, cyc_before, cyc_after; };

static inline u64 sched_gpr(u32 n) { return 1ull << n; }
static inline u64 sched_fpr(u32 n) { return 1ull << (32 + n); }

// rA as a base: r0 there reads as 0.
static inline void sched_base(SchedIns& d, u32 ra)
{
	d.base = (u8)ra;
	if (ra)
		d.use_r |= sched_gpr(ra);
}

static inline void sched_rc(SchedIns& d, u32 w)
{
	if (w & 1)
	{
		d.def_x |= 1u;
		d.use_x |= 1u << SX_SO;
	}
}

static inline bool sched_bar(SchedIns& d)
{
	d.barrier = true;
	return false;
}

// FP A form: fdiv 18, fsub 20, fadd 21, fsqrt 22, fsel 23, fres 24 (single), frsqrte 26 (double), fmul 25, the four
// multiply-adds 28-31. Rc=1 copies FPSCR status into CR1, which is not modelled.
static bool sched_fpa(SchedIns& d, u32 w, bool single)
{
	const u32 xo5 = (w >> 1) & 0x1F, rD = (w >> 21) & 31, rA = (w >> 16) & 31, rB = (w >> 11) & 31, rC = (w >> 6) & 31;
	bool a = true, b = true, c = false;
	switch (xo5)
	{
	case 18: d.lat = 74; d.busy = 70; break;
	case 20: case 21: break;
	case 22: a = false; d.lat = 84; d.busy = 80; break;
	case 23: if (single) return sched_bar(d); c = true; break;
	case 24: if (!single) return sched_bar(d); a = false; break;
	case 25: b = false; c = true; break;
	case 26: if (single) return sched_bar(d); a = false; break;
	case 28: case 29: case 30: case 31: c = true; break;
	default: return sched_bar(d);
	}
	if (w & 1)
		return sched_bar(d);
	d.use_r = (a ? sched_fpr(rA) : 0) | (b ? sched_fpr(rB) : 0) | (c ? sched_fpr(rC) : 0);
	d.def_r = sched_fpr(rD);
	return true;
}

// Decodes one word: its reads, writes, memory access, latency and unit. False, with barrier set, for a word it does
// not model.
static bool sched_decode(u32 w, SchedIns& d)
{
	d = SchedIns();
	d.w = w;
	d.lat = 2;
	d.unit = SU_FX;
	const u32 op = w >> 26, rD = (w >> 21) & 31, rA = (w >> 16) & 31, rB = (w >> 11) & 31;
	const s32 simm = (s16)(w & 0xFFFF);
	switch (op)
	{
	case 7:   // mulli
		d.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD); d.lat = 4;
		return true;
	case 8:   // subfic
		d.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD); d.def_x = 1u << SX_CA;
		return true;
	case 10: case 11:   // cmpli, cmpi
		d.use_r = sched_gpr(rA); d.use_x = 1u << SX_SO; d.def_x = 1u << (rD >> 2);
		return true;
	case 12: case 13:   // addic, addic.
		d.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD); d.def_x = 1u << SX_CA;
		if (op == 13) { d.def_x |= 1u; d.use_x |= 1u << SX_SO; }
		return true;
	case 14: case 15:   // addi, addis
		if (rA) d.use_r = sched_gpr(rA);
		d.def_r = sched_gpr(rD);
		return true;
	case 19:
		switch ((w >> 1) & 0x3FF)
		{
		case 0:   // mcrf
			d.use_x = 1u << (rA >> 2); d.def_x = 1u << (rD >> 2);
			return true;
		case 33: case 129: case 193: case 225: case 257: case 289: case 417: case 449:
			// CR bit logic writes one bit of BT's field, so it reads that field too.
			d.use_x = (1u << (rA >> 2)) | (1u << (rB >> 2)) | (1u << (rD >> 2)); d.def_x = 1u << (rD >> 2);
			return true;
		}
		return sched_bar(d);
	case 20:  // rlwimi keeps rA's bits outside the mask
		d.use_r = sched_gpr(rD) | sched_gpr(rA); d.def_r = sched_gpr(rA); sched_rc(d, w);
		return true;
	case 21:  // rlwinm
		d.use_r = sched_gpr(rD); d.def_r = sched_gpr(rA); sched_rc(d, w);
		return true;
	case 23:  // rlwnm
		d.use_r = sched_gpr(rD) | sched_gpr(rB); d.def_r = sched_gpr(rA); d.lat = 4; sched_rc(d, w);
		return true;
	case 24: case 25: case 26: case 27:   // ori, oris, xori, xoris
		d.use_r = sched_gpr(rD); d.def_r = sched_gpr(rA);
		return true;
	case 28: case 29:   // andi., andis.
		d.use_r = sched_gpr(rD); d.def_r = sched_gpr(rA); d.def_x = 1u; d.use_x = 1u << SX_SO;
		return true;
	case 30:   // rldicl, rldicr, rldic, rldimi
		if (((w >> 2) & 7) > 3)
			return sched_bar(d);
		d.use_r = sched_gpr(rD); d.def_r = sched_gpr(rA);
		if (((w >> 2) & 7) == 3) d.use_r |= sched_gpr(rA);
		sched_rc(d, w);
		return true;
	case 32: case 34: case 40: case 42:   // lwz, lbz, lhz, lha
		d.load = 1; d.size = op == 32 ? 4 : op == 34 ? 1 : 2; d.disp = simm; sched_base(d, rA);
		d.def_r = sched_gpr(rD); d.lat = 5; d.unit = SU_LS;
		return true;
	case 36: case 38: case 44:   // stw, stb, sth
		d.store = 1; d.size = op == 36 ? 4 : op == 38 ? 1 : 2; d.disp = simm; sched_base(d, rA);
		d.use_r |= sched_gpr(rD); d.lat = 1; d.unit = SU_LS;
		return true;
	case 48: case 50:   // lfs, lfd
		d.load = 1; d.size = op == 48 ? 4 : 8; d.disp = simm; sched_base(d, rA);
		d.def_r = sched_fpr(rD); d.lat = 6; d.unit = SU_LS;
		return true;
	case 52: case 54:   // stfs, stfd
		d.store = 1; d.size = op == 52 ? 4 : 8; d.disp = simm; sched_base(d, rA);
		d.use_r |= sched_fpr(rD); d.lat = 1; d.unit = SU_LS;
		return true;
	case 58:   // ld, lwa; ldu stays a barrier
		if ((w & 3) & 1)
			return sched_bar(d);
		d.load = 1; d.size = (w & 3) == 2 ? 4 : 8; d.disp = (s16)(w & 0xFFFC); sched_base(d, rA);
		d.def_r = sched_gpr(rD); d.lat = 5; d.unit = SU_LS;
		return true;
	case 62:   // std; stdu stays a barrier
		if (w & 3)
			return sched_bar(d);
		d.store = 1; d.size = 8; d.disp = (s16)(w & 0xFFFC); sched_base(d, rA);
		d.use_r |= sched_gpr(rD); d.lat = 1; d.unit = SU_LS;
		return true;
	case 59: case 63:
		d.unit = SU_FP;
		d.lat = 10;
		d.use_x = 1u << SX_FPSCR;
		if (op == 59 || ((w >> 1) & 0x1F) >= 18)
			return sched_fpa(d, w, op == 59);
		if (w & 1)
			return sched_bar(d);
		switch ((w >> 1) & 0x3FF)
		{
		case 0: case 32:   // fcmpu, fcmpo
			d.use_r = sched_fpr(rA) | sched_fpr(rB); d.def_x = 1u << (rD >> 2);
			return true;
		case 12: case 14: case 15: case 40: case 72: case 136: case 264: case 814: case 815: case 846:
			// frsp, fctiw, fctiwz, fneg, fmr, fnabs, fabs, fctid, fctidz, fcfid
			d.use_r = sched_fpr(rB); d.def_r = sched_fpr(rD);
			return true;
		}
		return sched_bar(d);
	case 31:
	{
		const u32 xo = (w >> 1) & 0x3FF, rS = rD;
		// XO forms; OE=1 writes OV/SO, which is not modelled.
		switch (xo & 0x1FF)
		{
		case 8: case 10: case 40: case 136: case 138: case 266:   // subfc, addc, subf, subfe, adde, add
			if (xo & 0x200) return sched_bar(d);
			d.use_r = sched_gpr(rA) | sched_gpr(rB); d.def_r = sched_gpr(rD); sched_rc(d, w);
			if (xo == 136 || xo == 138) d.use_x |= 1u << SX_CA;
			if (xo != 40 && xo != 266) d.def_x |= 1u << SX_CA;
			return true;
		case 104: case 200: case 202: case 232: case 234:   // neg, subfze, addze, subfme, addme
			if (xo & 0x200) return sched_bar(d);
			d.use_r = sched_gpr(rA); d.def_r = sched_gpr(rD); sched_rc(d, w);
			if (xo != 104) { d.use_x |= 1u << SX_CA; d.def_x |= 1u << SX_CA; }
			return true;
		case 11: case 75: case 235:   // mulhwu, mulhw, mullw
			if (xo & 0x200) return sched_bar(d);
			d.use_r = sched_gpr(rA) | sched_gpr(rB); d.def_r = sched_gpr(rD); d.lat = 9; sched_rc(d, w);
			return true;
		case 9: case 73: case 233:   // mulhdu, mulhd, mulld
			if (xo & 0x200) return sched_bar(d);
			d.use_r = sched_gpr(rA) | sched_gpr(rB); d.def_r = sched_gpr(rD); d.lat = 14; sched_rc(d, w);
			return true;
		case 457: case 459: case 489: case 491:   // divdu, divwu, divd, divw
			if (xo & 0x200) return sched_bar(d);
			d.use_r = sched_gpr(rA) | sched_gpr(rB); d.def_r = sched_gpr(rD); sched_rc(d, w);
			d.lat = (xo == 457 || xo == 489) ? 70 : 40; d.busy = d.lat - 4;
			return true;
		}
		switch (xo)
		{
		case 0: case 32:   // cmp, cmpl
			d.use_r = sched_gpr(rA) | sched_gpr(rB); d.use_x = 1u << SX_SO; d.def_x = 1u << (rD >> 2);
			return true;
		case 28: case 60: case 124: case 284: case 316: case 412: case 444: case 476:   // and andc nor eqv xor orc or nand
			d.use_r = sched_gpr(rS) | sched_gpr(rB); d.def_r = sched_gpr(rA); sched_rc(d, w);
			return true;
		case 24: case 536: case 27: case 539:   // slw, srw, sld, srd
			d.use_r = sched_gpr(rS) | sched_gpr(rB); d.def_r = sched_gpr(rA); d.lat = 4; sched_rc(d, w);
			return true;
		case 792: case 794:   // sraw, srad
			d.use_r = sched_gpr(rS) | sched_gpr(rB); d.def_r = sched_gpr(rA); d.def_x = 1u << SX_CA; d.lat = 4;
			sched_rc(d, w);
			return true;
		case 824:   // srawi
			d.use_r = sched_gpr(rS); d.def_r = sched_gpr(rA); d.def_x = 1u << SX_CA; sched_rc(d, w);
			return true;
		case 26: case 58: case 922: case 954: case 986:   // cntlzw, cntlzd, extsh, extsb, extsw
			d.use_r = sched_gpr(rS); d.def_r = sched_gpr(rA); sched_rc(d, w);
			return true;
		case 19:   // mfcr; mfocrf with one field
			d.def_r = sched_gpr(rD);
			if ((w >> 20) & 1)
			{
				const u32 fxm = (w >> 12) & 0xFF;
				if (!fxm || (fxm & (fxm - 1)))
					return sched_bar(d);
				for (u32 f = 0; f < 8; f++)
					if (fxm & (0x80 >> f)) d.use_x = 1u << f;
				d.lat = 4;
			}
			else
			{
				d.use_x = 0xFF;
				d.lat = 10;
			}
			return true;
		case 144:   // mtcrf, mtocrf: whole fields
		{
			const u32 fxm = (w >> 12) & 0xFF;
			d.use_r = sched_gpr(rS);
			for (u32 f = 0; f < 8; f++)
				if (fxm & (0x80 >> f)) d.def_x |= 1u << f;
			d.lat = 4;
			return true;
		}
		case 339: case 467:   // mfspr, mtspr: LR, CTR and XER only
		{
			const u32 spr = rA | (rB << 5);
			const u32 x = spr == 8 ? 1u << SX_LR : spr == 9 ? 1u << SX_CTR : spr == 1 ? (1u << SX_CA) | (1u << SX_SO) : 0;
			if (!x)
				return sched_bar(d);
			if (xo == 339) { d.use_x = x; d.def_r = sched_gpr(rD); }
			else { d.use_r = sched_gpr(rS); d.def_x = x; }
			d.lat = 4;
			return true;
		}
		case 23: case 87: case 279: case 343: case 21: case 341: case 534: case 790:   // lwzx lbzx lhzx lhax ldx lwax lwbrx lhbrx
			d.load = 1;
			d.size = (xo == 23 || xo == 341 || xo == 534) ? 4 : xo == 87 ? 1 : xo == 21 ? 8 : 2;
			sched_base(d, rA); d.index = (u8)rB; d.indexed = 1; d.use_r |= sched_gpr(rB);
			d.def_r = sched_gpr(rD); d.lat = 5; d.unit = SU_LS;
			return true;
		case 535: case 599:   // lfsx, lfdx
			d.load = 1; d.size = xo == 535 ? 4 : 8;
			sched_base(d, rA); d.index = (u8)rB; d.indexed = 1; d.use_r |= sched_gpr(rB);
			d.def_r = sched_fpr(rD); d.lat = 6; d.unit = SU_LS;
			return true;
		case 151: case 215: case 407: case 149: case 662: case 918:   // stwx stbx sthx stdx stwbrx sthbrx
			d.store = 1;
			d.size = (xo == 151 || xo == 662) ? 4 : xo == 215 ? 1 : xo == 149 ? 8 : 2;
			sched_base(d, rA); d.index = (u8)rB; d.indexed = 1; d.use_r |= sched_gpr(rB) | sched_gpr(rS);
			d.lat = 1; d.unit = SU_LS;
			return true;
		case 663: case 727: case 983:   // stfsx, stfdx, stfiwx
			d.store = 1; d.size = xo == 727 ? 8 : 4;
			sched_base(d, rA); d.index = (u8)rB; d.indexed = 1; d.use_r |= sched_gpr(rB) | sched_fpr(rS);
			d.lat = 1; d.unit = SU_LS;
			return true;
		}
		return sched_bar(d);
	}
	}
	return sched_bar(d);
}

// What each GPR holds as an address after word d. r30 is always the context.
static void sched_track(const SchedIns& d, SchedPtr* p, const SchedEnv& e)
{
	const u32 w = d.w, op = w >> 26, rD = (w >> 21) & 31, rA = (w >> 16) & 31;
	const s64 simm = (s16)(w & 0xFFFF);
	SchedPtr n = { SP_UNK, 0, 0 };
	u32 dst = 32;
	if (d.barrier)
	{
		for (u32 k = 0; k < 32; k++)
			p[k].k = SP_UNK;
	}
	else if (op == 14 || op == 15)   // addi, addis
	{
		dst = rD;
		const s64 v = op == 15 ? simm * 65536 : simm;
		if (!rA)
			n = SchedPtr{ SP_ABS, v, v };
		else if (p[rA].k == SP_ABS)
			n = SchedPtr{ SP_ABS, p[rA].lo + v, p[rA].lo + v };
		else if (op == 15 && p[rA].k == SP_RAMIDX && (w & 0xFFFF) == e.ram_hi)
			n = SchedPtr{ SP_RAM, 0, 0 };
		else if (op == 15 && p[rA].k == SP_SQIDX)
			n = SchedPtr{ SP_RANGE, v, v + 64 };
		else if (p[rA].k == SP_RANGE)
			n = SchedPtr{ SP_RANGE, p[rA].lo + v, p[rA].hi + v };
		else if (op == 15 && e.bias_hi && (w & 0xFFFF) == e.bias_hi)   // 8C PREFIX: used only past its guard
			n = SchedPtr{ SP_RAM, 0, 0 };
	}
	else if (op == 24 && p[rD].k == SP_ABS)   // ori after addis: an address built in two
	{
		dst = rA;
		const s64 v = p[rD].lo | (s64)(w & 0xFFFF);
		n = SchedPtr{ SP_ABS, v, v };
	}
	else if (op == 26 && p[rD].k == SP_RAM)   // xori (the sub-word swizzle) stays in its 64 KB of RAM
	{
		dst = rA;
		n = SchedPtr{ SP_RAM, 0, 0 };
	}
	else if (op == 21 && !(w & 1) && ((w >> 11) & 31) == 0)   // rlwinm rA,rS,0,MB,ME
	{
		dst = rA;
		const u32 mb = (w >> 6) & 31, me = (w >> 1) & 31;
		if (mb == 8 && me == 31)
			n = SchedPtr{ SP_RAMIDX, 0, 0 };
		else if (mb == 26 && (me == 28 || me == 29))
			n = SchedPtr{ SP_SQIDX, 0, 0 };
	}
	for (u32 k = 0; k < 32; k++)
		if (d.def_r & sched_gpr(k))
			p[k].k = SP_UNK;
	if (dst < 32)
		p[dst] = n;
	p[30] = SchedPtr{ SP_ABS, (s64)e.ctx, (s64)e.ctx };
}

// The bytes an access may touch, [lo, hi), from its base's kind before it (b); false: anything. An indexed access
// is anything.
static bool sched_span(const SchedIns& d, const SchedPtr& b, const SchedEnv& e, s64& lo, s64& hi)
{
	if (d.indexed)
		return false;
	if (!d.base)
	{
		lo = d.disp;
		hi = lo + d.size;
		return true;
	}
	switch (b.k)
	{
	case SP_ABS: lo = b.lo + d.disp; hi = lo + d.size; return true;
	case SP_RANGE: lo = b.lo + d.disp; hi = b.hi + d.disp + d.size; return true;
	case SP_RAM: lo = e.ram_lo + (d.disp < 0 ? d.disp : 0); hi = e.ram_end + (d.disp > 0 ? d.disp : 0); return true;
	}
	return false;
}

struct SchedDag
{
	u32 n;
	u64 succ[SCHED_MAX][(SCHED_MAX + 63) / 64];
	u8 lat[SCHED_MAX][SCHED_MAX];   // edge i -> j, i < j
	u32 npred[SCHED_MAX];
	u32 height[SCHED_MAX];
};

static inline bool sched_has(const SchedDag& g, u32 i, u32 j) { return (g.succ[i][j >> 6] >> (j & 63)) & 1; }

static void sched_edge(SchedDag& g, u32 i, u32 j, u32 lat)
{
	if (!sched_has(g, i, j))
	{
		g.succ[i][j >> 6] |= 1ull << (j & 63);
		g.npred[j]++;
		g.lat[i][j] = (u8)lat;
	}
	else if (lat > g.lat[i][j])
		g.lat[i][j] = (u8)lat;
}

struct SchedWork
{
	SchedDag g;
	SchedIns in[SCHED_MAX];
	SchedPtr pb[SCHED_MAX];   // each word's base kind before it, in program order
	s64 lo[SCHED_MAX], hi[SCHED_MAX];
	u8 span[SCHED_MAX];
	u8 start[SCHED_BODY];
	u32 pick[SCHED_MAX], tmp[SCHED_MAX];
};

// Edges for every read after write, write after read, write after write (per resource) and every pair of memory
// accesses that may overlap with a store among them. No variable is named r: sh4_registers.h defines it.
static void sched_build(SchedWork& s, u32 n, const SchedEnv& e)
{
	SchedDag& g = s.g;
	g.n = n;
	s32 last_def[64 + SX_N];
	u64 readers[64 + SX_N][(SCHED_MAX + 63) / 64];
	for (u32 q = 0; q < 64 + SX_N; q++)
	{
		last_def[q] = -1;
		for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
			readers[q][k] = 0;
	}
	for (u32 i = 0; i < n; i++)
	{
		for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
			g.succ[i][k] = 0;
		g.npred[i] = 0;
		const SchedIns& d = s.in[i];
		s.span[i] = (d.load || d.store) && sched_span(d, s.pb[i], e, s.lo[i], s.hi[i]);
	}
	for (u32 j = 0; j < n; j++)
	{
		const SchedIns& b = s.in[j];
		for (u32 q = 0; q < 64 + SX_N; q++)
		{
			const bool use = q < 64 ? (b.use_r >> q) & 1 : (b.use_x >> (q - 64)) & 1;
			const bool def = q < 64 ? (b.def_r >> q) & 1 : (b.def_x >> (q - 64)) & 1;
			if (use && last_def[q] >= 0)
				sched_edge(g, (u32)last_def[q], j, s.in[last_def[q]].lat);
			if (def)
			{
				if (last_def[q] >= 0)
					sched_edge(g, (u32)last_def[q], j, 1);
				for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
					for (u64 m = readers[q][k]; m; m &= m - 1)
					{
						const u32 i = k * 64 + (u32)__builtin_ctzll(m);
						if (i != j)
							sched_edge(g, i, j, 0);
					}
			}
		}
		for (u32 q = 0; q < 64 + SX_N; q++)
		{
			const bool use = q < 64 ? (b.use_r >> q) & 1 : (b.use_x >> (q - 64)) & 1;
			const bool def = q < 64 ? (b.def_r >> q) & 1 : (b.def_x >> (q - 64)) & 1;
			if (def)
			{
				last_def[q] = (s32)j;
				for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
					readers[q][k] = 0;
			}
			if (use && !def)
				readers[q][j >> 6] |= 1ull << (j & 63);
		}
		if (!b.load && !b.store)
			continue;
		for (u32 i = 0; i < j; i++)
		{
			const SchedIns& a = s.in[i];
			if (!(a.load || a.store) || !(a.store || b.store))
				continue;
			if (s.span[i] && s.span[j] && (s.hi[i] <= s.lo[j] || s.hi[j] <= s.lo[i]))
				continue;
			sched_edge(g, i, j, a.store && b.load ? SCHED_LHS : a.store ? 1 : 0);
		}
	}
	for (u32 i = n; i-- > 0;)
	{
		u32 h = s.in[i].lat;
		for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
			for (u64 m = g.succ[i][k]; m; m &= m - 1)
			{
				const u32 j = k * 64 + (u32)__builtin_ctzll(m);
				if (g.lat[i][j] + g.height[j] > h)
					h = g.lat[i][j] + g.height[j];
			}
		g.height[i] = h;
	}
}

// Issue model: two words a cycle, one a unit, a non-pipelined op holding its unit. In program order when pick is 0,
// else the tallest ready word each slot, into pick. Returns the cycles until the last result.
static u32 sched_issue(const SchedWork& s, u32* pick)
{
	const SchedDag& g = s.g;
	const u32 n = g.n;
	u32 ready_at[SCHED_MAX], left[SCHED_MAX];
	bool issued[SCHED_MAX];
	for (u32 i = 0; i < n; i++)
	{
		ready_at[i] = 0;
		left[i] = g.npred[i];
		issued[i] = false;
	}
	u32 done = 0, cycle = 0, end = 0, busy_fx = 0, busy_fp = 0;
	while (done < n)
	{
		u32 units = 0;
		for (u32 slot = 0; slot < 2; slot++)
		{
			u32 best = n;
			for (u32 i = pick ? 0 : done; i < (pick ? n : done + 1); i++)
			{
				const SchedIns& d = s.in[i];
				if (issued[i] || left[i] || ready_at[i] > cycle || (units >> d.unit) & 1 ||
				    (d.unit == SU_FX && busy_fx > cycle) || (d.unit == SU_FP && busy_fp > cycle))
					continue;
				if (best == n || g.height[i] > g.height[best])
					best = i;
			}
			if (best == n)
				break;
			const SchedIns& d = s.in[best];
			issued[best] = true;
			units |= 1u << d.unit;
			if (pick)
				pick[done] = best;
			done++;
			if (d.busy)
				(d.unit == SU_FP ? busy_fp : busy_fx) = cycle + d.busy;
			if (cycle + d.lat > end)
				end = cycle + d.lat;
			for (u32 k = 0; k < (SCHED_MAX + 63) / 64; k++)
				for (u64 m = g.succ[best][k]; m; m &= m - 1)
				{
					const u32 j = k * 64 + (u32)__builtin_ctzll(m);
					left[j]--;
					if (cycle + g.lat[best][j] > ready_at[j])
						ready_at[j] = cycle + g.lat[best][j];
				}
		}
		cycle++;
	}
	return cycle > end ? cycle : end;
}

// One region [w, w + n) of modelled words: track the address kinds, schedule, write back when it saves cycles.
static void sched_region(SchedWork& s, u32* w, u32 n, const SchedEnv& e, SchedPtr* ptr, SchedStats& st)
{
	for (u32 i = 0; i < n; i++)
	{
		sched_decode(w[i], s.in[i]);
		s.pb[i] = ptr[s.in[i].base];
		sched_track(s.in[i], ptr, e);
	}
	st.regions++;
	st.words += n;
	if (n < 3)
	{
		st.cyc_before += n;
		st.cyc_after += n;
		return;
	}
	sched_build(s, n, e);
	const u32 before = sched_issue(s, 0);
	const u32 after = sched_issue(s, s.pick);
	st.cyc_before += before;
	if (after >= before)
	{
		st.cyc_after += before;
		return;
	}
	st.cyc_after += after;
	for (u32 i = 0; i < n; i++)
	{
		s.tmp[i] = s.in[s.pick[i]].w;
		st.moved += s.pick[i] != i;
	}
	for (u32 i = 0; i < n; i++)
		w[i] = s.tmp[i];
}

// A relative branch's target, or 0.
static inline const u32* sched_target(const u32* at)
{
	const u32 w = *at, op = w >> 26;
	if (op == 18 && !(w & 2))
		return (const u32*)((const u8*)at + (((s32)(w << 6) >> 6) & ~3));
	if (op == 16 && !(w & 2))
		return (const u32*)((const u8*)at + (s32)(s16)(w & 0xFFFC));
	return 0;
}

// The block body [lo, hi). Regions run between anchors; joins are the addresses the cold paths come back to, and
// a join or a branch target starts a region with no address kind known.
static void sched_range(SchedWork& s, u32* lo, u32* hi, const void* const* joins, u32 njoins, const SchedEnv& e,
                        SchedStats& st)
{
	const u32 n = (u32)(hi - lo);
	if (!n || n > SCHED_BODY)
		return;
	for (u32 i = 0; i < n; i++)
		s.start[i] = 0;
	for (u32 k = 0; k < njoins; k++)
	{
		const u32* j = (const u32*)joins[k];
		if (j >= lo && j < hi)
			s.start[j - lo] = 1;
	}
	for (u32 i = 0; i < n; i++)
	{
		const u32* t = sched_target(lo + i);
		if (t && t >= lo && t < hi)
			s.start[t - lo] = 1;
	}
	SchedPtr ptr[32];
	for (u32 k = 0; k < 32; k++)
		ptr[k] = SchedPtr{ SP_UNK, 0, 0 };
	ptr[30] = SchedPtr{ SP_ABS, (s64)e.ctx, (s64)e.ctx };
	SchedIns d;
	u32 i = 0;
	while (i < n)
	{
		if (s.start[i])
			for (u32 k = 0; k < 32; k++)
				if (k != 30)
					ptr[k].k = SP_UNK;
		if (!sched_decode(lo[i], d))
		{
			sched_track(d, ptr, e);
			i++;
			continue;
		}
		u32 j = i + 1;
		while (j < n && j - i < SCHED_MAX && !s.start[j] && sched_decode(lo[j], d))
			j++;
		sched_region(s, lo + i, j - i, e, ptr, st);
		i = j;
	}
}
