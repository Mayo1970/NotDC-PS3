#pragma once
// OPTIMIZATION_PLAN 5.13 A1: caique AICA control pass on the PPU, the full model on an SPE from the write log,
// compared every AICA_BLOCK samples. "SPE AICA Check" (AUDIO), read at InitAica. nullAICA still makes the sound.
#include "types.h"

extern bool g_aica_spe_on;

bool ps3aica_init(u8* aica_ram);
void ps3aica_term();
// SH4 thread: a register write as nullAICA got it (old16 = the register before the write), and one sample.
void ps3aica_write(u32 off, u32 data, u32 size, u16 old16);
// Also prints the [aica-spe] line every ~5 s.
void ps3aica_step();
