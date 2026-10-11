// Bundle 4 PC check, SPU side: spe_dec.c on the shims, over the fake EA space of mfc_shim.c. Built twice by run.sh:
// the tree's file (or SPE_DEC_SOURCE, a mutated copy), and with T4_REF the frozen file of before bundle 4.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ta4.h"

#ifdef T4_REF
#define SIDE(x) spu_ref_##x
#else
#define SIDE(x) spu_new_##x
#endif
// One name space per build: the decoder's entry points and the echo it counts in.
#define dec_init    SIDE(dec_init)
#define dec_reset   SIDE(dec_reset)
#define dec_records SIDE(dec_records)
#define dec_fence   SIDE(dec_fence)
#define echo        SIDE(echo)

#include <spu_intrinsics.h>
// The decoder subtracts doubles too; the shim's spu_sub takes words only.
static inline vec_double2 shim_sub_d(vec_double2 a, vec_double2 b) { return a - b; }
#define spu_sub(a, b) _Generic((a), vec_double2: shim_sub_d, default: spu_sub)(a, b)

#include "spe_dec.h"
SpeEcho echo;
#include SPE_DEC_SOURCE

static uint8_t* out_mem[SPE_CTX];

void SIDE(init)(uint32_t opt)
{
	static SpeSetup setup;
	// Each build has its own shadow lists: the two SpeOut layouts differ.
	static uint8_t* block;
	if (!block)
	{
		block = g_arena + (
#ifdef T4_REF
			0
#else
			(32u << 20)
#endif
		);
	}
	memset(&setup, 0, sizeof(setup));
	for (int i = 0; i < SPE_CTX; i++)
	{
		out_mem[i] = block + (size_t)i * sizeof(SpeOut);
		setup.out_ea[i] = spu_ea(out_mem[i]);
	}
#ifndef T4_REF
	setup.opt = opt;
#else
	(void)opt;
#endif
	if (sizeof(SpeOut) * SPE_CTX > (32u << 20))
	{
		printf("FAIL: SpeOut does not fit the arena half\n");
		exit(1);
	}
	dec_init(&setup);
}

void SIDE(reset)(uint32_t ctx, uint32_t opt)
{
#ifdef T4_REF
	(void)opt;
	dec_reset(ctx);
#else
	dec_reset(ctx, opt);
#endif
}

void SIDE(data)(uint32_t ctx, const uint32_t* rec, uint32_t nrec)
{
	// The bell pulls each chunk into a 128-byte aligned LS buffer.
	static uint32_t buf[512 * 8] __attribute__((aligned(128)));
	memcpy(buf, rec, (size_t)nrec * 32);
	dec_records(ctx, buf, nrec);
}

void SIDE(fence)(uint32_t ctx, T4Lists* l)
{
	static SpeFence res;
	memset(&res, 0, sizeof(res));
	dec_fence(ctx, &res);
	if (shim_queued())
	{
		printf("FAIL: DMA left in the queue after a fence\n");
		exit(1);
	}
	const SpeOut* o = (const SpeOut*)out_mem[ctx];
	l->nvtx = res.nvtx;
	l->nstrip = res.nstrip;
	l->nparam = res.nparam;
	l->nmtri = res.nmtri;
	l->nmvol = res.nmvol;
	l->max_inv_w = res.max_inv_w;
	l->min_w = res.min_w;
	l->max_w = res.max_w;
	memcpy(l->mv_tris, res.mv_tris, sizeof(l->mv_tris));
	l->npass = 0;
#ifdef T4_REF
	l->opt = 0;
#else
	l->opt = res.opt;
#endif
	if (res.nvtx > T4_MAX_VTX || res.nstrip > T4_MAX_STRIPS || res.nparam > T4_MAX_PARAMS || res.nmtri > T4_MAX_MODTRI ||
	    res.nmvol > T4_MAX_MODVOL)
	{
		printf("FAIL: SPU list counts past the caps\n");
		exit(1);
	}
	memcpy(l->vtx, o->vtx, (size_t)res.nvtx * sizeof(SpeVtx));
	for (uint32_t i = 0; i < res.nstrip; i++)
	{
		l->strip[i][0] = o->strip[i].first;
		l->strip[i][1] = o->strip[i].count;
		l->strip[i][2] = o->strip[i].list;
		l->strip[i][3] = o->strip[i].param;
	}
	memcpy(l->param, o->param, (size_t)res.nparam * sizeof(SpeParam));
	memcpy(l->mtri, o->mtri, (size_t)res.nmtri * sizeof(SpeModTri));
	for (uint32_t i = 0; i < res.nmvol; i++)
	{
		l->mvol[i][0] = o->mvol[i].first;
		l->mvol[i][1] = o->mvol[i].cull;
		l->mvol[i][2] = o->mvol[i].depth;
		l->mvol[i][3] = o->mvol[i].volume_last;
#ifdef T4_REF
		l->mvol[i][4] = o->mvol[i].pad;
#else
		l->mvol[i][4] = o->mvol[i].list;
#endif
	}
}

#ifndef T4_REF
uint32_t spu_new_bad(void) { return echo.bad; }
#endif
