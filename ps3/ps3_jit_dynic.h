#pragma once
// Plan 5.3 DYN_IC: a per-site inline cache on dynamic block ends (jsr/bsrf, jmp/braf, rts). Needs ppc_emitter.h in
// scope; misc/dynic_check runs it on a PPC64 model and against ppu-as.

// Slot 0's `bl` to the fill stub. Relative, so the word also names the slot's own address.
static inline u32 dynic_call_word(const void* site, const void* stub)
{
	return 0x48000001u | ((u32)((const u8*)stub - (const u8*)site) & 0x03FFFFFCu);
}

// Site, after `mr a0,djump`: slot 0 calls the fill stub, which never returns here; slots 1-3 branch to site+16,
// the miss path. The fill writes the guard over all four words, once.
static inline void emit_dynic_site(const void* stub)
{
	ppc_emit(dynic_call_word(emit_GetCCPtr(), stub));
	ppc_bx(3, 0, 0);
	ppc_bx(2, 0, 0);
	ppc_bx(1, 0, 0);
}

// Slot 0 still holds its call. A cache clear in the fill's compile can reuse the address; a new site there is
// still fine to fill, as the guard is right at any site.
static inline bool dynic_site_live(const u32* site, const void* stub)
{
	return *site == dynic_call_word(site, stub);
}

// Guard: djump == pc -> b target, else bne to site+16. cmplwi reads only the low word, since pinned values can
// carry a junk high word into djump.
static inline void emit_dynic_guard(u32 t, u32 djump, u32 pc, const void* target)
{
	ppc_xoris(t, djump, pc >> 16);
	ppc_cmpli(ppc_cr0, t, pc & 0xFFFF, 0);
	ppc_bcx(BO_FALSE, BI_CR0_EQ, 2, 0, 0);
	ppc_emit(0x48000000u | ((u32)((const u8*)target - (const u8*)emit_GetCCPtr()) & 0x03FFFFFCu));
}

// At site+16, which only a filled site's miss reaches: one more miss in *ctr through t1 and t2. ctr must lie below
// 2 GB, as addis builds its address from 0.
static inline void emit_dynic_miss_count(u32 t1, u32 t2, const u32* ctr)
{
	const u32 a = (u32)(uintptr_t)ctr;
	const s32 lo = (s16)(a & 0xFFFF);
	ppc_addis(t1, 0, (a - (u32)lo) >> 16);
	ppc_lwz(t2, t1, (u32)lo);
	ppc_addi(t2, t2, 1);
	ppc_stw(t2, t1, (u32)lo);
}

// Fill stub head, entered from slot 0 (LR = site+4, the target pc in a0): a1 = site, and a0 zero-extended, as ELFv1
// callers extend u32 arguments. The driver then calls the C fill and jumps to the code it returns.
static inline void emit_dynic_stub_args(u32 a0, u32 a1)
{
	ppc_mfspr(a1, ppc_spr_lr);
	ppc_addi(a1, a1, (u32)-4);
	ppc_emit(0x78000020u | (a0 << 21) | (a0 << 16));	// clrldi a0,a0,32
}
