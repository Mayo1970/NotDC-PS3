// Dynarec driver: the native code cache and emission into it, block compile, lookup and invalidation,
// cache pressure (warn/flush hysteresis) and D-cache/I-cache coherency.

#include "types.h"

#if HOST_OS == OS_WINDOWS
#include <windows.h>
#elif HOST_OS == OS_LINUX
#include <unistd.h>
#include <sys/mman.h>
#elif HOST_OS == OS_PS3
#include "ps3/ps3_jitmem.h"
#endif

#include "../sh4_interpreter.h"
#include "../sh4_opcode_list.h"
#include "../sh4_registers.h"
#include "../sh4_if.h"
#include "../dmac.h"
#include "../intc.h"
#include "../tmu.h"

#include "dc/mem/sh4_mem.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dc/gdrom/gdrom_if.h"
#include "ps3/ps3_stall.h"

#include <time.h>
#include <float.h>
#include <string.h>

#include "blockmanager.h"
#include "ngen.h"
#include "decoder.h"
#include "../sh4_fullmmu.h"
#include "../sh4_mmu_jit.h"

// decoder.cpp: the MMU JIT's code read offset (physical - virtual pc) for the block in hand.
extern u32 g_dec_code_delta;

#ifndef HOST_NO_REC

// Hosts where we own cache pressure and I-cache coherency by hand.
#if HOST_OS == OS_PS3
#define JIT_MANAGED_CACHE 1
#else
#define JIT_MANAGED_CACHE 0
#endif

// Defined in ps3_presets.cpp — returns 1 when debug loop / boot-trace is active
extern "C" int get_debug_loop();
// Defined in ps3_presets.cpp (JIT_SBP preset) — 0=off (zero protection, legacy),
// 1=known self-modifying addresses (default), 2=all RAM blocks
extern "C" int get_jit_sbp_preset();
// JIT_HOTBLOCKS probe (ps3_driver.cpp) — drop the tracked-block table when the
// code cache it describes is thrown away. No-op unless the preset is on.
extern "C" void hotblocks_reset();
extern "C" void fb_reset();
#if HOST_OS == OS_PS3
extern "C" int get_jit_code_cache_preset();	// ps3_presets.cpp: 0 = 6 MB, 1 = 12 MB, 2 = CODE_SIZE (plan 5.28)
extern "C" char __executable_start[], __etext[];	// lv2.ld: the bounds of .text
#endif

// Optional performance counters: negligible overhead, for tuning the cache thresholds.
// #define ENABLE_PERF_MONITORING

#ifdef ENABLE_PERF_MONITORING
static u32 perf_blocks_compiled    = 0;
static u32 perf_cache_clears       = 0;
static u32 perf_block_check_fails  = 0;
#endif

void recSh4_ClearCache();

// CODE_SIZE comes from ngen.h: a smaller local copy once cleared the cache early (rdv_FailedToFindBlock spam).

extern volatile bool sh4_int_bCpuRun;

#if HOST_OS == OS_PS3
// .text: the only segment lv2 grants execute to. 128-byte Cell cache line.
u8 CodeCache[CODE_SIZE] __attribute__((aligned(128), section(".text"), used));
#elif HOST_OS == OS_LINUX
// Linux needs an executable mapping; allocate with a page-alignment margin.
u8  pMemBuff[CODE_SIZE + 4095];
u8* CodeCache;
#else
u8 CodeCache[CODE_SIZE];
#endif

// Current write pointer within the cache (byte offset)
u32  LastAddr     = 0;
// Lower bound after SetBaseAddr — clears only reclaim above this
u32  LastAddr_min = 0;
// Optional override pointer used during back-patching
u32* emit_ptr     = 0;

// Bytes of CodeCache in use, set by recSh4_Init (PS3: the "JIT Code Cache" option, plan 5.28).
static u32 s_code_size = CODE_SIZE;

// Plan 5.28 [jcache] counters: code emitted by compiles since boot, cache clears (full = out of room).
static u64 s_compiled_bytes = 0;
static u32 s_clears = 0, s_clears_full = 0;

// cold_check: begin alloc

// Row JIT COLD SPLIT: cold code in chunks taken from the top of the cache downward, each filled upward. lo is the
// lowest chunk's start (s_code_size: none), at and end the current chunk's cursor and end.
static struct { u32 lo, at, end; } s_cold = { CODE_SIZE, CODE_SIZE, CODE_SIZE };
#define COLD_CHUNK (128 * 1024)

