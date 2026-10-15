// Row "SH4 MMU" (plan section 7, bundle 20, P38): the interpreter with the full SH4 MMU, for Windows CE discs. UTLB and
// ITLB lookups, TLB miss, protection, address error, FPU-disabled and illegal-op exceptions; a faulting op leaves by longjmp.

#include "types.h"
#include "dc/pvr/pvr_if.h"
#include "sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/mem/sb.h"
#include "sh4_registers.h"
#include "sh4_opcode_list.h"
#include "ccn.h"
#include "intc.h"
#include "tmu.h"
#include "dc/gdrom/gdrom_if.h"
#include "plugins/plugin_manager.h"
#include "ps3/ps3_presets.h"
#include "ps3_timebase.h"
#include "sh4_fullmmu.h"

// The two preludes as sh4_cached.cpp takes them: the FPU set first, its GetN spellings dropped before the CPU set.
#include "sh4_fpu_prelude.h"
#undef GetN
#undef GetM
#undef GetImm4
#undef GetImm8
#undef GetImm12
#include "sh4_cpu_prelude.h"
#include "dc/mem/mmu.h"

bool g_disc_wince;
bool g_fullmmu_active;

// ---- exceptions ----
// __builtin_setjmp, not libc's: -mno-altivec makes jmp_buf 256 bytes while libc's setjmp stores 512.
static void* s_jb[5];
static u32 s_exc;           // the code mmu_raise() left for the landing
static u32 s_op_pc;         // the op the main loop runs: SPC of any exception, also from its delay slot
static bool s_in_delay;     // an exception from a delay slot: FPU-disabled and illegal take their slot codes
static volatile s32 s_l;    // the slice budget; in memory, so it holds across the longjmp
static s32 s_slice, s_ratio;

static struct
{
	u32 miss_r, miss_w, miss_i, prot, firstw, addr, mhit, fpu, illegal, other;
	u32 ldtlb, scans, hits, ifills, ihits, sq;
} s_st, s_st_last;
static u32 s_exc_logs;

static void __attribute__((noinline, noreturn)) mmu_raise(u32 code)
{
	s_exc = code;
	__builtin_longjmp(s_jb, 1);
}

// ---- translation (Flycast mmu.cpp, the full form; FAST_MMU's entry cache and WinCE hack are not taken) ----
enum { MX_MISS, MX_MHIT, MX_PROT, MX_FIRSTWRITE, MX_BADADDR };
enum { AC_R, AC_W, AC_I };
static const u32 k_page_mask[4] = { 0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u };

// The manual's MMUCR bits (P58): the bitfield reads wrong bits on big endian.
static inline bool at_on() { return MMUCR_AT(CCN_MMUCR.reg_data) != 0; }

static void __attribute__((noinline, noreturn)) mmu_fault(u32 err, u32 va, u32 acc)
{
	CCN_TEA = va;
	CCN_PTEH.VPN = va >> 10;
	u32 code;
	switch (err)
	{
	case MX_MISS:
		code = acc == AC_W ? 0x060 : 0x040;
		if (acc == AC_W)
			s_st.miss_w++;
		else if (acc == AC_I)
			s_st.miss_i++;
		else
			s_st.miss_r++;
		break;
	case MX_MHIT:       code = 0x140; s_st.mhit++; break;
	case MX_PROT:       code = acc == AC_W ? 0x0C0 : 0x0A0; s_st.prot++; break;
	case MX_FIRSTWRITE: code = 0x080; s_st.firstw++; break;
	default:            code = acc == AC_W ? 0x100 : 0x0E0; s_st.addr++; break;
	}
	mmu_raise(code);
}

// MMUCR.URC counts UTLB accesses; it wraps at URB, or at 64 (the manual, Flycast mmu_full_lookup).
static inline void urc_step()
{
	const u32 v = CCN_MMUCR.reg_data;
	u32 urc = (MMUCR_URC(v) + 1) & 63;
	if (urc == MMUCR_URB(v))
		urc = 0;
	CCN_MMUCR.reg_data = (v & ~(63u << 10)) | (urc << 10);
}

static inline u32 tlb_mask(const TLB_Entry& e) { return k_page_mask[e.Data.SZ1 * 2 + e.Data.SZ0]; }

// Flycast mmu_match: valid, the VPN at the page size, and the ASID unless shared or MD with MMUCR.SV.
static inline bool tlb_match(const TLB_Entry& e, u32 va)
{
	if (!e.Data.V)
		return false;
	const u32 mask = tlb_mask(e);
	if (((e.Address.VPN << 10) & mask) != (va & mask))
		return false;
	const bool need_asid = !e.Data.SH && (!sr.MD || !MMUCR_SV(CCN_MMUCR.reg_data));
	return !need_asid || e.Address.ASID == CCN_PTEH.ASID;
}

static inline u32 tlb_phys(const TLB_Entry& e, u32 va)
{
	const u32 mask = tlb_mask(e);
	return ((e.Data.PPN << 10) & mask) | (va & ~mask);
}

// The UTLB entry of va: its index, -1 for none, -2 for two or more.
static int utlb_lookup(u32 va)
{
	urc_step();
	s_st.scans++;
	int hit = -1;
	for (int i = 0; i < 64; i++)
		if (tlb_match(UTLB[i], va))
		{
			if (hit >= 0)
				return -2;
			hit = i;
		}
	return hit;
}

// The last entry each access kind used, valid while the entry's words, ASID, MD and SV are the same.
struct XCache { u32 vpage, mask, ppage, idx, aword, dword, asid, md, sv; bool valid; };
static XCache s_xc[3];   // AC_R, AC_W, AC_I

