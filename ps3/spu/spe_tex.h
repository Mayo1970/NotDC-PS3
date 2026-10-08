#pragma once
// OPTIMIZATION_PLAN 5.12 #3: an SPU hashes the textures of each scene's params, as ps3_tex.cpp's hash_texture().
// Shared by the PPU (ps3/ps3_spetex.cpp, ps3_tex.cpp) and the SPU (spe_tex.c, spe_tex_hash.h).
#include <stdint.h>
#include "spe_bell.h"

#define SPETEX_VRAM_SIZE 0x800000u  // VRAM_SIZE in dc/pvr
#define SPETEX_BG    SPE_MAX_PARAMS          // result slot of the background param
#define SPETEX_SLOTS (SPE_MAX_PARAMS + 16)   // the background slot fills a 128-byte line of its own
#define SPETEX_NONE  0xFFFFFFFFu             // a bind with no slot: the PPU hashes
#define SPETEX_SKIP  0x80000000u             // tag bit: the SPU did not hash it (no texture, or YUV)
#define SPETEX_SEQ   0x7FFFFFFFu             // tag bits that carry the request number

// One per param slot, written by the SPU in whole 128-byte lines. The PPU reads it with one 64-bit load.
typedef struct { uint32_t hash, tag; } SpeTexRes;

// PPU writes, seq last. The SPU holds a reservation on the line, so the store of seq wakes it.
typedef struct
{
	volatile uint32_t seq;
	volatile uint32_t quit;
	uint32_t params_ea, nparam;
	uint32_t bg_pcw, bg_tsp, bg_tcw;
	uint32_t text_control, pal_ram_ctrl;
	uint32_t pad[23];
} __attribute__((aligned(128))) SpeTexBell;

// SPU writes it with a fenced put after each request; counts are cumulative.
typedef struct
{
	volatile uint32_t seq;        // last request done
	volatile uint32_t state;
	volatile uint32_t reqs, busy, max_busy;  // decrementer ticks in requests
	volatile uint32_t params, hashed, dups, skips, bytes, wraps;
	volatile uint32_t wakes, timeouts;
	volatile uint32_t stage, ls_addr;
	uint32_t pad[17];
} __attribute__((aligned(128))) SpeTexEcho;

// Read once by the SPU at start.
typedef struct
{
	uint32_t vram_ea;  // PVR VRAM, 8 MB
	uint32_t pal_ea;   // PALETTE_RAM, 1024 words, 4-byte aligned
	uint32_t res_ea;   // SpeTexRes[SPETEX_SLOTS], 128-byte aligned
	uint32_t pad[29];
} __attribute__((aligned(128))) SpeTexSetup;
