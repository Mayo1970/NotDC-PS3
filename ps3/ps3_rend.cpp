// PVR renderer on RSX via ps3gl: OP, PT, TR lists with textures and the background color.
// Decoder follows plugs/drkPvr/gxRend.cpp; the Wii presets and workarounds are not carried over.
#include "plugs/drkPvr/Renderer_if.h"
#include "ps3_phase.h"
#include "plugs/drkPvr/regs.h"
#include "ps3_video.h"
#include "ps3_tex.h"
#include "ps3_rend_regs.h"
#include "dc/pvr/pvr_if.h"
#include <algorithm>
#include <malloc.h>
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
};
#define TA_CONTEXTS 4
static TaContext s_ctx[TA_CONTEXTS];
// s_ta is the store stage's target and belongs to the SH4 thread. s_dec (replay)
// and s_rc (draw) belong to the render thread once it runs.
static TaContext* s_ta = &s_ctx[0];
static TaContext* s_dec = &s_ctx[0];
static TaContext* s_rc = &s_ctx[0];
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

// -- TR3: render thread ------------------------------------------------------
// One job at a time, as libswirl's single-slot queue. The SH4 thread snapshots
// the PVR state, hands over a TA context and returns; the render thread replays
// the stored stream and owns every RSX call from then on. A second frame is
// dropped, never queued; only RTT makes the SH4 wait.
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
static u32 s_job_raw_end, s_job_cont_end;
static u32 s_stat_dropped;
// TR5: a vblank present the render thread was too busy to take. Unlike a dropped
// render this loses nothing - the framebuffer is unmarked, so the next vblank retries.
static u32 s_stat_fb_defer;
// TR5: total time the SH4 spent waiting for the job slot, so the drop policy costs a number.
static double s_stat_wait;
static double s_stat_job_max;
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
}