static inline bool xc_hit(const XCache& c, const TLB_Entry* tlb, u32 va)
{
	return c.valid && (va & c.mask) == c.vpage && tlb[c.idx].Address.reg_data == c.aword &&
	       tlb[c.idx].Data.reg_data == c.dword && CCN_PTEH.ASID == c.asid && sr.MD == c.md &&
	       MMUCR_SV(CCN_MMUCR.reg_data) == c.sv;
}

static inline void xc_fill(XCache& c, const TLB_Entry* tlb, u32 idx, u32 va)
{
	const TLB_Entry& e = tlb[idx];
	c.mask = tlb_mask(e);
	c.vpage = va & c.mask;
	c.ppage = (e.Data.PPN << 10) & c.mask;
	c.idx = idx;
	c.aword = e.Address.reg_data;
	c.dword = e.Data.reg_data;
	c.asid = CCN_PTEH.ASID;
	c.md = sr.MD;
	c.sv = MMUCR_SV(CCN_MMUCR.reg_data);
	c.valid = true;
}

// ---- row MMU Model FAST (Flycast FAST_MMU): each entry ldtlb loads stays until MMUCR.TI or a TLB array write; no ITLB,
// protection, first-write, multi-hit or URC count per access. A full table starts again empty.
bool g_mmu_fast;
#define FAST_N 32768u
#define FAST_B 8192u
struct FEnt { u32 base, ppn; u16 next; u8 sz, asid, sh, pad[3]; };
static FEnt s_fe[FAST_N];
static u16 s_fb[FAST_B];       // bucket heads, 0xFFFF = none
static u32 s_fn, s_fsz[4];     // entries in use, and per page size
static u32 s_fgen = 1;         // moves at a remap and a flush: the FCache entries of before go
static struct { u32 finds, misses, inserts, remaps, flushes, full; } s_fs;

static inline u32 fast_bucket(u32 base, u32 sz) { return ((base >> 10) ^ (base >> 20) ^ (sz << 11)) & (FAST_B - 1); }

static void fast_clear()
{
	memset(s_fb, 0xFF, sizeof(s_fb));
	s_fn = 0;
	memset(s_fsz, 0, sizeof(s_fsz));
	s_fgen++;
}

static void fast_flush()
{
	fast_clear();
	s_fs.flushes++;
	jmmu_fast_changed();
}

// The entry of va for this ASID, MD and SV, or -1. 4 KB first: Windows CE maps its pages at that size.
static int fast_find(u32 va)
{
	static const u8 k_order[4] = { 1, 3, 2, 0 };
	const bool any = sr.MD && MMUCR_SV(CCN_MMUCR.reg_data);
	const u32 asid = CCN_PTEH.ASID;
	s_fs.finds++;
	for (u32 k = 0; k < 4; k++)
	{
		const u32 sz = k_order[k];
		if (!s_fsz[sz])
			continue;
		const u32 base = va & k_page_mask[sz];
		for (u32 i = s_fb[fast_bucket(base, sz)]; i != 0xFFFF; i = s_fe[i].next)
		{
			const FEnt& e = s_fe[i];
			if (e.base == base && e.sz == sz && (e.sh || any || e.asid == asid))
				return (int)i;
		}
	}
	s_fs.misses++;
	return -1;
}

// ldtlb's entry joins the table. One of the same page, size and ASID (or both shared) takes the new frame: a remap.
static void fast_insert(const TLB_Entry& t)
{
	if (!t.Data.V)
		return;
	const u32 sz = t.Data.SZ1 * 2 + t.Data.SZ0, mask = k_page_mask[sz];
	const u32 base = (t.Address.VPN << 10) & mask, ppn = (t.Data.PPN << 10) & mask;
	const u32 asid = t.Address.ASID, sh = t.Data.SH;
	const u32 b = fast_bucket(base, sz);
	s_fs.inserts++;
	for (u32 i = s_fb[b]; i != 0xFFFF; i = s_fe[i].next)
	{
		FEnt& e = s_fe[i];
		if (e.base == base && e.sz == sz && e.sh == sh && (sh || e.asid == asid))
		{
			if (e.ppn != ppn)
			{
				e.ppn = ppn;
				s_fgen++;
				s_fs.remaps++;
				jmmu_fast_changed();
			}
			return;
		}
	}
	if (s_fn == FAST_N)
	{
		s_fs.full++;
		fast_flush();
	}
	FEnt& e = s_fe[s_fn];
	e.base = base;
	e.ppn = ppn;
	e.sz = (u8)sz;
	e.asid = (u8)asid;
	e.sh = (u8)sh;
	e.next = s_fb[b];
	s_fb[b] = (u16)s_fn;
	s_fn++;
	s_fsz[sz]++;
}

// The last page each access kind used (read, write, fetch, pref), while the table, ASID, MD and SV are the same.
enum { AC_SQ = 3 };
struct FCache { u32 vpage, mask, ppage, gen, asid; u8 md, sv, valid; };
static FCache s_fc[4];

static inline bool fc_hit(const FCache& c, u32 va)
{
	return c.valid && (va & c.mask) == c.vpage && c.gen == s_fgen && c.asid == CCN_PTEH.ASID && c.md == sr.MD &&
	       c.sv == MMUCR_SV(CCN_MMUCR.reg_data);
}