static inline u32 cache_used()
{
	return LastAddr + (s_code_size - s_cold.lo);
}

static void cold_reset()
{
	s_cold.lo = s_cold.at = s_cold.end = s_code_size;
}

static inline u32 cold_lo_now()
{
	return s_cold.lo;
}

// Where the next cold code goes, with room for bound bytes; 0 when the hot code is too close.
u32* emit_ColdBegin(u32 bound)
{
	if (s_cold.end - s_cold.at < bound)
	{
		u32 size = (bound + 127) & ~127u;
		if (s_cold.lo >= LastAddr + COLD_CHUNK + 16384)
			size = size > COLD_CHUNK ? size : COLD_CHUNK;
		if (s_cold.lo < LastAddr + size + 16384)
			return 0;
		s_cold.lo -= size;
		s_cold.at = s_cold.lo;
		s_cold.end = s_cold.lo + size;
	}
	return (u32*)&CodeCache[s_cold.at];
}

// The cold code written since emit_ColdBegin ends at end.
void emit_ColdEnd(u32* end)
{
	const u32 at = (u32)((u8*)end - CodeCache);
	verify(at >= s_cold.at && at <= s_cold.end);
	s_compiled_bytes += at - s_cold.at;
	s_cold.at = at;
}
// cold_check: end alloc

// Cache pressure: WARN logs once per fill, FLUSH clears before a compile can run out of room. The gap
// between them avoids clear/refill thrashing; both are fractions of the size in use.
#if JIT_MANAGED_CACHE
static u32 cache_high_water_mark = 0;
#define CACHE_WARN_THRESHOLD   ((u32)(((u64)s_code_size * 80) / 100))   // 80 %
#define CACHE_FLUSH_THRESHOLD  ((u32)(((u64)s_code_size * 92) / 100))   // 92 %
// Minimum free bytes required before attempting a new compilation.
// 128 KB gives room for the largest typical PPC block expansion.
#define CACHE_MIN_FREE         (128 * 1024)
#endif

// New code must reach the instruction fetch: write the D-cache lines back, then drop the stale I-cache
// lines (128-byte lines in ps3_jit_icache_sync).
#if JIT_MANAGED_CACHE
static inline void jit_sync_icache(const void* start, u32 size)
{
	if (!size) return;
	ps3_jit_icache_sync(start, size);
}
#endif

void* emit_GetCCPtr()
{
	return emit_ptr ? (void*)emit_ptr : (void*)&CodeCache[LastAddr];
}

void emit_SetBaseAddr()
{
	LastAddr_min = LastAddr;
}

void emit_Write8(u8 data)
{
	verify(!emit_ptr);
	verify(LastAddr + 1 <= s_code_size);
	CodeCache[LastAddr++] = data;
}

void emit_Write16(u16 data)
{
	verify(!emit_ptr);
	verify(LastAddr + 2 <= s_code_size);
	*(u16*)&CodeCache[LastAddr] = data;
	LastAddr += 2;
}

// cold_check: begin write
void emit_Write32(u32 data)
{
	if (emit_ptr)
	{
		*emit_ptr++ = data;
	}
	else
	{
		verify(LastAddr + 4 <= s_cold.lo);	// JIT COLD SPLIT: hot code never runs into the cold chunks
		*(u32*)&CodeCache[LastAddr] = data;
		LastAddr += 4;
	}
}

void emit_Skip(u32 sz)
{
	verify(LastAddr + sz <= s_code_size);
	LastAddr += sz;
}

u32 emit_FreeSpace()
{
	return s_cold.lo - LastAddr;
}
// cold_check: end write

void emit_WriteCodeCache()
{
	char path[512];
	snprintf(path, sizeof(path), "code_cache_%08X.bin", (u32)(uintptr_t)CodeCache);

	FILE* f = fopen(path, "wb");
	if (f)
	{
		fwrite(CodeCache, LastAddr, 1, f);
		fclose(f);
		printf("recSh4: cache dump -> %s (%u bytes)\n", path, LastAddr);
	}
	else
	{
		printf("recSh4: ERROR - failed to open %s for cache dump\n", path);
	}
}