// The stored stream is per list-init, so it is cleared with the decoded lists.
static void reset_raw(TaContext* c)
{
	c->raw_used = 0;
	c->raw_decoded = 0;
	c->n_cont = 0;
	c->n_cont_done = 0;
	c->raw_overflow = false;
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

// The TA's 1/W only needs a floor to keep W finite. It used to be 1e-4, which
// quantised every plane beyond W=10000 onto one depth: Space Channel 5's video
// backdrop and the background plane both landed there, so the backdrop's
// DepthMode 4 ("greater" on 1/W, GL_LESS here) compared equal and was rejected.
// reicast applies no floor at all; 1e-9 keeps x*W inside float range.
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

// The TA sends 1/W; the floor keeps W finite (NaN also takes the floor, as in gxRend).
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

	static void AppendPolyVertex0(TA_Vertex0* p) { VTX_ONE(p, p->BaseCol); }
	static void AppendPolyVertex1(TA_Vertex1* p) { VTX_ONE(p, float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); }
	static void AppendPolyVertex2(TA_Vertex2* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); }
	static void AppendPolyVertex3(TA_Vertex3* p) { VTX_ONE(p, p->BaseCol); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex4(TA_Vertex4* p) { VTX_ONE(p, p->BaseCol); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex5A(TA_Vertex5A* p) { VTX_A(p); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex5B(TA_Vertex5B* p) { VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); }
	static void AppendPolyVertex6A(TA_Vertex6A* p) { VTX_A(p); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex6B(TA_Vertex6B* p) { VTX_B(float_argb(p->BaseA, p->BaseR, p->BaseG, p->BaseB)); }
	static void AppendPolyVertex7(TA_Vertex7* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = p->u; v->v = p->v; }
	static void AppendPolyVertex8(TA_Vertex8* p) { VTX_ONE(p, intensity_argb(p->BaseInt)); v->u = uv16(p->u); v->v = uv16(p->v); }
	static void AppendPolyVertex9(TA_Vertex9* p) { VTX_ONE(p, p->BaseCol0); }
	static void AppendPolyVertex10(TA_Vertex10* p) { VTX_ONE(p, intensity_argb(p->BaseInt0)); }
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

	// As nullDC-v1: list init clears the context, a soft reset keeps the lists.
	// The context is picked by the exported ListInit/ListCont, because by replay
	// time TA_ISP_BASE may already point somewhere else.
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

// -- TR4: table-driven store stage --------------------------------------------
// The splitter above walks a switch and a function pointer for every 32-byte
// record. The store stage only needs the record size and the list state, so one
// table lookup per record replaces it (TArender.MD TR4, libswirl's ta_fsm).
// A state says what the next 32-byte word means.
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

// Indexed (state << 8) | (ParaType << 5) | (obj_ctrl >> 2). UV_16bit, Gouraud
// and Shadow never change a record's size, so the key drops them and ParaType
// owns bit 5 alone; without the mask a sprite would alias a 16-bit-UV polygon.
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

// One table lookup per 32-byte record. The state carries across calls, so a
// 64-byte record split over two SQ or DMA blocks resumes by itself.
static void ta_store_fsm_run(Ta_Dma* p, u32 words)
{
	u32 st = s_fsm_state;
	for (u32 i = 0; i < words; i++, p++)
	{
		// ParaType is bits 31..29 and obj_ctrl the low byte, in both endians.
		u32 full = p->pcw.full;
		u32 e = s_fsm[(st << 8) | ((full >> 24) & 0xE0) | ((full >> 2) & 0x1F)];
		st = (e & TAS_SLOW) ? ta_store_slow(p, (u8)(e & 7)) : e;
	}
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

// Replays [from, to) of the stored stream through the real decoder. Merging what
// arrived as separate SQ and DMA blocks is safe: a 64-byte record is only split
// when it lands on a block end, and both halves feed the same decoder calls.
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

// Decodes [raw_decoded, raw_end) of the stored stream, splitting where
// TA_LIST_CONT arrived. raw_end is latched on the SH4 thread at STARTRENDER, so
// the SH4 can keep appending while this runs and the extra bytes decode next
// time. A stream with nothing new decodes nothing, so the lists stay as they
// were and a second STARTRENDER redraws them, as nullDC-v1 does.
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

// On overflow the store splitter still runs, over the source bytes, so the guest
// keeps getting its list-end interrupts; only the replay copy is dropped.
void ps3ta_store_sq(u32* data)
{
	TaContext* c = s_ta;
	Ta_Dma* rec = (Ta_Dma*)data;
	if (c->raw && c->raw_used + 32 <= TA_RAW_SIZE)
	{
		u8* dst = c->raw + c->raw_used;
		memcpy(dst, data, 32);
		c->raw_used += 32;
		raw_note_peak(c);
		rec = (Ta_Dma*)dst;
	}
	else if (!c->raw_overflow)
	{
		c->raw_overflow = true;
		s_stat_raw_of++;
	}
	if (s_ta_fast)
	{
		ta_store_fsm_run(rec, 1);
		return;
	}
	verify(TaStore.TaCmd != 0);
	TaStore.TaCmd(rec, rec);
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
		raw_note_peak(c);
		p = (Ta_Dma*)dst;
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

// VRAM is stored as two interleaved 32-bit banks; see gxRend.cpp fast_ConvOffset32toOffset64.
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

// TR2: everything the draw path reads, latched on the SH4 thread at STARTRENDER.
// TR3: into s_snap, which the post copies into the job the render thread runs.
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
static u32 s_tr_attempt, s_tr_emit;

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
static u32 apply_param(const DcParam& p, u32 list)
{
	if (s_cache_valid && s_cache_list == list && s_cache_isp == p.isp.full &&
	    s_cache_tsp == p.tsp.full && s_cache_tcw == p.tcw.full && s_cache_pcw == p.pcw.full)
		return s_cache_or;

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

	bool textured = p.pcw.Texture && ps3tex_bind(p.tsp, p.tcw);
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

// Draws one list in submission order: opaque, punch-through, and presorted translucent.
static void draw_list(u32 list)
{
	u32 last_param = 0xFFFFFFFF;
	u32 alpha_or = 0;
	for (u32 i = 0; i < s_rc->nstrip; i++)
	{
		const DcStrip& s = s_rc->strip[i];
		if (s.list != list)
			continue;

		if (s.param != last_param)
		{
			last_param = s.param;
			alpha_or = apply_param(s_rc->param[s.param], list);
		}

		glBegin(GL_TRIANGLE_STRIP);
		const DcVertex* v = &s_rc->vtx[s.first];
		for (u32 k = 0; k < s.count; k++, v++)
			emit_vertex(v, alpha_or);
		glEnd();

		s_stat_strips[list]++;
	}
}

// nullDC-v1 sort entry: one per translucent polygon, keyed like PolyParam::zvZ.
struct SortStrip
{
	float key;
	u32 idx;
};
static SortStrip s_order[MAX_STRIPS];

// Autosort translucent pass, as nullDC-v1 SortPParams (AlphaSortMode 1, its default):
// whole polygons ordered by the average 1/W of their vertices, so the nearest draws last.
// The PVR itself sorts per pixel; this is the approximation the reference ships.
static void draw_tr_sorted()
{
	u32 n = 0;
	for (u32 i = 0; i < s_rc->nstrip; i++)
	{
		const DcStrip& st = s_rc->strip[i];
		if (st.list != ListType_Translucent || st.count == 0)
			continue;
		float sum = 0.0f;
		for (u32 k = 0; k < st.count; k++)
			sum += 1.0f / s_rc->vtx[st.first + k].w;
		s_order[n].key = sum / st.count;
		s_order[n].idx = i;
		n++;
	}
	if (n == 0)
		return;
	std::stable_sort(s_order, s_order + n, [](const SortStrip& a, const SortStrip& b) { return a.key < b.key; });

	u32 last_param = 0xFFFFFFFF;
	u32 alpha_or = 0;
	for (u32 j = 0; j < n; j++)
	{
		const DcStrip& s = s_rc->strip[s_order[j].idx];
		if (s.param != last_param)
		{
			last_param = s.param;
			alpha_or = apply_param(s_rc->param[s.param], ListType_Translucent);
		}

		glBegin(GL_TRIANGLE_STRIP);
		const DcVertex* v = &s_rc->vtx[s.first];
		for (u32 k = 0; k < s.count; k++, v++)
			emit_vertex(v, alpha_or);
		glEnd();

		s_stat_strips[ListType_Translucent]++;
	}
}

// nullDC-v1 SetMVS_Mode. Mode 0 counts volume crossings by flipping stencil bit 1;
// modes 1 and 2 fold the count of the volume just closed into bit 0.
static void set_mvs_mode(u32 mode, const ModVolParam& p)
{
	if (mode == 0)
	{
		glEnable(GL_DEPTH_TEST);
		glStencilMask(2);
		glStencilFunc(GL_ALWAYS, 0, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
		set_cull(p.cull);
	}
	else
	{
		// 1 = last "in" (any crossing counts), 2 = last "out".
		if (mode == 1)
			glStencilFunc(GL_LEQUAL, 1, 3);
		else
			glStencilFunc(GL_GREATER, 2, 3);
		glStencilOp(GL_ZERO, GL_KEEP, GL_REPLACE);
		// Summing the volume area must not be depth tested.
		glDisable(GL_DEPTH_TEST);
		glStencilMask(3);
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
	if (first + count > s_rc->nmtri)
		return;
	glBegin(GL_TRIANGLES);
	for (u32 i = 0; i < count; i++)
	{
		const ModTri& t = s_rc->mtri[first + i];
		emit_mod_vertex(t.x0, t.y0, t.z0);
		emit_mod_vertex(t.x1, t.y1, t.z1);
		emit_mod_vertex(t.x2, t.y2, t.z2);
	}
	glEnd();
}

// Opaque modifier volumes, as nullDC-v1 ModVolMode Normal: count the volume
// crossings in the stencil, then darken every pixel that ended up inside.
static void draw_modvols(float cx0, float cy0, float cw, float ch, float quad_w)
{
	if (s_rc->nmvol < 2 || s_rc->nmtri == 0)
		return;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	// The DC compares 1/W with "greater"; our depth is W, so the test flips.
	glDepthFunc(GL_LESS);
	glEnable(GL_STENCIL_TEST);

	u32 mod_last = 0;
	for (u32 i = 0; i + 1 < s_rc->nmvol; i++)
	{
		const ModVolParam& p = s_rc->mvol[i];
		u32 mod_base = p.first;
		u32 sz = s_rc->mvol[i + 1].first - mod_base;
		if (p.depth == 0)
		{
			set_mvs_mode(0, p);
			draw_mod_tris(mod_base, sz);
		}
		else if (p.depth < 3)
		{
			// Each triangle closes a volume: count it, then merge its area.
			while (sz)
			{
				set_mvs_mode(0, p);
				draw_mod_tris(mod_base, 1);
				set_mvs_mode(p.depth, p);
				draw_mod_tris(mod_last, mod_base - mod_last + 1);
				mod_last = mod_base + 1;
				sz--;
				mod_base++;
			}
		}
	}

	// Shade the odd ("in") pixels. nullDC-v1 uses a flat 50% black.
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glStencilFunc(GL_EQUAL, 1, 1);
	glStencilMask(0);
	glColor4ub(0, 0, 0, 128);
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
// Reset once per stats window so the dump costs nothing between them.
static u32 s_fmv_dump = 3;
static const char* s_diag_name[3] = { "op", "pt", "tr" };

// One line per big textured strip: is the fullscreen quad really the YUV one,
// and does the converter claim the address it samples?
static void dump_fmv_strip(const DcStrip& st, const DcParam& p,
                           float sx0, float sx1, float sy0, float sy1, float sw0)
{
	u32 addr = (p.tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
	u32 cw = 0, ch = 0;
	int hit = yuv_conv_lookup(addr, &cw, &ch);
	// 0..1 means the quad expects the whole declared texture; about 0..0.625
	// means it only samples the converter's 320 of the 512 declared columns.
	float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
	for (u32 k = 0; k < st.count; k++)
	{
		const DcVertex& vv = s_rc->vtx[st.first + k];
		if (vv.u < u0) u0 = vv.u;
		if (vv.u > u1) u1 = vv.u;
		if (vv.v < v0) v0 = vv.v;
		if (vv.v > v1) v1 = vv.v;
	}
	printf("[yuvq] tcw=%08X fmt=%u scan=%u stride=%u vq=%u mip=%u addr=%06X "
	       "decl=%ux%u texctl=%u x=[%g..%g] y=[%g..%g] w=%g | conv=%d %ux%u uv=[%g..%g],[%g..%g]\n",
	       (unsigned)p.tcw.full, p.tcw.NO_PAL.PixelFmt, p.tcw.NO_PAL.ScanOrder,
	       p.tcw.NO_PAL.StrideSel, p.tcw.NO_PAL.VQ_Comp, p.tcw.NO_PAL.MipMapped,
	       addr, 8u << p.tsp.TexU, 8u << p.tsp.TexV, (unsigned)(g_rr.text_control & 31),
	       (double)sx0, (double)sx1, (double)sy0, (double)sy1, (double)sw0,
	       hit, cw, ch, (double)u0, (double)u1, (double)v0, (double)v1);
}

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
		for (u32 i = 0; i < s_rc->nstrip; i++)
		{
			const DcStrip& st = s_rc->strip[i];
			if (st.list != lists[L])
				continue;
			const DcParam& p = s_rc->param[st.param];
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
				const DcVertex& v = s_rc->vtx[st.first + k];
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
				// Throttled: only the big textured ones, and only for the log window.
				if (p.pcw.Texture && s_fmv_dump < 3)
				{
					s_fmv_dump++;
					dump_fmv_strip(st, p, sx0, sx1, sy0, sy1, sw0);
				}
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

// Draws the render context into the active viewport. The canvas rect is the DC
// area mapped onto it: the whole screen when displaying, the FB clip window for
// a render to texture.
static void draw_scene(float cx0, float cy0, float cw, float ch)
{
	// ISP_BACKGND_D is the background 1/W (low 4 bits masked, as in nullDC-v1).
	u32 bgd_bits = g_rr.isp_backgnd_d & ~0xFu;
	float bg_inv_w;
	memcpy(&bg_inv_w, &bgd_bits, 4);
	float bg_w = 1.0f / ((bg_inv_w >= INV_W_FLOOR) ? bg_inv_w : INV_W_FLOOR);

	// reicast glesrend.cpp pins the far plane at 1/W = 0, so only the nearest
	// vertex sets the range; tracking the far end let one poly at 1/W <= 0.0001 eat it.
	union { float f; s32 i; } bz, mz;
	bz.f = bg_inv_w;
	mz.f = s_rc->max_inv_w;
	float max_inv_w = (mz.i < bz.i && bz.i < 0x49800000) ? bg_inv_w : mz.f;
	// reicast's sanity fallback: past 1024*1024 the range is garbage, not a scene.
	if (!(max_inv_w > 0.0f) || max_inv_w > 1048576.0f)
		max_inv_w = 10.0f * 1024.0f;
	// reicast's fZ_max *= 1.001: the nearest layer defines the plane it sits on,
	// and float rounding would otherwise clip it away.
	max_inv_w *= 1.001f;
	float n = 1.0f / max_inv_w;
	if (s_rc->min_w < s_stat_w_lo) s_stat_w_lo = s_rc->min_w;
	if (s_rc->max_w > s_stat_w_hi) s_stat_w_hi = s_rc->max_w;
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

	if (s_rc->nstrip != 0)
	{
		collect_diag();
		// Hardware order: opaque, punch-through, translucent.
		reset_param_cache();
		draw_list(ListType_Opaque);

		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GEQUAL, (g_rr.pt_alpha_ref & 0xFF) / 255.0f);
		reset_param_cache();
		draw_list(ListType_Punch_Through);

		// Hardware shades the opaque volumes between punch-through and translucent.
		draw_modvols(cx0, cy0, cw, ch, bg_w);

		// apply_param turns the "alpha > 0" test on per translucent poly.
		glDisable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
		reset_param_cache();
		u32 a0 = ps3gl_draw_attempt, e0 = ps3gl_draw_emit;
		if (g_rr.autosort)
			draw_tr_sorted();
		else
			draw_list(ListType_Translucent);
		s_tr_attempt = ps3gl_draw_attempt - a0;
		s_tr_emit = ps3gl_draw_emit - e0;
	}
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
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

// -- Render to texture -------------------------------------------------------
// FB_W_SOF1 bit 24 marks a pass the PVR writes into the texture area instead of
// the screen. gxRend.cpp does the same: draw the clip window at its own size,
// read the pixels back and pack them into VRAM, where the texture cache sees
// the change through its hash and re-decodes.
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

// -- Framebuffer writes ------------------------------------------------------
// 2D titles draw straight into VRAM and never issue a render, so nothing would
// reach the screen. gxRend.cpp marks the framebuffer after each PVR render; if
// the mark is gone at vblank, another writer refreshed it and we show it.
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
	if (now - s_stat_t0 < 5.0)
		return;
	u32 r = s_stat_renders ? s_stat_renders : 1;
	u32 decodes, live;
	ps3tex_stats(&decodes, &live);
	printf("[rend] renders=%u drop=%u wait=%ums fbdef=%u rtt=%u/%u fb=%u sort=%s store=%s job_max=%ums avg/render: vtx=%u strips op=%u pt=%u tr=%u (built op=%u pt=%u tr=%u) mv_tris op=%u tr=%u | trdraw=%u/%u | w=[%g..%g] near=%g wclamp=%u/%u bg=%g zw=%u | tex decodes=%u live=%u | overflow=%u ta_raw_of=%u peak=%uK/%uK\n",
	       s_stat_renders, s_stat_dropped, (u32)(s_stat_wait * 1000.0), s_stat_fb_defer,
	       s_stat_rtt_copies, s_stat_rtt, s_stat_fb_presents,
	       g_rr.autosort ? "auto" : "pre", s_ta_fast ? "fsm" : "split",
	       (u32)(s_stat_job_max * 1000.0), s_stat_vtx / r,
	       s_stat_strips[ListType_Opaque] / r, s_stat_strips[ListType_Punch_Through] / r,
	       s_stat_strips[ListType_Translucent] / r,
	       s_stat_built[ListType_Opaque] / r, s_stat_built[ListType_Punch_Through] / r,
	       s_stat_built[ListType_Translucent] / r,
	       s_stat_mv[1] / r, s_stat_mv[3] / r,
	       s_tr_emit, s_tr_attempt,
	       (double)s_stat_w_lo, (double)s_stat_w_hi, (double)s_stat_near_lo,
	       s_stat_wclamp, s_stat_wtotal, (double)s_stat_bg_inv_w, s_stat_bg_zw,
	       decodes, live, s_overflow,
	       s_stat_raw_of, s_stat_raw_peak / 1024, (u32)(TA_RAW_SIZE / 1024));
	{
		u32 y[20];
		yuv_stats(y);
		printf("[yuv] inits=%u calls=%u bytes=%u blk420=%u blk422=%u "
		       "dest=%06X size=%ux%u ctrl=%08X first=%06X:%08X midinit=%u@%u/%u "
		       "inUVY=%08X %08X %08X mean=%u/%u/%u\n",
		       y[0], y[1], y[2], y[3], y[4], y[5], y[6], y[7], y[8], y[9], y[10],
		       y[11], y[12], y[13], y[14], y[15], y[16],
		       y[17], y[18], y[19]);
	}
	s_fmv_dump = 0;
	print_diag();
	// A job far longer than a frame is the stall TArender.MD TR3 asks us to watch for.
	if (s_stat_job_max > 0.25)
		printf("[ps3rend] render job stalled for %u ms\n", (u32)(s_stat_job_max * 1000.0));
	memset(s_stat_mv, 0, sizeof(s_stat_mv));
	s_stat_renders = s_stat_rtt = s_stat_rtt_copies = s_stat_fb_presents = s_stat_vtx = 0;
	memset(s_stat_strips, 0, sizeof(s_stat_strips));
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

// Stage 1 of a job: the TA replay. It is split from the draw so the inline path
// can still run it under the guest rounding mode, as TR2 did.
static void rth_job_decode(TaContext* c, u32 raw_end, u32 cont_end)
{
	REND_PHASE(PH_TADEC);
	decode_pending(c, raw_end, cont_end);
	s_rc = c;
	rth_mark_decoded();
}

// Stage 2: every RSX call of the job.
static void rth_job_draw(int type)
{
	double t0 = os_GetSeconds();
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
		s_stat_vtx += s_rc->nvtx;
		VertexCount += s_rc->nvtx;
		for (int k = 0; k < 5; k++)
			s_stat_mv[k] += s_rc->mv_tris[k];

		REND_PHASE(PH_BEGINFRAME);
		if (ps3_video_begin_frame())
		{
			REND_PHASE(PH_DRAW);
			draw_frame();
			REND_PHASE(PH_ENDFRAME);
			ps3_video_end_frame();
		}
	}
	REND_PHASE(PH_IDLE);

	double dt = os_GetSeconds() - t0;
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
		u32 raw_end = s_job_raw_end, cont_end = s_job_cont_end;
		memcpy(&g_rr, &s_job_rr, sizeof(g_rr));
		s_job_pending = false;
		sysMutexUnlock(s_rmx);

		if (type != JOB_PRESENT)
			rth_job_decode(c, raw_end, cont_end);
		rth_job_draw(type);

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

// Hands one job over, or runs it inline when the render thread is off (the TR2
// path). Returns false when the job was not taken because the render thread was
// still busy. RTT never drops: the guest reads the rendered texture straight
// back from VRAM.
//
// TR5: how long a render waits for the slot is the Frame Drop preset. libswirl
// drops at once; waiting costs SH4 time but keeps the frame. The bounded wait is
// the default, so a render thread that hangs can never hang the SH4 with it.
// A present is never waited for: dropping one only defers it to the next vblank.
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
	if (type != JOB_PRESENT)
		rth_job_decode(c, c ? c->raw_used : 0, c ? c->n_cont : 0);
	// Host FPSCR follows the SH4 rounding mode (sh4_registers.cpp); draw with round-to-nearest.
	double host_fpscr;
	__asm__ volatile("mffs %0" : "=f"(host_fpscr) :: "memory");
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	rth_job_draw(type);
	__asm__ volatile("mtfsf 0xff,%0" :: "f"(host_fpscr) : "memory");
	return true;
}

bool InitRenderer()
{
	reset_all_contexts();
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		if (s_ctx[i].raw)
			continue;
		s_ctx[i].raw = (u8*)memalign(32, TA_RAW_SIZE);
		if (!s_ctx[i].raw)
			printf("[ps3rend] TA raw buffer %u alloc failed; that context drops records\n", i);
	}
	// Keep running headless if the RSX fails: the UDP log still shows why.
	if (!ps3_video_init())
		printf("[ps3rend] RSX init failed, running headless\n");
	if (!TileAccel.Init() || !TaStore.Init())
		return false;
	s_ta_fast = get_ta_fast_preset() != 0;
	ta_fsm_build();
	ta_store_reset();
	rth_start();
	return true;
}

void TermRenderer()
{
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
	SH4_PHASE(PH_RENDER);
	snapshot_render_regs();
	TaContext* rc = find_context(s_snap.param_base);
	if (!rc)
	{
		static u32 s_warned;
		if (s_warned++ < 5)
			printf("[ps3rend] no TA context for PARAM_BASE=%08X, drawing TA_ISP_BASE's\n", (u32)PARAM_BASE);
		rc = s_ta;
	}

	// Same render-done timing as gxRend.cpp StartRender(); games pace off these
	// IRQs. TR3 replays the TA stream after this point, so nvtx is still this
	// context's previous count - close enough for a delay that is clamped anyway.
	u32 vtx = rc->nvtx;
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
	if (s_ta->n_cont < MAX_CONT_MARKS)
		s_ta->cont_marks[s_ta->n_cont++] = s_ta->raw_used;
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
	if (s_ta_fast)
		ta_store_reset();
	else
		TaStore.SoftReset();
}