// The frame of va from the cache or the table, with no exception: false = not in the table.
static bool fast_try(u32 va, u32 acc, u32& pa)
{
	FCache& c = s_fc[acc];
	if (!fc_hit(c, va))
	{
		const int i = fast_find(va);
		if (i < 0)
			return false;
		const FEnt& e = s_fe[i];
		c.mask = k_page_mask[e.sz];
		c.vpage = va & c.mask;
		c.ppage = e.ppn;
		c.gen = s_fgen;
		c.asid = CCN_PTEH.ASID;
		c.md = sr.MD;
		c.sv = MMUCR_SV(CCN_MMUCR.reg_data);
		c.valid = 1;
	}
	pa = c.ppage | (va & ~c.mask);
	return true;
}

static u32 fast_xlat(u32 va, u32 acc)
{
	u32 pa;
	if (!fast_try(va, acc, pa))
		mmu_fault(MX_MISS, va, acc == AC_SQ ? (u32)AC_R : acc);
	return pa;
}

// A TLB array write leaves the table with what the UTLB holds after it: an entry the write added or took away.
void jx_fast_after_write(u32 pa, u32 v)
{
	if ((pa & 0x1FFFFFFF) == 0x1F000010 && (v & MMUCR_TI_BIT))
		fast_flush();
	else if (((pa >> 24) & 0xFA) == 0xF2)
	{
		fast_flush();
		for (u32 i = 0; i < 64; i++)
			fast_insert(UTLB[i]);
	}
}

// A data access with MMUCR.AT on: the physical address, or an exception.
template<u32 acc>
static u32 xlat_data(u32 va, u32 size)
{
	if (va & ((size > 4 ? 4 : size) - 1))
		mmu_fault(MX_BADADDR, va, acc);
	if (!sr.MD && (va & 0x80000000))
	{
		// User mode reaches only the store queues, and only with MMUCR.SQMD 0.
		if ((va & 0xFC000000) != 0xE0000000 || MMUCR_SQMD(CCN_MMUCR.reg_data))
			mmu_fault(MX_BADADDR, va, acc);
		return va;
	}
	const u32 region = va >> 29;
	// The on-chip RAM (7C-7F), P1, P2 and P4 are not translated; P0/U0 and P3 are.
	if ((va & 0xFC000000) == 0x7C000000 || region == 4 || region == 5 || region == 7)
		return va;
	if (g_mmu_fast)
	{
		const u32 fpa = fast_xlat(va, acc);
		return (fpa & 0x1C000000) == 0x1C000000 ? fpa | 0xF0000000 : fpa;
	}
	XCache& c = s_xc[acc];
	u32 pa;
	if (xc_hit(c, UTLB, va))
	{
		urc_step();
		s_st.hits++;
		pa = c.ppage | (va & ~c.mask);
	}
	else
	{
		const int i = utlb_lookup(va);
		if (i < 0)
			mmu_fault(i == -1 ? MX_MISS : MX_MHIT, va, acc);
		const TLB_Entry& e = UTLB[i];
		// PR bit 1: user mode may access; bit 0: writable. D 0: the first write raises the initial page write.
		if (!(e.Data.PR & 2) && !sr.MD)
			mmu_fault(MX_PROT, va, acc);
		if (acc == AC_W)
		{
			if (!(e.Data.PR & 1))
				mmu_fault(MX_PROT, va, acc);
			if (!e.Data.D)
				mmu_fault(MX_FIRSTWRITE, va, acc);
		}
		xc_fill(c, UTLB, (u32)i, va);
		pa = tlb_phys(e, va);
	}
	// Physical area 7 (1C-1F) is the P4 register area (Flycast b3de6a166).
	if ((pa & 0x1C000000) == 0x1C000000)
		pa |= 0xF0000000;
	return pa;
}

// ITLB replacement by MMUCR.LRUI (Flycast's tables): the entry each LRUI value replaces, and the update on a use.
static const u32 k_lru_or[4]  = { 0x00, 0x20, 0x14, 0x0B };
static const u32 k_lru_and[4] = { 0x07, 0x39, 0x3E, 0x3F };
static u32 s_lru_use[64];

static void lru_init()
{
	for (u32 i = 0; i < 64; i++)
		s_lru_use[i] = 0xFFFFFFFFu;
	for (u32 e = 0; e < 4; e++)
	{
		const u32 key = ~k_lru_and[e] & 0x3F, mask = key | k_lru_or[e];
		for (u32 i = 0; i < 64; i++)
			if ((i & mask) == key)
				s_lru_use[i] = e;
	}
}

static inline void lru_use(u32 e)
{
	const u32 v = CCN_MMUCR.reg_data;
	const u32 lrui = (MMUCR_LRUI(v) & k_lru_and[e]) | k_lru_or[e];
	CCN_MMUCR.reg_data = (v & ~(63u << 26)) | (lrui << 26);
}

// An instruction fetch with AT on: the ITLB, filled from the UTLB on a miss (Flycast mmu_instruction_lookup).
static u32 xlat_inst(u32 va)
{
	if ((va & 1) || (!sr.MD && (va & 0x80000000)) || (va >> 29) == 7)
		mmu_fault(MX_BADADDR, va, AC_I);
	const u32 region = va >> 29;
	if (region == 4 || region == 5)
		return va;
	if (g_mmu_fast)
		return fast_xlat(va, AC_I);
	XCache& c = s_xc[AC_I];
	if (xc_hit(c, ITLB, va))
	{
		s_st.ihits++;
		lru_use(c.idx);   // software may have written LRUI since the last fetch
		return c.ppage | (va & ~c.mask);
	}
	int hit = -1;
	for (int i = 0; i < 4; i++)
		if (tlb_match(ITLB[i], va))
		{
			if (hit >= 0)
				mmu_fault(MX_MHIT, va, AC_I);
			hit = i;
		}
	if (hit < 0)
	{
		const int u = utlb_lookup(va);
		if (u < 0)
			mmu_fault(u == -1 ? MX_MISS : MX_MHIT, va, AC_I);
		hit = (int)s_lru_use[MMUCR_LRUI(CCN_MMUCR.reg_data)];
		if (hit < 0 || hit > 3)
			hit = 0;
		ITLB[hit] = UTLB[u];
		s_st.ifills++;
	}
	lru_use((u32)hit);
	const TLB_Entry& e = ITLB[hit];
	if (!(e.Data.PR & 2) && !sr.MD)
		mmu_fault(MX_PROT, va, AC_I);
	xc_fill(c, ITLB, (u32)hit, va);
	return tlb_phys(e, va);
}

