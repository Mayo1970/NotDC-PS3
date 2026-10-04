#pragma once
// Plan 5.18 JIT HOTBLOCKS: classes of the emitted PPC and the cold-entry counter. emit_hot_cold_count needs
// ppc_emitter.h in scope; misc/hot_check tests both on the PC.

enum HotCls
{
	HC_INSN, HC_LD, HC_ST, HC_CTX, HC_LHS, HC_LHS_IF, HC_MFCR, HC_VSHIFT, HC_MCODE, HC_XER,
	HC_CALLC, HC_STUB, HC_BCTR, HC_BC, HC_BCCOLD, HC_FPU, HC_FMR, HC_FPSCR, HC_MULDIV, HC_COUNT
};

static const char* const k_hc_name[HC_COUNT] = {
	"insn", "ld", "st", "ctx", "lhs", "lhs_if", "mfcr", "vshift", "mcode", "xer",
	"callc", "stub", "bctr", "bc", "bccold", "fpu", "fmr", "fpscr", "muldiv"
};

// Size of the access (0 = not a load or store); fp for FPR data, dform for base rA + d (D and DS forms).
static inline u32 hot_mem(u32 x, bool& load, bool& fp, bool& dform, s32& d)
{
	const u32 op = x >> 26;
	fp = false;
	dform = true;
	d = (s16)(x & 0xFFFF);
	switch (op)
	{
	case 32: case 33: case 46:                load = true;  return 4;   // lwz, lwzu, lmw
	case 34: case 35:                         load = true;  return 1;
	case 40: case 41: case 42: case 43:       load = true;  return 2;   // lhz, lhzu, lha, lhau
	case 48: case 49:             fp = true;  load = true;  return 4;
	case 50: case 51:             fp = true;  load = true;  return 8;
	case 36: case 37: case 47:                load = false; return 4;   // stw, stwu, stmw
	case 38: case 39:                         load = false; return 1;
	case 44: case 45:                         load = false; return 2;
	case 52: case 53:             fp = true;  load = false; return 4;
	case 54: case 55:             fp = true;  load = false; return 8;
	case 58: d = (s16)(x & 0xFFFC); load = true;  return (x & 3) == 2 ? 4 : 8;   // ld, ldu, lwa
	case 62: d = (s16)(x & 0xFFFC); load = false; return 8;                      // std, stdu
	case 31: break;
	default: return 0;
	}
	dform = false;
	d = 0;
	switch ((x >> 1) & 0x3FF)
	{
	case 20: case 23: case 55: case 341: case 373: case 534:   load = true;  return 4;
	case 87: case 119:                                          load = true;  return 1;
	case 279: case 311: case 343: case 375: case 790:           load = true;  return 2;
	case 21: case 53: case 84: case 532:                        load = true;  return 8;
	case 535: case 567:                             fp = true;  load = true;  return 4;
	case 599: case 631:                             fp = true;  load = true;  return 8;
	case 150: case 151: case 183: case 662:                     load = false; return 4;
	case 215: case 247:                                         load = false; return 1;
	case 407: case 439: case 918:                               load = false; return 2;
	case 149: case 181: case 214: case 660:                     load = false; return 8;
	case 663: case 695: case 983:                   fp = true;  load = false; return 4;
	case 727: case 759:                             fp = true;  load = false; return 8;
	default: return 0;
	}
}

