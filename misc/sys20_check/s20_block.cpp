// sys20_check: sh4_mem.cpp's block writes (the cut between its sys20_check marks) on a flat RAM.
#include "types.h"
#define MEMCALL
extern int g_block_write_preset;
extern "C" int g_blockcopy_preset;
u8 g_blk_ram[RAM_SIZE + 8];
u32 g_blk_writes[3];   // by size: 8, 16, 32 bits

static u8* blk(u32 a) { return &g_blk_ram[a & RAM_MASK]; }
static void sh4_cached_invalidate_range(u32, u32) {}
static void blockcopy_announce() {}
static u8* _vmem_GetBlockPtr(u32 a, u32) { return blk(a); }
static u32 ReadMem32_nommu(u32 a) { u32 v; memcpy(&v, blk(a), 4); return v; }
static u16 ReadMem16_nommu(u32 a) { u16 v; memcpy(&v, blk(a), 2); return v; }
static u8 ReadMem8_nommu(u32 a) { return *blk(a); }
static void WriteMem32_nommu(u32 a, u32 v) { memcpy(blk(a), &v, 4); g_blk_writes[2]++; }
static void WriteMem16_nommu(u32 a, u16 v) { memcpy(blk(a), &v, 2); g_blk_writes[1]++; }
static void WriteMem8_nommu(u32 a, u8 v) { *blk(a) = v; g_blk_writes[0]++; }
// WriteMemBlock_ptr's path through the MMU: the same RAM here.
#define WriteMem32 WriteMem32_nommu
#define WriteMem16 WriteMem16_nommu
#define WriteMem8 WriteMem8_nommu
#include "block.inc"
