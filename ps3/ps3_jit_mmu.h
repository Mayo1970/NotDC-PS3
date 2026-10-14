#pragma once
// MMU JIT (plan section 7, bundle 20, P38): the inline JTC lookup of a data access, and the cold path around its C
// call (dc/sh4/sh4_mmu_jit.h). Needs ppc_emitter.h; misc/jmmu_check runs these on a PPC64 model and against ppu-as.

void ppc_bx(void* dst, u32 LK);
void ppc_call_c(void* fn);

// rldicl, rldicr, std, ld: the emitter header is PPC32-generated. MD form: the 6-bit mb/me field has its top bit last.
static inline void jm_rldicl(u32 A, u32 S, u32 sh, u32 mb)
{
	ppc_emit(0x78000000 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((((mb & 31) << 1) | (mb >> 5)) << 5) |
	         ((sh >> 5) << 1));
}
static inline void jm_rldicr(u32 A, u32 S, u32 sh, u32 me)
{
	ppc_emit(0x78000004 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((((me & 31) << 1) | (me >> 5)) << 5) |
	         ((sh >> 5) << 1));
}
static inline void jm_std(u32 S, u32 d, u32 A) { ppc_emit(0xF8000000 | (S << 21) | (A << 16) | (d & 0xFFFC)); }
static inline void jm_ld(u32 D, u32 d, u32 A)  { ppc_emit(0xE8000000 | (D << 21) | (A << 16) | (d & 0xFFFC)); }

// The JTC lookup of areg (junk above its low word allowed; scratch r0, r11, r12; tbl below 2 GB): a hit leaves r11 = the
// host page, r0 = the offset (big-endian lane); the tag keeps the alignment bits, so a misaligned access takes the bne.
static inline ppc_label* emit_jtc_lookup(u32 areg, u32 tbl, u32 sz)
{
	const s32 lo = (s16)(tbl & 0xFFFF);
	const u32 ha = (tbl - (u32)lo) >> 16;
	const u32 mb = sz == 1 ? 0 : sz == 2 ? 31 : sz == 4 ? 30 : 29;
	ppc_rlwinmx(ppc_r12, areg, 25, 19, 28, 0);		// ((va >> 10) & 1023) * 8
	ppc_addis(ppc_r11, ppc_r12, ha);
	ppc_lwz(ppc_r12, ppc_r11, (u32)lo);			// the tag
	ppc_rlwinmx(ppc_r0, areg, 0, mb, 21, 0);		// the page and the alignment bits
	ppc_cmpl(ppc_cr0, ppc_r12, ppc_r0, 0);
	ppc_label* miss = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	ppc_lwz(ppc_r11, ppc_r11, (u32)(lo + 4));		// the host page
	ppc_rlwinmx(ppc_r0, areg, 0, 22, 31, 0);
	if (sz < 4)
		ppc_xori(ppc_r0, ppc_r0, 4 - sz);
	return miss;
}

// After a hit: sz 1 and 2 sign-extended as SH4 loads, 4; f != 0xFF loads the FPR f instead (sz 4).
static inline void emit_jtc_load(u32 sz, u32 d, u32 f)
{
	if (f != 0xFF)
		ppc_lfsx(f, ppc_r11, ppc_r0);
	else if (sz == 1)
	{
		ppc_lbzx(d, ppc_r11, ppc_r0);
		ppc_extsbx(d, d, 0);
	}
	else if (sz == 2)
		ppc_lhax(d, ppc_r11, ppc_r0);
	else
		ppc_lwzx(d, ppc_r11, ppc_r0);
}

static inline void emit_jtc_store(u32 sz, u32 s, u32 f)
{
	if (f != 0xFF)
		ppc_stfsx(f, ppc_r11, ppc_r0);
	else if (sz == 1)
		ppc_stbx(s, ppc_r11, ppc_r0);
	else if (sz == 2)
		ppc_sthx(s, ppc_r11, ppc_r0);
	else
		ppc_stwx(s, ppc_r11, ppc_r0);
}

// A pair: word[a] in d0 (or f0), word[a+4] in d1 (f1). d0 may be the address register: the address is in r11 now.
static inline void emit_jtc_load_pair(u32 d0, u32 d1, u32 f0, u32 f1)
{
	ppc_addx(ppc_r11, ppc_r11, ppc_r0, 0, 0);
	if (f0 != 0xFF)
	{
		ppc_lfs(f0, ppc_r11, 0);
		ppc_lfs(f1, ppc_r11, 4);
	}
	else
	{
		ppc_lwz(d1, ppc_r11, 4);
		ppc_lwz(d0, ppc_r11, 0);
	}
}

static inline void emit_jtc_store_pair(u32 s0, u32 s1, u32 f0, u32 f1)
{
	ppc_addx(ppc_r11, ppc_r11, ppc_r0, 0, 0);
	if (f0 != 0xFF)
	{
		ppc_stfs(f0, ppc_r11, 0);
		ppc_stfs(f1, ppc_r11, 4);
	}
	else
	{
		ppc_stw(s0, ppc_r11, 0);
		ppc_stw(s1, ppc_r11, 4);
	}
}

// What a cold path needs besides its access: the context base, the FPR bounce slot, the fault word (below 2 GB), the
// op's SPC, the exception stub, the join point and the P11 cycle store (cyc_ofs, 0 = none).
struct JmmuCold
{
	u32 ctx, fofs, fault, epc, cyc_ofs;
	void* stub;
	void* done;
};

