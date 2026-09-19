#pragma once
// PS3 stand-in for libogc's timebase helpers, used by the perf brackets and timers.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Set once at boot from sysGetTimebaseFrequency() (79.8 MHz on retail PS3).
extern uint64_t ps3_timebase_hz;

static inline uint64_t gettime(void)
{
	uint64_t tb;
	__asm__ volatile("mftb %0" : "=r"(tb));
	return tb;
}

static inline uint64_t ticks_to_millisecs(uint64_t t) { return t / (ps3_timebase_hz / 1000u); }
static inline uint64_t ticks_to_microsecs(uint64_t t) { return t / (ps3_timebase_hz / 1000000u); }
static inline uint64_t ticks_to_nanosecs(uint64_t t)  { return (t * 1000u) / (ps3_timebase_hz / 1000000u); }
static inline uint64_t diff_ticks(uint64_t a, uint64_t b) { return b - a; }

#ifdef __cplusplus
}
#endif