// Classes of n words at host address `at`, the probe's 4 counter words at index `skip` left out. lhs: a load
// from r30 (the context) that overlaps a r30 store of the last 24 words; lhs_if when GPR data meets FPR data.
static inline void hot_classify(const u32* w, u32 n, u32 skip, uintptr_t at, uintptr_t cold,
                                uintptr_t cache_lo, uintptr_t cache_hi, u16* cls)
{
	struct St { s32 d; u32 sz; bool fp; u32 i; };
	St st[24];
	u32 st_n = 0;
	u32 c[HC_COUNT] = { 0 };
	for (u32 i = 0; i < n; i++)
	{
		if (i >= skip && i < skip + 4)
			continue;
		const u32 x = w[i], op = x >> 26, xo = (x >> 1) & 0x3FF;
		const uintptr_t pc = at + i * 4;
		c[HC_INSN]++;

		bool load, fp, dform;
		s32 d;
		const u32 sz = hot_mem(x, load, fp, dform, d);
		if (sz)
		{
			c[load ? HC_LD : HC_ST]++;
			if (dform && ((x >> 16) & 31) == 30)
			{
				c[HC_CTX]++;
				if (!load)
					st[st_n++ % 24] = St{ d, sz, fp, i };
				else
					for (u32 k = 0; k < 24 && k < st_n; k++)
					{
						const St& s = st[(st_n - 1 - k) % 24];
						if (i - s.i > 24)
							break;
						if (s.d < d + (s32)sz && d < s.d + (s32)s.sz)
						{
							c[HC_LHS]++;
							if (s.fp != fp)
								c[HC_LHS_IF]++;
							break;
						}
					}
			}
			// Algebraic loads and load/store multiple are microcoded on the PPE.
			if (op == 42 || op == 43 || op == 46 || op == 47 || (op == 58 && (x & 3) == 2) ||
			    (op == 31 && (xo == 341 || xo == 343 || xo == 373 || xo == 375)))
				c[HC_MCODE]++;
			continue;
		}

		switch (op)
		{
		case 7:
			c[HC_MULDIV]++;
			break;
		case 16:		// bc, unless BO says always
			if (((x >> 21) & 0x14) != 0x14)
			{
				const uintptr_t t = ((x & 2) ? 0 : pc) + (intptr_t)(s16)(x & 0xFFFC);
				c[HC_BC]++;
				if (t >= cold)
					c[HC_BCCOLD]++;
			}
			break;
		case 18:		// bl into the code cache is a link stub, anything else is C
			if (x & 1)
			{
				s32 li = (s32)(x & 0x03FFFFFC);
				if (li & 0x02000000)
					li -= 0x04000000;
				const uintptr_t t = ((x & 2) ? 0 : pc) + (intptr_t)li;
				c[(t >= cache_lo && t < cache_hi) ? HC_STUB : HC_CALLC]++;
			}
			break;
		case 19:
			if (xo == 528)
				c[(x & 1) ? HC_CALLC : HC_BCTR]++;
			break;
		case 23:		// rlwnm
			c[HC_VSHIFT]++;
			break;
		case 30:		// rldcl, rldcr
			if (((x >> 1) & 0xF) == 8 || ((x >> 1) & 0xF) == 9)
				c[HC_VSHIFT]++;
			break;
		case 31:
			switch (xo)
			{
			case 19:	// mfcr; mfocrf (one field) is not microcoded
				if (!(x & 0x00100000))
					c[HC_MFCR]++;
				break;
			case 144:	// mtcrf over more than one field
				if (!(x & 0x00100000) && __builtin_popcount((x >> 12) & 0xFF) > 1)
					c[HC_MCODE]++;
				break;
			case 339: case 467:
				if (((x >> 11) & 0x3FF) == 32)
					c[HC_XER]++;
				break;
			case 24: case 27: case 536: case 539: case 792: case 794:
				c[HC_VSHIFT]++;
				break;
			case 9: case 11: case 73: case 75: case 233: case 235: case 457: case 459: case 489: case 491:
			case 745: case 747: case 969: case 971: case 1001: case 1003:
				c[HC_MULDIV]++;
				break;
			}
			break;
		case 59:
			c[HC_FPU]++;
			break;
		case 63:
			if (xo == 40 || xo == 72 || xo == 136 || xo == 264)
				c[HC_FMR]++;
			else if (xo == 38 || xo == 64 || xo == 70 || xo == 134 || xo == 583 || xo == 711)
				c[HC_FPSCR]++;
			else
				c[HC_FPU]++;
			break;
		}
	}
	for (u32 k = 0; k < HC_COUNT; k++)
		cls[k] = c[k] > 0xFFFF ? 0xFFFF : (u16)c[k];
}

// ctr += 1 at a cold-fragment entry, through r0 and rb: rarg2 for a scalar write (kind 2), else rarg1; both
// are free at every entry. nor + neg add one without touching XER[CA]. ctr must lie below 2 GB.
static inline void emit_hot_cold_count(u32* ctr, u32 kind)
{
	const u32 rb = (kind == 2) ? ppc_rarg2 : ppc_rarg1;
	const u32 a = (u32)(uintptr_t)ctr;
	const s32 lo = (s16)(a & 0xFFFF);
	ppc_addis(rb, 0, (a - (u32)lo) >> 16);
	ppc_lwz(ppc_r0, rb, (u32)lo);
	ppc_norx(ppc_r0, ppc_r0, ppc_r0, 0);
	ppc_negx(ppc_r0, ppc_r0, 0, 0);
	ppc_stw(ppc_r0, rb, (u32)lo);
}
