// 5.15 B1 test: the PPU timebase is not used by the code under test.
#pragma once
#include <stdint.h>
static inline uint64_t gettime(void) { return 0; }
static inline uint64_t ticks_to_millisecs(uint64_t t) { return t; }