// Drops every compiled block; the write pointer goes back to the end of the main loop (LastAddr_min).
void recSh4_ClearCache()
{
	StallScope st_(ST_JIT);
#ifdef ENABLE_PERF_MONITORING
	perf_cache_clears++;
	printf("recSh4: cache clear #%u at pc=%08X (used %u / %u bytes)\n",
	       perf_cache_clears, curr_pc, LastAddr, s_code_size);
#else
	printf("recSh4: cache clear at pc=%08X (used %u / %u bytes)\n",
	       curr_pc, LastAddr, s_code_size);
#endif

	const u32 written = LastAddr, cold_lo = s_cold.lo;
	s_clears++;
	LastAddr = LastAddr_min;
	cold_reset();
	bm_Reset();

	// JIT_HOTBLOCKS: every tracked block described code in the cache we just
	// dropped, so its counters and code pointers are meaningless now.
	hotblocks_reset();
	fb_reset();   // P101: the miss counters described the dropped blocks

#if JIT_MANAGED_CACHE
	// Blocks and patches are synced when written. This second pass covers what was written since the last
	// clear, so its cost follows the code made, not the array size (plan 5.28).
	ps3_jit_icache_sync(CodeCache + LastAddr_min, written - LastAddr_min);
	ps3_jit_icache_sync(CodeCache + cold_lo, s_code_size - cold_lo);
	cache_high_water_mark = LastAddr;
#endif
}

void AnalyseBlock(DecodedBlock* blk);

// JIT_SBP entry guard against self-modifying code: 0 off, 1 known SMC addresses (default), 2 all RAM. A block
// with no contiguous host mapping (BIOS, MMIO, region edge) has nothing to compare, so it gets no guard.
static bool DoCheck(u32 pc, u32 len)
{
	const int mode = get_jit_sbp_preset();
	if (mode == 0)
		return false;

	// No contiguous host mapping for this block (BIOS, MMIO, region edge) —
	// nothing safe to compare against.
	if (!GetMemPtr(pc, len))
		return false;

	if (!IsOnRam(pc))
		return false;

	if (mode >= 2)
		return true;

	switch (pc & 0xFFFFFF)
	{
		// DOA2LE
		case 0x3DAFC6:
		case 0x3C83F8:
		// Shenmue 2
		case 0x348000:
		// Shenmue
		case 0x41860E:
			return true;
		default:
			return false;
	}
}

// Decode, analyse and compile one SH4 block; NULL on failure. Below CACHE_MIN_FREE or past the flush
// threshold the cache is cleared first; the warning zone only logs.
DynarecCodeEntry* rdv_CompileBlock(u32 bpc)
{
	StallScope st_(ST_JIT);
#if JIT_MANAGED_CACHE
	// Update high-water mark
	if (cache_used() > cache_high_water_mark)
	{
		const u32 prev = cache_high_water_mark;
		cache_high_water_mark = cache_used();

		// Once per fill: a line per block compiled in the zone sent 8155 lines in one Sonic run (plan 5.28).
		if (prev < CACHE_WARN_THRESHOLD && cache_high_water_mark >= CACHE_WARN_THRESHOLD &&
		    cache_high_water_mark < CACHE_FLUSH_THRESHOLD)
		{
			printf("recSh4: cache %u%% full (warning zone)\n",
			       (cache_high_water_mark * 100u) / s_code_size);
		}
	}

	// Hard floor: clear if we cannot safely fit another block
	if (emit_FreeSpace() < CACHE_MIN_FREE)
	{
		printf("recSh4: cache below min-free threshold, clearing (free=%u bytes)\n",
		       emit_FreeSpace());
		s_clears_full++;
		recSh4_ClearCache();
	}
	// Proactive clear at high-water threshold
	else if (cache_used() >= CACHE_FLUSH_THRESHOLD)
	{
		printf("recSh4: cache above flush threshold (%u%%), clearing\n",
		       (cache_used() * 100u) / s_code_size);
		s_clears_full++;
		recSh4_ClearCache();
	}
#else
	// Other hosts: simple floor check (64 KB)
	if (emit_FreeSpace() < 65536)
	{
		printf("recSh4: WARNING - low cache space (%u bytes), clearing\n",
		       emit_FreeSpace());
		recSh4_ClearCache();
	}
#endif

	// Decode
	DecodedBlock* blk = dec_DecodeBlock(bpc, fpscr, SH4_TIMESLICE / 2);
	if (!blk)
	{
		if (!g_dec_fpu_disabled)
			printf("recSh4: ERROR - decode failed at %08X\n", bpc);
		return 0;
	}

	AnalyseBlock(blk);

	// Remember where code for this block starts (for I-cache flush)
#if JIT_MANAGED_CACHE
	void* code_start = emit_GetCCPtr();
#endif

	// The MMU JIT's guard is its page check (sh4_mmu_jit.cpp): GetMemPtr would read a virtual pc.
	DynarecCodeEntry* rv = ngen_Compile(blk, !g_jit_mmu && DoCheck(blk->start, blk->sh4_code_size));

#if JIT_MANAGED_CACHE
	if (rv)
	{
		u32 code_size = (u32)((u8*)emit_GetCCPtr() - (u8*)code_start);
		jit_sync_icache(code_start, code_size);
		s_compiled_bytes += code_size;

		// Bring-up trace: the last line before a freeze names the block that
		// ran off, which the heartbeat pc alone cannot.
		if (get_debug_loop() == 1)
		{
			// Dump the guest opcodes too: with the plumbing proven, the failing
			// block's instruction list is what names the bad emitter.
			char ops[160];
			int  n = 0;
			for (u32 o = 0; o < blk->sh4_code_size && n < (int)sizeof(ops) - 6; o += 2)
				n += snprintf(ops + n, sizeof(ops) - n, "%04X ", ReadMem16(bpc + o + g_dec_code_delta));
			ops[n > 0 ? n - 1 : 0] = 0;
			printf("recSh4: ops %08X: %s\n", bpc, ops);
		}

		if (get_debug_loop() == 1)
			printf("recSh4: block %08X -> %p (%u B, %u sh4 B)\n",
			       bpc, (void*)rv, code_size, blk->sh4_code_size);
	}
#endif

	dec_Cleanup();

#ifdef ENABLE_PERF_MONITORING
	if (rv) perf_blocks_compiled++;
#endif

	return rv;
}

