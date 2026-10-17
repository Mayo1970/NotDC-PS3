#pragma once
// Fastmem RAM forms of rows JIT SHORT FORMS (2-op guard or 8C PREFIX, @(disp,Rn) folded) and JIT SCHEDULE FORMS (guard
// and address side by side, load before branch). Needs ppc_emitter.h; misc/fmem_check runs them against the old forms.

#define FM_FOLD_MAX 4088   // largest folded displacement: a pair's last byte stays in the 4 KB pad after RAM
#define FM_BASE_REG 11     // 8C BASE REG: the block's bias, so a word access is one indexed load or store

struct FmForm
{
	// 0: area 3, not P4. 1: area 3 (P4's 0xEC-0xEF window counts as RAM). 2 (8C PREFIX): the 64-bit register >> 24
	// is prefix, so another mirror or a junk upper half goes cold, and one addis gives the host address.
	u8 guard;
	u8 prefix;          // guard 2: 0x8C, or 0x0C in a block at 0x0C...
	bool sched;         // FORMS
	bool base;          // guard 2: FM_BASE_REG holds the bias (sign-extended); the caller sets it before the access
	u32 ram_hi;         // RAM base >> 16
	u32 bias_hi;        // guard 2: (RAM base - (prefix << 24)) >> 16, as addis takes it
};

// One access. kind: 0 read, 1 read pair, 2 write, 3 write pair. areg holds the address, or the base when disp is
// set; rt is the host address and never areg. d0/d1: destination or data (pairs: word 0, word 1); fpr: FPRs.
struct FmAcc
{
	u8 kind, sz, areg, rt, d0, d1;
	bool fpr;
	u16 disp;   // sz 4 and pairs only: a sub-word swizzle must see the whole address
};

// 8C BASE REG takes a scalar access with no displacement; a pair or a folded one adds the bias with addis.
static inline bool fm_base_use(const FmForm& f, const FmAcc& a)
{
	return f.guard == 2 && f.base && !(a.kind & 1) && !a.disp;
}

static inline void fm_g1(const FmForm& f, u32 areg)
{
	if (f.guard == 2)
		ppc_emit(0x78000000u | areg << 21 | 8 << 11 | 24 << 6 | 1 << 1);   // rldicl r0,areg,40,24: r0 = areg >> 24
	else if (f.guard == 1)
		ppc_rlwinmx(ppc_r0, areg, 6, 29, 31, 0);   // r0 = area
	else
		ppc_rlwinmx(ppc_r0, areg, 3, 29, 2, 0);    // r0 = (area<<29) | region
}
static inline void fm_g2(const FmForm& f)
{
	if (f.guard == 0)
		ppc_xoris(ppc_r0, ppc_r0, 0x6000);           // area 3 -> 0
}
static inline void fm_g3(const FmForm& f)
{
	ppc_cmpli(ppc_cr0, ppc_r0, f.guard == 2 ? f.prefix : f.guard ? 3 : 7, f.guard == 2);
}
static inline ppc_label* fm_gbr(const FmForm& f)
{
	ppc_label* l = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, f.guard ? BI_CR0_EQ : BI_CR0_LT, 0, 0, 0);   // bne / bge cold
	return l;
}

// Word k of the access: a read's loads (pairs: 0 then 1) or a write's stores. No variable is named r:
// sh4_registers.h defines it.
static inline void fm_word(const FmAcc& a, u32 k)
{
	const s32 d = a.disp + (s32)k * 4;
	const u32 rg = k ? a.d1 : a.d0;
	if (a.kind < 2)
	{
		if (a.fpr) ppc_lfs(rg, a.rt, (u32)d);
		else if (a.sz == 1) ppc_lbz(rg, a.rt, (u32)d);
		else if (a.sz == 2) ppc_lhz(rg, a.rt, (u32)d);
		else ppc_lwz(rg, a.rt, (u32)d);
	}
	else
	{
		if (a.fpr) ppc_stfs(rg, a.rt, (u32)d);
		else if (a.sz == 1) ppc_stb(rg, a.rt, (u32)d);
		else if (a.sz == 2) ppc_sth(rg, a.rt, (u32)d);
		else ppc_stw(rg, a.rt, (u32)d);
	}
}

