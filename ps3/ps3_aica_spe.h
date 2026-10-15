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
// 5.15 (ARM7 SPU): the log's addresses, the pacing point every AICA_BLOCK samples, a CD-DA sector into a free slot.
void ps3aica_spe_eas(u32* bell, u32* echo, u32* log);
void ps3aica_pace(u32 now);
u32 ps3aica_cdda_slot(const s16* sector, u32 ord);

// OPTIMIZATION_PLAN section 7, bundle 5 (6.6 SPE AICA Sync, P20 G2-G4): guest wave RAM writes and DSP register
// reads that must meet the sound SPU at their sample.
extern u8* g_aram_hz;         // the twin's page map (spu/spe_aica.h AicaHz), null while no SPU plays
extern bool g_aram_hz_stale;  // SPE ARM7: the SH4 is too far ahead of the map, so every write asks
extern u32 g_aram_hz_dma;     // 1 while a G2 DMA copies: the [aram-hz] writer
extern u32 g_aram_ring_base, g_aram_ring_len;   // PPU twin: the DSP ring the sound SPU mirrors (len 0 = none)
bool aram_hz_hit(u32 addr, u32 src);            // src: 0 SH4, 1 G2 DMA, 2 ARM7; true: SPE AICA Defer holds the store
void ps3aica_ring_wrote(u32 addr);
u16 ps3aica_dsp_read(u32 reg, u32 cpu, u16 old);   // cpu: 0 SH4, 1 ARM7; old = nullAICA's copy of the register
// Sound mode, before a store to wave RAM byte addr (< 2 MB): waits for the sound SPU when it has yet to play it.
// True: the store goes to arm_spe_defer instead (bundle 21, SPE AICA Defer).
static inline bool aram_hz_write(u32 addr, u32 src)
{
	bool held = false;
	if (g_aram_hz && (g_aram_hz_stale || (g_aram_hz[addr >> 11] & (0x80u >> ((addr >> 8) & 7)))))
		held = aram_hz_hit(addr, src);
	if (((addr - g_aram_ring_base) & 0x1FFFFFu) < g_aram_ring_len)
		ps3aica_ring_wrote(addr);
	return held;
}
// The AICA_SETUP_* flags the sound SPU got; the ARM7 SPU's twin must use the same (ps3_arm_spe.cpp).
u32 ps3aica_setup_flags();
// [aram-hz] every ~5 s while a bundle 5 row is on or a DSP register was read: hz = the twin's counts (AicaHz).
void ps3aica_hz_print(const void* hz, double w, const u32* ppu);
