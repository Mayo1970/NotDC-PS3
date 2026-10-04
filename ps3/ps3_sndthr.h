#pragma once
// OPTIMIZATION_PLAN 5.14: the ARM7 and the AICA run on a second PPU thread. The SH4 thread queues each
// MediumUpdate; whoever holds the token runs sound code, so the two threads never touch sound state at once.
#include "types.h"

enum { SND_LOCK_REG, SND_LOCK_MENU, SND_LOCK_STATS, SND_LOCK_OTHER, SND_LOCK_N };

extern bool g_snd_thread;              // AUDIO "Sound Thread", latched in InitAica
extern volatile bool g_snd_on_worker;  // the worker thread runs sound code now

void snd_thread_init();
// SH4 thread, once per MediumUpdate: the cycles and the AICA samples they make.
void snd_push(u32 cycles, u32 samples);
// SH4 thread: sole access to sound state, caught up to the last MediumUpdate. Nests.
void snd_lock(u32 why);
void snd_unlock();
// Sound code: the level of the Holly SPU interrupt; the SH4 thread applies it.
void snd_irq_set(bool on);
// Sound code (cdda_refill): the next CD-DA sector, read on the SH4 thread at its sample.
void snd_cdda_pop(s16* sector);
// After sh4_cpu.Run() returns: the worker stops and the SH4 thread runs what it left.
void snd_thread_stop();
// Once per second from the SPG stats block, under snd_lock; prints [sndthr] every ~5 s.
void snd_thread_stats();
