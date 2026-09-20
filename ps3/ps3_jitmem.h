#pragma once
#include "types.h"

// PS3 JIT code cache. It lives in .text because that is the only segment lv2
// gives execute permission to (Dynarec.MD 3.1).

extern u8* ps3_CodeCache;

void ps3_jit_icache_sync(const void* start, u32 size);

// ELFv1: a function pointer is a descriptor. Returns the real code address.
u64 ps3_opd_entry(void* fn);

// Emits a real ELFv1 function into the cache, calls out to C, and returns.
// Validates Dynarec.MD 3.2 on hardware. nullDC.cfg [ps3] jitprobe, 0 to skip.
void ps3_jit_selftest();
