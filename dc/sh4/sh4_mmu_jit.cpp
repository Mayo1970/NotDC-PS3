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
#include "ps3_timebase.h"

bool g_jit_mmu;
bool g_jmmu_bcache, g_jmmu_sq, g_jmmu_pref, g_jmmu_asidjtc;
volatile u32 g_jmmu_fault;
JtcEntry g_jtc[4][JTC_N] __attribute__((aligned(128)));
JDisp g_jdisp[JDISP_SETS][2] __attribute__((aligned(128)));
u32 g_jdgen = 1;

void recSh4_Run();
void recSh4_ClearCache();

static struct
{
	u32 lookups, fetch_exc, compiles, stale, fd_compile, fills, slow_r, slow_w, data_exc, raises, ifb_exc, ldtlb,
	    drops, flushes, epochs, hashed, changed, at_edges, bc_fills, bc_evicts, asid, asid_restores, prefs;
} s_js, s_js_last;

// The [jmmu] ms line: one call in 16 timed (two mftb) and counted 16 times; a compile always, and not in its lookup.
static u32 s_tick, s_jdgen_last;
static struct { u64 lookup, compile, rd, wr, ifb, raise, pref; } s_jt, s_jt_last;
struct JtScope
{
	u64* acc;
	u64 t0;
	explicit JtScope(u64& a) : acc((++s_tick & 15) ? nullptr : &a), t0(acc ? gettime() : 0) {}
	~JtScope()
	{
		if (!acc)
			return;
		const u64 d = gettime() - t0;
		if (d < (1ull << 31))
			*acc += d << 4;
	}
};

// The [jmmu] hot line: the top values of a second among the timed calls (the slow reads' physical addresses, the C
// lookups' pcs): what a poll loop reads, which blocks the block cache misses.
struct JHot { u32 v, n; };
static JHot s_hot[2][64];
static void hot_add(u32 t, u32 v)
{
	u32 h = (v * 2654435761u) >> 26;
	for (u32 k = 0; k < 8; k++, h = (h + 1) & 63)
		if (!s_hot[t][h].n || s_hot[t][h].v == v)
		{
			s_hot[t][h].v = v;
			s_hot[t][h].n++;
			return;
		}
}

static int hot_top(u32 t, char* out, int size)
{
	u32 total = 0, best[4] = {}, at[4] = {};
	for (u32 i = 0; i < 64; i++)
	{
		const u32 n = s_hot[t][i].n;
		total += n;
		for (u32 k = 0; k < 4; k++)
			if (n > best[k])
			{
				for (u32 j = 3; j > k; j--)
				{
					best[j] = best[j - 1];
					at[j] = at[j - 1];
				}
				best[k] = n;
				at[k] = i;
				break;
			}
	}
	int m = 0;
	for (u32 k = 0; k < 4 && best[k] && m < size; k++)
		m += snprintf(out + m, size - m, " %08X %u%%", s_hot[t][at[k]].v, best[k] * 100 / total);
	memset(s_hot[t], 0, sizeof(s_hot[t]));
	return m;
}

bool jmmu_at() { return MMUCR_AT(CCN_MMUCR.reg_data) != 0; }

u32 jmmu_cfg()
{
	return (sr.MD ? 1u : 0u) | ((fpscr.full >> 17) & 0xCu) | ((fpscr.full & 3u) == 1u ? 16u : 0u);
}

u32 jmmu_dkey() { return jmmu_cfg() | (CCN_PTEH.reg_data & 0xFF) << 8; }

// ---- the JTC: slots filled from a UTLB entry are listed under it, so a write of that entry drops them ----
// Owner 64: the slots MMU Model FAST's table filled.
#define JTC_OWN 48
#define JTC_OWNERS 65
static u16 s_own[JTC_OWNERS][JTC_OWN];
static u8 s_own_n[JTC_OWNERS];   // JTC_OWN + 1: the list overflowed, its drop flushes every table
static u32 s_jtc_asid;           // PTEH.ASID of the listed slots
static u32 s_last_sv;            // MMUCR.SV of the slots and the block cache

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
	for (u32 e = 0; e < JTC_OWNERS; e++)
		if (s_own_n[e])
			jtc_drop(e);
}

