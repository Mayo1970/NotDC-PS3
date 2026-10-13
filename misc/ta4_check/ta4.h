#pragma once
// Bundle 4 PC check: what each decoder build gives the driver. Lists as plain words, so the old and the new
// layouts compare the same way.
#include <stdint.h>

#define T4_MAX_VTX    (64 * 1024)
#define T4_MAX_STRIPS (16 * 1024)
#define T4_MAX_PARAMS (8 * 1024)
#define T4_MAX_MODTRI 8192
#define T4_MAX_MODVOL 256
#define T4_MAX_PASSES 8

typedef struct
{
	uint32_t nvtx, nstrip, nparam, nmtri, nmvol;
	uint32_t max_inv_w, min_w, max_w, mv_tris[5];
	uint32_t opt;                     // what the SPU's fence says (new builds)
	uint32_t npass;                   // the PPU replay's pass marks (new build)
	uint32_t pass_strip[T4_MAX_PASSES], pass_mvol[T4_MAX_PASSES];
	uint32_t vtx[T4_MAX_VTX][6];      // x, y, w, col, u, v
	uint32_t strip[T4_MAX_STRIPS][4]; // first, count, list, param
	uint32_t param[T4_MAX_PARAMS][4]; // pcw, isp, tsp, tcw
	uint32_t mtri[T4_MAX_MODTRI][9];
	uint32_t mvol[T4_MAX_MODVOL][5];  // first, cull, depth, volume_last, list
	uint32_t offs[T4_MAX_VTX];        // bundle 17: the offset colours (new builds, with TAOPT_OFFS)
} T4Lists;

#ifdef __cplusplus
extern "C" {
#endif
// The PPU replay of one context: reset (the list init), then data and cont marks in stream order, then the lists.
void ppu_new_reset(uint32_t opt, int bad_skip);
void ppu_new_data(const uint32_t* rec, uint32_t nrec);
void ppu_new_cont(void);
void ppu_new_lists(T4Lists* l);
uint32_t ppu_new_bad(void);
// Bundle 17: sprites whose fourth corner came from the plane, counted by each decoder.
uint32_t ppu_new_planes(void);
uint32_t spu_new_planes(void);
// spr_div as the SPU builds it (bit by bit); the driver's own divide is the PPU's form.
double spu_new_div(double a, double b);
void ppu_ref_reset(uint32_t opt, int bad_skip);
void ppu_ref_data(const uint32_t* rec, uint32_t nrec);
void ppu_ref_cont(void);
void ppu_ref_lists(T4Lists* l);

// The SPU decoder: init once (its shadow lists live in the arena), then the bell's commands.
void spu_new_init(uint32_t opt);
void spu_new_reset(uint32_t ctx, uint32_t opt);
void spu_new_data(uint32_t ctx, const uint32_t* rec, uint32_t nrec);
void spu_new_fence(uint32_t ctx, T4Lists* l);
uint32_t spu_new_bad(void);
void spu_ref_init(uint32_t opt);
void spu_ref_reset(uint32_t ctx, uint32_t opt);
void spu_ref_data(uint32_t ctx, const uint32_t* rec, uint32_t nrec);
void spu_ref_fence(uint32_t ctx, T4Lists* l);

void spu_arena(uint32_t size);
uint32_t spu_ea(const void* p);
int shim_queued(void);
extern uint8_t* g_arena;
extern uint64_t g_seed;
#ifdef __cplusplus
}
#endif
