// Block manager: SH4 PC -> compiled block lookup for the dynarec.
// The extern "C" block is for assembly code on Linux hosts (Beagleboard/Pandora).
#include "types.h"
#pragma once

#if HOST_OS==OS_LINUX
extern "C" {
#endif

// Block manager configuration
// Kept at 16K buckets like the original
#define BM_BLOCKLIST_COUNT (16384)
#define BM_BLOCKLIST_MASK (BM_BLOCKLIST_COUNT-1)
#define BM_BLOCKLIST_SHIFT 2

// Bucket capacity: adjust to the available memory
#ifndef BM_INITIAL_CAPACITY
#define BM_INITIAL_CAPACITY 16      // Initial blocks per hash bucket
#endif

#ifndef BM_MAX_BLOCKS_PER_BUCKET
#define BM_MAX_BLOCKS_PER_BUCKET 64 // Prevent excessive growth in single bucket
#endif

// Enable statistics tracking (disable in release builds for better performance)
// #define BM_ENABLE_STATS

typedef void DynarecCodeEntry();

// Core block manager functions (compatible with original API)
DynarecCodeEntry* FASTCALL bm_GetCode(u32 addr);
void bm_AddCode(u32 addr, DynarecCodeEntry* code);
void bm_Reset();

// Block structure (kept compatible with original for assembly code)
struct DynarecBlock
{
	DynarecCodeEntry* code;
	u32 addr;
	u32 lookups;
	// MMU JIT (P38) only: the physical pc, the mode bits (sh4_mmu_jit.h) and the code page's generation.
	u32 paddr, cfg, pgen;
};

// Cache array exposed for potential assembly access (like original)
extern DynarecBlock* cache[BM_BLOCKLIST_COUNT];

// BCACHE: {addr, code} mirror of cache[], one entry per bucket, for the JIT's dynamic branches (ps3_jit_bcache.h).
// addr 0xFFFFFFFF (odd, never an SH4 PC) = empty, else code is valid: a u32, as the code cache lies below 4 GB.
struct BlockCacheFlatEntry
{
	u32 addr;
	u32 code;
};
static_assert(sizeof(BlockCacheFlatEntry)==8, "the JIT's lookup reads 8-byte entries");
extern BlockCacheFlatEntry bm_bcache[BM_BLOCKLIST_COUNT];

// Not in the original API
void bm_Init();
bool bm_RemoveCode(u32 addr);
void bm_InvalidateRange(u32 start_addr, u32 end_addr);

// MMU JIT (P38): one virtual pc can have a block per physical pc and mode; NULL when there is none.
DynarecBlock* bm_GetBlockMmu(u32 vaddr, u32 paddr, u32 cfg);
void bm_AddCodeMmu(u32 vaddr, u32 paddr, u32 cfg, u32 pgen, DynarecCodeEntry* code);

// Statistics functions (only available when BM_ENABLE_STATS is defined)
#ifdef BM_ENABLE_STATS
void bm_GetStats(u32* hits, u32* misses, u32* cache_hits, u32* total_blocks);
void bm_ResetStats();
#endif

#if HOST_OS==OS_LINUX
}
#endif
