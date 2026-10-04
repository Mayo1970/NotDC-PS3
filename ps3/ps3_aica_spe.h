#pragma once
// OPTIMIZATION_PLAN 5.13 A1/A2/A3: caique AICA control pass on the PPU, the full model on an SPE from the write log.
// AUDIO "SPE AICA Check": nullAICA plays, the SPU is checked. "SPE AICA Sound": the SPU plays ("SPE AICA DSP": with the DSP).
#include "types.h"

extern bool g_aica_spe_on;    // the SPU thread runs
extern bool g_aica_spe_snd;   // and makes the sound: nullAICA's mixer is off

bool ps3aica_init(u8* aica_ram);
void ps3aica_term();
// SH4 thread: a register write as nullAICA got it (old16 = the register before the write), and one sample.
void ps3aica_write(u32 off, u32 data, u32 size, u16 old16);
// Also prints the [aica-spe] lines every ~5 s.
void ps3aica_step();
// Sound mode: the channel monitor 0x2810 or 0x2814 at this sample; keep_lp for a byte read that leaves LP set.
u16 ps3aica_read_mon(u32 reg, bool keep_lp);
// Sound mode: the CD-DA sector (host-order s16 L/R) EXTS plays from this sample on.
void ps3aica_cdda(const s16* sector, bool silent);