static inline u16 mmu_ifetch(u32 va)
{
	return _vmem_ReadMem16(at_on() ? xlat_inst(va) : va);
}

// ---- the op bodies' memory: AT off is the plain interpreter's _vmem path ----
// The last data read, for the diagnostic lines: what a wait loop polls.
static u32 s_rd_va, s_rd_val;
static u8  mmu_rd8(u32 a)  { s_rd_va = a; const u8 v = _vmem_ReadMem8(at_on() ? xlat_data<AC_R>(a, 1) : a); s_rd_val = v; return v; }
static u16 mmu_rd16(u32 a) { s_rd_va = a; const u16 v = _vmem_ReadMem16(at_on() ? xlat_data<AC_R>(a, 2) : a); s_rd_val = v; return v; }
static u32 mmu_rd32(u32 a) { s_rd_va = a; const u32 v = _vmem_ReadMem32(at_on() ? xlat_data<AC_R>(a, 4) : a); s_rd_val = v; return v; }
static u64 mmu_rd64(u32 a) { return _vmem_ReadMem64(at_on() ? xlat_data<AC_R>(a, 8) : a); }
// MMU Model FAST: a write that flushes the table. MMUCR and the TLB arrays are reached untranslated, so va serves.
static inline void mmu_wrote(u32 a, u32 v) { if (g_mmu_fast) jx_fast_after_write(a, v); }
static void mmu_wr8(u32 a, u8 v)   { _vmem_WriteMem8(at_on() ? xlat_data<AC_W>(a, 1) : a, v); mmu_wrote(a, 0); }
static void mmu_wr16(u32 a, u16 v) { _vmem_WriteMem16(at_on() ? xlat_data<AC_W>(a, 2) : a, v); mmu_wrote(a, 0); }
static void mmu_wr32(u32 a, u32 v) { _vmem_WriteMem32(at_on() ? xlat_data<AC_W>(a, 4) : a, v); mmu_wrote(a, v); }
static void mmu_wr64(u32 a, u64 v) { _vmem_WriteMem64(at_on() ? xlat_data<AC_W>(a, 8) : a, v); mmu_wrote(a, 0); }

#undef ReadMem8
#undef ReadMem16
#undef IReadMem16
#undef ReadMem32
#undef ReadMem64
#undef WriteMem8
#undef WriteMem16
#undef WriteMem32
#undef WriteMem64
#define ReadMem8   mmu_rd8
#define ReadMem16  mmu_rd16
#define ReadMem32  mmu_rd32
#define ReadMem64  mmu_rd64
#define WriteMem8  mmu_wr8
#define WriteMem16 mmu_wr16
#define WriteMem32 mmu_wr32
#define WriteMem64 mmu_wr64

// ---- the FPU-disabled test: SR.FD and an FPU op (Flycast's UsesFPU list: the Fxxx ops, lds/sts FPUL and FPSCR) ----
static u8 s_fpu_op[0x10000 / 8];
static inline bool fpu_op(u32 op) { return (s_fpu_op[op >> 3] >> (op & 7)) & 1; }

static OpCallFP* s_ops[0x10000];

namespace mmu_int
{
	void ExecuteDelayslot();
	void ExecuteDelayslot_RTE();

	// The op bodies of sh4_cpu.cpp and sh4_fpu.cpp, with the memory macros above.
	#include "sh4_cpu_movs.h"
	#include "sh4_cpu_branch.h"
	#include "sh4_cpu_arith.h"
	#include "sh4_cpu_logic.h"
	#include "sh4_cpu_misc.h"

	#undef iNimp
	#undef iWarn
	#undef GetN
	#undef GetM
	#undef GetImm4
	#undef GetImm8
	#undef GetImm12
	#define GetN(str) ((str >> 8) & 0xf)
	#define GetM(str) ((str >> 4) & 0xf)
	#define GetImm4(str) ((str >> 0) & 0xf)
	#define GetImm8(str) ((str >> 0) & 0xff)
	#define GetImm12(str) ((str >> 0) & 0xfff)

	#include "sh4_fpu_ops.h"

	static inline void run_slot_op(u32 op)
	{
		if (op == 0)
			return;
		if (sr.FD && fpu_op(op))
			mmu_raise(0x800);
		s_ops[op](op);
	}

	// The fetch through the ITLB; an exception keeps SPC at the branch (s_op_pc), the code becomes a slot code.
	void ExecuteDelayslot()
	{
		s_in_delay = true;
		const u32 op = mmu_ifetch(next_pc);
		next_pc += 2;
		run_slot_op(op);
		s_in_delay = false;
	}