// Row MMU ASID JTC (MMU Model FAST): the JTC of each recent ASID kept, so a process switch and its return keep the pages
// of both. A copy holds while s_jtc_tgen holds: a change of FAST's table, MMUCR.SV, an AT edge, the choice.
#define JTC_SAVES 8
struct JtcSave { u32 asid, tgen, used; JtcEntry jtc[4][JTC_N]; };
static JtcSave s_jsave[JTC_SAVES];
static u32 s_jtc_tgen = 1, s_jsave_clock;

static void jtc_switch(u32 to)
{
	JtcSave* in = nullptr;
	JtcSave* out = nullptr;
	for (u32 i = 0; i < JTC_SAVES; i++)
		if (s_jsave[i].used && s_jsave[i].asid == to && s_jsave[i].tgen == s_jtc_tgen)
			in = &s_jsave[i];
	// The old ASID's copy, else the least recently used one; never the copy that comes back.
	for (u32 i = 0; i < JTC_SAVES; i++)
	{
		JtcSave& c = s_jsave[i];
		if (&c == in)
			continue;
		if (c.used && c.asid == s_jtc_asid)
		{
			out = &c;
			break;
		}
		if (!out || c.used < out->used)
			out = &c;
	}
	out->asid = s_jtc_asid;
	out->tgen = s_jtc_tgen;
	out->used = ++s_jsave_clock;
	memcpy(out->jtc, g_jtc, sizeof(g_jtc));
	if (in)
	{
		memcpy(g_jtc, in->jtc, sizeof(g_jtc));
		in->used = ++s_jsave_clock;
		s_js.asid_restores++;
	}
	else
	{
		memset(g_jtc, 0xFF, sizeof(g_jtc));
		s_js.flushes++;
	}
	// The restored slots are not listed: a drop of owner 64 flushes every table.
	memset(s_own_n, 0, sizeof(s_own_n));
	s_own_n[64] = JTC_OWN + 1;
}