// JIT_SBP boot rule: IP.BIN (0x..08300) loads 1ST_READ.BIN (0x..10000) over RAM the JIT compiled, so clear
// BEFORE compiling either entry; a clear after bm_AddCode or deferred to a later compile leaves stale blocks.

u32 rdv_FailedToFindBlock_pc;

DynarecCodeEntry* rdv_CompilePC()
{
	u32 pc = next_pc;

	// Drop every stale translation before compiling the boot entry block.
	if (get_jit_sbp_preset() != 0)
	{
		const u32 masked = pc & 0xFFFFFF;
		if (masked == 0x08300 || masked == 0x10000)
		{
			printf("recSh4: boot address %08X reached, clearing block cache\n", pc);
			recSh4_ClearCache();
		}
	}

	DynarecCodeEntry* rv = rdv_CompileBlock(pc);

	if (!rv)
	{
		// First attempt failed (decode/ngen error). Retry with a clean cache.
		printf("recSh4: WARNING - compile failed at %08X, retrying after cache clear\n", pc);
		recSh4_ClearCache();
		rv = rdv_CompileBlock(pc);

		if (!rv)
		{
			printf("recSh4: FATAL - compile still failed at %08X after cache clear\n", pc);
			return 0;
		}
	}

	bm_AddCode(pc, rv);

	return rv;
}

// MMU JIT (P38): the block of (pc, pa, cfg), its code read at pa. The JIT_SBP boot rule only for the P1/P2 handoff
// addresses: Windows CE programs start at 0x00010000 in U0.
DynarecCodeEntry* rdv_CompilePCMmu(u32 pc, u32 pa, u32 cfg, u32 pgen)
{
	if (get_jit_sbp_preset() != 0 && (pc >> 30) == 2)
	{
		const u32 masked = pc & 0xFFFFFF;
		if (masked == 0x08300 || masked == 0x10000)
		{
			printf("recSh4: boot address %08X reached, clearing block cache\n", pc);
			recSh4_ClearCache();
		}
	}
	g_dec_code_delta = pa - pc;
	DynarecCodeEntry* rv = rdv_CompileBlock(pc);
	if (!rv && !g_dec_fpu_disabled)
	{
		printf("recSh4: WARNING - compile failed at %08X, retrying after cache clear\n", pc);
		recSh4_ClearCache();
		rv = rdv_CompileBlock(pc);
		if (!rv && !g_dec_fpu_disabled)
			printf("recSh4: FATAL - compile still failed at %08X after cache clear\n", pc);
	}
	g_dec_code_delta = 0;
	if (rv)
		bm_AddCodeMmu(pc, pa, cfg, pgen, rv);
	return rv;
}

