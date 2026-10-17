#pragma once
// Fastmem RAM forms of rows JIT SHORT FORMS (2-op guard or 8C PREFIX, @(disp,Rn) folded) and JIT SCHEDULE FORMS (guard
// and address side by side, load before branch). Needs ppc_emitter.h; misc/fmem_check runs them against the old forms.

#define FM_FOLD_MAX 4088   // largest folded displacement: a pair's last byte stays in the 4 KB pad after RAM

struct FmForm
{
	// 0: area 3, not P4. 1: area 3 (P4's 0xEC-0xEF window counts as RAM). 2 (8C PREFIX): the 64-bit register >> 24
	// is prefix, so another mirror or a junk upper half goes cold, and one addis gives the host address.
	u8 guard;
	u8 prefix;          // guard 2: 0x8C, or 0x0C in a block at 0x0C...
	bool sched;         // FORMS
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
	if (!f.sched)
	{
		fm_g1(f, a.areg);
		fm_g2(f);
		fm_g3(f);
		ppc_label* cold = fm_gbr(f);
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
	if (f.guard == 2)
		ppc_addis(a.rt, a.areg, f.bias_hi);
	else
		ppc_rlwinmx(a.rt, a.areg, 0, 8, 31, 0);
	fm_g2(f);
	if (f.guard != 2)
		ppc_addis(a.rt, a.rt, f.ram_hi);
	fm_g3(f);
	if (sz < 4)
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