// 8C BASE REG's access at ra + FM_BASE_REG (ra: the address, or the swizzled one of a sub-word).
static inline void fm_word_x(const FmAcc& a, u32 ra)
{
	if (a.kind < 2)
	{
		if (a.fpr) ppc_lfsx(a.d0, ra, FM_BASE_REG);
		else if (a.sz == 1) ppc_lbzx(a.d0, ra, FM_BASE_REG);
		else if (a.sz == 2) ppc_lhzx(a.d0, ra, FM_BASE_REG);
		else ppc_lwzx(a.d0, ra, FM_BASE_REG);
	}
	else
	{
		if (a.fpr) ppc_stfsx(a.d0, ra, FM_BASE_REG);
		else if (a.sz == 1) ppc_stbx(a.d0, ra, FM_BASE_REG);
		else if (a.sz == 2) ppc_sthx(a.d0, ra, FM_BASE_REG);
		else ppc_stwx(a.d0, ra, FM_BASE_REG);
	}
}

// A scalar int read's sign extension (sz 1 and 2); the cold path's result is already extended, so it can run twice.
static inline void fm_ext(const FmAcc& a)
{
	if (a.kind == 0 && !a.fpr && a.sz == 1) ppc_extsbx(a.d0, a.d0, 0);
	else if (a.kind == 0 && !a.fpr && a.sz == 2) ppc_extshx(a.d0, a.d0, 0);
}

// Emits one access. Returns the branch to the cold path; *done is where the cold path comes back. A read's load goes
// before the branch only when it cannot overwrite the address the cold path reads (the host address is in RAM).
static ppc_label* fm_emit(const FmForm& f, const FmAcc& a, void** done)
{
	const bool pair = a.kind & 1;
	const u32 sz = pair ? 4 : a.sz;
	const bool bx = fm_base_use(f, a);
	if (!f.sched)
	{
		fm_g1(f, a.areg);
		fm_g2(f);
		fm_g3(f);
		ppc_label* cold = fm_gbr(f);
		if (bx)
		{
			if (sz < 4)
				ppc_xori(a.rt, a.areg, 4 - sz);
			fm_word_x(a, sz < 4 ? a.rt : a.areg);
			fm_ext(a);
			*done = emit_GetCCPtr();
			return cold;
		}
		if (f.guard == 2)
			ppc_addis(a.rt, a.areg, f.bias_hi);      // the prefix's 16 MB, upper half clear
		else
		{
			ppc_rlwinmx(a.rt, a.areg, 0, 8, 31, 0);    // 16 MB mirror mask
			ppc_addis(a.rt, a.rt, f.ram_hi);
		}
		if (sz < 4)
			ppc_xori(a.rt, a.rt, 4 - sz);            // big-endian sub-word swizzle
		// A pair keeps the old order: word 1 first, as its word 0 may be the address register.
		if (pair && a.kind == 1 && !a.fpr)
		{
			fm_word(a, 1);
			fm_word(a, 0);
		}
		else
		{
			fm_word(a, 0);
			if (pair)
				fm_word(a, 1);
		}
		fm_ext(a);
		*done = emit_GetCCPtr();
		return cold;
	}
	fm_g1(f, a.areg);
	if (bx)
	{
		if (sz < 4)
			ppc_xori(a.rt, a.areg, 4 - sz);
	}
	else if (f.guard == 2)
		ppc_addis(a.rt, a.areg, f.bias_hi);
	else
		ppc_rlwinmx(a.rt, a.areg, 0, 8, 31, 0);
	fm_g2(f);
	if (f.guard != 2)
		ppc_addis(a.rt, a.rt, f.ram_hi);
	fm_g3(f);
	if (sz < 4 && !bx)
		ppc_xori(a.rt, a.rt, 4 - sz);
	// Guard 2's host address is in RAM only once the guard passed, so its loads wait for the branch.
	const bool early = f.guard != 2;
	bool late0 = true, late1 = pair;
	if (early && a.kind == 0)
	{
		if (a.fpr || a.d0 != a.areg)
		{
			fm_word(a, 0);
			late0 = false;
		}
	}
	else if (early && a.kind == 1)
	{
		if (a.fpr || a.d1 != a.areg)
		{
			fm_word(a, 1);
			late1 = false;
		}
		if (a.fpr || a.d0 != a.areg)
		{
			fm_word(a, 0);
			late0 = false;
		}
	}
	ppc_label* cold = fm_gbr(f);
	if (bx)
	{
		fm_word_x(a, sz < 4 ? a.rt : a.areg);
		*done = emit_GetCCPtr();
		fm_ext(a);
		return cold;
	}
	if (late1 && a.kind == 1)
		fm_word(a, 1);
	if (late0)
		fm_word(a, 0);
	if (late1 && a.kind == 3)
		fm_word(a, 1);
	*done = emit_GetCCPtr();
	fm_ext(a);
	return cold;
}

