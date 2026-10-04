#pragma once
// PC stand-in: the timebase is nanoseconds.
#include <stdint.h>
#include <time.h>
extern uint64_t ps3_timebase_hz;
static inline uint64_t gettime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
