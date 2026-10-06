#pragma once
// Plan 5.36 SYS GATE: the JIT runs the timeslice update only at a slice end where an event is due. Read and
// write hooks keep the skipped slices invisible to the guest. Off unless the JIT main loop turns it on.
#include "types.h"

extern bool g_sys_gate_on;

void sys_gate_sync_slow();
void sys_gate_kick_slow();
s32  sys_gate_pending_slow();

// Before a write that changes a deadline: run the slices the gate skipped, so they see the old state.
static inline void sys_sync() { if (g_sys_gate_on) sys_gate_sync_slow(); }
// After that write: the update runs at the next slice end and plans again.
static inline void sys_kick() { if (g_sys_gate_on) sys_gate_kick_slow(); }
// Slice ends not yet run, for reads that work out a value without running them (TCNT, SPG_STATUS).
static inline s32 sys_pending() { return g_sys_gate_on ? sys_gate_pending_slow() : 0; }

// Slice ends until the module's next event, at most max (each is 1..max).
s32 tmu_slices_to_event(s32 ts, s32 max);
s32 spg_slices_to_event(s32 ts, s32 max);
s32 aica_dma_slices_to_event(s32 ts, s32 max);

s32  sh4_GetTimeslice();
void sys_gate_install();
void tmu_gate_install();
void sys_gate_stats(double secs);
