#pragma once
// jmmu_check: the timebase sh4_mmu_jit.cpp reads for its [jmmu] ms line; each read is g_fake_step ticks after the last.
#include <cstdint>
extern uint64_t ps3_timebase_hz;
extern uint64_t g_fake_tb, g_fake_step;
static inline uint64_t gettime(void) { return g_fake_tb += g_fake_step; }
