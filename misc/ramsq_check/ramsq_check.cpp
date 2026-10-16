// Plan 5.44 + P74 PC check (WSL): do_sqw_nommu and do_sqw_mmu (ps3_rend.cpp) against the old chains do_sqw<false>
// and do_sqw<true> -> TAWriteSQ / WriteMemBlock_nommu_ptr, all cut from the tree by run.sh; 16 MB of RAM per side.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32; typedef uintptr_t unat;
#define INLINE inline
#define FASTCALL
#define MEMCALL
#define fastcall
#define verify(x) do { if (!(x)) { printf("verify failed: %s\n", #x); exit(1); } } while (0)
#define RAM_SIZE (16 * 1024 * 1024)
#define RAM_MASK (RAM_SIZE - 1)
#define HANDLER_MAX 0x1F

// ---- what the extracted code names ----
struct VArray2 { u8* data; u32 size; u8& operator[](u32 i) { return data[i]; } };
VArray2 mem_b;
alignas(256) u8 sq_both[64];
u32 sq_remap[64];
struct { u32 Area; } CCN_QACR0, CCN_QACR1;
struct VramSplit { u32 sq_mem; } g_vram_split;
struct TaSplit { u32 calls, next, armed, hit, ram; u64 t_store, t_copy, t_fsm; } g_ta_split;
int g_ta_sq_direct_preset, g_ram_sq_direct_preset, g_mmu_sq_direct_preset, g_jit_ccalls_preset = 1;
int g_blockcopy_preset = 1, g_block_write_preset = 1;
bool g_sh4_cached_live;
enum { CC_SQW, CC_COUNT };
u32 g_ccall[CC_COUNT];
#define CCALL(site) do { if (g_jit_ccalls_preset) g_ccall[site]++; } while (0)
void* _vmem_MemInfo_ptr[0x100];

// ---- one side of the comparison ----
struct Ev { u32 kind, addr; u8 bytes[32]; };
enum { EV_TA, EV_TAX, EV_W32, EV_INV };
struct World
{
	u8* ram;
	TaSplit split;
	VramSplit vs;
	u32 seed, wmb, ccall, ram_samples, ta_samples, tacalls, tawsq;
	std::vector<Ev> log;
};
static World* g_w;

static void ev(u32 kind, u32 addr, const void* bytes, u32 n)
{
	Ev e;
	memset(&e, 0, sizeof(e));
	e.kind = kind;
	e.addr = addr;
	if (bytes)
		memcpy(e.bytes, bytes, n);
	g_w->log.push_back(e);
}

// ---- stubs: everything past the code under test ----
static u64 g_stamp;
static inline u64 ta_stamp() { return ++g_stamp; }
static inline u64 gettime() { return ++g_stamp; }
static inline u64 ta_split_enter()
{
	if (++g_ta_split.calls != g_ta_split.next)
		return 0;
	g_ta_split.armed = 1;
	return gettime();
}
// ta.cpp's bookkeeping: the next sample 48..79 calls on, a RAM or a TA sample counted.
void ta_split_leave(u64 t_entry)
{
	(void)t_entry;
	TaSplit& g = g_ta_split;
	g.armed = 0;
	g_w->seed = g_w->seed * 1664525u + 1013904223u;
	g.next = g.calls + 48 + (g_w->seed >> 27);
	if (!g.hit)
	{
		g_w->ram_samples += g.ram;
		g.ram = 0;
		return;
	}
	g_w->ta_samples++;
	g.hit = 0;
}
// The TA store's gate, as ta_gate_set keeps it: the flat path needs TA Fast Store and an open gate.
static bool s_ta_fast, s_ta_closed, s_flat_ok;
static void gate_set() { s_flat_ok = s_ta_fast && !s_ta_closed; }
u32 TaCalls;
// TAWriteSQ: polygon data below 8 MB of area 4 goes to libPvr_TaSQ (ps3ta_store_sq, TaCalls), else YUV or VRAM.
void TAWriteSQ(u32 address, u32* data)
{
	g_w->tawsq++;
	if ((address & 0x01800000) != 0)
	{
		ev(EV_TAX, address, data, 32);
		return;
	}
	TaCalls++;
	if (s_ta_closed)
		return;
	ev(EV_TA, 0, data, 32);
	if (g_ta_split.armed)
		g_ta_split.hit = 1;
}
// sh4_cached.cpp: nothing unless the engine has blocks and the range is main RAM.
void sh4_cached_invalidate_range(u32 addr, u32 size)
{
	if (!g_sh4_cached_live || (addr & 0x1C000000) != 0x0C000000)
		return;
	ev(EV_INV, addr, &size, 4);
}
static void blockcopy_announce() {}
static void _vmem_WriteMem32(u32 addr, u32 data) { ev(EV_W32, addr, &data, 4); }
#define WriteMem32_nommu _vmem_WriteMem32
// A 32-byte store-queue block never takes the odd-size path.
static u8 block_src8(const u32*, u32) { verify(false); return 0; }
static u16 block_src16(const u32*, u32) { verify(false); return 0; }
static void WriteMem8_nommu(u32, u8) { verify(false); }
static void WriteMem16_nommu(u32, u16) { verify(false); }

