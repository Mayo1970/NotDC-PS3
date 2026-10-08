#pragma once
// OPTIMIZATION_PLAN 5.12 #4/#5, VIDEO / DEBUG "SPE Tex Decode" / "SPE FB Convert" (set before boot): one SPU decodes
// textures and converts the direct framebuffer into RSX memory. CHECK: it writes a main-memory copy the PPU compares.
#include "types.h"
#include "spu/spe_pix.h"

enum { SPEPIX_OFF, SPEPIX_ON, SPEPIX_CHECK };

bool ps3spepix_init();
void ps3spepix_term();
void ps3spepix_stats(double secs);

// The modes, OFF once the SPU is down.
int ps3spepix_tex_mode();
int ps3spepix_fb_mode();
// The SPE FB Convert preset, before init: ps3_rend.cpp allocates the framebuffer textures with it.
int ps3spepix_fb_preset();

// Render thread: one texture decode, waited for; *res gets SPEPIX_LIT | SPEPIX_OPAQUE. False: the SPU is down, or it
// was late and is down now.
bool ps3spepix_tex(const SpePixTex* job, u32* res);
// SH4 thread: one framebuffer convert, waited for.
bool ps3spepix_fb(const SpePixFb* job);
// The decode left to the PPU: the source would cross the end of VRAM.
void ps3spepix_tex_skip();

// CHECK: the SPU's copy and the PPU's words, 4 MB each (null when not in CHECK); the framebuffer copy, 768 x 576 words.
u32* ps3spepix_tex_check_buf(int ppu);
u32* ps3spepix_fb_check_buf();
// The result of one compare; on a mismatch, what to print (the first 8 only).
void ps3spepix_tex_check(bool ok, const char* what);
void ps3spepix_fb_check(bool ok, const char* what);