DynarecCodeEntry* rdv_FailedToFindBlock()
{
	next_pc = rdv_FailedToFindBlock_pc;

	if (get_debug_loop() == 1)
		printf("recSh4: rdv_FailedToFindBlock at %08X\n", next_pc);

	return rdv_CompilePC();
}

// A block's entry guard saw its SH4 source change. Full clear, not bm_RemoveCode: static links patched into
// callers would keep reaching the stale code and recompile it forever. The block compiled after it stays valid.
DynarecCodeEntry* FASTCALL rdv_BlockCheckFail(u32 pc)
{
	next_pc = pc;

#ifdef ENABLE_PERF_MONITORING
	perf_block_check_fails++;
#endif

	printf("recSh4: block check fail at %08X, clearing cache and recompiling\n", pc);

	recSh4_ClearCache();
	return rdv_CompilePC();
}

DynarecCodeEntry* rdv_FindCode()
{
	DynarecCodeEntry* rv = bm_GetCode(next_pc);
	return (rv == ngen_FailedToFindBlock) ? 0 : rv;
}

DynarecCodeEntry* rdv_FindOrCompile()
{
	DynarecCodeEntry* rv = bm_GetCode(next_pc);
	if (rv == ngen_FailedToFindBlock)
		rv = rdv_CompilePC();
	return rv;
}

void recSh4_Run()
{
	sh4_int_bCpuRun = true;

	// Latch the accuracy preset before ngen_mainloop(): its timeslice is baked into the main loop, emitted once,
	// and UpdateSystem must advance the peripherals by the same slice.
	Sh4_ApplyAccuracyPreset();

	printf("recSh4: starting dynarec execution\n");

#ifdef ENABLE_PERF_MONITORING
	u32 blocks_at_start = perf_blocks_compiled;
#endif

	ngen_mainloop();

#ifdef ENABLE_PERF_MONITORING
	printf("recSh4: execution stopped — compiled %u blocks this session\n",
	       perf_blocks_compiled - blocks_at_start);
#endif

	sh4_int_bCpuRun = false;
}

void recSh4_Stop()  { Sh4_int_Stop(); }
void recSh4_Step()  { Sh4_int_Step(); }
void recSh4_Skip()  { Sh4_int_Skip(); }

void recSh4_Reset(bool Manual)
{
	Sh4_int_Reset(Manual);
	recSh4_ClearCache();
}

void recSh4_Init()
{
	printf("recSh4: initializing dynarec\n");

	Sh4_int_Init();
	bm_Init();

#ifdef ENABLE_PERF_MONITORING
	perf_blocks_compiled   = 0;
	perf_cache_clears      = 0;
	perf_block_check_fails = 0;
#endif

	// Platform-specific: make the code cache executable
#if HOST_OS == OS_WINDOWS
	DWORD old;
	VirtualProtect(CodeCache, CODE_SIZE, PAGE_EXECUTE_READWRITE, &old);
	printf("recSh4: code cache at %p (%u KB, Windows)\n",
	       CodeCache, CODE_SIZE / 1024);

#elif HOST_OS == OS_LINUX
	CodeCache = (u8*)(((uintptr_t)pMemBuff + 4095) & ~(uintptr_t)4095);
	printf("recSh4: code cache at %p (aligned from %p, Linux)\n",
	       CodeCache, pMemBuff);

	if (mprotect(CodeCache, CODE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE))
	{
		perror("recSh4: ERROR - mprotect failed");
		verify(false);
	}
	memset(CodeCache, 0xCD, CODE_SIZE);

#elif HOST_OS == OS_PS3
	{
		const int p = get_jit_code_cache_preset();
		s_code_size = (p == 2) ? CODE_SIZE : (p == 1) ? (12u << 20) : (6u << 20);

		// Generated code calls C with `bl`, which reaches 32 MB, so every cache byte must reach all of .text.
		const u32 up    = (u32)((u8*)__etext - CodeCache);
		const u32 down  = (u32)(CodeCache + CODE_SIZE - (u8*)__executable_start);
		const u32 reach = up > down ? up : down;
		printf("recSh4: code cache at %p (%u of %u KB in use, PS3 .text), bl reach %u KB of 32768\n",
		       CodeCache, s_code_size / 1024, CODE_SIZE / 1024, reach / 1024);
		if (reach >= (32u << 20))
			printf("recSh4: ERROR - .text is wider than a bl reaches; C calls from the cache will fail\n");
	}

	// No protection call exists or is needed: lv2 already maps .text RWX.
	memset(CodeCache, 0x00, s_code_size);
	ps3_jit_icache_sync(CodeCache, s_code_size);
#endif

	LastAddr     = 0;
	LastAddr_min = 0;
	emit_ptr     = 0;
	cold_reset();

	printf("recSh4: initialization complete (cache=%u KB)\n",
	       s_code_size / 1024);
}

