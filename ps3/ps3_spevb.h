#pragma once
// OPTIMIZATION_PLAN 5.12 #2, VIDEO / DEBUG "SPE Build" (set before boot, needs SPE Draw). ON: an SPU builds the
// merged vertex runs, the render thread only draws them. CHECK: the SPU's copy against a PPU dry pass, each frame.
#include "types.h"
#include "spu/spe_vb.h"

enum { SPEVB_OFF, SPEVB_ON, SPEVB_CHECK };
enum { SPEVB_SEC_OP, SPEVB_SEC_PT, SPEVB_SEC_TR };

bool ps3spevb_init();
void ps3spevb_term();
void ps3spevb_stats(double secs);

// Render thread. The mode is 0 once the builder is down.
int ps3spevb_mode();
bool ps3spevb_request(u32 ctx, u32 nvtx, u32 nstrip, u32 nparam, void* vb, u32 cap, bool autosort);
// Run k of a section; null at its end, at TR_PPU, or when the SPU fails (the builder is then down).
const SpeRun* ps3spevb_run(u32 k, u32 sec);
u32 ps3spevb_sec_start(u32 sec);
u32 ps3spevb_strips(u32 sec);
bool ps3spevb_tr_ppu();
u32 ps3spevb_vb_used();
// ON: apply_param's alpha force against the SPU's rule; counted when they differ.
void ps3spevb_note_alpha(bool same);
void ps3spevb_end_frame();

// CHECK: the SPU's copy, the dry pass's copy and runs, and their vertex capacity.
bool ps3spevb_check_bufs(u32** spu_vb, u32** dry_vb, SpeRun** dry_runs, u32* cap);
const SpeRun* ps3spevb_runs();
// The finished build's echo, or null when it is late (the builder is then down).
const volatile SpeVbEcho* ps3spevb_wait_final();
void ps3spevb_check_result(bool ok);
