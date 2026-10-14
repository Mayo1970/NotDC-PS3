// Row "SH4 MMU Engine" JIT (bundle 20, P38): the C side of ps3_driver.cpp's MMU blocks: JTC misses, the block lookup by
// (vaddr, paddr, mode), a hash per 1 KB code page after an I-cache flush. Translations: sh4_mmu_int.cpp's jx_*.

#include "types.h"
#include "sh4_fullmmu.h"
#include "sh4_mmu_jit.h"
#include "sh4_registers.h"
#include "sh4_if.h"
#include "ccn.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/_vmem.h"
#include "dc/mem/mmu.h"
#include "rec_v2/blockmanager.h"
#include "plugins/plugin_manager.h"

bool g_jit_mmu;
volatile u32 g_jmmu_fault;
JtcEntry g_jtc[4][JTC_N] __attribute__((aligned(128)));

void recSh4_Run();
void recSh4_ClearCache();

static struct
{
	u32 lookups, fetch_exc, compiles, stale, fd_compile, fills, slow_r, slow_w, data_exc, raises, ifb_exc, ldtlb,
	    drops, flushes, epochs, hashed, changed, at_edges;
} s_js, s_js_last;

bool jmmu_at() { return MMUCR_AT(CCN_MMUCR.reg_data) != 0; }

u32 jmmu_cfg()
{
	return (sr.MD ? 1u : 0u) | ((fpscr.full >> 17) & 0xCu) | ((fpscr.full & 3u) == 1u ? 16u : 0u);
}

// ---- the JTC: slots filled from a UTLB entry are listed under it, so a write of that entry drops them ----
#define JTC_OWN 48
static u16 s_own[64][JTC_OWN];
static u8 s_own_n[64];     // JTC_OWN + 1: the list overflowed, its drop flushes every table
static u32 s_jtc_asid;     // PTEH.ASID of the listed slots

static void jtc_flush_all()
{
	memset(g_jtc, 0xFF, sizeof(g_jtc));
	memset(s_own_n, 0, sizeof(s_own_n));
	s_js.flushes++;
}

static void jtc_drop(u32 e)
{
	const u32 n = s_own_n[e];
	if (n > JTC_OWN)
	{
		jtc_flush_all();
		return;
	}
	for (u32 i = 0; i < n; i++)
	{
		const u32 s = s_own[e][i];
		g_jtc[s >> 10][s & (JTC_N - 1)].tag = 0xFFFFFFFF;
	}
	s_own_n[e] = 0;
	s_js.drops++;
}

// Every translated slot: a new ASID or MMUCR (TI, SV), or a TLB array write. Untranslated slots (P1, P2) stay.
static void jtc_drop_owned()
{
	for (u32 e = 0; e < 64; e++)
		if (s_own_n[e])
			jtc_drop(e);
}

static void jtc_asid_check()
{
	if (CCN_PTEH.ASID != s_jtc_asid)
	{
		s_jtc_asid = CCN_PTEH.ASID;
		jtc_drop_owned();
	}
}

// The host address of pa's 1 KB page, or 0: a handler, or a mirror under 1 KB (the store queues).
static u32 jtc_host(u32 pa)
{
	const uintptr_t iirf = (uintptr_t)_vmem_MemInfo_ptr[pa >> 24];
	const uintptr_t ptr = iirf & ~(uintptr_t)0x1F;
	const u32 shift = (u32)(iirf & 0x1F);
	if (!ptr || shift > 22)
		return 0;
	const u32 page = pa & ~0x3FFu;
	const uintptr_t host = ptr + ((page << shift) >> shift);
	return host >> 32 ? 0 : (u32)host;
}

static void jtc_fill(u32 va, u32 pa, u32 w, int ent)
{
	const u32 host = jtc_host(pa);
	if (!host)
		return;
	const u32 t = (sr.MD ? 2u : 0u) + w, idx = (va >> 10) & (JTC_N - 1);
	g_jtc[t][idx].base = host;
	g_jtc[t][idx].tag = va & ~0x3FFu;
	s_js.fills++;
	if (ent < 0)
		return;
	const u16 slot = (u16)((t << 10) | idx);
	u8& n = s_own_n[ent];
	if (n > JTC_OWN)
		return;
	for (u32 i = 0; i < n; i++)
		if (s_own[ent][i] == slot)
			return;
	if (n == JTC_OWN)
		n = JTC_OWN + 1;
	else
		s_own[ent][n++] = slot;
}

// ---- the code pages: a block holds its page's generation, which a changed hash after an I-cache flush moves ----
struct JPage { u64 hash; u32 epoch, gen; };
static JPage s_jpage[RAM_SIZE >> 10];
static u32 s_jepoch = 1;   // 0: a page never hashed

