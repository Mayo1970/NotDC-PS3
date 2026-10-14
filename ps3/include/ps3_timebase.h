#pragma once
// Cell timebase (mftb) helpers for the perf brackets and timers.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Set once at boot from sysGetTimebaseFrequency() (79.8 MHz on retail PS3).
extern uint64_t ps3_timebase_hz;

static inline uint64_t gettime(void)
{
	uint64_t tb;
	// Cell: a read at a rollover of the low 32 bits (each 53.8 s) can give them as 0 with the old high half, a time
	// 53.8 s in the past. Read again while they are 0, as PSL1GHT's __gettime() and Linux (CPU_FTR_CELL_TB_BUG).
	do
		__asm__ volatile("mftb %0" : "=r"(tb));
	while (__builtin_expect((uint32_t)tb == 0, 0));
	return tb;
}

static inline uint64_t ticks_to_millisecs(uint64_t t) { return t / (ps3_timebase_hz / 1000u); }
static inline uint64_t ticks_to_microsecs(uint64_t t) { return t / (ps3_timebase_hz / 1000000u); }
static inline uint64_t ticks_to_nanosecs(uint64_t t)  { return (t * 1000u) / (ps3_timebase_hz / 1000000u); }
static inline uint64_t diff_ticks(uint64_t a, uint64_t b) { return b - a; }

#ifdef __cplusplus
}
#endif
