#pragma once
// OPTIMIZATION_PLAN 5.12: SPE command ring and TA decoder. "SPE Doorbell" (VIDEO / DEBUG) compares the SPU lists
// with the PPU replay (S2); "SPE Draw" draws them with no PPU replay (S3). Both are read at boot.
#include "types.h"
#include "spu/spe_bell.h"

// Bytes of TA data between two DATA commands.
#define SPE_BATCH 2048

extern bool g_spe_on;
// S3: latched at init. Stays set if the SPE turns off; fence 0 then sends each job to the PPU replay.
extern bool g_spe_draw;

void ps3spe_sys_init();
bool ps3spe_init();
void ps3spe_term();
void ps3spe_stats(double secs);

// SH4 thread only: each call writes one ring entry and rings the bell. DATA returns its command number.
u32 ps3spe_data(u32 ctx, const u8* raw, u32 from, u32 to);
// opt: the context's TAOPT_* (bundle 4) until its next reset.
void ps3spe_reset(u32 ctx, u32 opt);
// The TAOPT_* an SPU started now gives every context before its first reset.
void ps3spe_boot_opt(u32 opt);
void ps3spe_cont(u32 ctx, u32 pos);
void ps3spe_soft();
u32 ps3spe_fence(u32 ctx, u32 pos);
// Waits until the SPU has run command cmd, so the raw bytes it named may be overwritten.
void ps3spe_wait_cmd(u32 cmd);

// Render thread: the SPU answer to fence id, or null when it is late (counted).
const SpeFence* ps3spe_wait_fence(u32 id);
const SpeOut* ps3spe_out(u32 ctx);
// One compared fence: ok or not, and the time the wait and the compare took.
void ps3spe_result(bool ok, u64 ticks);
// S3, after the job's last read of the shadow lists: the SPU may now reset that context.
void ps3spe_release(u32 id);
