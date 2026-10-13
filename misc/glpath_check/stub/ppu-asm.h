#pragma once
// Bundle 14 PC check: ps3gl_main.c reads the timebase for its telemetry only.
#include <stdint.h>
static inline uint64_t __gettime(void) { return 1; }