static void jtc_asid_check()
{
	if (CCN_PTEH.ASID != s_jtc_asid)
	{
		s_js.asid++;
		if (g_jmmu_asidjtc)
			jtc_switch(CCN_PTEH.ASID);
		else
			jtc_drop_owned();
		s_jtc_asid = CCN_PTEH.ASID;
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

// An I-cache flush: the code pages are hashed again at their next lookup, so the block cache's entries go.
static void new_epoch()
{
	s_jepoch++;
	s_js.epochs++;
	g_jdgen++;
}

// ---- the slow paths of a JTC miss ----
// MMUCR: FULL drops every translated slot; FAST flushes its table on TI (sh4_mmu_int.cpp) and drops on a new SV.
static void mmucr_written(u32 v)
{
	const u32 sv = MMUCR_SV(CCN_MMUCR.reg_data);
	if (!g_mmu_fast)
		jtc_drop_owned();
	else
	{
		jx_fast_after_write(0x1F000010, v);
		if (sv != s_last_sv)
		{
			jtc_drop_owned();
			g_jdgen++;
			s_jtc_tgen++;
		}
	}
	s_last_sv = sv;
}

// A register write that changes translations, or an I-cache array write (an I-cache flush, as CCR.ICI). v: a 32-bit
// write's value, else 0.
static void jmmu_after_write(u32 pa, u32 v)
{
	const u32 top = pa >> 24, a = pa & 0x1FFFFFFF;
	if (top == 0xF0 || top == 0xF1)
		new_epoch();
	else if ((top & 0xFA) == 0xF2)
	{
		jtc_drop_owned();
		if (g_mmu_fast)
			jx_fast_after_write(pa, v);
	}
	else if (a == 0x1F000010)
		mmucr_written(v);
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

template<u32 sz> static inline u64 jmmu_rd(u32 va)
{
	JtScope ts(s_jt.rd);
	u32 pa;
	if (!jmmu_xlat<sz>(va, 0, pa))
		return 0;
	if (ts.acc)
		hot_add(0, pa);
	return sz == 1 ? _vmem_ReadMem8(pa) : sz == 2 ? _vmem_ReadMem16(pa) : sz == 4 ? _vmem_ReadMem32(pa) : _vmem_ReadMem64(pa);
}

u32 jmmu_rd8(u32 va)  { return (u32)jmmu_rd<1>(va); }
u32 jmmu_rd16(u32 va) { return (u32)jmmu_rd<2>(va); }
u32 jmmu_rd32(u32 va) { return (u32)jmmu_rd<4>(va); }
u64 jmmu_rd64(u32 va) { return jmmu_rd<8>(va); }

void jmmu_wr8(u32 va, u32 v)
{
	JtScope ts(s_jt.wr);
	u32 pa;
	if (jmmu_xlat<1>(va, 1, pa))
	{
		_vmem_WriteMem8(pa, (u8)v);
		jmmu_after_write(pa, 0);
	}
}

void jmmu_wr16(u32 va, u32 v)
{
	JtScope ts(s_jt.wr);
	u32 pa;
	if (jmmu_xlat<2>(va, 1, pa))
	{
		_vmem_WriteMem16(pa, (u16)v);
		jmmu_after_write(pa, 0);
	}
}

void jmmu_wr32(u32 va, u32 v)
{
	JtScope ts(s_jt.wr);
	u32 pa;
	if (jmmu_xlat<4>(va, 1, pa))
	{
		_vmem_WriteMem32(pa, v);
		jmmu_after_write(pa, v);
	}
}

void jmmu_wr64(u32 va, u64 v)
{
	JtScope ts(s_jt.wr);
	u32 pa;
	if (jmmu_xlat<8>(va, 1, pa))
	{
		_vmem_WriteMem64(pa, v);
		jmmu_after_write(pa, 0);
	}
}

// Row MMU Pref JIT: a block's pref to the store queues; a fault is left as for a data access.
u32 jmmu_pref(u32 dest)
{
	JtScope ts(s_jt.pref);
	s_js.prefs++;
	const u32 code = jx_pref(dest);
	if (code)
	{
		g_jmmu_fault = code;
		s_js.data_exc++;
	}
	return code;
}

u32 jmmu_ifb(u32 op, u32 epc, u32 slot)
{
	JtScope ts(s_jt.ifb);
	const u32 taken = jx_ifb(op, epc, slot);
	s_js.ifb_exc += taken;
	jtc_asid_check();
	return taken;
}

// ldtlb: the JTC slots of the old entry go (FAST keeps the old entry's translation). The JTC's hits do not step URC as
// the UTLB accesses they stand for would, so each ldtlb steps it (wrapping at URB): the next miss loads another entry.
void jmmu_utlb_changed(u32 entry)
{
	if (!g_jit_mmu)
		return;
	s_js.ldtlb++;
	if (!g_mmu_fast)
		jtc_drop(entry & 63);
	const u32 v = CCN_MMUCR.reg_data;
	u32 urc = (MMUCR_URC(v) + 1) & 63;
	if (urc == MMUCR_URB(v))
		urc = 0;
	CCN_MMUCR.reg_data = (v & ~(63u << 10)) | (urc << 10);
}

void jmmu_fast_changed()
{
	if (!g_jit_mmu)
		return;
	jtc_drop(64);
	g_jdgen++;
	s_jtc_tgen++;
}

void jmmu_code_cleared() { g_jdgen++; }

// ---- the main loop's lookup ----
static u32 s_last_at;

static inline u32 jdisp_index(u32 pc) { return ((pc ^ (pc >> 12)) >> 1) & (JDISP_SETS - 1); }

// Row MMU Block Cache: an entry for the block found. With AT on only MMU Model FAST keeps a translation for a while.
// 2 ways: the way of the same pc, else a stale way, else way 0 moves to way 1 (the newest stays in way 0).
static void jdisp_fill(u32 pc, u32 cfg, u32 at, void* code)
{
	if (!g_jmmu_bcache || (at && !g_mmu_fast))
		return;
	JDisp* set = g_jdisp[jdisp_index(pc)];
	JDisp* d = set[0].pc == pc ? &set[0] : set[1].pc == pc ? &set[1] : set[0].gen != g_jdgen ? &set[0] :
	           set[1].gen != g_jdgen ? &set[1] : nullptr;
	if (!d)
	{
		set[1] = set[0];
		d = &set[0];
		s_js.bc_evicts++;
	}
	d->pc = pc;
	d->key = cfg | (CCN_PTEH.reg_data & 0xFF) << 8;
	d->gen = g_jdgen;
	d->code = (u32)(uintptr_t)code;
	s_js.bc_fills++;
}

void* jmmu_lookup(u32 pc)
{
	JtScope ts(s_jt.lookup);
	next_pc = pc;
	s_js.lookups++;
	if (ts.acc)
		hot_add(1, pc);
	// An AT edge: the blocks of before compiled their accesses for the other state (MMUCR Bits OLD has no clear).
	const u32 at = MMUCR_AT(CCN_MMUCR.reg_data);
	if (at != s_last_at)
	{
		s_last_at = at;
		s_js.at_edges++;
		jtc_flush_all();
		recSh4_ClearCache();
		g_jdgen++;
		s_jtc_tgen++;
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
			jdisp_fill(pc, cfg, at, (void*)b->code);
			return (void*)b->code;
		}
		s_js.stale++;
	}
	s_js.compiles++;
	const u64 c0 = gettime();
	DynarecCodeEntry* rv = rdv_CompilePCMmu(pc, pa, cfg, gen);
	const u64 cd = gettime() - c0;
	if (cd < (1ull << 31))
	{
		s_jt.compile += cd;
		ts.t0 += cd;
	}
	if (rv)
	{
		jdisp_fill(pc, cfg, at, (void*)rv);
		return (void*)rv;
	}
	// SR.FD with an FPU op: the exception first, so its handler sets FPSCR before the block compiles (Flycast).
	g_jmmu_fault = g_dec_fpu_disabled ? 0x800 : 0;
	s_js.fd_compile += g_dec_fpu_disabled;
	return 0;
}

// Code 0 is a block that does not compile (its FATAL line printed): the SH4 spins on it.
u32 jmmu_raise(u32 epc)
{
	JtScope ts(s_jt.raise);
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
static void jmmu_reset_cache() { new_epoch(); }

bool jmmu_can_run() { return settings.dynarec.Enable; }

void jmmu_select(bool on)
{
	g_jit_mmu = on;
	if (!on)
		g_jmmu_bcache = g_jmmu_sq = g_jmmu_pref = g_jmmu_asidjtc = false;
	jtc_flush_all();
	memset(s_jsave, 0, sizeof(s_jsave));
	s_jtc_tgen = 1;
	memset(s_hot, 0, sizeof(s_hot));
	memset(s_jpage, 0, sizeof(s_jpage));
	s_jepoch = 1;
	memset(g_jdisp, 0, sizeof(g_jdisp));
	g_jdgen = 1;
	s_jdgen_last = 1;
	s_last_at = MMUCR_AT(CCN_MMUCR.reg_data);
	s_last_sv = MMUCR_SV(CCN_MMUCR.reg_data);
	s_jtc_asid = CCN_PTEH.ASID;
	g_jmmu_fault = 0;
	memset(&s_js, 0, sizeof(s_js));
	s_js_last = s_js;
	memset(&s_jt, 0, sizeof(s_jt));
	s_jt_last = s_jt;
	if (on)
	{
		sh4_cpu.Run = recSh4_Run;
		sh4_cpu.ResetCache = jmmu_reset_cache;
		printf("[fullmmu] MMU JIT: blocks by (vaddr, paddr, mode) in 1 KB pages, JTC %u x 4, JIT RM 2/3 ACCEPT; AT 1 blocks"
		       " without fastmem, SQ shapes, constprop, DVE, DYN_IC and BCACHE\n", (u32)JTC_N);
		printf("[fullmmu] MMU JIT rows: block cache %s (%u x 2 entries), SQ stores %s, pref %s, ASID JTC %s\n",
		       g_jmmu_bcache ? "ON" : "OFF", (u32)JDISP_SETS, g_jmmu_sq ? "INLINE" : "C", g_jmmu_pref ? "JIT" : "IFB",
		       g_jmmu_asidjtc ? "ON" : "OFF");
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
	const double ms = 1000.0 / (double)ps3_timebase_hz / seconds;
#define T(f) ((double)(s_jt.f - s_jt_last.f) * ms)
	printf("[jmmu] ms/s: lookup %.1f, compile %.1f, slow r %.1f w %.1f, ifb %.1f, exc %.1f, pref %.1f | block cache %s:"
	       " fills %u, evicts %u, gen +%u\n", T(lookup), T(compile), T(rd), T(wr), T(ifb), T(raise), T(pref),
	       g_jmmu_bcache ? "ON" : "OFF", D(bc_fills), D(bc_evicts), g_jdgen - s_jdgen_last);
	char hr[160], hl[160];
	hr[hot_top(0, hr, sizeof(hr))] = 0;
	hl[hot_top(1, hl, sizeof(hl))] = 0;
	printf("[jmmu] prefs %u, ASID switches %u (restored %u) | hot slow r:%s | hot lookups:%s\n", D(prefs), D(asid),
	       D(asid_restores), hr, hl);
#undef T
#undef D
	s_js_last = s_js;
	s_jt_last = s_jt;
	s_jdgen_last = g_jdgen;
}