// The fault test after the call, then the exit: r3 = epc (addis + ori; the stub clears the upper half), b stub.
static inline void emit_jmmu_call_tail(const JmmuCold& c, void* fn)
{
	if (c.cyc_ofs)
		ppc_stw(29, c.ctx, c.cyc_ofs);		// r29: the JIT's cycle count
	ppc_call_c(fn);
	const s32 lo = (s16)(c.fault & 0xFFFF);
	ppc_addis(ppc_r12, 0, (c.fault - (u32)lo) >> 16);
	ppc_lwz(ppc_r0, ppc_r12, (u32)lo);
	ppc_cmpi(ppc_cr0, ppc_r0, 0, 0);
}

static inline void emit_jmmu_exit(const JmmuCold& c, ppc_label* exc)
{
	ppc_bx(c.done, 0);
	exc->MarkLabel();
	ppc_addis(ppc_r3, 0, c.epc >> 16);
	ppc_ori(ppc_r3, ppc_r3, c.epc & 0xFFFF);
	ppc_bx(c.stub, 0);
}

// A read's cold path: fn(va) into d (sz 1, 2 sign-extended), or into the FPR f through the slot.
static inline void emit_jmmu_cold_read(const JmmuCold& c, u32 sz, u32 areg, u32 d, u32 f, void* fn)
{
	if (areg != ppc_r3)
		ppc_ori(ppc_r3, areg, 0);
	jm_rldicl(ppc_r3, ppc_r3, 0, 32);			// ELFv1: the caller extends a u32
	emit_jmmu_call_tail(c, fn);
	ppc_label* exc = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	if (f != 0xFF)
	{
		ppc_stw(ppc_r3, c.ctx, c.fofs);
		ppc_lfs(f, c.ctx, c.fofs);
	}
	else if (sz == 1)
		ppc_extsbx(d, ppc_r3, 0);
	else if (sz == 2)
		ppc_extshx(d, ppc_r3, 0);
	else if (d != ppc_r3)
		ppc_ori(d, ppc_r3, 0);
	emit_jmmu_exit(c, exc);
}

// A pair read: fn(va) returns the big-endian u64, word[a] in r3 and word[a+4] in r4 (or f, f1). The address is in r3.
static inline void emit_jmmu_cold_read_pair(const JmmuCold& c, u32 f, u32 f1, void* fn)
{
	jm_rldicl(ppc_r3, ppc_r3, 0, 32);
	emit_jmmu_call_tail(c, fn);
	ppc_label* exc = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	if (f != 0xFF)
	{
		jm_std(ppc_r3, c.fofs, c.ctx);
		ppc_lfs(f, c.ctx, c.fofs);
		ppc_lfs(f1, c.ctx, c.fofs + 4);
	}
	else
	{
		jm_rldicl(ppc_r4, ppc_r3, 0, 32);		// word[a+4]
		jm_rldicl(ppc_r3, ppc_r3, 32, 32);		// word[a]
	}
	emit_jmmu_exit(c, exc);
}

// A write's cold path: fn(va, v), v from the GPR s (masked to sz) or the FPR f through the slot.
static inline void emit_jmmu_cold_write(const JmmuCold& c, u32 sz, u32 areg, u32 s, u32 f, void* fn)
{
	if (areg != ppc_r3)
		ppc_ori(ppc_r3, areg, 0);
	jm_rldicl(ppc_r3, ppc_r3, 0, 32);
	if (f != 0xFF)
	{
		ppc_stfs(f, c.ctx, c.fofs);
		ppc_lwz(ppc_r4, c.ctx, c.fofs);
	}
	else
	{
		if (s != ppc_r4)
			ppc_ori(ppc_r4, s, 0);
		if (sz == 1)
			ppc_andi(ppc_r4, ppc_r4, 0xFF);
		else if (sz == 2)
			ppc_andi(ppc_r4, ppc_r4, 0xFFFF);
		else
			jm_rldicl(ppc_r4, ppc_r4, 0, 32);
	}
	emit_jmmu_call_tail(c, fn);
	ppc_label* exc = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	emit_jmmu_exit(c, exc);
}

// A pair write: fn(va, (high << 32) | low), the words in r5:r6 (or f, f1). The address is in r3.
static inline void emit_jmmu_cold_write_pair(const JmmuCold& c, u32 f, u32 f1, void* fn)
{
	jm_rldicl(ppc_r3, ppc_r3, 0, 32);
	if (f != 0xFF)
	{
		ppc_stfs(f, c.ctx, c.fofs);
		ppc_stfs(f1, c.ctx, c.fofs + 4);
		jm_ld(ppc_r4, c.fofs, c.ctx);
	}
	else
	{
		jm_rldicr(ppc_r4, ppc_r5, 32, 31);
		jm_rldicl(ppc_r6, ppc_r6, 0, 32);
		ppc_orx(ppc_r4, ppc_r4, ppc_r6, 0);
	}
	emit_jmmu_call_tail(c, fn);
	ppc_label* exc = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	emit_jmmu_exit(c, exc);
}

// SR.FD at the start of a block with an FPU op: the FPU-disabled exception (0x800) with SPC = the block's start.
static inline ppc_label* emit_jmmu_fd_test(u32 ctx, u32 sr_ofs)
{
	ppc_lwz(ppc_r0, ctx, sr_ofs);
	ppc_andi(ppc_r0, ppc_r0, 0x8000);
	ppc_label* off = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);
	return off;
}

static inline void emit_jmmu_fd_exit(const JmmuCold& c)
{
	const s32 lo = (s16)(c.fault & 0xFFFF);
	ppc_addi(ppc_r0, 0, 0x800);
	ppc_addis(ppc_r12, 0, (c.fault - (u32)lo) >> 16);
	ppc_stw(ppc_r0, ppc_r12, (u32)lo);
	ppc_addis(ppc_r3, 0, c.epc >> 16);
	ppc_ori(ppc_r3, ppc_r3, c.epc & 0xFFFF);
	ppc_bx(c.stub, 0);
}
