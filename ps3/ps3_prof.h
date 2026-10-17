#pragma once
// Row SH4 Time Profile: the JIT stores the running block's guest pc, or a C part's code, in g_prof_where; a sampler
// thread reads it ~2000 times a second and prints the SH4 thread's time by part and by block every 5 s.
#include "types.h"

enum { PROF_NONE, PROF_TA, PROF_MEM, PROF_SYS, PROF_INT, PROF_LOOKUP, PROF_LINK, PROF_COMPILE, PROF_IFB, PROF_SYNC,
       PROF_OTHER, PROF_RENDER, PROF_PARTS };
#define PROF_PART(k) (0xFFFFFF00u | (u32)(k))

extern "C" volatile u32 g_prof_where;

// Once at boot with the row ON: the sampler thread. code_lo/code_hi: the code cache, for the block dumps.
void prof_start(uintptr_t code_lo, uintptr_t code_hi);
// ngen_Compile with the row ON: where block pc's code is, for the dumps.
void prof_note_block(u32 pc, const void* host, u32 hot_bytes, u32 total_bytes, u32 sh4_bytes);
// A code cache clear: the noted code is gone.
void prof_reset_blocks();
