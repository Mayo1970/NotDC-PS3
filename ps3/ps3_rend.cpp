// PVR renderer on RSX via ps3gl: OP, PT, TR lists with textures and the background color.
#include "plugs/drkPvr/Renderer_if.h"
#include "ps3_phase.h"
#include "plugs/drkPvr/regs.h"
#include "ps3_video.h"
#include "ps3_tex.h"
#include "ps3_rend_regs.h"
#include "ps3_spe.h"
#include "ps3_menu.h"
#include "ps3_presets.h"
#include "ps3_tasplit.h"
#include "ps3_stall.h"
#include "dc/pvr/pvr_if.h"
#include "dc/sh4/sh4_sqw.h"
#include "dc/sh4/sys_gate.h"
#include <algorithm>
#include <math.h>
#include <malloc.h>
#include <stddef.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/cond.h>

// ps3gl.h silences C-only warnings, which g++ reports as unknown pragmas.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
extern "C" {
#include "ps3gl.h"
}
#pragma GCC diagnostic pop

using namespace TASplitter;

extern s32 render_end_pending_cycles;
extern "C" int get_render_delay_preset();
extern "C" int get_render_thread_preset();
extern "C" int get_ta_fast_preset();
extern "C" int get_drop_policy_preset();
extern "C" int get_strip_merge_preset();
extern "C" int get_vertex_array_preset();
extern s32 render_isp_pending_cycles;
extern s32 render_tsp_pending_cycles;
extern s32 render_vd_pending_cycles;

#define MAX_VTX    (64 * 1024)
#define MAX_STRIPS (16 * 1024)
#define MAX_PARAMS (8 * 1024)

// x, y in DC screen pixels; w = eye depth (1 / the TA's 1/W).
struct DcVertex
{
	float x, y, w;
	u32 col; // DC ARGB8888
	float u, v;
};

struct DcStrip
{
	u32 first;
	u16 count;
	u8 list;
	u16 param;
};

// nullDC-v1 ModTriangle: three corners of x, y and 1/W.
struct ModTri
{
	float x0, y0, z0, x1, y1, z1, x2, y2, z2;
};

// nullDC-v1 ISP_Modvol: where this volume's triangles start, plus its ISP bits.
// A terminator entry closes the list, so entry i owns [first, next.first).
struct ModVolParam
{
	u32 first;
	u8 cull, depth, volume_last;
};
#define MAX_MODTRI 4096
#define MAX_MODVOL 256

// TR1: the SH4 thread appends raw 32-byte TA records here; StartRender decodes them.
// TR5: 2 MB against a measured high-water of 993 KB (Sonic Adventure, 30K vtx).
#define TA_RAW_SIZE  (2 * 1024 * 1024)
#define MAX_CONT_MARKS 64

// One TA context per parameter buffer, as nullDC-v1 SetCurrentTARC/SetCurrentPVRRC:
// the TA fills the one at TA_ISP_BASE, STARTRENDER draws the one at PARAM_BASE.
struct TaContext
{
	u32 addr;
	DcVertex vtx[MAX_VTX];
	DcStrip strip[MAX_STRIPS];
	DcParam param[MAX_PARAMS];
	u32 nvtx, nstrip, nparam;
	u32 strip_first;
	float min_w, max_w;
	// reicast fZ_max: the largest usable 1/W of the frame, which alone sets the
	// near plane. min_w/max_w are kept for the stats line only.
	float max_inv_w;
	u32 mv_tris[5];
	ModTri mtri[MAX_MODTRI];
	ModVolParam mvol[MAX_MODVOL];
	u32 nmtri, nmvol;
	// Raw TA stream, decoded lazily. cont_marks holds the raw_used values at
	// which TA_LIST_CONT arrived, so the replay can split at the same points.
	u8* raw;
	u32 raw_used, raw_decoded;
	u32 cont_marks[MAX_CONT_MARKS];
	u32 n_cont, n_cont_done;
	bool raw_overflow;
	// 5.12: raw bytes already sent to the SPE, and the ring number of the last DATA that names them.
	u32 spe_pub;
	u32 spe_cmd;
	// Vertices of the last job's lists, for the render-end IRQ delay. In S3 nvtx stays at the PPU replay's.
	u32 rend_nvtx;
};
#define TA_CONTEXTS 4

// 5.12 S2: the SPU decoder writes these lists in the same layout.
static_assert(TA_CONTEXTS == SPE_CTX && MAX_VTX == SPE_MAX_VTX && MAX_STRIPS == SPE_MAX_STRIPS &&
              MAX_PARAMS == SPE_MAX_PARAMS && MAX_MODTRI == SPE_MAX_MODTRI && MAX_MODVOL == SPE_MAX_MODVOL,
              "SPE list sizes");
static_assert(sizeof(DcVertex) == sizeof(SpeVtx) && offsetof(DcVertex, col) == offsetof(SpeVtx, col) &&
              offsetof(DcVertex, u) == offsetof(SpeVtx, u), "SpeVtx layout");
static_assert(sizeof(DcStrip) == sizeof(SpeStrip) && offsetof(DcStrip, count) == offsetof(SpeStrip, count) &&
              offsetof(DcStrip, list) == offsetof(SpeStrip, list) && offsetof(DcStrip, param) == offsetof(SpeStrip, param),
              "SpeStrip layout");
static_assert(sizeof(DcParam) == sizeof(SpeParam) && sizeof(ModTri) == sizeof(SpeModTri), "SpeParam/SpeModTri layout");
static_assert(sizeof(ModVolParam) == sizeof(SpeModVol) && offsetof(ModVolParam, cull) == offsetof(SpeModVol, cull) &&
              offsetof(ModVolParam, volume_last) == offsetof(SpeModVol, volume_last), "SpeModVol layout");
static TaContext s_ctx[TA_CONTEXTS];
// s_ta is the store stage's target and belongs to the SH4 thread. s_dec (replay)
// and s_rc (draw) belong to the render thread once it runs.
static TaContext* s_ta = &s_ctx[0];
static TaContext* s_dec = &s_ctx[0];
static TaContext* s_rc = &s_ctx[0];

// What the draw reads: s_rc's own lists, or the SPU's copy of them at the job's fence (5.12 S3).
struct DrawLists
{
	const DcVertex* vtx;
	const DcStrip* strip;
	const DcParam* param;
	const ModTri* mtri;
	const ModVolParam* mvol;
	u32 nvtx, nstrip, nparam, nmtri, nmvol;
	float min_w, max_w, max_inv_w;
	u32 mv_tris[5];
};
static DrawLists s_rl;
static u32 s_cur_list;
static u32 s_overflow;
// TA raw-stream health: dropped records, and the high-water mark for sizing.
static u32 s_stat_raw_of;
static u32 s_stat_raw_peak;

// TR5: the peak is taken at store time, not at replay time. A stream that grows
// after STARTRENDER latches it, or is never drawn at all, still counts here.
static inline void raw_note_peak(const TaContext* c)
{
	if (c->raw_used > s_stat_raw_peak)
		s_stat_raw_peak = c->raw_used;
}

// Plan 5.36 TA Peak Frame, latched in InitRenderer: no note per record; list init, STARTRENDER and the
// stats line note the peak instead. The stream only grows between list inits, so the peak is the same.
static bool s_peak_frame;

// Intensity polys scale this face color per vertex (PolyParam 1/2B/4B).
static float s_face_a = 1, s_face_r = 1, s_face_g = 1, s_face_b = 1;
static u32 s_sprite_col = 0xFFFFFFFF;

static float s_fb_scale_x = 1.0f, s_fb_scale_y = 1.0f;

// g_rr is the draw side's copy, read only by whoever is drawing. s_snap is
// filled on the SH4 thread, s_job_rr carries one posted job's values across.
RenderRegs g_rr;
static RenderRegs s_snap;
static RenderRegs s_job_rr;

static u32 s_stat_renders, s_stat_rtt, s_stat_vtx;
static u32 s_stat_strips[5];
// Strips as the TA builds them, so a list that is captured but never drawn shows up.
static u32 s_stat_built[5];
// Modifier volume triangles per list for the stats line (1 = opaque MV, 3 = translucent MV).
static u32 s_mv_list;
static u32 s_stat_mv[5];
static double s_stat_t0;
#define STAT_WINDOW_S 5.0

// -- TR3: render thread. One job at a time (libswirl single-slot queue): the SH4 snapshots and posts,
// this thread replays the TA stream and owns every RSX call. A second frame drops; only RTT waits.
enum { JOB_NONE = 0, JOB_RENDER, JOB_RTT, JOB_PRESENT };

static sys_ppu_thread_t s_rend_tid;
static sys_mutex_t s_rmx;
static sys_cond_t s_job_cv;    // render thread waits here for work
static sys_cond_t s_done_cv;   // SH4 thread waits here for decode or frame end
static bool s_rend_running;
static volatile int s_rend_quit;

static int  s_job_type;
static bool s_job_pending;     // posted, not taken yet
static bool s_job_busy;        // posted or in flight
static bool s_job_decoded;     // this job's TA replay has finished
static TaContext* s_job_ctx;
static TaContext* s_busy_ctx;  // context the render thread owns right now
static u32 s_job_raw_end, s_job_cont_end, s_job_fence;
static u32 s_stat_dropped;
// TR5: a vblank present the render thread was too busy to take. Unlike a dropped
// render this loses nothing - the framebuffer is unmarked, so the next vblank retries.
static u32 s_stat_fb_defer;
// TR5: total time the SH4 spent waiting for the job slot, so the drop policy costs a number.
static double s_stat_wait;
static double s_stat_job_max;
// Render-thread busy time per window: TA replay, draw_frame (incl. texture decode), end_frame.
static double s_stat_t_dec, s_stat_t_draw, s_stat_t_end;
// Split of draw time (timebase ticks): apply_param, its texture bind, glEnd, TR sort keys, modifier volumes.
enum { DT_PARAM, DT_TEX, DT_GLEND, DT_SORT, DT_MODVOL, DT_COUNT };
static u64 s_dt[DT_COUNT];
// Depth diagnostic: the widest W range and the tightest near plane of the window.
static float s_stat_w_lo = 1e30f, s_stat_w_hi = 0.0f, s_stat_near_lo = 1e30f;
// Vertices that hit set_pos's 1/W floor: they all land on one depth, so a scene
// drawn beyond it cannot separate itself from the background plane.
static u32 s_stat_wclamp, s_stat_wtotal;
static float s_stat_bg_inv_w; static u32 s_stat_bg_zw;

static void reset_lists(TaContext* c)
{
	c->nvtx = 0;
	c->nstrip = 0;
	c->nparam = 0;
	c->strip_first = 0;
	c->min_w = 1e30f;
	c->max_w = 0.0f;
	c->max_inv_w = 1.0f;
	c->nmtri = 0;
	c->nmvol = 0;
	memset(c->mv_tris, 0, sizeof(c->mv_tris));
	c->rend_nvtx = 0;
}

// The stored stream is per list-init, so it is cleared with the decoded lists.
static void reset_raw(TaContext* c)
{
	// The SPU must have pulled the old bytes before the SH4 writes over them.
	if (g_spe_on)
		ps3spe_wait_cmd(c->spe_cmd);
	raw_note_peak(c);
	c->raw_used = 0;
	c->raw_decoded = 0;
	c->n_cont = 0;
	c->n_cont_done = 0;
	c->raw_overflow = false;
	c->spe_pub = 0;
	if (g_spe_on)
		ps3spe_reset((u32)(c - s_ctx));
}

// 5.12: sends the stored bytes the SPE has not seen yet.
static inline void spe_flush(TaContext* c)
{
	if (g_spe_on && c->raw_used > c->spe_pub)
	{
		c->spe_cmd = ps3spe_data((u32)(c - s_ctx), c->raw, c->spe_pub, c->raw_used);
		c->spe_pub = c->raw_used;
	}
}

static inline void spe_note(TaContext* c)
{
	if (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH)
		spe_flush(c);
}

// Every byte up to pos goes out before the fence, so the SPE answer covers the whole replay.
static inline u32 spe_fence(TaContext* c)
{
	if (!g_spe_on || !c)
		return 0;
	spe_flush(c);
	return ps3spe_fence((u32)(c - s_ctx), c->raw_used);
}

static void reset_all_contexts()
{
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		s_ctx[i].addr = 0xFFFFFFFF;
		reset_lists(&s_ctx[i]);
		reset_raw(&s_ctx[i]);
	}
	s_ta = s_dec = s_rc = &s_ctx[0];
}

static TaContext* find_context(u32 addr)
{
	for (u32 i = 0; i < TA_CONTEXTS; i++)
		if (s_ctx[i].addr == addr)
			return &s_ctx[i];
	return 0;
}

// Switches the TA to the context of its parameter buffer; a new buffer reuses the oldest slot.
static void set_ta_context(u32 addr)
{
	static u32 s_next_slot;
	addr &= 0xF00000;
	if (s_ta->addr == addr)
		return;
	spe_flush(s_ta);
	TaContext* c = find_context(addr);
	if (!c)
	{
		c = &s_ctx[s_next_slot++ % TA_CONTEXTS];
		// Skip the slot being drawn and the one the render thread holds.
		for (u32 i = 0; i < TA_CONTEXTS && (c == s_rc || c == s_busy_ctx); i++)
			c = &s_ctx[s_next_slot++ % TA_CONTEXTS];
		c->addr = addr;
		reset_lists(c);
		reset_raw(c);
	}
	s_ta = c;
}

static inline u32 clamp_byte(float f)
{
	// Flycast c0f0a758d: NaN is max. PPC fctiwz turns NaN into 0x80000000, which clamped to black.
	if (f != f)
		return 255;
	s32 v = (s32)(f * 255.0f);
	return v < 0 ? 0 : (v > 255 ? 255 : (u32)v);
}

static inline u32 float_argb(float a, float r, float g, float b)
{
	return (clamp_byte(a) << 24) | (clamp_byte(r) << 16) | (clamp_byte(g) << 8) | clamp_byte(b);
}

static inline u32 intensity_argb(float i)
{
	return float_argb(s_face_a, s_face_r * i, s_face_g * i, s_face_b * i);
}

// Floor on 1/W to keep W finite. 1e-4 merged every plane past W=10000 (SC5's video backdrop failed
// DepthMode 4 against the background); reicast has none, 1e-9 keeps x*W in float range.
#define INV_W_FLOOR 1e-9f

