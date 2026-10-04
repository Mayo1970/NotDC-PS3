#pragma once
// OPTIMIZATION_PLAN 5.15: the ARM7, nullAICA's registers and the caique control twin run on an SPU (spe_arm7). The SH4
// thread queues each MediumUpdate and its AICA register accesses; a register read waits for the SPU.
#include "types.h"

extern bool g_arm_spe;   // AUDIO "SPE ARM7", latched in InitAica; needs SPE AICA Sound
extern u32 g_arm_spe_dlo, g_arm_spe_dhi;

bool arm_spe_init(u8* aica_ram);   // after ps3aica_init
void arm_spe_term();
// SH4 thread: armUpdateARM's cycles and the AICA samples they make
void arm_spe_update(u32 cycles, u32 samples);
// SH4 AICA register access (0x00700000-0x00707FFF), as libAICA_*_aica_reg
u32  arm_spe_read(u32 addr, u32 size);
void arm_spe_write(u32 addr, u32 data, u32 size);
// An SH4 store to ARAM (offset < 2 MB); the SPU drops its cached copy of that line before the next command.
static inline void arm_spe_aram_written(u32 addr)
{
	const u32 l = addr >> 7;
	if (l < g_arm_spe_dlo)
		g_arm_spe_dlo = l;
	if (l > g_arm_spe_dhi)
		g_arm_spe_dhi = l;
}
void arm_spe_aram_all();   // ARAM changed as a whole (reset)
void arm_spe_reset();      // armReset
void arm_spe_flush();      // arm_FlushCode
void arm_spe_stats(double wall_s);   // SPG stats block, once per second; prints [arm7spe] every ~5 s