	// RTE's slot is fetched with the old MD and runs with the new SR (Flycast ExecuteDelayslot_RTE).
	void ExecuteDelayslot_RTE()
	{
		s_in_delay = true;
		const u32 op = mmu_ifetch(next_pc);
		next_pc += 2;
		sr.SetFull(ssr);
		run_slot_op(op);
		s_in_delay = false;
	}
}

// ---- ops that differ under the MMU ----
#undef GetN
#define GetN(str) ((str >> 8) & 0xF)

// ldtlb with PTEA, as Flycast; no "ldtlb" line per call (Windows CE loads one per TLB miss).
static void FASTCALL mmu_op_ldtlb(u32 op)
{
	const u32 urc = MMUCR_URC(CCN_MMUCR.reg_data);
	UTLB[urc].Data = CCN_PTEL;
	UTLB[urc].Address = CCN_PTEH;
	UTLB[urc].Assistance = CCN_PTEA;
	s_st.ldtlb++;
	if (g_mmu_fast)
		fast_insert(UTLB[urc]);
	if ((UTLB[urc].Address.VPN & (0xFC000000 >> 10)) == (0xE0000000 >> 10))
		UTLB_Sync(urc);
	jmmu_utlb_changed(urc);
}

static void pref_flush(u32 addr, u32 dest)
{
	u32* sq = (u32*)&sq_both[dest & 0x20];
	s_st.sq++;
	if (((addr >> 26) & 7) == 4)
		TAWriteSQ(addr, sq);
	else
		WriteMemBlock_nommu_ptr(addr, sq, 32);
}

// pref to the store queues: with AT on, the UTLB gives the target (Flycast mmu_full_SQ, a read's checks).
static void pref_at(u32 dest)
{
	if ((dest >> 26) != 0x38)
		return;
	if (!at_on())
	{
		do_sqw<false>(dest);
		return;
	}
	if ((dest & 3) || (!sr.MD && MMUCR_SQMD(CCN_MMUCR.reg_data)))
		mmu_fault(MX_BADADDR, dest, AC_R);
	if (g_mmu_fast)
	{
		pref_flush(fast_xlat(dest, AC_SQ) & ~31u, dest);
		return;
	}
	const int i = utlb_lookup(dest);
	if (i < 0)
		mmu_fault(i == -1 ? MX_MISS : MX_MHIT, dest, AC_R);
	if (!(UTLB[i].Data.PR & 2) && !sr.MD)
		mmu_fault(MX_PROT, dest, AC_R);
	const u32 addr = tlb_phys(UTLB[i], dest) & ~31u;
	pref_flush(addr, dest);
}

static void FASTCALL mmu_op_pref(u32 op) { pref_at(r[GetN(op)]); }

static u32 s_illegal_logs;
static void FASTCALL mmu_op_illegal(u32 op)
{
	s_st.illegal++;
	if (s_illegal_logs++ < 8)
		printf("[fullmmu] illegal op %04X at %08X\n", op, s_op_pc);
	mmu_raise(0x180);
}

// ---- tables ----
#define SH4_CACHED_OP(name) extern sh4op(name);
#include "sh4_cached_ops.inc"
#undef SH4_CACHED_OP

static OpCallFP* const k_plain_ops[] = {
#define SH4_CACHED_OP(name) name,
#include "sh4_cached_ops.inc"
#undef SH4_CACHED_OP
};
static OpCallFP* const k_mmu_ops[] = {
#define SH4_CACHED_OP(name) mmu_int::name,
#include "sh4_cached_ops.inc"
#undef SH4_CACHED_OP
};

static bool s_tables;

static void mmu_tables_init()
{
	if (s_tables)
		return;
	s_tables = true;
	lru_init();
	const u32 n = sizeof(k_plain_ops) / sizeof(k_plain_ops[0]);
	u32 last = 0, plain = 0;
	for (u32 op = 0; op < 0x10000; op++)
	{
		OpCallFP* fp = OpPtr[op];
		if (k_plain_ops[last] != fp)
		{
			u32 j = 0;
			while (j < n && k_plain_ops[j] != fp)
				j++;
			if (j == n)
			{
				// A handler missing from sh4_cached_ops.inc runs with no MMU; the count says if one exists.
				s_ops[op] = fp;
				plain++;
				continue;
			}
			last = j;
		}
		s_ops[op] = k_mmu_ops[last];
		if (fp == (OpCallFP*)iNotImplemented)
			s_ops[op] = mmu_op_illegal;
		else if (fp == (OpCallFP*)i0000_0000_0011_1000)
			s_ops[op] = mmu_op_ldtlb;
		else if (fp == (OpCallFP*)i0000_nnnn_1000_0011)
			s_ops[op] = mmu_op_pref;

		const u32 f = op & 0xF0FF;
		const bool fpu = ((op >> 12) == 0xF && fp != (OpCallFP*)iNotImplemented) || f == 0x405A || f == 0x406A ||
		                 f == 0x4056 || f == 0x4066 || f == 0x005A || f == 0x006A || f == 0x4052 || f == 0x4062;
		if (fpu)
			s_fpu_op[op >> 3] |= (u8)(1u << (op & 7));
	}
	printf("[fullmmu] tables: %u op handlers paired, %u opcodes without an MMU copy\n", n, plain);
}

