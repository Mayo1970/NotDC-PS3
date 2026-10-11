#pragma once
// OPTIMIZATION_PLAN 5.12 #2: the SPU builds draw_scene's merged vertex buffer and draw runs from the S3 lists.
// Shared by the PPU (ps3/ps3_spevb.cpp, ps3_rend.cpp) and the SPU (spe_vb.c, spe_vb_build.h).
#include <stdint.h>
#include "spe_bell.h"

#define SPEVB_RUN_MAX_VTX 16000  // RUN_MAX_VTX in ps3_rend.cpp
#define SPEVB_TR_MAX      4096   // TR strips the SPU sorts; with more, the PPU draws the TR list
#define SPEVB_MAX_RUNS    SPE_MAX_STRIPS
#define SPEVB_VTX_BYTES   36     // ps3gl_vertex_t: x*W, y*W, W, 1, u, v, 0, 0, RGBA
// The SPU writes each vertex as 3 whole quadwords (ps3gl_vertex_t and 12 zero bytes); the RSX reads stride 48.
#define SPEVB_VTX_STRIDE  48

// Request flags.
#define SPEVB_AUTOSORT 1u
#define SPEVB_TEST     2u  // boot test: 32 pattern words to vb_ea
#define SPEVB_BENCH    4u  // boot bench: DMA timings to the RSX block at vb_ea and the main memory block at ctx
// Bundle 4 (plan section 7), as s_mo_sprite and the s_key masks of ps3_rend.cpp.
#define SPEVB_SPRITE_CULL 8u   // P25: a sprite's cull direction is the other one
#define SPEVB_CLIP_KEY    16u  // P10: the clip rect bits (ISP 19:0, PCW 9:8) are part of the merge key
#define SPEVB_NO_TR       32u  // 3k: the PPU sorts the TR list by triangle; the build answers TR_PPU at once

// Echo flags. TR_PPU: the TR list is left to the PPU, which continues the vertex buffer at vb_used.
#define SPEVB_OP_DONE 1u
#define SPEVB_PT_DONE 2u
#define SPEVB_TR_DONE 4u
#define SPEVB_TR_PPU  8u
#define SPEVB_FINAL   (SPEVB_TR_DONE | SPEVB_TR_PPU)

// One merged GL strip: [first, first + count) of the vertex buffer, drawn with param's state.
typedef struct { uint32_t first, count, param, pad; } SpeRun;

// PPU writes, seq last. The SPU holds a reservation on the line, so the store of seq wakes it.
typedef struct
{
	volatile uint32_t seq;
	volatile uint32_t quit;
	uint32_t ctx, nvtx, nstrip, nparam;
	uint32_t vb_ea, cap, flags;
	uint32_t pad[23];
} __attribute__((aligned(128))) SpeVbBell;

// SPU writes it with fenced puts. seq and runs share one doubleword: the PPU reads them with one load.
typedef struct
{
	volatile uint32_t seq;         // the request these fields answer
	volatile uint32_t runs;        // runs whose records and vertices are in memory
	volatile uint32_t flags;       // SPEVB_*_DONE, SPEVB_TR_PPU
	volatile uint32_t sec_end[3];  // run count at the end of OP, PT and TR (valid with the flag)
	volatile uint32_t vb_used;     // vertices written
	volatile uint32_t strips[3];   // strips drawn per section
	volatile uint32_t state;
	volatile uint32_t builds, busy, max_busy;  // decrementer ticks in builds
	volatile uint32_t wakes, timeouts, trppu;
	volatile uint32_t stage, ls_addr;
	uint32_t pad[13];
} __attribute__((aligned(128))) SpeVbEcho;

// Diagnostic split of the build time (decrementer ticks, all cumulative), put after every build.
typedef struct
{
	volatile uint32_t t_build, t_slot, t_pub, t_slide, t_in, t_qfull, t_key, t_sort, t_tr;
	volatile uint32_t n_build, n_pub, n_slide, n_cmd, n_strip, n_vin, n_vout, kb_out, n_slowmul;
	// Boot bench: 1 MB of 16 KB puts to RSX and to main memory; 100 x (128-byte put + wait) each; 100 x
	// (192-byte get + wait) from main memory.
	volatile uint32_t b_put_rsx, b_put_xdr, b_lat_rsx, b_lat_xdr, b_get_xdr;
	uint32_t pad[9];
} __attribute__((aligned(128))) SpeVbDiag;

// Read once by the SPU at start.
typedef struct
{
	uint32_t out_ea[SPE_CTX];  // SpeOut of each context
	uint32_t runs_ea;          // SpeRun[SPEVB_MAX_RUNS]
	uint32_t diag_ea;          // SpeVbDiag
	uint32_t pad[26];
} __attribute__((aligned(128))) SpeVbSetup;
