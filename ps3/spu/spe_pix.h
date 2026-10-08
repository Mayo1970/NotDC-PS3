#pragma once
// OPTIMIZATION_PLAN 5.12 #4/#5: an SPU decodes textures (decode()) and converts the framebuffer (present_fb_convert())
// into A8R8G8B8 words in RSX memory. Shared by the PPU (ps3/ps3_spepix.cpp) and the SPU (spe_pix.c, spe_pix_job.h).
#include <stdint.h>

#define SPEPIX_VRAM_SIZE 0x800000u  // VRAM_SIZE in dc/pvr

// SpePixTex.flags
#define SPEPIX_TW    1u   // twiddled source (VQ and palette textures always are)
#define SPEPIX_VQ    2u
#define SPEPIX_PAL4  4u
#define SPEPIX_PAL8  8u
#define SPEPIX_IGNA  16u  // IgnoreTexA: alpha 255 (never with YUV)
#define SPEPIX_SWZ   32u  // RSX swizzled layout; else linear rows dst_pitch bytes apart
#define SPEPIX_TEST  64u  // boot test: 32 pattern words to dst_ea

// Texture job result bits, decode()'s black and alpha-0 checks.
#define SPEPIX_LIT    1u  // some texel has RGB != 0
#define SPEPIX_OPAQUE 2u  // some texel has alpha != 0

// One texture as describe() gives it. The render thread writes it, seq last.
typedef struct
{
	volatile uint32_t seq;
	uint32_t fmt, w, h;        // PixelFmt; size in texels (powers of two, 8 to 1024)
	uint32_t addr, pitch;      // VRAM offset after the mip and codebook offsets; texels per planar source row
	uint32_t flags, pal_base;  // SPEPIX_*; first palette entry
	uint32_t vq_cb, pal_ctrl;  // VQ codebook offset; PAL_RAM_CTRL & 3
	uint32_t dst_ea, dst_pitch;
} SpePixTex;

// One framebuffer present as present_fb_convert() computes it. The SH4 thread writes it, seq last.
typedef struct
{
	volatile uint32_t seq;
	uint32_t sof1, depth;       // FB_R_SOF1 (a 32-bit area address); FB_R_CTRL depth 0 (0555), 1 (565) or 3 (0888)
	uint32_t w, h, line_bytes;  // pixels and lines converted; bytes from one line start to the next
	uint32_t dst_ea, dst_pitch;
} SpePixFb;

// One lock line, two writers: each PPU thread stores only its own half. A store to either wakes the SPU.
typedef struct
{
	SpePixTex tex;
	uint32_t pad0[4];
	SpePixFb fb;
	volatile uint32_t quit;
	uint32_t pad1[6];
} __attribute__((aligned(128))) SpePixBell;

// SPU writes it with a fenced put after each job; counts are cumulative.
typedef struct
{
	volatile uint32_t tex_seq, tex_res;  // the last texture job and its SPEPIX_LIT/OPAQUE bits: one 64-bit load
	volatile uint32_t fb_seq;
	volatile uint32_t state;
	volatile uint32_t tex_jobs, tex_busy, tex_max, tex_kb;  // decrementer ticks in jobs, KB written
	volatile uint32_t fb_jobs, fb_busy, fb_max;
	volatile uint32_t wakes, timeouts;
	volatile uint32_t stage, ls_addr;
	uint32_t pad[17];
} __attribute__((aligned(128))) SpePixEcho;

// Read once by the SPU at start.
typedef struct
{
	uint32_t vram_ea;  // PVR VRAM, 8 MB
	uint32_t pal_ea;   // PALETTE_RAM, 1024 words, 4-byte aligned
	uint32_t pad[30];
} __attribute__((aligned(128))) SpePixSetup;