// ---- the engine ----
static void __attribute__((noinline)) mmu_land()
{
	u32 code = s_exc;
	if (s_in_delay)
	{
		if (code == 0x800)
			code = 0x820;
		else if (code == 0x180)
			code = 0x1A0;
		s_in_delay = false;
	}
	if (code == 0x800 || code == 0x820)
		s_st.fpu++;
	if (s_exc_logs++ < 16)
		printf("[fullmmu] exception %03X at %08X, TEA %08X, SR %08X, MMUCR %08X\n", code, s_op_pc, CCN_TEA,
		       sr.GetFull(), CCN_MMUCR.reg_data);
	if (sr.BL)
	{
		// The SH4 resets here; Flycast stops with a fatal error.
		printf("[fullmmu] FATAL: exception %03X at %08X with SR.BL set (TEA %08X): the SH4 stops\n", code, s_op_pc,
		       CCN_TEA);
		sh4_int_bCpuRun = false;
		return;
	}
	Do_Exeption(s_op_pc, code, (code == 0x040 || code == 0x060) ? 0x400 : 0x100);
	// An exception drains the pipeline: about 5 cycles (Flycast).
	s_l -= 5 * s_ratio;
}

// ---- diagnostics (HW run 3, a black screen): where the SH4 runs, what it reads, the interrupt state ----
extern u32 interrupt_vpend, interrupt_vmask, decoded_srimask;
struct PcHit { u32 pc, n, rd_va, rd_val; };
static PcHit s_pcs[256];
static u32 s_pc_samples, s_last_dump_pc, s_dumps;

// One sample per slice end: the op that ended it and the last data read.
static inline void diag_sample(u32 pc)
{
	s_pc_samples++;
	u32 h = ((pc >> 1) * 2654435761u) >> 24;
	for (u32 i = 0; i < 8; i++, h = (h + 1) & 255)
	{
		PcHit& e = s_pcs[h];
		if (e.n == 0 || e.pc == pc)
		{
			e.pc = pc;
			e.n++;
			e.rd_va = s_rd_va;
			e.rd_val = s_rd_val;
			return;
		}
	}
}

// The halfword at a virtual address for the log: RAM only, no exception, no URC step.
static bool mmu_peek16(u32 va, u16& v)
{
	u32 pa = va;
	if (at_on() && (va >> 29) != 4 && (va >> 29) != 5)
	{
		int hit = -1;
		for (int i = 0; i < 64 && hit < 0; i++)
			if (tlb_match(UTLB[i], va))
				hit = i;
		if (hit < 0)
			return false;
		pa = tlb_phys(UTLB[hit], va);
	}
	if ((pa >> 29) == 7 || (pa & 0x1C000000) != 0x0C000000)
		return false;
	v = _vmem_ReadMem16(pa);
	return true;
}

static void diag_print()
{
	if (!s_pc_samples)
		return;
	PcHit top[6] = {};
	for (const PcHit& e : s_pcs)
		for (int k = 0; k < 6 && e.n; k++)
			if (e.n > top[k].n)
			{
				for (int j = 5; j > k; j--)
					top[j] = top[j - 1];
				top[k] = e;
				break;
			}
	char line[512];
	int n = snprintf(line, sizeof(line), "[fullmmu] top pc of %u slices:", s_pc_samples);
	for (int k = 0; k < 6 && top[k].n && n < (int)sizeof(line); k++)
		n += snprintf(line + n, sizeof(line) - n, " %08X %u%% (rd %08X=%08X)", top[k].pc, top[k].n * 100 / s_pc_samples,
		              top[k].rd_va, top[k].rd_val);
	printf("%s\n", line);
	printf("[fullmmu] irq: SR %08X | vpend %08X vmask %08X srimask %08X | holly nrm %08X ext %08X err %08X | iml6 "
	       "%08X %08X iml4 %08X %08X iml2 %08X %08X\n", sr.GetFull(), interrupt_vpend, interrupt_vmask, decoded_srimask,
	       SB_ISTNRM, SB_ISTEXT, SB_ISTERR, SB_IML6NRM, SB_IML6EXT, SB_IML4NRM, SB_IML4EXT, SB_IML2NRM, SB_IML2EXT);
	// The code around a new hottest op, as the JIT's [idle] lines; the first 24 only.
	if (top[0].pc != s_last_dump_pc && s_dumps < 24)
	{
		s_last_dump_pc = top[0].pc;
		s_dumps++;
		printf("[fullmmu] code at %08X:\n", top[0].pc);
		for (u32 pc = top[0].pc - 12; pc != top[0].pc + 12; pc += 2)
		{
			u16 op;
			if (mmu_peek16(pc, op))
				printf("[fullmmu]  %08X %04X %s%s\n", pc, op, OpDesc[op]->diss, pc == top[0].pc ? "   <-" : "");
		}
	}
	memset(s_pcs, 0, sizeof(s_pcs));
	s_pc_samples = 0;
}

// The stats line when a count changed; the JIT form prints it too (jx_stats_print).
static void stats_line()
{
	if (!memcmp(&s_st, &s_st_last, sizeof(s_st)))
		return;
	printf("[fullmmu] AT=%u | TLB miss r %u w %u i %u, prot %u, first write %u, addr err %u, multi %u, FPU off %u, "
	       "illegal %u | ldtlb %u, UTLB scans %u, data hits %u, ITLB fills %u, fetch hits %u, SQ %u\n",
	       (u32)at_on(), s_st.miss_r, s_st.miss_w, s_st.miss_i, s_st.prot, s_st.firstw, s_st.addr, s_st.mhit, s_st.fpu,
	       s_st.illegal, s_st.ldtlb, s_st.scans, s_st.hits, s_st.ifills, s_st.ihits, s_st.sq);
	if (g_mmu_fast)
		printf("[fullmmu] FAST table: entries %u (4K %u, 64K %u, 1M %u, 1K %u) | finds %u, misses %u, inserts %u, remaps %u,"
		       " flushes %u, full %u\n", s_fn, s_fsz[1], s_fsz[2], s_fsz[3], s_fsz[0], s_fs.finds, s_fs.misses, s_fs.inserts,
		       s_fs.remaps, s_fs.flushes, s_fs.full);
	s_st_last = s_st;
}

