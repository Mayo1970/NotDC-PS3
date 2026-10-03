#pragma once
// OPTIMIZATION_PLAN 5.12 S1: SPE command ring. Set before boot with "SPE Doorbell" (VIDEO / DEBUG).
#include "types.h"
#include "spu/spe_bell.h"

// Bytes of TA data between two DATA commands.
#define SPE_BATCH 2048

extern bool g_spe_on;

bool ps3spe_init();
void ps3spe_term();
void ps3spe_stats(double secs);

// SH4 thread only: each call writes one ring entry and rings the bell.
void ps3spe_data(u32 ctx, const u8* raw, u32 from, u32 to);
void ps3spe_reset(u32 ctx);
void ps3spe_cont(u32 ctx, u32 pos);
void ps3spe_soft();
u32 ps3spe_fence(u32 ctx, u32 pos);

// Same sums as the SPU; bytes is a multiple of 16 and p starts the stream at a lane-0 word.
void ps3spe_hash(SpeHash* h, const u8* p, u32 bytes);
// Waits for the SPU answer to fence id and compares it with the PPU's own replay.
void ps3spe_check(u32 id, u32 ctx, u32 pos, u32 ncont, const SpeHash* h);
