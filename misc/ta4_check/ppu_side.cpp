// Bundle 4 PC check, PPU side: Ps3VertexDecoder (run.sh cuts it out of ps3_rend.cpp) through ta.h's real splitter,
// driven as decode_pending does. Built twice, each in its own namespace: the tree's code, and T4_REF = the frozen one.
#include "drkPvr.h"
#include "spg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ta4.h"

#ifdef T4_REF
#define SIDE_NS ppu_ref_ns
#define SIDE(x) ppu_ref_##x
#else
#define SIDE_NS ppu_new_ns
#define SIDE(x) ppu_new_##x
#include "ps3/spu/spe_bell.h"
// run.sh puts the tree's header (or a mutant) on the include path.
#include "spe_sprite.h"
#endif

namespace SIDE_NS
{
#ifndef T4_REF
// ta.h declares these two at file scope; here that is this namespace.
int g_ta_bad_rec_preset;
u32 g_ta_bad_records;
#endif
#include "ta.h"
using namespace TASplitter;
namespace TASplitter { TaListFP* TaCmd = 0; void TA_ListInit() {} void TA_ListCont() {} void TA_SoftReset() {} }
u32 StripCount;

#include "ps3/ps3_rend_regs.h"
#include "defines.inc"
#include "structs.inc"

// What the region reads from the rest of ps3_rend.cpp, with its start values.
static TaContext s_ctx;
static TaContext* s_dec = &s_ctx;
static u32 s_cur_list, s_mv_list, s_stat_built[5], s_stat_wtotal, s_stat_wclamp, s_overflow;
static float s_face_a = 1, s_face_r = 1, s_face_g = 1, s_face_b = 1;
static u32 s_sprite_col = 0xFFFFFFFF;
static float s_foffs_a = 1, s_foffs_r = 1, s_foffs_g = 1, s_foffs_b = 1;
static u32 s_sprite_offs;
static bool s_bump_param;
static bool s_bump_on;
static u32 s_fog;  // plan 3a: off
#ifndef T4_REF
static u32 s_face8 = 0xFFFFFFFF, s_foffs8 = 0xFFFFFFFF;
static u32 s_dopt;
static u32 s_clip_rect;
static u32 s_stat_spr_plane;
#endif

// reset_lists of ps3_rend.cpp.
static void reset_lists(TaContext* c)
{
	c->nvtx = c->nstrip = c->nparam = c->strip_first = c->nmtri = c->nmvol = 0;
	c->min_w = 1e30f;
	c->max_w = 0.0f;
	c->max_inv_w = 1.0f;
	memset(c->mv_tris, 0, sizeof(c->mv_tris));
#ifndef T4_REF
	c->npass = 0;
#endif
}

#include "region.inc"

FifoSplitter<Ps3VertexDecoder, false> TileAccel;

static bool s_first = true;
static bool s_inited;
}

using namespace SIDE_NS;

// decode_pending: the options are the context's; the first bytes after a list init start with the splitter's.
void SIDE(reset)(uint32_t opt, int bad_skip)
{
	if (!s_inited)
	{
		TileAccel.Init();
		s_inited = true;
	}
	s_first = true;
#ifndef T4_REF
	s_ctx.opt = opt;
	g_ta_bad_rec_preset = bad_skip;
#else
	(void)opt;
	(void)bad_skip;
#endif
}

static void first_decode()
{
	if (!s_first)
		return;
	s_first = false;
#ifndef T4_REF
	s_dopt = s_ctx.opt;
#endif
	TileAccel.ListInit();
}

void SIDE(data)(const uint32_t* rec, uint32_t nrec)
{
	first_decode();
	if (!nrec)
		return;
	// decode_range.
	Ta_Dma* p = (Ta_Dma*)rec;
	Ta_Dma* end = p + nrec - 1;
	do
		p = TileAccel.TaCmd(p, end);
	while (p <= end);
}

void SIDE(cont)(void)
{
	first_decode();
	TileAccel.ListCont();
}

void SIDE(lists)(T4Lists* l)
{
	first_decode();
	const TaContext* c = &s_ctx;
	l->nvtx = c->nvtx;
	l->nstrip = c->nstrip;
	l->nparam = c->nparam;
	l->nmtri = c->nmtri;
	l->nmvol = c->nmvol;
	memcpy(&l->max_inv_w, &c->max_inv_w, 4);
	memcpy(&l->min_w, &c->min_w, 4);
	memcpy(&l->max_w, &c->max_w, 4);
	memcpy(l->mv_tris, c->mv_tris, sizeof(l->mv_tris));
	l->opt = 0;
	l->npass = 0;
#ifndef T4_REF
	l->opt = c->opt;
	l->npass = c->npass;
	memcpy(l->pass_strip, c->pass_strip, sizeof(l->pass_strip));
	memcpy(l->pass_mvol, c->pass_mvol, sizeof(l->pass_mvol));
	memcpy(l->offs, c->offs, (size_t)c->nvtx * 4);
#endif
	memcpy(l->vtx, c->vtx, (size_t)c->nvtx * sizeof(DcVertex));
	for (u32 i = 0; i < c->nstrip; i++)
	{
		l->strip[i][0] = c->strip[i].first;
		l->strip[i][1] = c->strip[i].count;
		l->strip[i][2] = c->strip[i].list;
		l->strip[i][3] = c->strip[i].param;
	}
	memcpy(l->param, c->param, (size_t)c->nparam * sizeof(DcParam));
	memcpy(l->mtri, c->mtri, (size_t)c->nmtri * sizeof(ModTri));
	for (u32 i = 0; i < c->nmvol; i++)
	{
		l->mvol[i][0] = c->mvol[i].first;
		l->mvol[i][1] = c->mvol[i].cull;
		l->mvol[i][2] = c->mvol[i].depth;
		l->mvol[i][3] = c->mvol[i].volume_last;
#ifndef T4_REF
		l->mvol[i][4] = c->mvol[i].list;
#else
		l->mvol[i][4] = 0;
#endif
	}
}

#ifndef T4_REF
uint32_t ppu_new_bad(void) { return g_ta_bad_records; }
uint32_t ppu_new_planes(void) { return s_stat_spr_plane; }
#endif
