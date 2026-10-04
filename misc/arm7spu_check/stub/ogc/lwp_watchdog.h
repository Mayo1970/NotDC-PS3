#pragma once
// PC stand-in: arm7.cpp includes it for its off-by-default FIQ counter.
#include <stdint.h>
extern uint64_t ps3_timebase_hz;
static inline uint64_t gettime(void) { return 0; }
static inline uint64_t ticks_to_millisecs(uint64_t t) { return t; }
