#pragma once
// OPTIMIZATION_PLAN 5.12 #3, VIDEO / DEBUG "SPE Tex Hash" (set before boot). ON: an SPU hashes each scene's textures and
// ps3tex_bind takes its hashes. CHECK: the PPU hashes as before and compares at each first bind of a frame.
#include "types.h"
#include "spu/spe_tex.h"

enum { SPETEX_OFF, SPETEX_ON, SPETEX_CHECK };

bool ps3spetex_init();
void ps3spetex_term();
void ps3spetex_stats(double secs);

// Render thread. The mode is 0 once the SPU is down.
int ps3spetex_mode();
// Starts the hashes of one scene: its params, its background param and the registers describe() reads.
void ps3spetex_request(const void* params, u32 nparam, u32 bg_pcw, u32 bg_tsp, u32 bg_tcw, u32 text_control,
                       u32 pal_ram_ctrl);
// The SPU's hash for slot idx of this scene, after a wait if it is not there yet. False: no slot, the SPU left it
// to the PPU (no texture, YUV), or the SPU is late (it is then down).
bool ps3spetex_hash(u32 idx, u32* h);
// CHECK: the SPU's hash against the PPU's for one slot.
void ps3spetex_check(u32 idx, u32 tsp, u32 tcw, u32 spu, u32 ppu);