// reicast update_fz: one integer compare drops negatives, NaN and anything at or
// above 1024*1024, so a single rogue vertex cannot pull the near plane down.
static inline void track_inv_w(float inv_w)
{
	union { float f; s32 i; } z, m;
	z.f = inv_w;
	m.f = s_dec->max_inv_w;
	if (m.i < z.i && z.i < 0x49800000)
		s_dec->max_inv_w = inv_w;
}

// Modifier volume corners carry no color, so only their depth joins the range.
static inline void track_w(float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	float w = 1.0f / safe;
	if (w < s_dec->min_w) s_dec->min_w = w;
	if (w > s_dec->max_w) s_dec->max_w = w;
	s_stat_wtotal++;
	if (safe != inv_w) s_stat_wclamp++;
	track_inv_w(inv_w);
}

// The TA sends 1/W; the floor keeps W finite (NaN also takes the floor).
static inline void set_pos(DcVertex* v, float x, float y, float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	v->x = x;
	v->y = y;
	v->w = 1.0f / safe;
	track_w(inv_w);
}

// 16-bit UVs are the top half of a float.
static inline float uv16(u32 uv)
{
	union { u32 i; float f; } c;
	c.i = uv << 16;
	return c.f;
}

static inline DcVertex* vtx_slot()
{
	if (s_dec->nvtx < MAX_VTX)
		return &s_dec->vtx[s_dec->nvtx];
	s_overflow++;
	return 0;
}

// One-part vertex: position and color in the same TA packet.
#define VTX_ONE(pkt, color)                                          \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);        \
	v->col = (color);                                                \
	s_dec->nvtx++;

// Two-part vertex: part A has the position, part B the color and the commit.
#define VTX_A(pkt)                                                   \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);

#define VTX_B(color)                                                 \
	if (s_dec->nvtx >= MAX_VTX) return;                              \
	s_dec->vtx[s_dec->nvtx].col = (color);                           \
	s_dec->nvtx++;

#define VTX_B_KEEP()                                                 \
	if (s_dec->nvtx >= MAX_VTX) return;                              \
	s_dec->nvtx++;