void recSh4_Term()
{
	printf("recSh4: terminating dynarec\n");

#ifdef ENABLE_PERF_MONITORING
	printf("recSh4 stats:\n");
	printf("  blocks compiled   : %u\n", perf_blocks_compiled);
	printf("  cache clears      : %u\n", perf_cache_clears);
	printf("  block check fails : %u\n", perf_block_check_fails);
	printf("  cache usage       : %u / %u bytes (%.1f%%)\n",
	       LastAddr, s_code_size, (LastAddr * 100.0f) / s_code_size);
#if JIT_MANAGED_CACHE
	printf("  high water mark   : %u bytes (%.1f%%)\n",
	       cache_high_water_mark,
	       (cache_high_water_mark * 100.0f) / s_code_size);
#endif

#ifdef BM_ENABLE_STATS
	u32 hits, misses, cache_hits, total_blocks;
	bm_GetStats(&hits, &misses, &cache_hits, &total_blocks);
	printf("  bm hits           : %u\n", hits);
	printf("  bm misses         : %u\n", misses);
	printf("  bm cache hits     : %u\n", cache_hits);
	printf("  bm total blocks   : %u\n", total_blocks);
	if (hits + cache_hits > 0)
		printf("  bm efficiency     : %.2f%%\n",
		       (cache_hits * 100.0f) / (hits + cache_hits));
#endif
#endif // ENABLE_PERF_MONITORING

	Sh4_int_Term();
}

bool recSh4_IsCpuRunning()
{
	return Sh4_int_IsCpuRunning();
}

void recSh4_GetCacheStats(u32* total_size, u32* used_size, u32* free_size)
{
	if (total_size) *total_size = s_code_size;
	if (used_size)  *used_size  = cache_used();
	if (free_size)  *free_size  = s_code_size - cache_used();
}

// Plan 5.28: one [jcache] line per stats window that compiled code: fill, compile rate, clears since boot.
void recSh4_CacheDump(double seconds)
{
	static u64 last;
	if (seconds <= 0.0 || s_compiled_bytes == last)
		return;
	printf("[jcache] used %u of %u KB, compiled %.0f KB/s, clears full=%u all=%u, cold %u KB\n",
	       cache_used() / 1024, s_code_size / 1024, (double)(s_compiled_bytes - last) / 1024.0 / seconds,
	       s_clears_full, s_clears, (s_code_size - s_cold.lo) / 1024);
	last = s_compiled_bytes;
}

#ifdef ENABLE_PERF_MONITORING
void recSh4_GetPerfStats(u32* blocks_compiled, u32* cache_clears, u32* check_failures)
{
	if (blocks_compiled) *blocks_compiled = perf_blocks_compiled;
	if (cache_clears)    *cache_clears    = perf_cache_clears;
	if (check_failures)  *check_failures  = perf_block_check_fails;
}

void recSh4_ResetPerfStats()
{
	perf_blocks_compiled   = 0;
	perf_cache_clears      = 0;
	perf_block_check_fails = 0;
}
#endif // ENABLE_PERF_MONITORING

#endif // HOST_NO_REC

void Get_Sh4Recompiler(sh4_if* rv)
{
#ifdef HOST_NO_REC
	Get_Sh4Interpreter(rv);
#else
	rv->Run           = recSh4_Run;
	rv->Stop          = recSh4_Stop;
	rv->Step          = recSh4_Step;
	rv->Skip          = recSh4_Skip;
	rv->Reset         = recSh4_Reset;
	rv->Init          = recSh4_Init;
	rv->Term          = recSh4_Term;
	rv->IsCpuRunning  = recSh4_IsCpuRunning;
	rv->ResetCache    = recSh4_ClearCache;
#endif
}
