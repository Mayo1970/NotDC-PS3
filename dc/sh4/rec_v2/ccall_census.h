#pragma once
#include "types.h"

// ---- C call-out census (JIT_CCALLS preset, default OFF; dave's ask): JIT -> C hand-offs per second and per frame ----

// Count first: the only JIT change to move a game (jit_fschg, +10% ChuChu) was found by a 1.3 M sync_fpscr/s counter;
// changes argued from static counts (superblocks, bcache, jit_align, hot-block clustering, dyn_ic, jit_cr0) gave 0-3%.

enum CCallSite
{
	// Not counted: shop_ifb (IFB_PROBE's job, closed at 2 calls/s on HW) and the fastmem DSI trampolines
	// (patched-in branches, not C calls).
	CC_TIMESLICE = 0,  // UpdateSystem_no_event  - the per-timeslice peripheral tick
	CC_INTERRUPT,      // UpdateSystem_handle_event / UpdateINTC - a real dispatch
	CC_GETCODE,        // bm_GetCode - dynamic-branch cache miss, or timeslice resume
	CC_SQW,            // do_sqw - store-queue flush (this is the TA submission path)
	CC_SYNC_SR,        // UpdateSR - SR write side effects
	CC_SYNC_FPSCR,     // UpdateFPSCR - FULL path only; jit_fschg bypasses it
	CC_FSQRT,          // rec_fsqrt -> libm sqrtf (JIT_FSQRT inlines fsqrts instead)
	CC_FSRRA,          // rec_fsrra -> 1.0f/sqrtf
	CC_READMEM,        // _vmem_ReadMem*  - the inline/fastmem fast path was missed
	CC_WRITEMEM,       // _vmem_WriteMem*
	CC_MAC_SAT,        // sh4_mac_l / sh4_mac_w - the rare SR.S saturation path
	CC_COUNT
};

// Defined in dc/sh4/rec_v2/shil.cpp so every target links; ps3_menu.cpp and ps3_presets.cpp just write them.
extern "C" u32 g_ccall[CC_COUNT];
extern "C" int g_jit_ccalls_preset;

// Used in the C callees, not emitted code: one load + a predicted-not-taken branch when off, far below JIT_HOTBLOCKS'
// 12.6% (it counts per block entry). Rates hold; a census run's SPEED% reads slightly low, so measure speed with it off.
#define CCALL(site) do { if (g_jit_ccalls_preset) g_ccall[site]++; } while (0)

// ---- Per-area memory-dispatcher split: Crazy Taxi's 1.1 M writemem/s vs 2.7 K readmem/s hit different ranges ----

// SH4 area = physical address bits 26..28 (one rlwinm). Area 1 VRAM could become an inlined store + texture dirty mark;
// 4 TA (every write drives its state machine) and 0 MMIO must stay; 3 RAM would mean a FASTMEM bug (missed addresses).
enum { CCA_COUNT = 8 };
extern "C" u32 g_ccall_area[2][CCA_COUNT];   // [0]=read, [1]=write

// ---- Caller-region split of the memory dispatchers (keep it: "which side of the JIT boundary?" comes first) ----

// The area split says where writes go (95% RAM), this says how: Crazy Taxi's RAM writes were 95.6% "other", do_sqw/DMA
// block transfers calling it per 32-bit word from C, not the JIT (dc/mem: WriteMemBlock_nommu_ptr, _vmem_GetBlockPtr).
enum { CCS_TRAMP = 0, CCS_CODECACHE = 1, CCS_OTHER = 2, CCS_COUNT = 3 };
extern "C" u32 g_ccall_src[2][CCS_COUNT];

// Published by ps3_driver.cpp (rec_fastmem_reset_pool) at cache-clear time; null until then, which classifies
// everything as CCS_OTHER rather than misreporting.
extern "C" const u8* g_ccall_tramp_lo;
extern "C" const u8* g_ccall_tramp_hi;
extern "C" const u8* g_ccall_cc_lo;
extern "C" const u8* g_ccall_cc_hi;

// ra is the real caller (WriteMem32 is a plain #define onto _vmem_WriteMem32); back-patched FASTMEM trampolines live
// in s_fm_pool, emitted code in CodeCache, anything else is the emulator's own C code.
static inline u32 ccall_src_of(const void* ra)
{
	const u8* q = (const u8*)ra;
	if (q >= g_ccall_tramp_lo && q < g_ccall_tramp_hi) return CCS_TRAMP;
	if (q >= g_ccall_cc_lo    && q < g_ccall_cc_hi)    return CCS_CODECACHE;
	return CCS_OTHER;
}

// _vmem dispatchers also serve the interpreter, DMA and debugger, so CC_READMEM/CC_WRITEMEM are an UPPER BOUND on JIT
// call-outs; every other site is reached only from translated code (or its mainloop), so those counts are exact.
#define CCALL_MEM(site, rw, addr) \
	do { \
		if (g_jit_ccalls_preset) \
		{ \
			g_ccall[site]++; \
			g_ccall_area[rw][((u32)(addr) >> 26) & 7]++; \
			g_ccall_src[rw][ccall_src_of(__builtin_return_address(0))]++; \
		} \
	} while (0)

// Prints and resets the census; called once a second from the SPG stats block (plugs/drkPvr/SPG.cpp) to share the stats
// line's time base and fflush. `vbs` = vblanks/sec, for the per-frame column.
extern "C" void ccall_census_dump(double seconds, double vbs);