// JIT SCHEDULE FORMS: a guessed store-queue store, its test and host address side by side (ps3_jit_sq.h's
// emit_sq_check + emit_sq_body otherwise). Returns the branch any other address takes.
static inline ppc_label* fm_emit_sq_sched(u32 areg, u32 ea, bool pair, bool fpr, u32 d0, u32 d1, u32 sq_base)
{
	const s32 lo = (s16)(sq_base & 0xFFFF);
	const u32 ha = (sq_base - (u32)lo) >> 16;
	ppc_rlwinmx(ppc_r0, areg, 6, 26, 31, 0);
	ppc_rlwinmx(ea, areg, 0, 26, pair ? 28 : 29, 0);
	ppc_cmpli(ppc_cr0, ppc_r0, 0x38, 0);
	ppc_addis(ea, ea, ha);
	ppc_label* other = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	if (fpr)
	{
		ppc_stfs(d0, ea, (u32)lo);
		if (pair)
			ppc_stfs(d1, ea, (u32)(lo + 4));
	}
	else
	{
		ppc_stw(d0, ea, (u32)lo);
		if (pair)
			ppc_stw(d1, ea, (u32)(lo + 4));
	}
	return other;
}

// P101 fallback, called first by each cold path of an 8C PREFIX access: r0 = the address, r12 = its block's miss
// count. A RAM address counts; the count that reaches limit writes r12 to *req. Only r0, r12, CR0, CTR and LR change.
static inline void fm_emit_fb_stub(u32 req, u32 limit)
{
	const s32 lo = (s16)(req & 0xFFFF);
	ppc_rlwinmx(ppc_r0, ppc_r0, 6, 29, 31, 0);   // area of the low word: MMIO and the store queue do not count
	ppc_cmpli(ppc_cr0, ppc_r0, 3, 0);
	ppc_bclrx(BO_FALSE, BI_CR0_EQ, 0);
	ppc_lwz(ppc_r0, ppc_r12, 0);
	ppc_addic(ppc_r0, ppc_r0, 1, 0);              // addi would read r0 as zero
	ppc_stw(ppc_r0, ppc_r12, 0);
	ppc_cmpli(ppc_cr0, ppc_r0, limit, 0);
	ppc_bclrx(BO_FALSE, BI_CR0_EQ, 0);
	ppc_mtctr(ppc_r12);
	ppc_addis(ppc_r12, 0, (req - (u32)lo) >> 16);
	ppc_mfctr(ppc_r0);
	ppc_stw(ppc_r0, ppc_r12, (u32)lo);
	ppc_bclrx(BO_ALWAYS, BI_CR0_EQ, 0);
}

// The call to the stub at a cold path's start: the address, the block's counter.
static inline void fm_emit_fb_count(u32 areg, u32 counter, void* stub)
{
	const s32 lo = (s16)(counter & 0xFFFF);
	ppc_ori(ppc_r0, areg, 0);
	ppc_addis(ppc_r12, 0, (counter - (u32)lo) >> 16);
	ppc_addi(ppc_r12, ppc_r12, (u32)lo);
	ppc_bx(stub, 1);
}
