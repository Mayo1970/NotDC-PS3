#pragma once
// fullmmu_check: the stats line's clock, a counter in the harness.
#include <stdint.h>
extern "C" uint64_t ps3_timebase_hz;
extern "C" uint64_t gettime(void);
