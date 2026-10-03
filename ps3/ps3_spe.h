#pragma once
// OPTIMIZATION_PLAN 5.12 S0: SPE doorbell. Set before boot with "SPE Doorbell" (VIDEO / DEBUG).
#include "types.h"
#include "spu/spe_bell.h"

// Bytes of TA data between two rings, as the S1 batch size.
#define SPE_BATCH 2048

extern SpeBell g_spe_bell;
extern u32 g_spe_seq;      // PPU copy of the bell seq, so a ring never loads the reserved line
extern u32 g_spe_pending;  // TA bytes since the last ring
extern bool g_spe_on;

bool ps3spe_init();
void ps3spe_term();
void ps3spe_stats(double secs);

static inline void ps3spe_ring()
{
	__asm__ volatile("lwsync" ::: "memory");
	g_spe_bell.seq = ++g_spe_seq;
}

// List end, LIST_INIT, LIST_CONT, SOFTRESET, STARTRENDER.
static inline void ps3spe_ring_event()
{
	if (g_spe_on)
	{
		g_spe_pending = 0;
		ps3spe_ring();
	}
}

static inline void ps3spe_note_bytes(u32 n)
{
	if (g_spe_on && (g_spe_pending += n) >= SPE_BATCH)
	{
		g_spe_pending = 0;
		ps3spe_ring();
	}
}