static void mmu_stats(u64& t_last)
{
	const u64 now = gettime();
	if (now - t_last < ps3_timebase_hz)
		return;
	t_last = now;
	diag_print();
	stats_line();
}

void Sh4_int_Run_Mmu()
{
	Sh4_ApplyAccuracyPreset();
	mmu_tables_init();
	s_slice = sh4_GetCpuTimeslice();
	s_ratio = sh4_GetCpuRatio();
	s_l = s_slice;
	static u64 t_last;
	static u32 slices;
	t_last = gettime();
	sh4_int_bCpuRun = true;
	printf("[fullmmu] the SH4 runs on the MMU interpreter (row SH4 MMU %s)\n", g_full_mmu_preset == 2 ? "ON" : "AUTO");

	if (__builtin_setjmp(s_jb))
		mmu_land();

	while (sh4_int_bCpuRun)
	{
		do
		{
			s_op_pc = next_pc;
			const u32 op = mmu_ifetch(s_op_pc);
			next_pc = s_op_pc + 2;
			if (sr.FD && fpu_op(op))
				mmu_raise(0x800);
			s_ops[op](op);
			s_l -= s_ratio;
		} while (s_l > 0);
		s_l += s_slice;
		diag_sample(s_op_pc);
		UpdateSystem();
		if ((++slices & 1023) == 0)
			mmu_stats(t_last);
	}
	sh4_int_bCpuRun = false;
}

// The MMU engine has no compiled code: an I-cache flush or an AT edge has nothing to drop (the JIT's clear did).
static void mmu_reset_cache() {}

// AUTO: the disc's IP.BIN names Windows CE (Flycast isWindowsCE()); ON: any disc. SH4 MMU Engine JIT keeps the JIT,
// which then compiles MMU blocks (sh4_mmu_jit.cpp); with the interpreter as the menu's engine the MMU interpreter runs.
void sh4_fullmmu_select()
{
	g_fullmmu_active = g_full_mmu_preset == 2 || (g_full_mmu_preset == 1 && g_disc_wince);
	const bool jit = g_fullmmu_active && g_mmu_jit_preset && jmmu_can_run();
	printf("[fullmmu] SH4 MMU %s, Windows CE disc %s: %s\n", g_full_mmu_preset == 2 ? "ON" : g_full_mmu_preset ? "AUTO" : "OFF",
	       g_disc_wince ? "yes" : "no", jit ? "the MMU JIT runs this boot" : g_fullmmu_active ? "the MMU interpreter runs this boot" :
	       "the engine of the menu runs");
	if (g_fullmmu_active && !jit)
	{
		sh4_cpu.Run = Sh4_int_Run_Mmu;
		sh4_cpu.ResetCache = mmu_reset_cache;
	}
	if (jit)
		mmu_tables_init();
	g_mmu_fast = g_fullmmu_active && g_mmu_fast_preset;
	fast_clear();
	memset(&s_fs, 0, sizeof(s_fs));
	if (g_mmu_fast)
		printf("[fullmmu] MMU Model FAST: each TLB entry loaded stays (%u at most) until MMUCR.TI or a TLB array write;"
		       " no ITLB, protection or first-write checks\n", FAST_N);
	g_jmmu_bcache = jit && g_mmu_bcache_preset;
	g_jmmu_sq = jit && g_mmu_sq_preset;
	g_jmmu_pref = jit && g_mmu_pref_preset;
	g_jmmu_asidjtc = jit && g_mmu_fast && g_mmu_asidjtc_preset;
	jmmu_select(jit);
	if (g_fullmmu_active && !g_mmu_bits_preset)
		printf("[fullmmu] WARNING: MMUCR Bits is OLD, so MMUCR.TI flushes are lost: set it to FIXED\n");
}

// ---- the JIT form (row SH4 MMU Engine JIT): what sh4_mmu_jit.cpp takes from this engine ----
// Each entry that can raise sets s_jb itself; __builtin_setjmp stays out of the fast paths (its prologue saves all).

// The addresses xlat_data takes through the UTLB; it returns the others as they are.
static inline bool jx_translated(u32 va)
{
	if (!sr.MD && (va & 0x80000000))
		return false;
	const u32 region = va >> 29;
	return !((va & 0xFC000000) == 0x7C000000 || region == 4 || region == 5 || region == 7);
}

// A data access: 0 and the physical address, or the exception code (TEA and PTEH set). *ent: the UTLB entry used
// (64 for MMU Model FAST's table), or -1 for an address the TLB does not translate.
static u32 __attribute__((noinline)) jx_data_xlat_slow(u32 va, u32 size, u32 write, u32* pa, int* ent)
{
	if (__builtin_setjmp(s_jb))
		return s_exc;
	*ent = -1;
	if (!at_on())
	{
		*pa = va;
		return 0;
	}
	*pa = write ? xlat_data<AC_W>(va, size) : xlat_data<AC_R>(va, size);
	if (jx_translated(va))
		*ent = g_mmu_fast ? 64 : (int)s_xc[write ? AC_W : AC_R].idx;
	return 0;
}