struct Ps3VertexDecoder
{
	static void StartList(u32 ListType) { s_cur_list = ListType; }
	// nullDC-v1 closes the volume list with a terminator holding the final count.
	static void EndList(u32 ListType)
	{
		if (ListType != ListType_Opaque_Modifier_Volume || s_dec->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_dec->mvol[s_dec->nmvol++];
		p.first = s_dec->nmtri;
		p.cull = p.depth = p.volume_last = 0;
	}

	template <class T>
	static void add_param(T* pp)
	{
		// When full, keep rewriting the last slot: wrong state, but no overrun.
		if (s_dec->nparam < MAX_PARAMS)
			s_dec->nparam++;
		DcParam& p = s_dec->param[s_dec->nparam - 1];
		p.pcw = pp->pcw;
		p.isp = pp->isp;
		p.tsp = pp->tsp;
		p.tcw = pp->tcw;
	}

	static void AppendPolyParam0(TA_PolyParam0* pp) { add_param(pp); }
	static void AppendPolyParam1(TA_PolyParam1* pp)
	{
		add_param(pp);
		s_face_a = pp->FaceColorA; s_face_r = pp->FaceColorR;
		s_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;
	}
	static void AppendPolyParam2A(TA_PolyParam2A* pp) { add_param(pp); }
	static void AppendPolyParam2B(TA_PolyParam2B* pp)
	{
		s_face_a = pp->FaceColorA; s_face_r = pp->FaceColorR;
		s_face_g = pp->FaceColorG; s_face_b = pp->FaceColorB;
	}
	static void AppendPolyParam3(TA_PolyParam3* pp) { add_param(pp); }
	static void AppendPolyParam4A(TA_PolyParam4A* pp) { add_param(pp); }
	// Two-volume polys: only volume 0 is drawn.
	static void AppendPolyParam4B(TA_PolyParam4B* pp)
	{
		s_face_a = pp->FaceColor0A; s_face_r = pp->FaceColor0R;
		s_face_g = pp->FaceColor0G; s_face_b = pp->FaceColor0B;
	}

	static void StartPolyStrip() { s_dec->strip_first = s_dec->nvtx; }
	static void EndPolyStrip()
	{
		u32 count = s_dec->nvtx - s_dec->strip_first;
		StripCount++;
		if (count < 3 || s_dec->nparam == 0 || s_dec->nstrip >= MAX_STRIPS)
			return;
		DcStrip& s = s_dec->strip[s_dec->nstrip++];
		s.first = s_dec->strip_first;
		s.count = (u16)(count > 0xFFFF ? 0xFFFF : count);
		s.list = (u8)s_cur_list;
		s.param = (u16)(s_dec->nparam - 1);
		if (s_cur_list < 5)
			s_stat_built[s_cur_list]++;
	}

	// Types without UV write 0, so every vertex field is defined (the SPE decoder compares them, 5.12 S2).
	static void AppendPolyVertex0(TA_Vertex0* p) { VTX_ONE(p, p->BaseCol); v->u = v->v = 0.0f; }
	static void AppendPolyVertex1(TA_Vertex1* p) { VTX_ONE(p, float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); v->u = v->v = 0.0f; }
	static void AppendPolyVertex2(TA_Vertex2* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = v->v = 0.0f; }
	static void AppendPolyVertex3(TA_Vertex3* p) { VTX_ONE(p, p->BaseCol); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex4(TA_Vertex4* p) { VTX_ONE(p, p->BaseCol); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex5A(TA_Vertex5A* p) { VTX_A(p); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex5B(TA_Vertex5B* p) { VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); }
	static void AppendPolyVertex6A(TA_Vertex6A* p) { VTX_A(p); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex6B(TA_Vertex6B* p) { VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); }
	static void AppendPolyVertex7(TA_Vertex7* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex8(TA_Vertex8* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex9(TA_Vertex9* p) { VTX_ONE(p, p->BaseCol0); v->u = v->v = 0.0f; }
	static void AppendPolyVertex10(TA_Vertex10* p) { VTX_ONE(p, intensity_argb(p->BaseInt0)); v->u = v->v = 0.0f; }
	static void AppendPolyVertex11A(TA_Vertex11A* p) { VTX_A(p); v->col = p->BaseCol0; v->u = p->u0; v->v = p->v0; }
	static void AppendPolyVertex11B(TA_Vertex11B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex12A(TA_Vertex12A* p) { VTX_A(p); v->col = p->BaseCol0; v->u = uv16(p->u0); v->v = uv16(p->v0); }
	static void AppendPolyVertex12B(TA_Vertex12B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex13A(TA_Vertex13A* p) { VTX_A(p); v->col = intensity_argb(p->BaseInt0); v->u = p->u0; v->v = p->v0; }
	static void AppendPolyVertex13B(TA_Vertex13B* p) { VTX_B_KEEP(); }
	static void AppendPolyVertex14A(TA_Vertex14A* p) { VTX_A(p); v->col = intensity_argb(p->BaseInt0); v->u = uv16(p->u0); v->v = uv16(p->v0); }
	static void AppendPolyVertex14B(TA_Vertex14B* p) { VTX_B_KEEP(); }

	static void AppendSpriteParam(TA_SpriteParam* spr)
	{
		add_param(spr);
		s_sprite_col = spr->BaseCol;
	}

	// Sprite corners A,B,C arrive with UVs; D is sent without z or UV, so it completes the parallelogram.
	// Strip order D,C,A,B makes the quad from two triangles.
	static DcVertex s_spr[4];
	static float s_spr_z[3];
	static void AppendSpriteVertexA(TA_Sprite1A* sv)
	{
		s_spr[0].x = sv->x0; s_spr[0].y = sv->y0; s_spr_z[0] = sv->z0;
		s_spr[1].x = sv->x1; s_spr[1].y = sv->y1; s_spr_z[1] = sv->z1;
		s_spr[2].x = sv->x2;
	}
	static void AppendSpriteVertexB(TA_Sprite1B* sv)
	{
		if (s_dec->nvtx + 4 > MAX_VTX)
		{
			s_overflow++;
			return;
		}
		s_spr[2].y = sv->y2; s_spr_z[2] = sv->z2;
		float z3 = s_spr_z[0] + s_spr_z[2] - s_spr_z[1];
		set_pos(&s_spr[0], s_spr[0].x, s_spr[0].y, s_spr_z[0]);
		set_pos(&s_spr[1], s_spr[1].x, s_spr[1].y, s_spr_z[1]);
		set_pos(&s_spr[2], s_spr[2].x, s_spr[2].y, s_spr_z[2]);
		set_pos(&s_spr[3], sv->x3, sv->y3, z3);
		s_spr[0].u = uv16(sv->u0); s_spr[0].v = uv16(sv->v0);
		s_spr[1].u = uv16(sv->u1); s_spr[1].v = uv16(sv->v1);
		s_spr[2].u = uv16(sv->u2); s_spr[2].v = uv16(sv->v2);
		s_spr[3].u = s_spr[0].u + s_spr[2].u - s_spr[1].u;
		s_spr[3].v = s_spr[0].v + s_spr[2].v - s_spr[1].v;

		static const int order[4] = { 3, 2, 0, 1 };
		StartPolyStrip();
		for (int i = 0; i < 4; i++)
		{
			s_dec->vtx[s_dec->nvtx] = s_spr[order[i]];
			s_dec->vtx[s_dec->nvtx].col = s_sprite_col;
			s_dec->nvtx++;
		}
		EndPolyStrip();
	}

	// Only the opaque volume list is kept, as nullDC-v1 does.
	static void StartModVol(TA_ModVolParam* param)
	{
		s_mv_list = s_cur_list;
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_dec->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_dec->mvol[s_dec->nmvol++];
		p.first = s_dec->nmtri;
		p.cull = (u8)param->isp.CullMode;
		p.depth = (u8)param->isp.DepthMode;
		p.volume_last = (u8)param->pcw.Volume;
	}
	static void ModVolStripEnd() {}
	// Part A opens the triangle, part B closes it: both test the same cap.
	static void AppendModVolVertexA(TA_ModVolA* mvv)
	{
		s_dec->mv_tris[s_mv_list % 5]++;
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_dec->nmtri >= MAX_MODTRI)
			return;
		ModTri& t = s_dec->mtri[s_dec->nmtri];
		t.x0 = mvv->x0; t.y0 = mvv->y0; t.z0 = mvv->z0;
		t.x1 = mvv->x1; t.y1 = mvv->y1; t.z1 = mvv->z1;
		t.x2 = mvv->x2;
		track_w(mvv->z0);
		track_w(mvv->z1);
	}
	static void AppendModVolVertexB(TA_ModVolB* mvv)
	{
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_dec->nmtri >= MAX_MODTRI)
			return;
		ModTri& t = s_dec->mtri[s_dec->nmtri++];
		t.y2 = mvv->y2;
		t.z2 = mvv->z2;
		track_w(mvv->z2);
	}

	static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax) {}
	static void TileClipMode(u32 mode) {}

	// As nullDC-v1: list init clears the context, a soft reset keeps the lists. ListInit/ListCont
	// pick the context, because by replay time TA_ISP_BASE may point elsewhere.
	static void ListCont() {}
	static void ListInit() { reset_lists(s_dec); }
	static void SoftReset() {}
};
DcVertex Ps3VertexDecoder::s_spr[4];
float Ps3VertexDecoder::s_spr_z[3];

// The decode stage replays bytes the store stage already answered for, so it
// must not raise the list-end interrupts a second time.
FifoSplitter<Ps3VertexDecoder, false> TileAccel;

// Store stage: keeps list state and fires the list-end interrupts, and decodes
// nothing. All vertex work happens later, on the replay (TArender.MD TR1).
struct TaStoreDecoder
{
#define TA_STORE_NOP1(name, type) static void name(type*) {}
	static void StartList(u32) {}
	static void EndList(u32) {}
	static void StartPolyStrip() {}
	static void EndPolyStrip() {}
	static void ModVolStripEnd() {}
	static void SetTileClip(u32, u32, u32, u32) {}
	static void TileClipMode(u32) {}
	TA_STORE_NOP1(AppendPolyParam0, TA_PolyParam0)
	TA_STORE_NOP1(AppendPolyParam1, TA_PolyParam1)
	TA_STORE_NOP1(AppendPolyParam2A, TA_PolyParam2A)
	TA_STORE_NOP1(AppendPolyParam2B, TA_PolyParam2B)
	TA_STORE_NOP1(AppendPolyParam3, TA_PolyParam3)
	TA_STORE_NOP1(AppendPolyParam4A, TA_PolyParam4A)
	TA_STORE_NOP1(AppendPolyParam4B, TA_PolyParam4B)
	TA_STORE_NOP1(AppendPolyVertex0, TA_Vertex0)
	TA_STORE_NOP1(AppendPolyVertex1, TA_Vertex1)
	TA_STORE_NOP1(AppendPolyVertex2, TA_Vertex2)
	TA_STORE_NOP1(AppendPolyVertex3, TA_Vertex3)
	TA_STORE_NOP1(AppendPolyVertex4, TA_Vertex4)
	TA_STORE_NOP1(AppendPolyVertex5A, TA_Vertex5A)
	TA_STORE_NOP1(AppendPolyVertex5B, TA_Vertex5B)
	TA_STORE_NOP1(AppendPolyVertex6A, TA_Vertex6A)
	TA_STORE_NOP1(AppendPolyVertex6B, TA_Vertex6B)
	TA_STORE_NOP1(AppendPolyVertex7, TA_Vertex7)
	TA_STORE_NOP1(AppendPolyVertex8, TA_Vertex8)
	TA_STORE_NOP1(AppendPolyVertex9, TA_Vertex9)
	TA_STORE_NOP1(AppendPolyVertex10, TA_Vertex10)
	TA_STORE_NOP1(AppendPolyVertex11A, TA_Vertex11A)
	TA_STORE_NOP1(AppendPolyVertex11B, TA_Vertex11B)
	TA_STORE_NOP1(AppendPolyVertex12A, TA_Vertex12A)
	TA_STORE_NOP1(AppendPolyVertex12B, TA_Vertex12B)
	TA_STORE_NOP1(AppendPolyVertex13A, TA_Vertex13A)
	TA_STORE_NOP1(AppendPolyVertex13B, TA_Vertex13B)
	TA_STORE_NOP1(AppendPolyVertex14A, TA_Vertex14A)
	TA_STORE_NOP1(AppendPolyVertex14B, TA_Vertex14B)
	TA_STORE_NOP1(AppendSpriteParam, TA_SpriteParam)
	TA_STORE_NOP1(AppendSpriteVertexA, TA_Sprite1A)
	TA_STORE_NOP1(AppendSpriteVertexB, TA_Sprite1B)
	TA_STORE_NOP1(StartModVol, TA_ModVolParam)
	TA_STORE_NOP1(AppendModVolVertexA, TA_ModVolA)
	TA_STORE_NOP1(AppendModVolVertexB, TA_ModVolB)
#undef TA_STORE_NOP1
	static void ListInit() {}
	static void ListCont() {}
	static void SoftReset() {}
};

FifoSplitter<TaStoreDecoder> TaStore;

// -- TR4: table-driven store stage. The store only needs record size and list state, so one table
// lookup per 32-byte record replaces the splitter (libswirl ta_fsm); a state says what the next word is.
enum
{
	TAS_NOLIST = 0,  // no list open; a header must open one
	TAS_P32,         // list open, not a modifier volume, 32-byte vertices
	TAS_P64,         // same, 64-byte vertices
	TAS_SKIP32,      // second half of a 64-byte record, then TAS_P32
	TAS_SKIP64,      // second half of a 64-byte record, then TAS_P64
	TAS_MV,          // modifier volume list open; its vertices are 64 bytes
	TAS_SKIPMV,      // second half of a modvol vertex, then TAS_MV
	TAS_COUNT = 8
};
#define TAS_SLOW 0x80

// Indexed (state << 8) | (ParaType << 5) | (obj_ctrl >> 2). UV_16bit/Gouraud/Shadow never change a
// record's size, so the key drops them; without the mask a sprite aliases a 16-bit-UV polygon.
static u8 s_fsm[TAS_COUNT * 256];
static u8 s_fsm_state = TAS_NOLIST;
static u32 s_store_list = ListType_None;
// Latched once at InitRenderer, so a run never changes store path mid-stream.
static bool s_ta_fast;

static inline u32 fsm_key(u32 para, u32 objc)
{
	return (para << 5) | ((objc >> 2) & 31);
}

// The 64-byte vertex types, from the table above poly_data_type_id() in ta.h.
static inline bool ta_vtx_is_64(u32 pdid)
{
	return pdid == 5 || pdid == 6 || pdid == 11 || pdid == 12 || pdid == 13 || pdid == 14;
}

// Built from drkPvr's ta_type_lut, so the two store paths size every record the
// same way. TileAccel.Init() must have run first.
static void ta_fsm_build()
{
	for (u32 st = 0; st < TAS_COUNT; st++)
	for (u32 pt = 0; pt < 8; pt++)
	for (u32 k = 0; k < 32; k++)
	{
		u8 e;
		if (st == TAS_SKIP32)
			e = TAS_P32;
		else if (st == TAS_SKIP64)
			e = TAS_P64;
		else if (st == TAS_SKIPMV)
			e = TAS_MV;
		else if (st >= TAS_COUNT - 1)
			e = TAS_NOLIST;  // state 7 is unused
		else switch (pt)
		{
		case ParamType_User_Tile_Clip:
			e = (u8)st;      // 32 bytes, ignored by the store stage
			break;

		case ParamType_Polygon_or_Modifier_Volume:
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;   // opens the list, so it needs ListType
			else if (st == TAS_MV)
				e = TAS_MV;              // 32-byte header, 64-byte vertices
			else
			{
				u32 uid = TileAccel.ta_type_lut[k << 2];
				u8 vtx = ta_vtx_is_64((u8)uid) ? TAS_P64 : TAS_P32;
				if ((uid >> 30) == SZ64)
					e = (vtx == TAS_P64) ? TAS_SKIP64 : TAS_SKIP32;
				else
					e = vtx;
			}
			break;

		case ParamType_Sprite:
			// Sprite vertices are 64 bytes, like modvol ones, so a modvol list
			// keeps its state either way.
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;
			else
				e = (st == TAS_MV) ? TAS_MV : TAS_P64;
			break;

		case ParamType_Vertex_Parameter:
			if (st == TAS_P64)
				e = TAS_SKIP64;
			else if (st == TAS_MV)
				e = TAS_SKIPMV;
			else
				e = (u8)st;  // 32-byte vertex, or a stray one with no list open
			break;

		default:             // End_Of_List and the reserved types
			e = TAS_SLOW | (u8)st;
			break;
		}
		s_fsm[(st << 8) | (pt << 5) | k] = e;
	}
}

static void ta_store_reset()
{
	s_fsm_state = TAS_NOLIST;
	s_store_list = ListType_None;
}

// The records the table cannot answer from obj_ctrl alone: they open or close a
// list, so they need the live list state.
static u8 ta_store_slow(Ta_Dma* d, u8 st)
{
	PCW pcw = d->pcw;
	switch (pcw.ParaType)
	{
	case ParamType_End_Of_List:
		// As the splitter: an end with no list open adopts the parameter's own
		// ListType, and spg_SchedListEndIrq drops anything above 4.
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		spg_SchedListEndIrq(s_store_list);
		s_store_list = ListType_None;
		spe_flush(s_ta);
		return TAS_NOLIST;

	case ParamType_Polygon_or_Modifier_Volume:
	case ParamType_Sprite:
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		if (IsModVolList(s_store_list))
			return TAS_MV;
		// The list is open and ordinary now, so the table row answers the rest.
		return s_fsm[(TAS_P32 << 8) | fsm_key(pcw.ParaType, pcw.obj_ctrl)];

	default:
		// Object_List_Set and the two reserved types. The splitter calls die()
		// here, which on PS3 is a silent freeze; log once and skip instead.
		{
			static bool warned;
			if (!warned)
			{
				warned = true;
				printf("[ps3rend] TA parameter type %u ignored\n", pcw.ParaType);
			}
		}
		return st;
	}
}

// The table entry for state st and a record whose first word is pcw. ParaType is bits 31..29 and
// obj_ctrl the low byte, in both endians.
static inline u32 ta_fsm_entry(u32 st, u32 pcw)
{
	return s_fsm[(st << 8) | ((pcw >> 24) & 0xE0) | ((pcw >> 2) & 0x1F)];
}

// One record at p: the next state. A list open or close takes ta_store_slow.
static inline u32 ta_fsm_step(u32 st, u32 pcw, Ta_Dma* p)
{
	const u32 e = ta_fsm_entry(st, pcw);
	return (e & TAS_SLOW) ? ta_store_slow(p, (u8)(e & 7)) : e;
}

// The state carries across calls, so a 64-byte record split over two SQ or DMA blocks resumes by itself.
static void ta_store_fsm_run(Ta_Dma* p, u32 words)
{
	u32 st = s_fsm_state;
	for (u32 i = 0; i < words; i++, p++)
		st = ta_fsm_step(st, p->pcw.full, p);
	s_fsm_state = (u8)st;
}

// The SH4 must not disturb a raw stream the render thread is still replaying.
static void rth_wait_decode(TaContext* c)
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	while (s_job_busy && s_busy_ctx == c && !s_job_decoded)
		sysCondWait(s_done_cv, 0);
	sysMutexUnlock(s_rmx);
}

static void rth_wait_idle()
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	while (s_job_busy)
		sysCondWait(s_done_cv, 0);
	sysMutexUnlock(s_rmx);
}

static bool rth_busy()
{
	if (!s_rend_running)
		return false;
	sysMutexLock(s_rmx, 0);
	bool busy = s_job_busy;
	sysMutexUnlock(s_rmx);
	return busy;
}

// Replays [from, to) of the stored stream. Joining SQ and DMA blocks is safe: a 64-byte record split
// at a block end still feeds both halves to the same decoder calls.
static void decode_range(TaContext* c, u32 from, u32 to)
{
	Ta_Dma* p = (Ta_Dma*)(c->raw + from);
	Ta_Dma* end = (Ta_Dma*)(c->raw + to) - 1;
	do
	{
		p = TileAccel.TaCmd(p, end);
	}
	while (p <= end);
}

// Decodes [raw_decoded, raw_end), split at TA_LIST_CONT; raw_end is latched at STARTRENDER, later bytes
// decode next time. Nothing new keeps the lists, so a second STARTRENDER redraws them (nullDC-v1).
static void decode_pending(TaContext* c, u32 raw_end, u32 cont_end)
{
	if (!c || !c->raw)
		return;
	s_dec = c;
	if (c->raw_decoded == 0)
		TileAccel.ListInit();

	u32 pos = c->raw_decoded;
	while (c->n_cont_done < cont_end && c->cont_marks[c->n_cont_done] <= raw_end)
	{
		u32 mark = c->cont_marks[c->n_cont_done++];
		if (mark > pos)
		{
			decode_range(c, pos, mark);
			pos = mark;
		}
		TileAccel.ListCont();
	}
	if (raw_end > pos)
		decode_range(c, pos, raw_end);
	c->raw_decoded = raw_end;
}

// On overflow the store stage still runs over the source bytes, so list ends still interrupt; only the
// replay copy is dropped. flat (5.17): one inline FSM step on the PCW read before the copy, not from it.
template<bool probe, bool flat>
static inline void store_sq(u32* data)
{
	if (probe)
		g_ta_split.t_store = ta_stamp();
	const u32 pcw = data[0];
	TaContext* c = s_ta;
	Ta_Dma* rec = (Ta_Dma*)data;
	if (c->raw && c->raw_used + 32 <= TA_RAW_SIZE)
	{
		u8* dst = c->raw + c->raw_used;
		memcpy(dst, data, 32);
		c->raw_used += 32;
		if (!s_peak_frame)
			raw_note_peak(c);
		rec = (Ta_Dma*)dst;
		spe_note(c);
	}
	else if (!c->raw_overflow)
	{
		c->raw_overflow = true;
		s_stat_raw_of++;
	}
	if (probe)
		g_ta_split.t_copy = ta_stamp();
	if (!s_ta_fast)
	{
		verify(TaStore.TaCmd != 0);
		TaStore.TaCmd(rec, rec);
	}
	else if (flat)
		s_fsm_state = (u8)ta_fsm_step(s_fsm_state, pcw, rec);
	else
		ta_store_fsm_run(rec, 1);
	if (probe)
	{
		g_ta_split.t_fsm = ta_stamp();
		g_ta_split.hit = 1;
	}
}

// A 5.17 sample takes the stamped copy; the other blocks run the same code without stamps.
void ps3ta_store_sq(u32* data)
{
	if (g_ta_split.armed)
		store_sq<true, false>(data);
	else
		store_sq<false, false>(data);
}

// The flat path's flushes: TA SQ Direct and TA Fast Store ON, and TA polygon data, which is area 4
// below 8 MB, as TAWriteSQ decides. YUV and direct VRAM keep the old path.
static inline bool sqw_flat_target(u32 dest)
{
	if (!g_ta_sq_direct_preset || !s_ta_fast)
		return false;
	const u32 area = (dest & 0x20) ? CCN_QACR1.Area : CCN_QACR0.Area;
	return area == 4 && (dest & 0x01800000) == 0;
}

// The rare cases of do_sqw_nommu. It reaches them by tail calls, so its fast path needs no stack frame.
// The old path; do_sqw_nommu has already counted the flush for the probe.
static __attribute__((noinline)) void sqw_chain(u32 dest)
{
	CCALL(CC_SQW);
	do_sqw_target<false>(dest);
}

// A 5.17 sample, stamped from here on whichever path the flush takes.
static __attribute__((noinline)) void sqw_sampled(u32 dest)
{
	const u64 t_entry = ta_stamp();
	CCALL(CC_SQW);
	if (sqw_flat_target(dest))
	{
		store_sq<true, true>((u32*)&sq_both[dest & 0x20]);
		TaCalls++;
	}
	else
	{
		g_ta_split.armed = 1;   // ps3ta_store_sq stamps the store if the old path reaches it
		do_sqw_target<false>(dest);
	}
	ta_split_leave(t_entry);
}

// No raw buffer yet, or it is full.
static __attribute__((noinline)) void sqw_flat_general(u32* data)
{
	store_sq<false, true>(data);
	TaCalls++;
}

// After the copy: an SPE batch is due, or the record opens or closes a list.
static __attribute__((noinline)) void sqw_finish(u32 pcw, Ta_Dma* rec)
{
	spe_note(s_ta);
	s_fsm_state = (u8)ta_fsm_step(s_fsm_state, pcw, rec);
	TaCalls++;
}

// The JIT's PREF target without the MMU. TA SQ Direct ON: a TA polygon flush runs here in one flat body,
// not through TAWriteSQ / libPvr_TaSQ / ps3ta_store_sq (plan 5.17); everything else takes the old path.
template<bool peak>
static inline __attribute__((always_inline)) void do_sqw_flat(u32 dest)
{
	if (++g_ta_split.calls == g_ta_split.next)
		return sqw_sampled(dest);
	if (!sqw_flat_target(dest))
		return sqw_chain(dest);
	CCALL(CC_SQW);
	u32* data = (u32*)&sq_both[dest & 0x20];
	TaContext* c = s_ta;
	if (!c->raw || c->raw_used + 32 > TA_RAW_SIZE)
		return sqw_flat_general(data);
	// The PCW comes from the source, so the FSM never reloads the copy it just stored.
	const u32 pcw = data[0];
	u8* dst = c->raw + c->raw_used;
	memcpy(dst, data, 32);
	c->raw_used += 32;
	if (peak)
		raw_note_peak(c);
	const u32 e = ta_fsm_entry(s_fsm_state, pcw);
	if ((e & TAS_SLOW) || (g_spe_on && c->raw_used - c->spe_pub >= SPE_BATCH))
		return sqw_finish(pcw, (Ta_Dma*)dst);
	s_fsm_state = (u8)e;
	TaCalls++;
}

void FASTCALL do_sqw_nommu(u32 dest)
{
	do_sqw_flat<true>(dest);
}

// Plan 5.36 TA Peak Frame: the JIT calls this one; the peak is noted per frame, not per record.
void FASTCALL do_sqw_nommu_frame(u32 dest)
{
	do_sqw_flat<false>(dest);
}

void ps3ta_store_dma(u32* data, u32 size)
{
	TaContext* c = s_ta;
	u32 bytes = size * 32;
	Ta_Dma* p = (Ta_Dma*)data;
	if (c->raw && c->raw_used + bytes <= TA_RAW_SIZE)
	{
		u8* dst = c->raw + c->raw_used;
		memcpy(dst, data, bytes);
		c->raw_used += bytes;
		if (!s_peak_frame)
			raw_note_peak(c);
		p = (Ta_Dma*)dst;
		spe_note(c);
	}
	else if (!c->raw_overflow)
	{
		c->raw_overflow = true;
		s_stat_raw_of++;
	}
	if (s_ta_fast)
	{
		ta_store_fsm_run(p, size);
		return;
	}
	verify(TaStore.TaCmd != 0);
	Ta_Dma* end = p + size - 1;
	do
	{
		p = TaStore.TaCmd(p, end);
	}
	while (p <= end);
}

void SetFpsText(char* text) {}

// Canvas scale from SPG: 240p modes and SCALER_CTL hscale change the TA screen size.
void SetFbScale(float x, float y)
{
	s_fb_scale_x = x;
	s_fb_scale_y = y;
}

// VRAM is stored as two interleaved 32-bit banks.
static u32 vri_off(u32 addr)
{
	addr &= VRAM_MASK;
	return ((addr & 0x3FFFFC) << 1) | (((addr >> 22) & 1) << 2) | (addr & 3);
}

static u32 vri(u32 addr)
{
	return *(u32*)&PVRPARAMS.vram[vri_off(addr)];
}

static void vwi(u32 addr, u32 val)
{
	*(u32*)&PVRPARAMS.vram[vri_off(addr & ~3u)] = val;
}

// A native word holds two pixels; the low half is the left one, as in ps3_tex.cpp.
static u32 vri16(u32 addr)
{
	u32 w = *(u32*)&PVRPARAMS.vram[vri_off(addr & ~3u)];
	return (addr & 2) ? (w >> 16) : (w & 0xFFFF);
}

// nullDC-v1 UsingAutoSort(): with FPU_PARAM_CFG bit 21 set, the region array header picks the mode.
static bool using_autosort()
{
	if (((FPU_PARAM_CFG >> 21) & 1) == 0)
		return (ISP_FEED_CFG & 1) == 0;
	return ((vri(REGION_BASE) >> 29) & 1) == 0;
}

// Background plane (ISP_BACKGND_T): 3 vertices; the PVR extends their plane over the whole screen.
// Bitfields are extracted by hand; the regs.h struct layout is little-endian.
static void background_plane(BgVertex* out, DcParam* param)
{
	u32 param_base = s_snap.param_base;
	u32 bgt = ISP_BACKGND_T;
	u32 tag_offset = bgt & 0x7;
	u32 tag_address = (bgt >> 3) & 0x1FFFFF;
	u32 skip = (bgt >> 24) & 0x7;
	u32 shadow = (bgt >> 27) & 1;

	u32 strip_base = param_base + tag_address * 4;
	u32 vs = (3 + skip) * 4;
	if ((FPU_SHAD_SCALE & 0x100) && shadow)
		vs += skip * 4;
	u32 ptr = strip_base + 3 * 4 + tag_offset * vs;

	// Drawn as the first opaque poly, like nullDC-v1: depth always passes, no culling.
	ISP_TSP isp;
	isp.full = vri(strip_base);
	param->isp = isp;
	param->isp.DepthMode = 7;
	param->isp.CullMode = 0;
	param->tsp.full = vri(strip_base + 4);
	param->tcw.full = vri(strip_base + 8);
	param->pcw.full = 0;
	param->pcw.Texture = isp.Texture;
	param->pcw.Offset = isp.Offset;
	param->pcw.Gouraud = isp.Gouraud;
	param->pcw.Shadow = shadow;

	u32 col_off = 12; // x, y, 1/W
	if (isp.Texture)
		col_off += isp.UV_16b ? 4 : 8;
	for (u32 k = 0; k < 3; k++, ptr += vs)
	{
		u32 xi = vri(ptr), yi = vri(ptr + 4);
		memcpy(&out[k].x, &xi, 4);
		memcpy(&out[k].y, &yi, 4);
		out[k].u = out[k].v = 0.0f;
		if (isp.Texture && isp.UV_16b)
		{
			// nullDC-v1 decode_pvr_vertex: U in the low half, V in the high half.
			u32 uv = vri(ptr + 12);
			out[k].u = uv16(uv & 0xFFFF);
			out[k].v = uv16(uv >> 16);
		}
		else if (isp.Texture)
		{
			u32 ui = vri(ptr + 12), vi = vri(ptr + 16);
			memcpy(&out[k].u, &ui, 4);
			memcpy(&out[k].v, &vi, 4);
		}
		out[k].col = vri(ptr + col_off);
	}
}

// TR2: the framebuffer read state the present path uses, latched at vblank.
static void snapshot_present_regs()
{
	s_snap.fb_r_sof1 = FB_R_SOF1;
	s_snap.fb_r_size = FB_R_SIZE;
	s_snap.fb_r_depth = FB_R_CTRL.fb_depth;
}

// TR2/TR3: everything the draw path reads, latched on the SH4 thread at STARTRENDER into s_snap.
// Order matters: param_base feeds background_plane, which reads VRAM.
static void snapshot_render_regs()
{
	s_snap.param_base = PARAM_BASE & 0xF00000;
	s_snap.isp_backgnd_d = ISP_BACKGND_D;
	s_snap.pt_alpha_ref = PT_ALPHA_REF;
	s_snap.fb_w_sof1 = FB_W_SOF1;
	s_snap.fb_w_sof2 = FB_W_SOF2;
	s_snap.fb_w_ctrl = FB_W_CTRL;
	s_snap.fb_w_linestride = FB_W_LINESTRIDE;
	s_snap.fb_x_clip_min = FB_X_CLIP.min;
	s_snap.fb_x_clip_max = FB_X_CLIP.max;
	s_snap.fb_y_clip_min = FB_Y_CLIP.min;
	s_snap.fb_y_clip_max = FB_Y_CLIP.max;
	s_snap.pal_ram_ctrl = PAL_RAM_CTRL;
	s_snap.text_control = TEXT_CONTROL;
	s_snap.fpu_shad_scale = FPU_SHAD_SCALE;
	s_snap.fb_scale_x = s_fb_scale_x;
	s_snap.fb_scale_y = s_fb_scale_y;
	s_snap.autosort = using_autosort();
	background_plane(s_snap.bg_vtx, &s_snap.bg_param);
}

// Plane through the 3 background vertices, evaluated per color channel and for UV at (x, y).
static void bg_eval(const BgVertex* v, float x, float y, BgVertex* out)
{
	float den = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
	if (den == 0.0f)
	{
		*out = v[0];
		return;
	}
	float w0 = ((v[1].y - v[2].y) * (x - v[2].x) + (v[2].x - v[1].x) * (y - v[2].y)) / den;
	float w1 = ((v[2].y - v[0].y) * (x - v[2].x) + (v[0].x - v[2].x) * (y - v[2].y)) / den;
	float w2 = 1.0f - w0 - w1;
	u32 col = 0;
	for (int sh = 0; sh < 32; sh += 8)
	{
		float c = ((v[0].col >> sh) & 0xFF) * w0 + ((v[1].col >> sh) & 0xFF) * w1 + ((v[2].col >> sh) & 0xFF) * w2;
		s32 ci = (s32)(c + 0.5f);
		col |= (u32)(ci < 0 ? 0 : (ci > 255 ? 255 : ci)) << sh;
	}
	out->x = x;
	out->y = y;
	out->u = v[0].u * w0 + v[1].u * w1 + v[2].u * w2;
	out->v = v[0].v * w0 + v[1].v * w1 + v[2].v * w2;
	out->col = col;
}

// Draws ps3gl actually emitted for the translucent pass, against draws offered.

// DC compares 1/W; our depth grows with W, so each test flips direction.
static const GLenum depth_func[8] = {
	GL_NEVER, GL_GREATER, GL_EQUAL, GL_GEQUAL, GL_LESS, GL_NOTEQUAL, GL_LEQUAL, GL_ALWAYS
};

// TSP SrcInstr/DstInstr; "other color" is the destination for src and the source for dst.
static const GLenum blend_src[8] = {
	GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA
};
static const GLenum blend_dst[8] = {
	GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA
};

// ShadInstr 0 decal, 1 modulate, 2 decal alpha, 3 modulate alpha. Offset color is not added yet.
static const GLenum tex_env[4] = { GL_REPLACE, GL_MODULATE, GL_DECAL, GL_MODULATE };

// nullDC-v1 CullMode[]: 2 culls counterclockwise, 3 clockwise, as seen on screen.
static void set_cull(u32 mode)
{
	if (mode < 2)
	{
		glDisable(GL_CULL_FACE);
		return;
	}
	glEnable(GL_CULL_FACE);
	glCullFace(mode == 2 ? GL_FRONT : GL_BACK);
}

// nullDC-v1 GPstate_cache: sorted drawing revisits the same polygon state often,
// and repeating it would re-bind the texture and re-push every GL state.
static u32 s_cache_isp, s_cache_tsp, s_cache_tcw, s_cache_pcw, s_cache_list, s_cache_or;
static bool s_cache_valid;

static void reset_param_cache()
{
	s_cache_valid = false;
}

// Applies one polygon's state. Returns the OR mask that forces vertex alpha to 255.
static u32 apply_param_body(const DcParam& p, u32 list);

static u32 apply_param(const DcParam& p, u32 list)
{
	if (s_cache_valid && s_cache_list == list && s_cache_isp == p.isp.full &&
	    s_cache_tsp == p.tsp.full && s_cache_tcw == p.tcw.full && s_cache_pcw == p.pcw.full)
		return s_cache_or;
	const u64 t0 = PERF_TICKS();
	u32 rv = apply_param_body(p, list);
	s_dt[DT_PARAM] += PERF_TICKS() - t0;
	return rv;
}

static u32 apply_param_body(const DcParam& p, u32 list)
{

	if (list == ListType_Translucent)
	{
		glEnable(GL_BLEND);
		glBlendFunc(blend_src[p.tsp.SrcInstr], blend_dst[p.tsp.DstInstr]);
		// Zero alpha leaves the pixel unchanged with these modes; clip it so it does not write Z.
		bool clip_zero = p.tsp.SrcInstr == 4 && (p.tsp.DstInstr == 1 || p.tsp.DstInstr == 5);
		if (clip_zero)
			glEnable(GL_ALPHA_TEST);
		else
			glDisable(GL_ALPHA_TEST);
		// Autosort ignores DepthMode and compares ">=" on 1/W.
		glDepthFunc(g_rr.autosort ? GL_LEQUAL : depth_func[p.isp.DepthMode]);
	}
	else
	{
		glDisable(GL_BLEND);
		// Punch-through ignores DepthMode and compares ">=" on 1/W.
		glDepthFunc(list == ListType_Punch_Through ? GL_LEQUAL : depth_func[p.isp.DepthMode]);
	}
	glDepthMask(p.isp.ZWriteDis ? GL_FALSE : GL_TRUE);
	set_cull(p.isp.CullMode);
	// Flycast SetGPState: stencil bit 7 marks pixels whose polygon takes modifier-volume shadows.
	glStencilFunc(GL_ALWAYS, p.pcw.Shadow ? 0x80 : 0, 0xFF);

	const u64 t_tex = PERF_TICKS();
	bool textured = p.pcw.Texture && ps3tex_bind(p.tsp, p.tcw);
	s_dt[DT_TEX] += PERF_TICKS() - t_tex;
	if (textured)
	{
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, tex_env[p.tsp.ShadInstr]);
	}
	else
		glDisable(GL_TEXTURE_2D);

	// UseAlpha=0 means opaque vertex alpha; modulate (1) takes alpha from the texture only.
	bool force = !p.tsp.UseAlpha || (textured && p.tsp.ShadInstr == 1);
	s_cache_isp = p.isp.full;
	s_cache_tsp = p.tsp.full;
	s_cache_tcw = p.tcw.full;
	s_cache_pcw = p.pcw.full;
	s_cache_list = list;
	s_cache_or = force ? 0xFF000000u : 0;
	s_cache_valid = true;
	return s_cache_or;
}

static inline void emit_vertex(const DcVertex* v, u32 alpha_or)
{
	u32 c = v->col | alpha_or;
	glColor4ub((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
	glTexCoord2f(v->u, v->v);
	glVertex3f(v->x * v->w, v->y * v->w, v->w);
}

// Flycast ta_util.cpp makeIndex(): consecutive strips with the same state share one GL
// strip, joined by degenerate triangles, and inf vertices split a strip.
#define RUN_MAX_VTX 16000   // ps3gl drops vertices past PS3GL_MAX_VERTS (16384) per glBegin

static bool s_merge;          // Strip Merge preset, latched per scene
static u32 s_stat_draws[5];   // glBegin/glEnd pairs per list

// Vertex Arrays preset: the scene's merged strips are written once, straight into one vring
// block, and each run is a draw of a range of it. Null = the glBegin/glEnd path.
static ps3gl_vertex_t* s_vb;
static u32 s_vb_off, s_vb_used, s_vb_cap;

struct StripRun
{
	bool open;
	bool reversed;          // current strip goes out with flipped winding
	u8 cull;                // CullMode of the run's first strip
	u16 param;              // the run's first strip; its state is the one applied
	u32 list;
	u32 count;              // vertices in the open GL strip
	u32 first;              // Vertex Arrays: the run's first vertex in s_vb
	u32 alpha_or;
	const DcVertex* last;   // last vertex emitted
};
static StripRun s_run;

// Flycast PolyParam::equivalentIgnoreCullingDirection: ISP 0xF4000000 keeps cull on/off, not its direction.
static inline bool strip_equivalent(const DcParam& a, const DcParam& b)
{
	return ((a.pcw.full ^ b.pcw.full) & 0x300CE) == 0
	    && ((a.isp.full ^ b.isp.full) & 0xF4000000) == 0
	    && a.tsp.full == b.tsp.full && a.tcw.full == b.tcw.full;
}

// Flycast is_vertex_inf. set_pos stores W, so its "1/W > 3.4e37" is W < 1/3.4e37 (+inf 1/W gives W = 0).
static inline bool vertex_inf(const DcVertex* v)
{
	return v->x != v->x || fabsf(v->x) > 1e25f || v->y != v->y || fabsf(v->y) > 1e25f
	    || v->w < 1.0f / 3.4e37f;
}

// emit_vertex in ps3gl_vertex_t form (z = W, w = 1, RGBA). Word stores only: RSX memory is
// cache-inhibited, and GCC's merged u1/v1 std is misaligned on odd 36-byte vertices (alignment trap).
static inline bool vb_emit(const DcVertex* v, u32 alpha_or)
{
	if (s_vb_used >= s_vb_cap)
		return false;
	volatile float* o = (volatile float*)&s_vb[s_vb_used++];
	u32 c = v->col | alpha_or;
	o[0] = v->x * v->w;
	o[1] = v->y * v->w;
	o[2] = v->w;
	o[3] = 1.0f;
	o[4] = v->u;
	o[5] = v->v;
	o[6] = 0.0f;
	o[7] = 0.0f;
	((volatile u32*)o)[8] = (c << 8) | (c >> 24);
	return true;
}

static inline void run_emit(const DcVertex* v)
{
	if (s_vb)
	{
		if (!vb_emit(v, s_run.alpha_or))
			return;
	}
	else
		emit_vertex(v, s_run.alpha_or);
	s_run.last = v;
	s_run.count++;
}

static void run_close()
{
	if (!s_run.open)
		return;
	const u64 t0 = PERF_TICKS();
	if (s_vb)
		ps3gl_draw_vb(GL_TRIANGLE_STRIP, s_vb_off, (int)s_run.first, (int)s_run.count);
	else
		glEnd();
	s_dt[DT_GLEND] += PERF_TICKS() - t0;
	s_run.open = false;
	if (s_run.count != 0)
		s_stat_draws[s_run.list]++;
}

// No GL state may change while a run is open: apply_param only runs after run_close().
static void draw_strip(const DcStrip& s, u32 list)
{
	const DcParam& p = s_rl.param[s.param];
	s_stat_strips[list]++;

	if (!s_merge)
	{
		u32 alpha_or = apply_param(p, list);
		glBegin(GL_TRIANGLE_STRIP);
		const DcVertex* v = &s_rl.vtx[s.first];
		for (u32 k = 0; k < s.count; k++, v++)
			emit_vertex(v, alpha_or);
		const u64 t0 = PERF_TICKS();
		glEnd();
		s_dt[DT_GLEND] += PERF_TICKS() - t0;
		s_stat_draws[list]++;
		return;
	}

	bool dupe_next = false;
	if (s_run.open && s_run.count != 0 && s_run.list == list &&
	    s_run.count + s.count * 2u + 4u <= RUN_MAX_VTX &&
	    strip_equivalent(p, s_rl.param[s_run.param]))
	{
		// Repeat the last vertex; one more copy flips the parity when the cull direction differs.
		const DcVertex* last = s_run.last;
		run_emit(last);
		if (p.isp.CullMode < 2 || p.isp.CullMode == s_run.cull)
		{
			if (s_run.reversed)
				run_emit(last);
			s_run.reversed = false;
		}
		else
		{
			if (!s_run.reversed)
				run_emit(last);
			s_run.reversed = true;
		}
		dupe_next = true;
	}
	else
	{
		run_close();
		s_run.alpha_or = apply_param(p, list);
		if (!s_vb)
			glBegin(GL_TRIANGLE_STRIP);
		s_run.first = s_vb_used;
		s_run.open = true;
		s_run.reversed = false;
		s_run.cull = p.isp.CullMode;
		s_run.param = s.param;
		s_run.list = list;
		s_run.count = 0;
	}

	const DcVertex* v0 = &s_rl.vtx[s.first];
	const DcVertex* last_good = 0;
	for (u32 i = 0; i < s.count; i++)
	{
		const DcVertex* v = v0 + i;
		if (vertex_inf(v))
		{
			// Skip the inf run; the next good vertex restarts the strip after a degenerate join.
			while (i < s.count - 1u)
			{
				if (!vertex_inf(v0 + i + 1))
				{
					if (last_good)
					{
						run_emit(last_good);
						dupe_next = true;
					}
					break;
				}
				i++;
			}
			continue;
		}
		last_good = v;
		if (dupe_next)
		{
			run_emit(v);
			dupe_next = false;
		}
		// Vertex i must sit at the same parity as in its own strip, or its triangles flip winding.
		if (((i ^ s_run.count) & 1) ^ (u32)s_run.reversed)
			run_emit(v);
		run_emit(v);
	}
}

// Draws one list in submission order: opaque, punch-through, and presorted translucent.
static void draw_list(u32 list)
{
	for (u32 i = 0; i < s_rl.nstrip; i++)
	{
		const DcStrip& s = s_rl.strip[i];
		if (s.list == list)
			draw_strip(s, list);
	}
	run_close();
}

// nullDC-v1 sort entry: one per translucent polygon, keyed like PolyParam::zvZ.
struct SortStrip
{
	float key;
	u32 idx;
};
static SortStrip s_order[MAX_STRIPS];

// Autosort pass, nullDC-v1 SortPParams (AlphaSortMode 1): polygons ordered by average 1/W, nearest
// last. The PVR sorts per pixel; this is the approximation the reference ships.
static void draw_tr_sorted()
{
	const u64 t_sort = PERF_TICKS();
	u32 n = 0;
	for (u32 i = 0; i < s_rl.nstrip; i++)
	{
		const DcStrip& st = s_rl.strip[i];
		if (st.list != ListType_Translucent || st.count == 0)
			continue;
		float sum = 0.0f;
		for (u32 k = 0; k < st.count; k++)
			sum += 1.0f / s_rl.vtx[st.first + k].w;
		s_order[n].key = sum / st.count;
		s_order[n].idx = i;
		n++;
	}
	if (n == 0)
	{
		s_dt[DT_SORT] += PERF_TICKS() - t_sort;
		return;
	}
	std::stable_sort(s_order, s_order + n, [](const SortStrip& a, const SortStrip& b) { return a.key < b.key; });
	s_dt[DT_SORT] += PERF_TICKS() - t_sort;

	// Merging after the sort keeps the draw order, as Flycast's per-strip sort mode does.
	for (u32 j = 0; j < n; j++)
		draw_strip(s_rl.strip[s_order[j].idx], ListType_Translucent);
	run_close();
}

// Flycast gldraw.cpp SetMVS_Mode. Stencil bit 7 = polygon takes shadows (kept), bit 1 = volume
// parity, bit 0 = inside the summed volume. Inclusion ORs bit 1 into bit 0, exclusion ANDs.
enum { MV_XOR, MV_OR, MV_INCLUSION, MV_EXCLUSION };

static void set_mvs_mode(u32 mode, u32 cull)
{
	if (mode == MV_XOR || mode == MV_OR)
	{
		glEnable(GL_DEPTH_TEST);
		glStencilMask(2);
		if (mode == MV_XOR)
		{
			glStencilFunc(GL_ALWAYS, 0, 2);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
		}
		else
		{
			glStencilFunc(GL_ALWAYS, 2, 2);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		}
		set_cull(cull);
		return;
	}
	// Summing the area is not depth tested.
	glDisable(GL_DEPTH_TEST);
	glStencilMask(3);
	if (mode == MV_INCLUSION)
	{
		glStencilFunc(GL_LEQUAL, 1, 3);
		glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);
	}
	else
	{
		glStencilFunc(GL_EQUAL, 1, 3);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
	}
}

static inline void emit_mod_vertex(float x, float y, float inv_w)
{
	float safe = (inv_w >= INV_W_FLOOR) ? inv_w : INV_W_FLOOR;
	float w = 1.0f / safe;
	glVertex3f(x * w, y * w, w);
}

static void draw_mod_tris(u32 first, u32 count)
{
	if (first + count > s_rl.nmtri)
		return;
	glBegin(GL_TRIANGLES);
	for (u32 i = 0; i < count; i++)
	{
		const ModTri& t = s_rl.mtri[first + i];
		emit_mod_vertex(t.x0, t.y0, t.z0);
		emit_mod_vertex(t.x1, t.y1, t.z1);
		emit_mod_vertex(t.x2, t.y2, t.z2);
	}
	glEnd();
}

// Opaque modifier volumes, as Flycast gldraw.cpp DrawModVols: build the volume in stencil bits 1:0,
// then shade only pixels with bit 7 (Shadow polygon) and bit 0 set, at FPU_SHAD_SCALE.
static void draw_modvols(float cx0, float cy0, float cw, float ch, float quad_w)
{
	if (s_rl.nmvol < 2 || s_rl.nmtri == 0)
		return;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	// The DC compares 1/W with "greater"; our depth is W, so the test flips.
	glDepthFunc(GL_LESS);
	glEnable(GL_STENCIL_TEST);

	s32 mod_base = -1;
	for (u32 i = 0; i + 1 < s_rl.nmvol; i++)
	{
		const ModVolParam& p = s_rl.mvol[i];
		u32 count = s_rl.mvol[i + 1].first - p.first;
		if (count == 0)
			continue;
		if (mod_base < 0)
			mod_base = (s32)p.first;

		// An open volume or quad ORs; a closed volume XORs.
		set_mvs_mode((!p.volume_last && p.depth > 0) ? MV_OR : MV_XOR, p.cull);
		draw_mod_tris(p.first, count);

		// DepthMode 1/2 closes the volume: sum its area as inclusion or exclusion.
		if (p.depth == 1 || p.depth == 2)
		{
			set_mvs_mode(p.depth == 1 ? MV_INCLUSION : MV_EXCLUSION, p.cull);
			draw_mod_tris((u32)mod_base, p.first + count - (u32)mod_base);
			mod_base = -1;
		}
	}

	// Shade Shadow polygons inside a volume; the op also clears bits 1:0.
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glStencilFunc(GL_EQUAL, 0x81, 0x81);
	glStencilMask(3);
	glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
	// Flycast: black at alpha 1 - scale/256, so the lit colour keeps scale/256 of itself.
	u32 sf = g_rr.fpu_shad_scale & 0xFF;
	glColor4ub(0, 0, 0, (GLubyte)(255 - (sf * 255 + 128) / 256));
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(cx0 * quad_w, cy0 * quad_w, quad_w);
	glVertex3f((cx0 + cw) * quad_w, cy0 * quad_w, quad_w);
	glVertex3f(cx0 * quad_w, (cy0 + ch) * quad_w, quad_w);
	glVertex3f((cx0 + cw) * quad_w, (cy0 + ch) * quad_w, quad_w);
	glEnd();

	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	reset_param_cache();
}

// Resets the GL state the passes below assume; must run before the clear.
static void reset_gl_state()
{
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
}

// Per-list dump of the last drawn frame: what the geometry looks like and what
// state it carries. Prints beside the stats line while the SC5 fault is open.
struct ListDiag
{
	u32 strips, verts, zwd, tex, forced, usealpha, ignoretexa;
	float x0, x1, y0, y1, w0, w1;
	u32 dm[8], cull[4], src[8], dst[8], shad[4], fmt[8];
	u32 amin, amax;
	// Shade mode 3 modulates the texture by the vertex color, so an all-black
	// vertex color makes a correctly textured poly render as nothing.
	u32 cmin, cmax, black, ct[4];
	// Strips whose bounding box covers most of the canvas: one of these drawn
	// near, with Z write on, masks everything the later lists put behind it.
	u32 big;
	float bigw0, bigw1;
};
static ListDiag s_diag[3];
static const char* s_diag_name[3] = { "op", "pt", "tr" };

static void collect_diag()
{
	static const u32 lists[3] = { ListType_Opaque, ListType_Punch_Through, ListType_Translucent };
	for (u32 L = 0; L < 3; L++)
	{
		ListDiag& d = s_diag[L];
		memset(&d, 0, sizeof(d));
		d.x0 = d.y0 = d.w0 = 1e30f;
		d.x1 = d.y1 = -1e30f;
		d.w1 = 0.0f;
		d.amin = 256; d.amax = 0;
		d.cmin = 256; d.cmax = 0;
		d.bigw0 = 1e30f; d.bigw1 = 0.0f;
		for (u32 i = 0; i < s_rl.nstrip; i++)
		{
			const DcStrip& st = s_rl.strip[i];
			if (st.list != lists[L])
				continue;
			const DcParam& p = s_rl.param[st.param];
			d.strips++;
			d.verts += st.count;
			d.dm[p.isp.DepthMode & 7]++;
			d.cull[p.isp.CullMode & 3]++;
			if (p.isp.ZWriteDis) d.zwd++;
			if (p.pcw.Texture) d.tex++;
			d.src[p.tsp.SrcInstr & 7]++;
			d.dst[p.tsp.DstInstr & 7]++;
			d.shad[p.tsp.ShadInstr & 3]++;
			if (p.tsp.UseAlpha) d.usealpha++;
			if (p.tsp.IgnoreTexA) d.ignoretexa++;
			if (p.pcw.Texture) d.fmt[p.tcw.NO_PAL.PixelFmt & 7]++;
			// The same rule apply_param uses, minus the bind result.
			bool force = !p.tsp.UseAlpha || (p.pcw.Texture && p.tsp.ShadInstr == 1);
			if (force) d.forced++;
			d.ct[p.pcw.Col_Type & 3]++;
			float sx0 = 1e30f, sx1 = -1e30f, sy0 = 1e30f, sy1 = -1e30f;
			float sw0 = 1e30f, sw1 = 0.0f;
			for (u32 k = 0; k < st.count; k++)
			{
				const DcVertex& v = s_rl.vtx[st.first + k];
				if (v.x < sx0) sx0 = v.x;
				if (v.x > sx1) sx1 = v.x;
				if (v.y < sy0) sy0 = v.y;
				if (v.y > sy1) sy1 = v.y;
				if (v.w < sw0) sw0 = v.w;
				if (v.w > sw1) sw1 = v.w;
				u32 a = force ? 255u : (v.col >> 24);
				if (a < d.amin) d.amin = a;
				if (a > d.amax) d.amax = a;
				u32 r = (v.col >> 16) & 0xFF, g = (v.col >> 8) & 0xFF, b = v.col & 0xFF;
				u32 mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
				if (mx < d.cmin) d.cmin = mx;
				if (mx > d.cmax) d.cmax = mx;
				if (mx == 0) d.black++;
				if (v.x < d.x0) d.x0 = v.x;
				if (v.x > d.x1) d.x1 = v.x;
				if (v.y < d.y0) d.y0 = v.y;
				if (v.y > d.y1) d.y1 = v.y;
				if (v.w < d.w0) d.w0 = v.w;
				if (v.w > d.w1) d.w1 = v.w;
			}
			if ((sx1 - sx0) >= 320.0f && (sy1 - sy0) >= 240.0f)
			{
				d.big++;
				if (sw0 < d.bigw0) d.bigw0 = sw0;
				if (sw1 > d.bigw1) d.bigw1 = sw1;
			}
		}
	}
}

static void print_diag()
{
	for (u32 L = 0; L < 3; L++)
	{
		const ListDiag& d = s_diag[L];
		if (d.strips == 0)
			continue;
		printf("[rendz] %s n=%u v=%u x=[%g..%g] y=[%g..%g] w=[%g..%g] "
		       "dm=%u/%u/%u/%u/%u/%u/%u/%u zwd=%u cull=%u/%u/%u/%u tex=%u big=%u bigw=[%g..%g]\n",
		       s_diag_name[L], d.strips, d.verts,
		       (double)d.x0, (double)d.x1, (double)d.y0, (double)d.y1,
		       (double)d.w0, (double)d.w1,
		       d.dm[0], d.dm[1], d.dm[2], d.dm[3], d.dm[4], d.dm[5], d.dm[6], d.dm[7],
		       d.zwd, d.cull[0], d.cull[1], d.cull[2], d.cull[3], d.tex,
		       d.big, (double)d.bigw0, (double)d.bigw1);
		printf("[rendz] %s a=[%u..%u] forced=%u ua=%u ita=%u shad=%u/%u/%u/%u "
		       "src=%u/%u/%u/%u/%u/%u/%u/%u dst=%u/%u/%u/%u/%u/%u/%u/%u "
		       "fmt=%u/%u/%u/%u/%u/%u/%u/%u rgb=[%u..%u] black=%u ct=%u/%u/%u/%u\n",
		       s_diag_name[L], d.amin, d.amax, d.forced, d.usealpha, d.ignoretexa,
		       d.shad[0], d.shad[1], d.shad[2], d.shad[3],
		       d.src[0], d.src[1], d.src[2], d.src[3], d.src[4], d.src[5], d.src[6], d.src[7],
		       d.dst[0], d.dst[1], d.dst[2], d.dst[3], d.dst[4], d.dst[5], d.dst[6], d.dst[7],
		       d.fmt[0], d.fmt[1], d.fmt[2], d.fmt[3], d.fmt[4], d.fmt[5], d.fmt[6], d.fmt[7],
		       d.cmin, d.cmax, d.black, d.ct[0], d.ct[1], d.ct[2], d.ct[3]);
	}
}

// Draws the render context into the viewport. The canvas rect is the DC area mapped onto it:
// the whole screen for display, the FB clip window for a render to texture.
static void draw_scene(float cx0, float cy0, float cw, float ch)
{
	// ISP_BACKGND_D is the background 1/W (low 4 bits masked, as in nullDC-v1). The register value
	// is a native u32 here, so memcpy gives the float on BE as on LE.
	u32 bgd_bits = g_rr.isp_backgnd_d & ~0xFu;
	float bg_inv_w;
	memcpy(&bg_inv_w, &bgd_bits, 4);
	// Flycast 443d5a2ba: push the plane 1e-6 back so geometry at its depth wins (Xtreme Sports skybox).
	bg_inv_w = std::max(bg_inv_w - 1e-6f, 1e-11f);
	float bg_w = 1.0f / ((bg_inv_w >= INV_W_FLOOR) ? bg_inv_w : INV_W_FLOOR);

	// reicast glesrend.cpp pins the far plane at 1/W = 0, so only the nearest
	// vertex sets the range; tracking the far end let one poly at 1/W <= 0.0001 eat it.
	union { float f; s32 i; } bz, mz;
	bz.f = bg_inv_w;
	mz.f = s_rl.max_inv_w;
	float max_inv_w = (mz.i < bz.i && bz.i < 0x49800000) ? bg_inv_w : mz.f;
	// reicast's sanity fallback: past 1024*1024 the range is garbage, not a scene.
	if (!(max_inv_w > 0.0f) || max_inv_w > 1048576.0f)
		max_inv_w = 10.0f * 1024.0f;
	// reicast's fZ_max *= 1.001: the nearest layer defines the plane it sits on,
	// and float rounding would otherwise clip it away.
	max_inv_w *= 1.001f;
	float n = 1.0f / max_inv_w;
	if (s_rl.min_w < s_stat_w_lo) s_stat_w_lo = s_rl.min_w;
	if (s_rl.max_w > s_stat_w_hi) s_stat_w_hi = s_rl.max_w;
	if (n < s_stat_near_lo) s_stat_near_lo = n;
	s_stat_bg_inv_w = bg_inv_w;
	s_stat_bg_zw = g_rr.bg_param.isp.ZWriteDis;

	// Input (x*W, y*W, W): clip.w = W, so attributes interpolate with perspective
	// and z_ndc = 1 - 2n/W, which is -1 at W = n and +1 at 1/W = 0.
	float m[16] = { 0 };
	m[0] = 2.0f / cw;
	m[8] = -1.0f - 2.0f * cx0 / cw;
	m[5] = -2.0f / ch;
	m[9] = 1.0f + 2.0f * cy0 / ch;
	m[10] = 1.0f;
	m[14] = -2.0f * n;
	m[11] = 1.0f;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// nullDC-v1 always shades Gouraud; its winding is judged as seen on screen.
	glShadeModel(GL_SMOOTH);
	glFrontFace(GL_CCW);
	glEnable(GL_DEPTH_TEST);

	// OP/PT write stencil bit 7 from each polygon's Shadow flag (apply_param), as Flycast DrawList.
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	// Background quad over the whole canvas at the background depth; colors and UV from its plane.
	reset_param_cache();
	u32 bg_alpha_or = apply_param(g_rr.bg_param, ListType_Opaque);
	static const float corner[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
	glBegin(GL_TRIANGLE_STRIP);
	for (int k = 0; k < 4; k++)
	{
		BgVertex bv;
		bg_eval(g_rr.bg_vtx, cx0 + corner[k][0] * cw, cy0 + corner[k][1] * ch, &bv);
		u32 c = bv.col | bg_alpha_or;
		glColor4ub((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
		glTexCoord2f(bv.u, bv.v);
		glVertex3f(bv.x * bg_w, bv.y * bg_w, bg_w);
	}
	glEnd();

	if (s_rl.nstrip != 0)
	{
		s_merge = get_strip_merge_preset() != 0;
		// [rendz] prints only the last frame of a window, and a render job always ends in rth_print_stats.
		if (os_GetSeconds() - s_stat_t0 >= STAT_WINDOW_S)
			collect_diag();
		// Worst case per strip: 3 joins, and per vertex a join copy, a parity copy and itself.
		s_vb = 0;
		if (s_merge && get_vertex_array_preset())
		{
			s_vb_cap = 3 * s_rl.nvtx + 4 * s_rl.nstrip + 16;
			s_vb_used = 0;
			s_vb = ps3gl_vring_alloc((int)s_vb_cap, &s_vb_off);
		}
		// Hardware order: opaque, punch-through, translucent.
		reset_param_cache();
		draw_list(ListType_Opaque);

		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GEQUAL, (g_rr.pt_alpha_ref & 0xFF) / 255.0f);
		reset_param_cache();
		draw_list(ListType_Punch_Through);

		// Hardware shades the opaque volumes between punch-through and translucent.
		const u64 t_mv = PERF_TICKS();
		draw_modvols(cx0, cy0, cw, ch, bg_w);
		s_dt[DT_MODVOL] += PERF_TICKS() - t_mv;
		glDisable(GL_STENCIL_TEST);

		// apply_param turns the "alpha > 0" test on per translucent poly.
		glDisable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
		reset_param_cache();
		if (g_rr.autosort)
			draw_tr_sorted();
		else
			draw_list(ListType_Translucent);
		s_vb = 0;
	}
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_TRUE);
}

static void draw_frame()
{
	u32 sw, sh;
	ps3_video_size(&sw, &sh);

	// 4:3 pillarbox inside the 16:9 display.
	u32 vp_w = sh * 4 / 3;
	if (vp_w > sw)
		vp_w = sw;
	u32 vp_x = (sw - vp_w) / 2;

	ps3tex_frame_begin();
	reset_gl_state();

	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, sw, sh);
	ps3gl_apply_states();
	glClearColor(0, 0, 0, 1);
	glClearDepth(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_SCISSOR_TEST);
	glScissor(vp_x, 0, vp_w, sh);
	glViewport(vp_x, 0, vp_w, sh);
	ps3gl_apply_states();

	draw_scene(0.0f, 0.0f, 640.0f * g_rr.fb_scale_x, 480.0f * g_rr.fb_scale_y);
}

// -- Render to texture (FB_W_SOF1 bit 24): draw the clip window at its own size, read it
// back and pack it into VRAM, where the texture cache sees the new hash and re-decodes.
static u32 s_stat_rtt_copies;

static inline u16 pack16(u32 argb, u32 mode, u16 kbit)
{
	u32 a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	switch (mode)
	{
	case 1:  return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	case 2:  return (u16)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
	case 3:  return (u16)(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	default: return (u16)(kbit | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	}
}

static void rtt_render()
{
	u32 x0 = g_rr.fb_x_clip_min, x1 = g_rr.fb_x_clip_max;
	u32 y0 = g_rr.fb_y_clip_min, y1 = g_rr.fb_y_clip_max;
	if (x1 < x0 || y1 < y0)
		return;
	u32 w = x1 - x0 + 1, h = y1 - y0 + 1;
	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	if (w > sw || h > sh || w > 1024 || h > 1024)
		return;

	u32 packmode = g_rr.fb_w_ctrl & 7;
	if (packmode == 4 || packmode == 5)
	{
		static u32 s_warned;
		if (s_warned++ < 3)
			printf("[ps3rend] RTT pack mode %u not handled\n", packmode);
		return;
	}

	if (!ps3_video_begin_frame())
		return;
	ps3tex_frame_begin();
	reset_gl_state();
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, w, h);
	ps3gl_apply_states();
	glClearColor(0, 0, 0, 1);
	glClearDepth(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	draw_scene((float)x0, (float)y0, (float)w, (float)h);
	ps3_video_end_frame_noflip();

	const u32* px = ps3_video_readback(0, 0, w, h);
	if (!px)
		return;

	// Bit 24 addresses the linear texture area, so no bank conversion here.
	u32 base = g_rr.fb_w_sof1 & VRAM_MASK & ~3u;
	u32 stride = (g_rr.fb_w_linestride & 0x1FF) * 8;
	if (stride == 0)
		stride = (packmode >= 6) ? w * 4 : w * 2;
	u16 kbit = (u16)(g_rr.fb_w_ctrl & 0x8000);

	for (u32 y = 0; y < h; y++)
	{
		const u32* srow = px + (size_t)y * w;
		u32 line = base + y * stride;
		if (packmode <= 3)
		{
			for (u32 x = 0; x < w; x += 2)
			{
				u16 p0 = pack16(srow[x], packmode, kbit);
				u16 p1 = (x + 1 < w) ? pack16(srow[x + 1], packmode, kbit) : 0;
				*(u32*)&PVRPARAMS.vram[(line + x * 2) & VRAM_MASK & ~3u] = ((u32)p1 << 16) | p0;
			}
		}
		else
		{
			for (u32 x = 0; x < w; x++)
			{
				u32 c = srow[x];
				if (packmode == 6)
					c &= 0x00FFFFFF; // KRGB0888: the K bit stays clear
				*(u32*)&PVRPARAMS.vram[(line + x * 4) & VRAM_MASK & ~3u] = c;
			}
		}
	}
	s_stat_rtt_copies++;
}

// -- Framebuffer writes. 2D titles draw straight into VRAM with no render; a mark is
// stamped after each render, and if it is gone at vblank another writer refreshed it, so show it.
#define FB_PRESENT_MAGIC 0xDC4F0FB0u
#define FB2D_W 768
#define FB2D_H 576
static u32 s_fb_pix[FB2D_W * FB2D_H];
static GLuint s_fb_tex;
static u32 s_stat_fb_presents;

static void fb_stamp_magic()
{
	vwi(s_snap.fb_w_sof1, FB_PRESENT_MAGIC);
	vwi(s_snap.fb_w_sof2, FB_PRESENT_MAGIC);
}

static bool fb_needs_present()
{
	return vri(FB_R_SOF1 & ~3u) != FB_PRESENT_MAGIC;
}

static inline u32 e5(u32 v) { return (v << 3) | (v >> 2); }
static inline u32 e6(u32 v) { return (v << 2) | (v >> 4); }

// TR3: the VRAM read stays on the SH4 thread, so the pixels are the ones the
// guest had written at this vblank; only the quad goes to the render thread.
static u32 s_fb_cpy_w, s_fb_cpy_h;

static bool present_fb_convert()
{
	u32 fb_x_size = s_snap.fb_r_size & 0x3FF;
	u32 fb_y_size = (s_snap.fb_r_size >> 10) & 0x3FF;
	u32 fb_modulus = (s_snap.fb_r_size >> 20) & 0x3FF;
	u32 fb_depth = s_snap.fb_r_depth;
	if (fb_x_size == 0 || fb_y_size == 0)
		return false;
	if (fb_depth == 2)
	{
		static u32 s_warned;
		if (s_warned++ < 3)
			printf("[ps3rend] 24-bit framebuffer read not handled\n");
		return false;
	}

	// fb_x_size counts 32-bit words per line, so a 16-bit line holds twice as many pixels.
	bool wide = (fb_depth == 3);
	u32 src_w = wide ? (fb_x_size + 1) : ((fb_x_size + 1) << 1);
	u32 src_h = fb_y_size + 1;
	u32 mod_px = (fb_modulus > 0 ? fb_modulus - 1 : 0) << (wide ? 0 : 1);
	u32 bpp = wide ? 4 : 2;
	u32 cpy_w = src_w > FB2D_W ? FB2D_W : src_w;
	u32 cpy_h = src_h > FB2D_H ? FB2D_H : src_h;

	u32 addr = s_snap.fb_r_sof1;
	for (u32 y = 0; y < cpy_h; y++)
	{
		u32 line = addr;
		u32* out = &s_fb_pix[(size_t)y * cpy_w];
		for (u32 x = 0; x < cpy_w; x++, line += bpp)
		{
			u32 r, g, b;
			if (wide)
			{
				u32 c = vri(line & ~3u);
				r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF;
			}
			else
			{
				u32 c = vri16(line);
				if (fb_depth == 1) { r = e5(c >> 11); g = e6((c >> 5) & 0x3F); b = e5(c & 0x1F); }
				else               { r = e5((c >> 10) & 0x1F); g = e5((c >> 5) & 0x1F); b = e5(c & 0x1F); }
			}
			out[x] = (r << 24) | (g << 16) | (b << 8) | 0xFF;
		}
		addr += (src_w + mod_px) * bpp;
	}
	s_fb_cpy_w = cpy_w;
	s_fb_cpy_h = cpy_h;
	return true;
}

static void present_fb_draw()
{
	u32 cpy_w = s_fb_cpy_w, cpy_h = s_fb_cpy_h;
	if (!ps3_video_begin_frame())
		return;

	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	u32 vp_w = sh * 4 / 3;
	if (vp_w > sw)
		vp_w = sw;
	u32 vp_x = (sw - vp_w) / 2;

	reset_gl_state();
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, sw, sh);
	ps3gl_apply_states();
	glClearColor(0, 0, 0, 1);
	glClearDepth(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glViewport(vp_x, 0, vp_w, sh);
	ps3gl_apply_states();

	if (!s_fb_tex)
		glGenTextures(1, &s_fb_tex);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_fb_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cpy_w, cpy_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_fb_pix);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// Straight to clip space: the quad covers the whole viewport.
	float m[16] = { 0 };
	m[0] = m[5] = m[10] = m[15] = 1.0f;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glColor4ub(255, 255, 255, 255);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(0, 0); glVertex3f(-1.0f,  1.0f, 0.0f);
	glTexCoord2f(1, 0); glVertex3f( 1.0f,  1.0f, 0.0f);
	glTexCoord2f(0, 1); glVertex3f(-1.0f, -1.0f, 0.0f);
	glTexCoord2f(1, 1); glVertex3f( 1.0f, -1.0f, 0.0f);
	glEnd();

	glDisable(GL_TEXTURE_2D);
	glDepthMask(GL_TRUE);
	ps3_video_end_frame();
	reset_param_cache();
	s_stat_fb_presents++;
}

// -- TR3: the render thread --------------------------------------------------

// Publishes "the replay is done", so the SH4 may touch this context's raw stream again.
static void rth_mark_decoded()
{
	if (!s_rend_running)
		return;
	sysMutexLock(s_rmx, 0);
	s_job_decoded = true;
	sysCondBroadcast(s_done_cv);
	sysMutexUnlock(s_rmx);
}

static void rth_print_stats()
{
	double now = os_GetSeconds();
	if (now - s_stat_t0 < STAT_WINDOW_S)
		return;
	raw_note_peak(s_ta);   // the stream still being stored
	u32 r = s_stat_renders ? s_stat_renders : 1;
	u32 decodes, live;
	ps3tex_stats(&decodes, &live);
	const double win = (now - s_stat_t0) / 100.0;
	printf("[rend] busy=%.0f%% (dec %.0f draw %.0f end %.0f)\n",
	       (s_stat_t_dec + s_stat_t_draw + s_stat_t_end) / win,
	       s_stat_t_dec / win, s_stat_t_draw / win, s_stat_t_end / win);
	{
		double us[DT_COUNT];
		double sum = 0.0;
		for (int k = 0; k < DT_COUNT; k++)
		{
			us[k] = PERF_TICKS_US(s_dt[k]) / 1e6 / win;
			s_dt[k] = 0;
		}
		sum = us[DT_PARAM] + us[DT_GLEND] + us[DT_SORT] + us[DT_MODVOL];
		// param includes tex; "vtx" is the rest of draw: vertex emit, strip merge, list walks, frame setup.
		printf("[rend] draw split %%: param %.1f (tex %.1f) glEnd %.1f sort %.1f modvol %.1f vtx+other %.1f\n",
		       us[DT_PARAM], us[DT_TEX], us[DT_GLEND], us[DT_SORT], us[DT_MODVOL],
		       s_stat_t_draw / win - sum);
		u64 t_hash, t_decode, t_gl, hash_bytes;
		u32 hashes;
		ps3tex_split(&t_hash, &t_decode, &t_gl, &hashes, &hash_bytes);
		double th = PERF_TICKS_US(t_hash) / 1e6 / win;
		double td = PERF_TICKS_US(t_decode) / 1e6 / win;
		double tg = PERF_TICKS_US(t_gl) / 1e6 / win;
		// "other" is the cache probe and describe(); hash/render is textures checked per frame.
		printf("[rend] tex split %%: hash %.1f decode %.1f gl %.1f other %.1f | hash/render n=%u KB=%u\n",
		       th, td, tg, us[DT_TEX] - th - td - tg, hashes / r, (u32)(hash_bytes / r / 1024));
	}
	s_stat_t_dec = s_stat_t_draw = s_stat_t_end = 0.0;
	printf("[rend] renders=%u drop=%u wait=%ums fbdef=%u rtt=%u/%u fb=%u sort=%s store=%s job_max=%ums avg/render: vtx=%u strips op=%u pt=%u tr=%u draws=%s op=%u pt=%u tr=%u (built op=%u pt=%u tr=%u) mv_tris op=%u tr=%u | w=[%g..%g] near=%g wclamp=%u/%u bg=%g zw=%u | tex decodes=%u live=%u | overflow=%u ta_raw_of=%u peak=%uK/%uK\n",
	       s_stat_renders, s_stat_dropped, (u32)(s_stat_wait * 1000.0), s_stat_fb_defer,
	       s_stat_rtt_copies, s_stat_rtt, s_stat_fb_presents,
	       g_rr.autosort ? "auto" : "pre", s_ta_fast ? "fsm" : "split",
	       (u32)(s_stat_job_max * 1000.0), s_stat_vtx / r,
	       s_stat_strips[ListType_Opaque] / r, s_stat_strips[ListType_Punch_Through] / r,
	       s_stat_strips[ListType_Translucent] / r,
	       s_merge ? "merge" : "strip",
	       s_stat_draws[ListType_Opaque] / r, s_stat_draws[ListType_Punch_Through] / r,
	       s_stat_draws[ListType_Translucent] / r,
	       s_stat_built[ListType_Opaque] / r, s_stat_built[ListType_Punch_Through] / r,
	       s_stat_built[ListType_Translucent] / r,
	       s_stat_mv[1] / r, s_stat_mv[3] / r,
	       (double)s_stat_w_lo, (double)s_stat_w_hi, (double)s_stat_near_lo,
	       s_stat_wclamp, s_stat_wtotal, (double)s_stat_bg_inv_w, s_stat_bg_zw,
	       decodes, live, s_overflow,
	       s_stat_raw_of, s_stat_raw_peak / 1024, (u32)(TA_RAW_SIZE / 1024));
	print_diag();
	ps3spe_stats(now - s_stat_t0);
	// A job far longer than a frame is the stall TArender.MD TR3 asks us to watch for.
	if (s_stat_job_max > 0.25)
		printf("[ps3rend] render job stalled for %u ms\n", (u32)(s_stat_job_max * 1000.0));
	memset(s_stat_mv, 0, sizeof(s_stat_mv));
	s_stat_renders = s_stat_rtt = s_stat_rtt_copies = s_stat_fb_presents = s_stat_vtx = 0;
	memset(s_stat_strips, 0, sizeof(s_stat_strips));
	memset(s_stat_draws, 0, sizeof(s_stat_draws));
	memset(s_stat_built, 0, sizeof(s_stat_built));
	s_overflow = 0;
	s_stat_raw_of = 0;
	s_stat_raw_peak = 0;
	s_stat_dropped = 0;
	s_stat_fb_defer = 0;
	s_stat_wait = 0.0;
	s_stat_job_max = 0.0;
	s_stat_w_lo = s_stat_near_lo = 1e30f;
	s_stat_w_hi = 0.0f;
	s_stat_wclamp = s_stat_wtotal = 0;
	s_stat_t0 = now;
}

static u32 fbits(float f)
{
	u32 u;
	memcpy(&u, &f, 4);
	return u;
}

// The first 8 mismatches, as spu/ppu word pairs.
static void spe_report(u32 fence, u32 ctx, const char* what, u32 idx, const u32* spu, const u32* ppu, u32 words)
{
	static u32 s_logged;
	if (s_logged++ >= 8)
		return;
	char line[256];
	int n = snprintf(line, sizeof(line), "[spe] fence %u MISMATCH ctx=%u %s[%u] spu/ppu:", fence, ctx, what, idx);
	for (u32 k = 0; k < words && n > 0 && n < (int)sizeof(line); k++)
		n += snprintf(line + n, sizeof(line) - n, " %08X/%08X", spu[k], ppu[k]);
	printf("%s\n", line);
}

// 5.12 S2: the SPU lists for this fence must equal the replay's, bit for bit.
static void spe_compare(TaContext* c, u32 fence, u32 raw_end, u32 cont_end)
{
	const u64 t0 = gettime();
	const SpeFence* r = ps3spe_wait_fence(fence);
	if (!r)
		return;
	const u32 ctx = (u32)(c - s_ctx);
	const SpeOut* o = ps3spe_out(ctx);
	const u32 hs[] = { r->id, r->ctx, r->pos, r->ncont, r->nvtx, r->nstrip, r->nparam, r->nmtri, r->nmvol,
	                   r->max_inv_w, r->min_w, r->max_w,
	                   r->mv_tris[0], r->mv_tris[1], r->mv_tris[2], r->mv_tris[3], r->mv_tris[4] };
	const u32 hp[] = { fence, ctx, raw_end, cont_end, c->nvtx, c->nstrip, c->nparam, c->nmtri, c->nmvol,
	                   fbits(c->max_inv_w), fbits(c->min_w), fbits(c->max_w),
	                   c->mv_tris[0], c->mv_tris[1], c->mv_tris[2], c->mv_tris[3], c->mv_tris[4] };
	static const char* const hn[] = { "id", "ctx", "pos", "ncont", "nvtx", "nstrip", "nparam", "nmtri", "nmvol",
	                                  "max_inv_w", "min_w", "max_w", "mv0", "mv1", "mv2", "mv3", "mv4" };
	bool ok = true;
	for (u32 k = 0; k < sizeof(hs) / sizeof(hs[0]) && ok; k++)
		if (hs[k] != hp[k])
		{
			spe_report(fence, ctx, hn[k], 0, &hs[k], &hp[k], 1);
			ok = false;
		}
	if (ok && memcmp(o->vtx, c->vtx, c->nvtx * sizeof(DcVertex)) != 0)
		for (u32 i = 0; i < c->nvtx && ok; i++)
			if (memcmp(&o->vtx[i], &c->vtx[i], sizeof(DcVertex)) != 0)
			{
				spe_report(fence, ctx, "vtx", i, (const u32*)&o->vtx[i], (const u32*)&c->vtx[i], 6);
				ok = false;
			}
	for (u32 i = 0; i < c->nstrip && ok; i++)
	{
		const SpeStrip& a = o->strip[i];
		const DcStrip& b = c->strip[i];
		if (a.first != b.first || a.count != b.count || a.list != b.list || a.param != b.param)
		{
			const u32 ws[] = { a.first, a.count, a.list, a.param }, wp[] = { b.first, b.count, b.list, b.param };
			spe_report(fence, ctx, "strip", i, ws, wp, 4);
			ok = false;
		}
	}
	if (ok && memcmp(o->param, c->param, c->nparam * sizeof(DcParam)) != 0)
		for (u32 i = 0; i < c->nparam && ok; i++)
			if (memcmp(&o->param[i], &c->param[i], sizeof(DcParam)) != 0)
			{
				spe_report(fence, ctx, "param", i, (const u32*)&o->param[i], (const u32*)&c->param[i], 4);
				ok = false;
			}
	if (ok && memcmp(o->mtri, c->mtri, c->nmtri * sizeof(ModTri)) != 0)
		for (u32 i = 0; i < c->nmtri && ok; i++)
			if (memcmp(&o->mtri[i], &c->mtri[i], sizeof(ModTri)) != 0)
			{
				spe_report(fence, ctx, "mtri", i, (const u32*)&o->mtri[i], (const u32*)&c->mtri[i], 9);
				ok = false;
			}
	for (u32 i = 0; i < c->nmvol && ok; i++)
	{
		const SpeModVol& a = o->mvol[i];
		const ModVolParam& b = c->mvol[i];
		if (a.first != b.first || a.cull != b.cull || a.depth != b.depth || a.volume_last != b.volume_last)
		{
			const u32 ws[] = { a.first, a.cull, a.depth, a.volume_last }, wp[] = { b.first, b.cull, b.depth, b.volume_last };
			spe_report(fence, ctx, "mvol", i, ws, wp, 4);
			ok = false;
		}
	}
	ps3spe_result(ok, gettime() - t0);
}

static void lists_from_ctx(const TaContext* c)
{
	s_rl.vtx = c->vtx;
	s_rl.strip = c->strip;
	s_rl.param = c->param;
	s_rl.mtri = c->mtri;
	s_rl.mvol = c->mvol;
	s_rl.nvtx = c->nvtx;
	s_rl.nstrip = c->nstrip;
	s_rl.nparam = c->nparam;
	s_rl.nmtri = c->nmtri;
	s_rl.nmvol = c->nmvol;
	s_rl.min_w = c->min_w;
	s_rl.max_w = c->max_w;
	s_rl.max_inv_w = c->max_inv_w;
	memcpy(s_rl.mv_tris, c->mv_tris, sizeof(s_rl.mv_tris));
}

static float bitsf(u32 u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

// 5.12 S3: the SPU lists for this fence, if they cover the same bytes the replay would. The SPU only
// appends past these counts, and its RESET of the context waits for ps3spe_release after the draw.
static bool lists_from_spe(TaContext* c, u32 fence, u32 raw_end, u32 cont_end)
{
	const u64 t0 = gettime();
	const SpeFence* r = ps3spe_wait_fence(fence);
	if (!r)
		return false;
	const u32 ctx = (u32)(c - s_ctx);
	if (r->id != fence || r->ctx != ctx || r->pos != raw_end || r->ncont != cont_end)
	{
		const u32 hs[] = { r->id, r->ctx, r->pos, r->ncont }, hp[] = { fence, ctx, raw_end, cont_end };
		spe_report(fence, ctx, "id/ctx/pos/ncont", 0, hs, hp, 4);
		ps3spe_result(false, gettime() - t0);
		return false;
	}
	const SpeOut* o = ps3spe_out(ctx);
	s_rl.vtx = (const DcVertex*)o->vtx;
	s_rl.strip = (const DcStrip*)o->strip;
	s_rl.param = (const DcParam*)o->param;
	s_rl.mtri = (const ModTri*)o->mtri;
	s_rl.mvol = (const ModVolParam*)o->mvol;
	s_rl.nvtx = r->nvtx;
	s_rl.nstrip = r->nstrip;
	s_rl.nparam = r->nparam;
	s_rl.nmtri = r->nmtri;
	s_rl.nmvol = r->nmvol;
	s_rl.min_w = bitsf(r->min_w);
	s_rl.max_w = bitsf(r->max_w);
	s_rl.max_inv_w = bitsf(r->max_inv_w);
	memcpy(s_rl.mv_tris, r->mv_tris, sizeof(s_rl.mv_tris));
	ps3spe_result(true, gettime() - t0);
	return true;
}

// Stage 1 of a job: the TA replay. It is split from the draw so the inline path
// can still run it under the guest rounding mode, as TR2 did.
static void rth_job_decode(TaContext* c, u32 raw_end, u32 cont_end, u32 fence)
{
	REND_PHASE(PH_TADEC);
	double t0 = os_GetSeconds();
	// S3 skips the replay; a late or wrong fence replays from raw_decoded, which S3 never moves.
	if (!(fence && g_spe_draw && lists_from_spe(c, fence, raw_end, cont_end)))
	{
		decode_pending(c, raw_end, cont_end);
		lists_from_ctx(c);
	}
	c->rend_nvtx = s_rl.nvtx;
	s_stat_t_dec += os_GetSeconds() - t0;
	s_rc = c;
	// Compared before the mark: once marked, the SH4 may reset this context and the SPU its lists.
	if (fence && !g_spe_draw)
		spe_compare(c, fence, raw_end, cont_end);
	rth_mark_decoded();
}

// Stage 2: every RSX call of the job.
static void rth_job_draw(int type)
{
	double t0 = os_GetSeconds();
	double end_dt = 0.0;
	if (type == JOB_PRESENT)
	{
		REND_PHASE(PH_PRESENT);
		present_fb_draw();
	}
	else if (type == JOB_RTT)
	{
		REND_PHASE(PH_RTT);
		rtt_render();
	}
	else
	{
		s_stat_renders++;
		s_stat_vtx += s_rl.nvtx;
		VertexCount += s_rl.nvtx;
		for (int k = 0; k < 5; k++)
			s_stat_mv[k] += s_rl.mv_tris[k];

		REND_PHASE(PH_BEGINFRAME);
		if (ps3_video_begin_frame())
		{
			REND_PHASE(PH_DRAW);
			draw_frame();
			REND_PHASE(PH_ENDFRAME);
			double t_end = os_GetSeconds();
			ps3_video_end_frame();
			end_dt = os_GetSeconds() - t_end;
		}
	}
	REND_PHASE(PH_IDLE);

	double dt = os_GetSeconds() - t0;
	s_stat_t_end  += end_dt;
	s_stat_t_draw += dt - end_dt;
	if (dt > s_stat_job_max)
		s_stat_job_max = dt;
	if (type == JOB_RENDER)
		rth_print_stats();
}

static void rth_thread(void* arg)
{
	// The SH4 thread FPSCR follows the guest rounding mode. This thread has its
	// own, set once here, so no draw call has to save and restore it.
	__asm__ volatile("mtfsfi 7,0" ::: "memory");

	sysMutexLock(s_rmx, 0);
	while (!s_rend_quit)
	{
		if (!s_job_pending)
		{
			// Timed wait: a missed signal costs 100 ms, never the whole run.
			sysCondWait(s_job_cv, 100 * 1000);
			continue;
		}
		int type = s_job_type;
		TaContext* c = s_job_ctx;
		u32 raw_end = s_job_raw_end, cont_end = s_job_cont_end, fence = s_job_fence;
		memcpy(&g_rr, &s_job_rr, sizeof(g_rr));
		s_job_pending = false;
		sysMutexUnlock(s_rmx);

		if (type != JOB_PRESENT)
			rth_job_decode(c, raw_end, cont_end, fence);
		rth_job_draw(type);
		// Before the job slot frees: the next fence's RESET may wait on this one (5.12 S3).
		ps3spe_release(fence);

		sysMutexLock(s_rmx, 0);
		s_job_type = JOB_NONE;
		s_job_busy = false;
		s_busy_ctx = 0;
		s_job_decoded = true;
		sysCondBroadcast(s_done_cv);
	}
	sysMutexUnlock(s_rmx);
	sysThreadExit(0);
}

static bool rth_start()
{
	if (!get_render_thread_preset())
	{
		printf("[ps3rend] render thread off, drawing on the SH4 thread\n");
		return false;
	}
	sys_mutex_attr_t mattr;
	sysMutexAttrInitialize(mattr);
	if (sysMutexCreate(&s_rmx, &mattr) != 0)
	{
		printf("[ps3rend] render mutex create failed\n");
		return false;
	}
	// PSL1GHT spells the default cond attr 0x200 (pshared) and the default mutex
	// attr 0x200 (not pshared); if lv2 rejects that pair, retry with it cleared.
	sys_cond_attr_t cattr;
	sysCondAttrInitialize(cattr);
	if (sysCondCreate(&s_job_cv, s_rmx, &cattr) != 0)
	{
		cattr.attr_pshared = 0;
		if (sysCondCreate(&s_job_cv, s_rmx, &cattr) != 0)
		{
			printf("[ps3rend] render cond create failed\n");
			sysMutexDestroy(s_rmx);
			return false;
		}
	}
	if (sysCondCreate(&s_done_cv, s_rmx, &cattr) != 0)
	{
		printf("[ps3rend] render cond create failed\n");
		sysCondDestroy(s_job_cv);
		sysMutexDestroy(s_rmx);
		return false;
	}

	s_job_type = JOB_NONE;
	s_job_pending = s_job_busy = false;
	s_job_decoded = true;
	s_busy_ctx = 0;
	s_rend_quit = 0;
	// Priority 1000: just above the SH4 thread (1001), below the audio feeder (100).
	s32 ret = sysThreadCreate(&s_rend_tid, rth_thread, NULL, 1000, 0x20000,
	                          THREAD_JOINABLE, (char*)"nulldc_render");
	if (ret != 0)
	{
		printf("[ps3rend] render thread create failed: 0x%08X\n", (unsigned)ret);
		return false;
	}
	s_rend_running = true;
	printf("[ps3rend] render thread up\n");
	return true;
}

static void rth_stop()
{
	if (!s_rend_running)
		return;
	rth_wait_idle();
	sysMutexLock(s_rmx, 0);
	s_rend_quit = 1;
	sysCondBroadcast(s_job_cv);
	sysMutexUnlock(s_rmx);
	u64 rv;
	sysThreadJoin(s_rend_tid, &rv);
	s_rend_running = false;
	sysCondDestroy(s_job_cv);
	sysCondDestroy(s_done_cv);
	sysMutexDestroy(s_rmx);
	printf("[ps3rend] render thread down\n");
}

// Posts one job, or runs it inline with the render thread off (TR2); false = not taken, thread busy.
// RTT never drops (the guest reads it back); renders wait per Frame Drop (TR5); presents never wait.
static bool rth_run(int type, TaContext* c)
{
	if (s_rend_running)
	{
		bool blocking = (type == JOB_RTT);
		int pol = (type == JOB_RENDER) ? get_drop_policy_preset() : 0;
		sysMutexLock(s_rmx, 0);
		while (blocking && s_job_busy)
			sysCondWait(s_done_cv, 0);
		if (s_job_busy && pol != 0)
		{
			// One 59.94 Hz frame. Policy 2 waits with no budget at all.
			const double budget = (pol == 1) ? (1.0 / 59.94) : 0.0;
			double t0 = os_GetSeconds();
			while (s_job_busy)
			{
				if (budget == 0.0)
				{
					sysCondWait(s_done_cv, 0);
					continue;
				}
				double left = budget - (os_GetSeconds() - t0);
				if (left <= 0.0)
					break;
				sysCondWait(s_done_cv, (u64)(left * 1000000.0) + 1);
			}
			s_stat_wait += os_GetSeconds() - t0;
		}
		if (s_job_busy)
		{
			sysMutexUnlock(s_rmx);
			if (type == JOB_PRESENT)
				s_stat_fb_defer++;
			else
				s_stat_dropped++;
			return false;
		}
		s_job_type = type;
		s_job_ctx = c;
		s_busy_ctx = (type == JOB_PRESENT) ? 0 : c;
		s_job_raw_end = c ? c->raw_used : 0;
		s_job_cont_end = c ? c->n_cont : 0;
		s_job_fence = (type == JOB_PRESENT) ? 0 : spe_fence(c);
		s_job_decoded = (type == JOB_PRESENT);
		memcpy(&s_job_rr, &s_snap, sizeof(s_job_rr));
		s_job_pending = true;
		s_job_busy = true;
		sysCondSignal(s_job_cv);
		while (blocking && s_job_busy)
			sysCondWait(s_done_cv, 0);
		sysMutexUnlock(s_rmx);
		return true;
	}

	memcpy(&g_rr, &s_snap, sizeof(g_rr));
	const u32 fence = (type != JOB_PRESENT) ? spe_fence(c) : 0;
	if (type != JOB_PRESENT)
		rth_job_decode(c, c ? c->raw_used : 0, c ? c->n_cont : 0, fence);
	// Host FPSCR follows the SH4 rounding mode (sh4_registers.cpp); draw with round-to-nearest.
	double host_fpscr;
	__asm__ volatile("mffs %0" : "=f"(host_fpscr) :: "memory");
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	rth_job_draw(type);
	__asm__ volatile("mtfsf 0xff,%0" :: "f"(host_fpscr) : "memory");
	ps3spe_release(fence);
	return true;
}

bool InitRenderer()
{
	reset_all_contexts();
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		if (s_ctx[i].raw)
			continue;
		// 128 bytes: the SPE pulls the stream by DMA at full-line alignment.
		s_ctx[i].raw = (u8*)memalign(128, TA_RAW_SIZE);
		if (!s_ctx[i].raw)
			printf("[ps3rend] TA raw buffer %u alloc failed; that context drops records\n", i);
	}
	// Keep running headless if the RSX fails: the UDP log still shows why.
	if (!ps3_video_init())
		printf("[ps3rend] RSX init failed, running headless\n");
	if (!TileAccel.Init() || !TaStore.Init())
		return false;
	s_ta_fast = get_ta_fast_preset() != 0;
	s_peak_frame = g_ta_peak_frame_preset != 0;
	ta_fsm_build();
	ta_store_reset();
	rth_start();
	ps3spe_init();
	return true;
}

void TermRenderer()
{
	ps3spe_term();
	rth_stop();
	TileAccel.Term();
	TaStore.Term();
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		free(s_ctx[i].raw);
		s_ctx[i].raw = 0;
	}
	ps3tex_reset();
	s_fb_tex = 0;
	ps3_video_term();
}

void ResetRenderer(bool Manual)
{
	rth_wait_idle();
	TileAccel.Reset(Manual);
	TaStore.Reset(Manual);
	ta_store_reset();
	reset_all_contexts();
	ps3tex_reset();
	VertexCount = 0;
	FrameCount = 0;
}

bool ThreadStart() { return true; }
void ThreadEnd() {}

// Called at the start of every emulated vblank from SPG.cpp.
void VBlank()
{
	StallScope st_(ST_REND);
	// In-game menu (SELECT): the guest stops here; the menu draws with ps3gl on this thread.
	if (ps3_menu_pending())
	{
		rth_wait_idle();
		ps3_menu_ingame();
	}
	if (!FB_R_CTRL.fb_enable)
		return;
	if (!fb_needs_present())
		return; // the PVR render is still what the screen shows
	if (rth_busy())
	{
		// The next vblank retries; nothing has been marked yet.
		s_stat_fb_defer++;
		return;
	}

	SH4_PHASE(PH_VBLANK);
	snapshot_present_regs();
	if (present_fb_convert() && rth_run(JOB_PRESENT, 0))
	{
		// Mark what is now on screen; the next write by the game clears the mark.
		vwi(s_snap.fb_r_sof1, FB_PRESENT_MAGIC);
		FrameCount++;
	}
	SH4_PHASE(PH_GUEST);
}

void StartRender()
{
	StallScope st_(ST_REND);
	SH4_PHASE(PH_RENDER);
	spe_flush(s_ta);
	snapshot_render_regs();
	TaContext* rc = find_context(s_snap.param_base);
	if (!rc)
	{
		static u32 s_warned;
		if (s_warned++ < 5)
			printf("[ps3rend] no TA context for PARAM_BASE=%08X, drawing TA_ISP_BASE's\n", (u32)PARAM_BASE);
		rc = s_ta;
	}

	// Render-done IRQ timing; games pace off these IRQs. This is the previous
	// job's count (TR3 replays later), close enough for a clamped delay.
	u32 vtx = rc->rend_nvtx;
	sys_sync();   // plan 5.36: the countdowns start at the next slice end, as with the update every slice
	if (get_render_delay_preset())
	{
		render_isp_pending_cycles = 800000;
		render_tsp_pending_cycles = 850000;
		render_vd_pending_cycles  = 900000;
		render_end_pending_cycles = 0;
	}
	else
	{
		render_end_pending_cycles = vtx * 15;
		if (render_end_pending_cycles < 50000)
			render_end_pending_cycles = 50000;
	}
	sys_kick();
	raw_note_peak(rc);

	// FB_W_SOF1 bit 24 = render to texture, not a displayed frame.
	if (s_snap.fb_w_sof1 & 0x1000000)
	{
		s_stat_rtt++;
		SH4_PHASE(PH_RTT);
		rth_run(JOB_RTT, rc);
		SH4_PHASE(PH_GUEST);
		return;
	}

	if (rth_run(JOB_RENDER, rc))
		FrameCount++;
	// Mark the framebuffer the PVR just produced, so VBlank knows it is on
	// screen. A dropped frame is marked too: the guest believes it rendered.
	fb_stamp_magic();
	SH4_PHASE(PH_GUEST);
}

void EndRender() {}

// The store stage owns TA_ISP_BASE: it picks the context the raw bytes land in.
// The decoder-side ListInit/ListCont run later, from decode_pending().
void ListCont()
{
	set_ta_context(TA_ISP_BASE);
	spe_flush(s_ta);
	if (s_ta->n_cont < MAX_CONT_MARKS)
	{
		s_ta->cont_marks[s_ta->n_cont++] = s_ta->raw_used;
		if (g_spe_on)
			ps3spe_cont((u32)(s_ta - s_ctx), s_ta->raw_used);
	}
	else if (!s_ta->raw_overflow)
	{
		s_ta->raw_overflow = true;
		s_stat_raw_of++;
	}
	// The fast store keeps no per-list-cont state: a continuation reopens the
	// finished flags the splitter tracks, and the table does not use them.
	if (!s_ta_fast)
		TaStore.ListCont();
}

void ListInit()
{
	set_ta_context(TA_ISP_BASE);
	// reset_raw drops bytes the render thread may still be replaying.
	rth_wait_decode(s_ta);
	reset_raw(s_ta);
	if (s_ta_fast)
		ta_store_reset();
	else
		TaStore.ListInit();
}

// As nullDC-v1: a soft reset keeps the lists and the stored bytes.
void SoftReset()
{
	spe_flush(s_ta);
	if (g_spe_on)
		ps3spe_soft();
	if (s_ta_fast)
		ta_store_reset();
	else
		TaStore.SoftReset();
}