// The TA store of ps3_rend.cpp, reduced to the record it takes.
struct Ta_Dma { u32 w[8]; };
struct TaContext { u8* raw; u32 raw_used, spe_pub; };
enum { TA_RAW_SIZE = 1 << 12, TAS_SLOW = 0x100, SPE_BATCH = 256 };
static u8 g_raw[TA_RAW_SIZE];
static TaContext g_tc = { g_raw, 0, 0 };
static TaContext* s_ta = &g_tc;
static bool g_spe_on, s_peak_frame;
static u8 s_fsm_state;
static u32 g_peak_notes;
static void raw_note_peak(TaContext*) { g_peak_notes++; }
static void spe_note(TaContext* c)
{
	if (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH)
		c->spe_pub = c->raw_used;
}
static u32 ta_fsm_entry(u32 st, u32 pcw) { return (pcw & 1) ? TAS_SLOW : st; }
static u32 ta_fsm_step(u32 st, u32, Ta_Dma*) { return st; }
template<bool probe, bool flat>
static inline void store_sq(u32* data)
{
	verify(!s_ta_closed);
	ev(EV_TA, 0, data, 32);
	if (probe)
	{
		g_ta_split.t_store = g_ta_split.t_copy = g_ta_split.t_fsm = ta_stamp();
		g_ta_split.hit = 1;
	}
}

#include "vmem.inc"
#include "map.inc"
#include "wmb.inc"
// The reference counts its block writes: the direct path must make none.
static void wmb_counted(u32 dst, u32* src, u32 size)
{
	g_w->wmb++;
	WriteMemBlock_nommu_ptr(dst, src, size);
}
#define WriteMemBlock_nommu_ptr wmb_counted
#include "mmu.inc"
#include "sqw.inc"
#include "rend.inc"

// The flat TA body logs its record after the copy into the raw buffer; the stub FSM does not, so log it here.
static u32 g_raw_seen;
static void ta_flat_drain()
{
	while (g_raw_seen < g_tc.raw_used)
	{
		ev(EV_TA, 0, g_raw + g_raw_seen, 32);
		g_raw_seen += 32;
	}
	if (g_tc.raw_used + 32 > TA_RAW_SIZE && (g_ccall[CC_SQW] & 7) == 0)
		g_tc.raw_used = g_tc.spe_pub = g_raw_seen = 0;
}

static u64 g_seed;
static u32 rnd()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return (u32)(g_seed >> 16);
}

// One UTLB slot as UTLB_Sync writes it (PPN << 10): mostly main RAM or the TA, then other areas or none.
static u32 slot_value()
{
	const u32 k = rnd() & 15;
	if (k < 6)
		return 0x0C000000u | (rnd() & 0x03FFFC00u);
	if (k < 9)
		return 0x10000000u | (rnd() & 0x007FFC00u);
	if (k < 11)
		return 0x10000000u | (rnd() & 0x03FFFC00u);
	if (k < 14)
		return rnd() & 0x1FFFFC00u;
	return 0;
}

static void enter(World& w)
{
	g_w = &w;
	mem_b.data = w.ram;
	mem_b.size = RAM_SIZE;
	memset(_vmem_MemInfo_ptr, 0, sizeof(_vmem_MemInfo_ptr));
	// As mem_map_defualt (sh4_mem.cpp) maps main RAM in area 3.
	_vmem_map_block_mirror(mem_b.data, 0x0C, 0x0F, RAM_SIZE);
	g_ta_split = w.split;
	g_vram_split = w.vs;
	g_ccall[CC_SQW] = w.ccall;
	TaCalls = w.tacalls;
}

static void leave(World& w)
{
	w.split = g_ta_split;
	w.vs = g_vram_split;
	w.ccall = g_ccall[CC_SQW];
	w.tacalls = TaCalls;
}

