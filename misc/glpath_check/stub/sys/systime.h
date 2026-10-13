#pragma once
// Bundle 14 PC check: the PS3's timebase frequency, for ps3gl_main.c's telemetry.
#include <stdint.h>
static inline uint64_t sysGetTimebaseFrequency(void) { return 79800000; }