static u64 jpage_hash(u32 pa)
{
	const u32* p = (const u32*)&mem_b[(pa & RAM_MASK) & ~0x3FFu];
	u64 a = 0xCBF29CE484222325ull, b = 0x9E3779B97F4A7C15ull;
	for (u32 i = 0; i < 256; i += 2)
	{
		a = (a ^ p[i]) * 0x100000001B3ull;
		b = (b ^ p[i + 1]) * 0x100000001B3ull;
	}
	return a ^ (b << 1);
}

// RAM pages only: the BIOS is ROM.
static u32 jpage_gen(u32 pa)
{
	if (((pa >> 26) & 7) != 3 || (pa >> 29) == 7)
		return 0;
	JPage& p = s_jpage[(pa & RAM_MASK) >> 10];
	if (p.epoch != s_jepoch)
	{
		const u64 h = jpage_hash(pa);
		if (p.epoch && h != p.hash)
		{
			p.gen++;
			s_js.changed++;
		}
		p.hash = h;
		p.epoch = s_jepoch;
		s_js.hashed++;
	}
	return p.gen;
}

// ---- the slow paths of a JTC miss ----
// A register write that changes translations, or an I-cache array write (an I-cache flush, as CCR.ICI).
static void jmmu_after_write(u32 pa)
{
	const u32 top = pa >> 24, a = pa & 0x1FFFFFFF;
	if (top == 0xF0 || top == 0xF1)
	{
		s_jepoch++;
		s_js.epochs++;
	}
	else if ((top & 0xFA) == 0xF2 || a == 0x1F000010)
		jtc_drop_owned();
	else if (a == 0x1F000000)
		jtc_asid_check();
}

template<u32 sz> static inline bool jmmu_xlat(u32 va, u32 w, u32& pa)
{
	// Privileged and aligned in P1, P2 or P4: xlat_data returns va as it is, so no setjmp (the P4 registers never fill).
	const u32 region = va >> 29;
	if (sr.MD && (region == 4 || region == 5 || region == 7) && !(va & ((sz > 4 ? 4 : sz) - 1)))
	{
		pa = va;
		if (w)
			s_js.slow_w++;
		else
			s_js.slow_r++;
		if (jmmu_at())
			jtc_fill(va, pa, w, -1);
		return true;
	}
	int ent;
	const u32 code = jx_data_xlat(va, sz, w, &pa, &ent);
	if (code)
	{
		g_jmmu_fault = code;
		s_js.data_exc++;
		return false;
	}
	if (w)
		s_js.slow_w++;
	else
		s_js.slow_r++;
	if (jmmu_at())
		jtc_fill(va, pa, w, ent);
	return true;
}

u32 jmmu_rd8(u32 va)  { u32 pa; return jmmu_xlat<1>(va, 0, pa) ? _vmem_ReadMem8(pa) : 0; }
u32 jmmu_rd16(u32 va) { u32 pa; return jmmu_xlat<2>(va, 0, pa) ? _vmem_ReadMem16(pa) : 0; }
u32 jmmu_rd32(u32 va) { u32 pa; return jmmu_xlat<4>(va, 0, pa) ? _vmem_ReadMem32(pa) : 0; }
u64 jmmu_rd64(u32 va) { u32 pa; return jmmu_xlat<8>(va, 0, pa) ? _vmem_ReadMem64(pa) : 0; }

void jmmu_wr8(u32 va, u32 v)
{
	u32 pa;
	if (jmmu_xlat<1>(va, 1, pa))
	{
		_vmem_WriteMem8(pa, (u8)v);
		jmmu_after_write(pa);
	}
}

void jmmu_wr16(u32 va, u32 v)
{
	u32 pa;
	if (jmmu_xlat<2>(va, 1, pa))
	{
		_vmem_WriteMem16(pa, (u16)v);
		jmmu_after_write(pa);
	}
}

void jmmu_wr32(u32 va, u32 v)
{
	u32 pa;
	if (jmmu_xlat<4>(va, 1, pa))
	{
		_vmem_WriteMem32(pa, v);
		jmmu_after_write(pa);
	}
}

void jmmu_wr64(u32 va, u64 v)
{
	u32 pa;
	if (jmmu_xlat<8>(va, 1, pa))
	{
		_vmem_WriteMem64(pa, v);
		jmmu_after_write(pa);
	}
}

u32 jmmu_ifb(u32 op, u32 epc, u32 slot)
{
	const u32 taken = jx_ifb(op, epc, slot);
	s_js.ifb_exc += taken;
	jtc_asid_check();
	return taken;
}

// ldtlb: the JTC slots of the old entry go. The JTC's hits do not step URC as the UTLB accesses they stand for
// would, so each ldtlb steps it (wrapping at URB): the next miss loads another entry.
void jmmu_utlb_changed(u32 entry)
{
	if (!g_jit_mmu)
		return;
	s_js.ldtlb++;
	jtc_drop(entry & 63);
	const u32 v = CCN_MMUCR.reg_data;
	u32 urc = (MMUCR_URC(v) + 1) & 63;
	if (urc == MMUCR_URB(v))
		urc = 0;
	CCN_MMUCR.reg_data = (v & ~(63u << 10)) | (urc << 10);
}

