#pragma once
// Plan 5.32 stall probe, on with JIT C-Calls: each second, the SH4 thread's slowest frame split by where its time
// went. Times are exclusive: a section entered inside another pauses the outer one. SH4 thread only.
#include "types.h"

enum { ST_JIT, ST_GDREAD, ST_GDDMA, ST_MAPLE, ST_DMA, ST_REND, ST_SPE, ST_SYS, ST_LOG, ST_SLEEP, ST_N };

extern "C" int g_jit_ccalls_preset;
extern bool g_stall_on;   // g_jit_ccalls_preset, latched at each vblank
extern u64 g_stall_nested;   // exclusive ticks of every section but sys, a running total ([sys] probe)

void stall_push_on(int cat);
void stall_pop_on();
static inline void stall_push(int cat) { if (g_stall_on) stall_push_on(cat); }
static inline void stall_pop() { if (g_stall_on) stall_pop_on(); }

// A section for the rest of the scope.
struct StallScope
{
	explicit StallScope(int cat) { stall_push(cat); }
	~StallScope() { stall_pop(); }
};

void stall_frame();               // at each vblank, first thing in spg_PaceFrame
void stall_dump(double secs);     // once a second, with the stats line
