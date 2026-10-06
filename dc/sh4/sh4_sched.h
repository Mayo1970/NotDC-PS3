// Cycle-deadline scheduler (nullDC PSP port / libswirl): fires DMA/GD-ROM/render/IRQ events in true deadline order
// at 448cy granularity, which the timeslice cascade breaks. Own clock (sh4_sched_tick), not the JIT's; inert unless SCHED is on.

#pragma once

#include "types.h"

// tag: the value given to sh4_sched_register (one function can serve several ids); sch_cycl: the cycles requested;
// jitter: cycles late (0 unless a deadline fell mid-tick). Return > 0 to re-arm that many cycles later, else <= 0.
typedef int sh4_sched_callback(int tag, int sch_cycl, int jitter);

// Register a callback. Returns an id used by sh4_sched_request/elapsed.
// Registration is permanent for the session (ids are stable indices).
int sh4_sched_register(int tag, sh4_sched_callback* ssc);

// Current scheduler time in SH4 cycles since reset. 32-bit form wraps
// (~21 emulated seconds); 64-bit form effectively never wraps.
u32 sh4_sched_now();
u64 sh4_sched_now64();

// Schedule id to fire `cycles` SH4 cycles from now. cycles == -1 disables it.
// Calling again replaces any previous pending request for that id.
void sh4_sched_request(int id, int cycles);

// Cycles elapsed for this id since it was armed / last queried (-1 if disabled).
int sh4_sched_elapsed(int id);

// Advances the clock by `cycles` and fires the callbacks due in that window, in deadline order. Called once per
// timeslice from UpdateSystem when the `sched` preset is on.
void sh4_sched_tick(int cycles);

// Drop all pending requests and zero the clock (called on hard reset). Keeps the
// registered callback list intact so re-registration is not required.
void sh4_sched_reset();

// Remove every registration too (for a full teardown). Rarely needed.
void sh4_sched_cleanup();

struct sched_list
{
	sh4_sched_callback* cb;
	int tag;
	s64 start;   // absolute cycle at which the pending request was armed
	s64 end;     // absolute cycle deadline; -1 = disabled
};
