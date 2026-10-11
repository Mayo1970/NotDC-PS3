#pragma once
// OPTIMIZATION_PLAN 5.12 S3: shared by the PPU (ps3/ps3_spe.cpp, ps3_rend.cpp) and the SPU (spe_bell.c, spe_dec.c).
// Bell and echo each fill one 128-byte lock line, so a store to the bell never touches the echo.
#include <stdint.h>

#define SPE_STATE_RUNNING 1
#define SPE_STATE_EXITED  2

#define SPE_RING    4096  // command entries, a power of two
#define SPE_RESULTS 16    // fence result slots
#define SPE_CTX     4     // TA_CONTEXTS in ps3_rend.cpp

// List capacities: MAX_VTX, MAX_STRIPS, MAX_PARAMS, MAX_MODTRI, MAX_MODVOL in ps3_rend.cpp.
#define SPE_MAX_VTX    (64 * 1024)
#define SPE_MAX_STRIPS (16 * 1024)
#define SPE_MAX_PARAMS (8 * 1024)
#define SPE_MAX_MODTRI 8192
#define SPE_MAX_MODVOL 256
// The triangle cap without TAOPT_VOL_8K (the cap before bundle 4).
#define SPE_MODTRI_OLD 4096

// Plan section 7, bundle 4: decode options of one context, latched at its RESET; the PPU replay reads the same word.
#define TAOPT_INT_CLAMP  1u   // P8: face colour and intensity as saturated bytes, then face * intensity / 256
#define TAOPT_FACE_RESET 2u   // P63: the face colours are white again at each list init
#define TAOPT_TR_VOL     4u   // P27: the translucent volume list is kept, tagged 1 in the volume record
#define TAOPT_VOL_8K     8u   // P66: 8192 volume triangles
#define TAOPT_TILE_CLIP  16u  // P10: the user clip rect in the param's reserved bits (ISP 19:0, PCW 9:8)
// Flycast's tileclip rect, xmin | xmax << 6 | ymin << 12 | ymax << 17, at a list init: tiles 0..39 by 0..14.
#define TA_CLIP_RESET    ((39u << 6) | (14u << 17))

enum { SPE_OP_NOP, SPE_OP_DATA, SPE_OP_RESET, SPE_OP_CONT, SPE_OP_SOFT, SPE_OP_FENCE };
enum { SPE_STAGE_RUN, SPE_STAGE_RING, SPE_STAGE_DATA, SPE_STAGE_FENCE, SPE_STAGE_ECHO, SPE_STAGE_EVENT, SPE_STAGE_OUT,
       SPE_STAGE_RELEASE };

// DATA: ea = raw + from, a = bytes, b = to. CONT: b = mark. FENCE: a = fence id, b = raw end. RESET: a = TAOPT_*.
typedef struct
{
	uint8_t op, ctx;
	uint16_t pad;
	uint32_t ea, a, b;
} SpeCmd;

// PPU writes, SPU holds a reservation on it. A PPU store kills the reservation and wakes the SPU.
typedef struct
{
	volatile uint32_t head;  // commands written to the ring
	volatile uint32_t quit;
	// S3, render thread: last fence whose lists it has drawn. A RESET waits for it.
	volatile uint32_t released;
	uint32_t pad[29];
} __attribute__((aligned(128))) SpeBell;

// SPU writes it with mfc_put; the PPU only reads it.
typedef struct
{
	volatile uint32_t head;       // last bell head the SPU saw
	volatile uint32_t tail;       // commands done; the PPU reuses ring slots and raw bytes below it
	volatile uint32_t fence;      // last fence id in the results
	volatile uint32_t handled;    // wakes that found new commands
	volatile uint32_t wakes;      // reservation-lost events
	volatile uint32_t timeouts;   // 1 ms decrementer events
	volatile uint32_t rescued;    // new commands found by a timeout with no lost event pending
	volatile uint32_t state;
	volatile uint32_t data_bytes; // raw bytes pulled
	volatile uint32_t gaps;       // DATA that did not start at its context's position
	volatile uint32_t cont_errs;  // CONT whose mark was not the context's position
	// Debug: the PPU reads these from LS on a stall (the copy in main memory is stale by then).
	volatile uint32_t ls_addr;    // LS address of the SPU's echo
	volatile uint32_t stage;      // SPE_STAGE_*
	volatile uint32_t cmd_op, cmd_ctx, cmd_ea, cmd_a, cmd_b;
	volatile uint32_t dma_ea, dma_size;
	// Decoder: decrementer ticks spent on commands, and the cases where it may part from the PPU replay.
	volatile uint32_t busy;
	volatile uint32_t vtx_of;     // vertices dropped at MAX_VTX
	volatile uint32_t odd;        // non-vertex record inside an open strip (the PPU reads it as a vertex)
	volatile uint32_t bad;        // reserved para types and invalid obj_ctrl (the PPU dies or runs off its tables)
	volatile uint32_t pfull;      // params past MAX_PARAMS (the PPU rewrites the last slot, the SPU drops them)
	volatile uint32_t wden;       // 1/W below FLT_MIN (the PPU keeps a denormal)
	volatile uint32_t rel_waits;  // S3: RESETs that waited for the render thread, and the ticks they waited
	volatile uint32_t rel_ticks;
	uint32_t pad[4];
} __attribute__((aligned(128))) SpeEcho;

// One per FENCE: the context's position and list counts since its last RESET. Floats as bits.
typedef struct
{
	uint32_t id, ctx, pos, ncont;
	uint32_t gaps, nvtx, nstrip, nparam;
	uint32_t nmtri, nmvol, max_inv_w, min_w;
	uint32_t max_w, mv_tris[5];
	uint32_t opt;  // the TAOPT_* these lists were decoded with
	uint32_t pad[13];
} __attribute__((aligned(128))) SpeFence;

// Mirrors of DcVertex, DcStrip, DcParam, ModTri and ModVolParam; ps3_rend.cpp asserts the layouts.
typedef struct { uint32_t x, y, w, col, u, v; } SpeVtx;
typedef struct { uint32_t first; uint16_t count; uint8_t list, pad; uint16_t param, pad2; } SpeStrip;
typedef struct { uint32_t pcw, isp, tsp, tcw; } SpeParam;
typedef struct { uint32_t v[9]; } SpeModTri;
// list: 0 = the opaque volume list, 1 = the translucent one (P27).
typedef struct { uint32_t first; uint8_t cull, depth, volume_last, list; } SpeModVol;

// Shadow lists the SPU decoder writes, one per context. Every array starts on 128 bytes.
typedef struct
{
	SpeVtx vtx[SPE_MAX_VTX];
	SpeStrip strip[SPE_MAX_STRIPS];
	SpeParam param[SPE_MAX_PARAMS];
	SpeModTri mtri[SPE_MAX_MODTRI];
	SpeModVol mvol[SPE_MAX_MODVOL];
} __attribute__((aligned(128))) SpeOut;

// Read once by the SPU at start.
typedef struct
{
	uint32_t out_ea[SPE_CTX];
	uint32_t draw;  // S3: the render thread draws the shadow lists, so a RESET waits for its release
	uint32_t opt;   // TAOPT_* of every context until its first RESET
	uint32_t pad[26];
} __attribute__((aligned(128))) SpeSetup;

// One block for setup, ring and results: the SPU main only gets 3 thread arguments (arg3 arrives as 0).
typedef struct
{
	SpeSetup setup;
	SpeCmd ring[SPE_RING];
	SpeFence results[SPE_RESULTS];
} __attribute__((aligned(128))) SpeShared;