// ---- the main loop's lookup ----
static u32 s_last_at;

void* jmmu_lookup(u32 pc)
{
	next_pc = pc;
	s_js.lookups++;
	// An AT edge: the blocks of before compiled their accesses for the other state (MMUCR Bits OLD has no clear).
	const u32 at = MMUCR_AT(CCN_MMUCR.reg_data);
	if (at != s_last_at)
	{
		s_last_at = at;
		s_js.at_edges++;
		jtc_flush_all();
		recSh4_ClearCache();
	}
	u32 pa;
	const u32 code = jx_fetch(pc, &pa);
	if (code)
	{
		g_jmmu_fault = code;
		s_js.fetch_exc++;
		return 0;
	}
	const u32 cfg = jmmu_cfg(), gen = jpage_gen(pa);
	DynarecBlock* b = bm_GetBlockMmu(pc, pa, cfg);
	if (b)
	{
		if (b->pgen == gen)
		{
			b->lookups++;
			return (void*)b->code;
		}
		s_js.stale++;
	}
	s_js.compiles++;
	DynarecCodeEntry* rv = rdv_CompilePCMmu(pc, pa, cfg, gen);
	if (rv)
		return (void*)rv;
	// SR.FD with an FPU op: the exception first, so its handler sets FPSCR before the block compiles (Flycast).
	g_jmmu_fault = g_dec_fpu_disabled ? 0x800 : 0;
	s_js.fd_compile += g_dec_fpu_disabled;
	return 0;
}

// Code 0 is a block that does not compile (its FATAL line printed): the SH4 spins on it.
u32 jmmu_raise(u32 epc)
{
	const u32 code = g_jmmu_fault;
	g_jmmu_fault = 0;
	if (!code)
	{
		next_pc = epc;
		return epc;
	}
	s_js.raises++;
	jx_take(epc, code);
	return next_pc;
}

// ---- the choice at each reset ----
// sh4_cpu.ResetCache with the MMU JIT: an I-cache flush moves the epoch; the code pages are checked when next run.
static void jmmu_reset_cache()
{
	s_jepoch++;
	s_js.epochs++;
}

bool jmmu_can_run() { return settings.dynarec.Enable; }

void jmmu_select(bool on)
{
	g_jit_mmu = on;
	jtc_flush_all();
	memset(s_jpage, 0, sizeof(s_jpage));
	s_jepoch = 1;
	s_last_at = MMUCR_AT(CCN_MMUCR.reg_data);
	s_jtc_asid = CCN_PTEH.ASID;
	g_jmmu_fault = 0;
	memset(&s_js, 0, sizeof(s_js));
	s_js_last = s_js;
	if (on)
	{
		sh4_cpu.Run = recSh4_Run;
		sh4_cpu.ResetCache = jmmu_reset_cache;
		printf("[fullmmu] MMU JIT: blocks by (vaddr, paddr, mode) in 1 KB pages, JTC %u x 4, JIT RM 2/3 ACCEPT; AT 1 blocks"
		       " without fastmem, SQ shapes, constprop, DVE, DYN_IC and BCACHE\n", (u32)JTC_N);
	}
	else if (sh4_cpu.ResetCache == jmmu_reset_cache)
		sh4_cpu.ResetCache = recSh4_ClearCache;
	ngen_mmu_dispatch(on);
}

// Per second: lookups (block ends), compiles, stale (a changed page), FPU off at a compile, fills and slow paths,
// exceptions (fetch, data, ifb, all taken), ldtlb, slot drops and flushes, I-cache flushes, pages hashed and changed.
void jmmu_stats(double seconds)
{
	if (!g_jit_mmu || seconds <= 0.0)
		return;
	jx_stats_print();
	if (!memcmp(&s_js, &s_js_last, sizeof(s_js)))
		return;
#define D(f) (s_js.f - s_js_last.f)
	printf("[jmmu] lookups %u, compiles %u, stale %u, FPU off %u | fills %u, slow r %u w %u | exc fetch %u data %u"
	       " ifb %u taken %u | ldtlb %u, drops %u, flushes %u | I-cache %u, pages hashed %u changed %u, AT edges %u\n",
	       D(lookups), D(compiles), D(stale), D(fd_compile), D(fills), D(slow_r), D(slow_w), D(fetch_exc), D(data_exc),
	       D(ifb_exc), D(raises), D(ldtlb), D(drops), D(flushes), D(epochs), D(hashed), D(changed), D(at_edges));
#undef D
	s_js_last = s_js;
}