// MMU Model FAST, a page of its table: no setjmp, whose prologue saves every register. Any other access: the slow path.
u32 jx_data_xlat(u32 va, u32 size, u32 write, u32* pa, int* ent)
{
	u32 p;
	if (g_mmu_fast && at_on() && !(va & ((size > 4 ? 4 : size) - 1)) && jx_translated(va) &&
	    fast_try(va, write ? AC_W : AC_R, p))
	{
		*pa = (p & 0x1C000000) == 0x1C000000 ? p | 0xF0000000 : p;
		*ent = 64;
		return 0;
	}
	return jx_data_xlat_slow(va, size, write, pa, ent);
}

// The dispatcher's code translations: s_xc[AC_I]'s check, one per 1 KB page, LRUI as a fetch; FAST: s_fc[AC_I]'s.
static XCache s_jx_ctc[64];
static FCache s_jx_fctc[64];

static u32 __attribute__((noinline)) jx_fetch_slow(u32 va, u32* pa)
{
	if (__builtin_setjmp(s_jb))
		return s_exc;
	*pa = xlat_inst(va);
	return 0;
}

// A block's first fetch: 0 and the physical pc, or the exception code.
u32 jx_fetch(u32 va, u32* pa)
{
	const u32 region = va >> 29;
	if (!at_on() || ((region == 4 || region == 5) && sr.MD && !(va & 1)))
	{
		*pa = va;
		return 0;
	}
	if (g_mmu_fast)
	{
		FCache& f = s_jx_fctc[(va >> 10) & 63];
		const bool plain = !(va & 1) && region != 7 && (sr.MD || !(va & 0x80000000));
		if (plain && fc_hit(f, va))
		{
			s_st.ihits++;
			*pa = f.ppage | (va & ~f.mask);
			return 0;
		}
		u32 p;
		if (plain && fast_try(va, AC_I, p))
		{
			*pa = p;
			f = s_fc[AC_I];
			return 0;
		}
		const u32 fcode = jx_fetch_slow(va, pa);
		if (!fcode && region != 4 && region != 5)
			f = s_fc[AC_I];
		return fcode;
	}
	XCache& c = s_jx_ctc[(va >> 10) & 63];
	if (!(va & 1) && region != 7 && (sr.MD || !(va & 0x80000000)) && xc_hit(c, ITLB, va))
	{
		s_st.ihits++;
		lru_use(c.idx);
		*pa = c.ppage | (va & ~c.mask);
		return 0;
	}
	const u32 code = jx_fetch_slow(va, pa);
	if (!code && region != 4 && region != 5)
		c = s_xc[AC_I];
	return code;
}

// SR.BL set: the SH4 resets there; the JIT spins on the op instead, as the interpreter stops.
static void jx_fatal(u32 epc, u32 code)
{
	static u32 lines;
	if (lines++ < 4)
		printf("[fullmmu] FATAL: exception %03X at %08X with SR.BL set (TEA %08X): the SH4 stops\n", code, epc, CCN_TEA);
	next_pc = epc;
}

// An op the JIT leaves to this engine: 1 = it raised an exception, now taken (next_pc is the vector). epc: the op, or
// the branch of its delay slot.
u32 __attribute__((noinline)) jx_ifb(u32 op, u32 epc, u32 slot)
{
	s_op_pc = epc;
	s_in_delay = slot != 0;
	if (__builtin_setjmp(s_jb))
	{
		// After the longjmp: the statics, not the arguments.
		if (sr.BL)
		{
			jx_fatal(s_op_pc, s_exc);
			s_in_delay = false;
			return 1;
		}
		mmu_land();
		return 1;
	}
	if (sr.FD && fpu_op(op))
		mmu_raise(0x800);
	s_ops[op](op);
	s_in_delay = false;
	return 0;
}

// An exception the JIT found outside an op (a translation, FPU disabled at a block start), taken as mmu_land.
// 0 = SR.BL is set: next_pc stays at epc.
u32 jx_take(u32 epc, u32 code)
{
	if (!sr.BL)
	{
		s_exc = code;
		s_op_pc = epc;
		s_in_delay = false;
		mmu_land();
		return 1;
	}
	jx_fatal(epc, code);
	return 0;
}

static u32 __attribute__((noinline)) jx_pref_slow(u32 dest)
{
	if (__builtin_setjmp(s_jb))
		return s_exc;
	pref_at(dest);
	return 0;
}

// pref from a JIT block (row MMU Pref JIT): 0, or the exception code (TEA, PTEH set). A store queue page in MMU Model
// FAST's table needs no setjmp; any other case is pref_at's.
u32 jx_pref(u32 dest)
{
	u32 p;
	if (g_mmu_fast && at_on() && (dest >> 26) == 0x38 && !(dest & 3) && (sr.MD || !MMUCR_SQMD(CCN_MMUCR.reg_data)) &&
	    fast_try(dest, AC_SQ, p))
	{
		pref_flush(p & ~31u, dest);
		return 0;
	}
	return jx_pref_slow(dest);
}

// The halfword at a virtual address for a log line (no exception): MMU Model FAST's table, else the UTLB; RAM only.
bool jx_peek16(u32 va, u16& v)
{
	u32 pa;
	if (g_mmu_fast && at_on() && (va >> 29) != 4 && (va >> 29) != 5 && !(va & 1) && va < 0xE0000000)
	{
		const int i = fast_find(va);
		if (i < 0)
			return false;
		pa = s_fe[i].ppn | (va & ~k_page_mask[s_fe[i].sz]);
		if ((pa >> 29) == 7 || (pa & 0x1C000000) != 0x0C000000)
			return false;
		v = _vmem_ReadMem16(pa);
		return true;
	}
	return mmu_peek16(va, v);
}

bool jx_fpu_op(u32 op) { return fpu_op(op); }
void jx_stats_print() { stats_line(); }
