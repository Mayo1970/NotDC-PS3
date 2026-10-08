#pragma once
// Plan 5.17 probe: about 1 SQ flush in 64 gets mftb stamps along the TA store path, and [ta-split]
// prints the split once a second (plugs/drkPvr/ta.cpp). The other flushes pay one counter step.
#include "types.h"
#include "ps3_timebase.h"

struct TaSplit
{
	u32 calls;  // do_sqw calls, every target
	u32 next;   // call number of the next sample
	u32 armed;  // the running do_sqw call is a sample
	u32 hit;    // the sample reached ps3ta_store_sq
	u32 ram;    // the sample wrote main RAM; t_store and t_copy then bracket the block write
	u64 t_store, t_copy, t_fsm;
};
extern TaSplit g_ta_split;
extern u32 g_jit_sq_sites;   // Step 3: store sites the JIT gave the store-queue shape (ps3_driver.cpp)
extern u32 g_jit_sq_la_sites, g_jit_sq_cold_sites;   // plan 5.27: look-ahead shapes, cold-path SQ tests

void ta_split_leave(u64 t_entry);
void ta_split_dump(double seconds);

// A stamp inside a sampled call. The memory clobber keeps the loads and stores on their side of it.
static inline u64 ta_stamp()
{
	u64 tb;
	__asm__ volatile("mftb %0" : "=r"(tb) : : "memory");
	return tb;
}

// do_sqw entry: the entry stamp on a sampled call, else 0.
static inline u64 ta_split_enter()
{
	if (++g_ta_split.calls != g_ta_split.next)
		return 0;
	g_ta_split.armed = 1;
	return gettime();
}