int main(int argc, char** argv)
{
	const u32 total = argc > 1 ? (u32)atoi(argv[1]) : 4000000;
	g_seed = 0x9E3779B97F4A7C15ull ^ ((u64)(argc > 2 ? atoi(argv[2]) : 1) * 0x100000001B3ull);
	World a, b;
	for (World* w : { &a, &b })
	{
		w->ram = (u8*)aligned_alloc(65536, RAM_SIZE);
		memset(w->ram, 0, RAM_SIZE);
		w->split = { 0, 64, 0, 0, 0, 0, 0, 0 };
		w->vs = { 0 };
		w->seed = 0x9E3779B9u;
		w->wmb = w->ccall = w->ram_samples = w->ta_samples = w->tacalls = w->tawsq = 0;
	}
	for (u32 k = 0; k < 64; k++)
		sq_remap[k] = slot_value();
	g_ta_sq_direct_preset = g_ram_sq_direct_preset = g_mmu_sq_direct_preset = 1;
	s_ta_fast = true;
	gate_set();
	u32 fails = 0, direct = 0, ram = 0, chain_ram = 0, ta = 0, ta_flat = 0, other = 0, sampled_ram = 0;
	u32 mmu = 0, mmu_ram = 0, mmu_ta = 0, mmu_chain = 0, mmu_off = 0, mmu_peak_off = 0, sampled_mmu = 0;
	for (u32 i = 0; i < total && fails < 8; i++)
	{
		// Settings move now and then; the areas lean to RAM and the TA.
		if ((rnd() & 255) == 0) g_ram_sq_direct_preset = rnd() & 1 ? 1 : (rnd() & 3) != 0;
		if ((rnd() & 1023) == 0) g_ta_sq_direct_preset = (rnd() & 3) != 0;
		if ((rnd() & 1023) == 0) g_mmu_sq_direct_preset = (rnd() & 3) != 0;
		if ((rnd() & 1023) == 0) { s_ta_fast = (rnd() & 3) != 0; gate_set(); }
		if ((rnd() & 1023) == 0) { s_ta_closed = (rnd() & 7) == 0; gate_set(); }
		if ((rnd() & 1023) == 0) s_peak_frame = (rnd() & 3) == 0;
		if ((rnd() & 1023) == 0) g_spe_on = rnd() & 1;
		if ((rnd() & 4095) == 0) g_sh4_cached_live = (rnd() & 3) == 0;
		if ((rnd() & 15) == 0) CCN_QACR0.Area = (rnd() & 3) ? ((rnd() & 1) ? 3 : 4) : (rnd() & 7);
		if ((rnd() & 15) == 0) CCN_QACR1.Area = (rnd() & 3) ? ((rnd() & 1) ? 3 : 4) : (rnd() & 7);
		if ((rnd() & 63) == 0) sq_remap[rnd() & 63] = slot_value();
		for (u32 k = 0; k < 64; k += 4)
		{
			const u32 v = rnd();
			memcpy(sq_both + k, &v, 4);
		}
		// Any store-queue address: every low bit can be set, and the top two bits of the 26 pick a RAM mirror.
		const u32 dest = 0xE0000000u | (rnd() & 0x03FFFFFF);
		const bool at = rnd() & 1;
		// The target as each old chain computes it, written here apart from the code under test.
		const u32 qarea = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;
		const u32 target = at ? sq_remap[(dest >> 20) & 0x3F] | (dest & 0x000FFFE0) : (dest & 0x03FFFFE0) | (qarea << 26);
		const u32 area = (target >> 26) & 7;
		const bool row = !at || g_mmu_sq_direct_preset;
		const bool want_direct = row && area == 3 && g_ram_sq_direct_preset && !g_sh4_cached_live;
		const bool want_flat = row && area == 4 && (target & 0x01800000) == 0 && g_ta_sq_direct_preset && s_flat_ok;

		enter(a);
		const u32 wmb_a = a.wmb, mem_a = g_vram_split.sq_mem, cc_a = g_ccall[CC_SQW], tc_a = TaCalls, tw_a = a.tawsq;
		const size_t log_a = a.log.size();
		if (at)
			do_sqw<true>(dest);
		else
			do_sqw<false>(dest);
		const u32 dm_a = g_vram_split.sq_mem - mem_a, dc_a = g_ccall[CC_SQW] - cc_a, dt_a = TaCalls - tc_a;
		leave(a);

		enter(b);
		const u32 wmb_b = b.wmb, mem_b0 = g_vram_split.sq_mem, cc_b = g_ccall[CC_SQW], tc_b = TaCalls, tw_b = b.tawsq;
		const u32 rs_b = b.ram_samples, ts_b = b.ta_samples, raw0 = g_tc.raw_used, peak0 = g_peak_notes;
		const size_t log_b = b.log.size();
		if (at)
			do_sqw_mmu(dest);
		else
			do_sqw_nommu(dest);
		const bool raw_copy = g_tc.raw_used != raw0;
		const u32 peaks = g_peak_notes - peak0;
		ta_flat_drain();
		const u32 dm_b = g_vram_split.sq_mem - mem_b0, dc_b = g_ccall[CC_SQW] - cc_b, dt_b = TaCalls - tc_b;
		leave(b);

		bool ok = a.log.size() - log_a == b.log.size() - log_b && dm_a == dm_b && dc_a == 1 && dc_b == 1 && dt_a == dt_b;
		for (size_t k = 0; ok && log_a + k < a.log.size(); k++)
			ok = memcmp(&a.log[log_a + k], &b.log[log_b + k], sizeof(Ev)) == 0;
		// The bytes the old chain wrote, and nothing else in that block's neighbourhood.
		if (area == 3)
		{
			const u32 off = target & (RAM_MASK & ~31u);
			const u32 lo = off >= 64 ? off - 64 : 0, hi = off + 96 <= RAM_SIZE ? off + 96 : RAM_SIZE;
			ok = ok && memcmp(a.ram + lo, b.ram + lo, hi - lo) == 0 && memcmp(a.ram + off, sq_both + (dest & 0x20), 32) == 0;
			ram++;
		}
		// Which path ran: direct makes no block write, the flat TA body no TAWriteSQ; the rest is the reference's.
		const u32 w_a = a.wmb - wmb_a, w_b = b.wmb - wmb_b, t_a = a.tawsq - tw_a, t_b = b.tawsq - tw_b;
		ok = ok && (want_direct ? (w_b == 0 && w_a == 1) : w_b == w_a);
		ok = ok && (want_flat ? (t_b == 0 && t_a == 1) : t_b == t_a);
		// A record copied by the flat body notes the TA peak: always without the MMU, as TA Peak Frame says with it.
		ok = ok && peaks == (raw_copy ? (at ? !s_peak_frame : 1u) : 0u) && (!raw_copy || want_flat);
		direct += want_direct;
		chain_ram += area == 3 && !want_direct;
		ta += area == 4;
		ta_flat += want_flat;
		other += area != 3 && area != 4;
		sampled_ram += b.ram_samples - rs_b;
		if (at)
		{
			mmu++;
			mmu_off += !g_mmu_sq_direct_preset;
			mmu_ram += want_direct;
			mmu_ta += want_flat;
			mmu_chain += row && !want_direct && !want_flat;
			mmu_peak_off += raw_copy && s_peak_frame;
			sampled_mmu += (b.ram_samples - rs_b) + (b.ta_samples - ts_b);
		}
		if (!ok && fails++ < 8)
			printf("FAIL flush %u dest %08X at %d target %08X area %u rows mmu %d ram %d ta %d flat_ok %d cached %d: log %zu/%zu "
			       "sq_mem %u/%u census %u/%u TaCalls %u/%u block writes %u/%u TAWriteSQ %u/%u peaks %u copy %d\n",
			       i, dest, (int)at, target, area, g_mmu_sq_direct_preset, g_ram_sq_direct_preset, g_ta_sq_direct_preset,
			       (int)s_flat_ok, (int)g_sh4_cached_live, a.log.size() - log_a, b.log.size() - log_b, dm_a, dm_b, dc_a, dc_b,
			       dt_a, dt_b, w_a, w_b, t_a, t_b, peaks, (int)raw_copy);
		// The logs are compared; drop them so they stay small.
		if (a.log.size() > 4096)
		{
			a.log.clear();
			b.log.clear();
		}
		if ((i & 0xFFFFF) == 0xFFFFF && memcmp(a.ram, b.ram, RAM_SIZE) != 0 && fails++ < 8)
			printf("FAIL: the RAM images differ at flush %u\n", i);
	}
	if (memcmp(a.ram, b.ram, RAM_SIZE) != 0 && fails++ < 8)
		printf("FAIL: the RAM images differ at the end\n");
	if (a.ram_samples != b.ram_samples || a.ta_samples != b.ta_samples || a.split.calls != b.split.calls)
	{
		printf("FAIL: probe samples RAM %u/%u TA %u/%u, calls %u/%u\n", a.ram_samples, b.ram_samples, a.ta_samples,
		       b.ta_samples, a.split.calls, b.split.calls);
		fails++;
	}
	// Every kind of flush must have run, the sampled direct paths too.
	if (!direct || !chain_ram || !ta || !ta_flat || !other || !sampled_ram || !mmu_ram || !mmu_ta || !mmu_chain
	    || !mmu_off || !mmu_peak_off || !sampled_mmu)
	{
		printf("FAIL: a path did not run\n");
		fails++;
	}
	printf("%s: %u flushes: RAM %u (direct %u, old chain %u, sampled %u), TA %u (flat %u), other areas %u; MMU %u "
	       "(RAM direct %u, TA flat %u, chain %u, row OFF %u, sampled %u); %u failures\n",
	       fails ? "FAIL" : "PASS", total, ram, direct, chain_ram, sampled_ram, ta, ta_flat, other, mmu, mmu_ram, mmu_ta,
	       mmu_chain, mmu_off, sampled_mmu, fails);
	free(a.ram);
	free(b.ram);
	return fails != 0;
}
