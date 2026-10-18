#pragma once
// rsxwatch_check: the clock is the harness's.
#include <stdint.h>
extern uint64_t ps3_timebase_hz;
uint64_t gettime(void);
