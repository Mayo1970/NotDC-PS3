// PVR renderer on RSX via ps3gl: OP, PT, TR lists with textures and the background color.
// Decoder follows plugs/drkPvr/gxRend.cpp; the Wii presets and workarounds are not carried over.
#include "plugs/drkPvr/Renderer_if.h"
#include "plugs/drkPvr/regs.h"
#include "ps3_video.h"
#include "ps3_tex.h"
#include <algorithm>

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

struct DcParam
{
	PCW pcw;
	ISP_TSP isp;
	TSP tsp;
	TCW tcw;
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
	u32 mv_tris[5];
	ModTri mtri[MAX_MODTRI];
	ModVolParam mvol[MAX_MODVOL];
	u32 nmtri, nmvol;
};
#define TA_CONTEXTS 4
static TaContext s_ctx[TA_CONTEXTS];
static TaContext* s_ta = &s_ctx[0];
static TaContext* s_rc = &s_ctx[0];
static u32 s_cur_list;
static u32 s_overflow;

// Intensity polys scale this face color per vertex (PolyParam 1/2B/4B).
static float s_face_a = 1, s_face_r = 1, s_face_g = 1, s_face_b = 1;
static u32 s_sprite_col = 0xFFFFFFFF;

static float s_fb_scale_x = 1.0f, s_fb_scale_y = 1.0f;

static u32 s_stat_renders, s_stat_rtt, s_stat_vtx;
static u32 s_stat_strips[5];
// Strips as the TA builds them, so a list that is captured but never drawn shows up.
static u32 s_stat_built[5];
// Modifier volume triangles per list for the stats line (1 = opaque MV, 3 = translucent MV).
static u32 s_mv_list;
static u32 s_stat_mv[5];
static double s_stat_t0;

static void reset_lists(TaContext* c)
{
	c->nvtx = 0;
	c->nstrip = 0;
	c->nparam = 0;
	c->strip_first = 0;
	c->min_w = 1e30f;
	c->max_w = 0.0f;
	c->nmtri = 0;
	c->nmvol = 0;
	memset(c->mv_tris, 0, sizeof(c->mv_tris));
}

static void reset_all_contexts()
{
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		s_ctx[i].addr = 0xFFFFFFFF;
		reset_lists(&s_ctx[i]);
	}
	s_ta = s_rc = &s_ctx[0];
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
		if (c == s_rc)
			c = &s_ctx[s_next_slot++ % TA_CONTEXTS];
		c->addr = addr;
		reset_lists(c);
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

// The TA sends 1/W; the floor keeps W finite (NaN also takes the floor, as in gxRend).
static inline void set_pos(DcVertex* v, float x, float y, float inv_w)
{
	float safe = (inv_w >= 0.0001f) ? inv_w : 0.0001f;
	float w = 1.0f / safe;
	v->x = x;
	v->y = y;
	v->w = w;
	if (w < s_ta->min_w) s_ta->min_w = w;
	if (w > s_ta->max_w) s_ta->max_w = w;
}

// Modifier volume corners carry no color, so only their depth joins the range.
static inline void track_w(float inv_w)
{
	float safe = (inv_w >= 0.0001f) ? inv_w : 0.0001f;
	float w = 1.0f / safe;
	if (w < s_ta->min_w) s_ta->min_w = w;
	if (w > s_ta->max_w) s_ta->max_w = w;
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
	if (s_ta->nvtx < MAX_VTX)
		return &s_ta->vtx[s_ta->nvtx];
	s_overflow++;
	return 0;
}

// One-part vertex: position and color in the same TA packet.
#define VTX_ONE(pkt, color)                                          \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);        \
	v->col = (color);                                                \
	s_ta->nvtx++;

// Two-part vertex: part A has the position, part B the color and the commit.
#define VTX_A(pkt)                                                   \
	DcVertex* v = vtx_slot();                                        \
	if (!v) return;                                                  \
	set_pos(v, (pkt)->xyz[0], (pkt)->xyz[1], (pkt)->xyz[2]);

#define VTX_B(color)                                                 \
	if (s_ta->nvtx >= MAX_VTX) return;                               \
	s_ta->vtx[s_ta->nvtx].col = (color);                             \
	s_ta->nvtx++;

#define VTX_B_KEEP()                                                 \
	if (s_ta->nvtx >= MAX_VTX) return;                               \
	s_ta->nvtx++;

struct Ps3VertexDecoder
{
	static void StartList(u32 ListType) { s_cur_list = ListType; }
	// nullDC-v1 closes the volume list with a terminator holding the final count.
	static void EndList(u32 ListType)
	{
		if (ListType != ListType_Opaque_Modifier_Volume || s_ta->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_ta->mvol[s_ta->nmvol++];
		p.first = s_ta->nmtri;
		p.cull = p.depth = p.volume_last = 0;
	}

	template <class T>
	static void add_param(T* pp)
	{
		// When full, keep rewriting the last slot: wrong state, but no overrun.
		if (s_ta->nparam < MAX_PARAMS)
			s_ta->nparam++;
		DcParam& p = s_ta->param[s_ta->nparam - 1];
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

	static void StartPolyStrip() { s_ta->strip_first = s_ta->nvtx; }
	static void EndPolyStrip()
	{
		u32 count = s_ta->nvtx - s_ta->strip_first;
		StripCount++;
		if (count < 3 || s_ta->nparam == 0 || s_ta->nstrip >= MAX_STRIPS)
			return;
		DcStrip& s = s_ta->strip[s_ta->nstrip++];
		s.first = s_ta->strip_first;
		s.count = (u16)(count > 0xFFFF ? 0xFFFF : count);
		s.list = (u8)s_cur_list;
		s.param = (u16)(s_ta->nparam - 1);
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
		if (s_ta->nvtx + 4 > MAX_VTX)
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
			s_ta->vtx[s_ta->nvtx] = s_spr[order[i]];
			s_ta->vtx[s_ta->nvtx].col = s_sprite_col;
			s_ta->nvtx++;
		}
		EndPolyStrip();
	}

	// Only the opaque volume list is kept, as nullDC-v1 does.
	static void StartModVol(TA_ModVolParam* param)
	{
		s_mv_list = s_cur_list;
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_ta->nmvol >= MAX_MODVOL)
			return;
		ModVolParam& p = s_ta->mvol[s_ta->nmvol++];
		p.first = s_ta->nmtri;
		p.cull = (u8)param->isp.CullMode;
		p.depth = (u8)param->isp.DepthMode;
		p.volume_last = (u8)param->pcw.Volume;
	}
	static void ModVolStripEnd() {}
	// Part A opens the triangle, part B closes it: both test the same cap.
	static void AppendModVolVertexA(TA_ModVolA* mvv)
	{
		s_ta->mv_tris[s_mv_list % 5]++;
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_ta->nmtri >= MAX_MODTRI)
			return;
		ModTri& t = s_ta->mtri[s_ta->nmtri];
		t.x0 = mvv->x0; t.y0 = mvv->y0; t.z0 = mvv->z0;
		t.x1 = mvv->x1; t.y1 = mvv->y1; t.z1 = mvv->z1;
		t.x2 = mvv->x2;
		track_w(mvv->z0);
		track_w(mvv->z1);
	}
	static void AppendModVolVertexB(TA_ModVolB* mvv)
	{
		if (s_cur_list != ListType_Opaque_Modifier_Volume || s_ta->nmtri >= MAX_MODTRI)
			return;
		ModTri& t = s_ta->mtri[s_ta->nmtri++];
		t.y2 = mvv->y2;
		t.z2 = mvv->z2;
		track_w(mvv->z2);
	}

	static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax) {}
	static void TileClipMode(u32 mode) {}

	// As nullDC-v1: list init clears the context at TA_ISP_BASE, a soft reset keeps the lists.
	static void ListCont() { set_ta_context(TA_ISP_BASE); }
	static void ListInit() { set_ta_context(TA_ISP_BASE); reset_lists(s_ta); }
	static void SoftReset() {}
};
DcVertex Ps3VertexDecoder::s_spr[4];
float Ps3VertexDecoder::s_spr_z[3];

FifoSplitter<Ps3VertexDecoder> TileAccel;

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

struct BgVertex
{
	float x, y, u, v;
	u32 col;
};

// Background plane (ISP_BACKGND_T): 3 vertices; the PVR extends their plane over the whole screen.
// Bitfields are extracted by hand; the regs.h struct layout is little-endian.
static void background_plane(BgVertex* out, DcParam* param)
{
	u32 param_base = PARAM_BASE & 0xF00000;
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

static bool s_autosort = true;

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
		glDepthFunc(s_autosort ? GL_LEQUAL : depth_func[p.isp.DepthMode]);
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
	float safe = (inv_w >= 0.0001f) ? inv_w : 0.0001f;
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

// Draws the render context into the active viewport. The canvas rect is the DC
// area mapped onto it: the whole screen when displaying, the FB clip window for
// a render to texture.
static void draw_scene(float cx0, float cy0, float cw, float ch)
{
	// ISP_BACKGND_D is the background 1/W (low 4 bits masked, as in nullDC-v1).
	u32 bgd_bits = ISP_BACKGND_D & ~0xFu;
	float bg_inv_w;
	memcpy(&bg_inv_w, &bgd_bits, 4);
	float bg_w = 1.0f / ((bg_inv_w >= 0.0001f) ? bg_inv_w : 0.0001f);

	// Depth range from this frame's W; the margins keep the nearest and farthest layer unclipped.
	float n = s_rc->min_w < bg_w ? s_rc->min_w : bg_w;
	float f = s_rc->max_w > bg_w ? s_rc->max_w : bg_w;
	if (!(n < f) || n <= 0.0f)
	{
		n = 0.0001f;
		f = 100000.0f;
	}
	n *= 0.999f;
	f *= 1.001f;

	// Input (x*W, y*W, W): clip.w = W, so attributes interpolate with perspective and z_ndc = A + B/W.
	float m[16] = { 0 };
	m[0] = 2.0f / cw;
	m[8] = -1.0f - 2.0f * cx0 / cw;
	m[5] = -2.0f / ch;
	m[9] = 1.0f + 2.0f * cy0 / ch;
	m[10] = (f + n) / (f - n);
	m[14] = -2.0f * f * n / (f - n);
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
	BgVertex bgv[3];
	DcParam bgp;
	background_plane(bgv, &bgp);
	reset_param_cache();
	u32 bg_alpha_or = apply_param(bgp, ListType_Opaque);
	static const float corner[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
	glBegin(GL_TRIANGLE_STRIP);
	for (int k = 0; k < 4; k++)
	{
		BgVertex bv;
		bg_eval(bgv, cx0 + corner[k][0] * cw, cy0 + corner[k][1] * ch, &bv);
		u32 c = bv.col | bg_alpha_or;
		glColor4ub((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
		glTexCoord2f(bv.u, bv.v);
		glVertex3f(bv.x * bg_w, bv.y * bg_w, bg_w);
	}
	glEnd();

	s_autosort = using_autosort();
	if (s_rc->nstrip != 0)
	{
		// Hardware order: opaque, punch-through, translucent.
		reset_param_cache();
		draw_list(ListType_Opaque);

		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GEQUAL, (PT_ALPHA_REF & 0xFF) / 255.0f);
		reset_param_cache();
		draw_list(ListType_Punch_Through);

		// Hardware shades the opaque volumes between punch-through and translucent.
		draw_modvols(cx0, cy0, cw, ch, bg_w);

		// apply_param turns the "alpha > 0" test on per translucent poly.
		glDisable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
		reset_param_cache();
		if (s_autosort)
			draw_tr_sorted();
		else
			draw_list(ListType_Translucent);
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

	draw_scene(0.0f, 0.0f, 640.0f * s_fb_scale_x, 480.0f * s_fb_scale_y);
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
	u32 x0 = FB_X_CLIP.min, x1 = FB_X_CLIP.max;
	u32 y0 = FB_Y_CLIP.min, y1 = FB_Y_CLIP.max;
	if (x1 < x0 || y1 < y0)
		return;
	u32 w = x1 - x0 + 1, h = y1 - y0 + 1;
	u32 sw, sh;
	ps3_video_size(&sw, &sh);
	if (w > sw || h > sh || w > 1024 || h > 1024)
		return;

	u32 packmode = FB_W_CTRL & 7;
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
	u32 base = FB_W_SOF1 & VRAM_MASK & ~3u;
	u32 stride = (FB_W_LINESTRIDE & 0x1FF) * 8;
	if (stride == 0)
		stride = (packmode >= 6) ? w * 4 : w * 2;
	u16 kbit = (u16)(FB_W_CTRL & 0x8000);

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
	vwi(FB_W_SOF1, FB_PRESENT_MAGIC);
	vwi(FB_W_SOF2, FB_PRESENT_MAGIC);
}

static bool fb_needs_present()
{
	return vri(FB_R_SOF1 & ~3u) != FB_PRESENT_MAGIC;
}

static inline u32 e5(u32 v) { return (v << 3) | (v >> 2); }
static inline u32 e6(u32 v) { return (v << 2) | (v >> 4); }

static void present_framebuffer()
{
	u32 fb_x_size = FB_R_SIZE & 0x3FF;
	u32 fb_y_size = (FB_R_SIZE >> 10) & 0x3FF;
	u32 fb_modulus = (FB_R_SIZE >> 20) & 0x3FF;
	u32 fb_depth = FB_R_CTRL.fb_depth;
	if (fb_x_size == 0 || fb_y_size == 0)
		return;
	if (fb_depth == 2)
	{
		static u32 s_warned;
		if (s_warned++ < 3)
			printf("[ps3rend] 24-bit framebuffer read not handled\n");
		return;
	}

	// fb_x_size counts 32-bit words per line, so a 16-bit line holds twice as many pixels.
	bool wide = (fb_depth == 3);
	u32 src_w = wide ? (fb_x_size + 1) : ((fb_x_size + 1) << 1);
	u32 src_h = fb_y_size + 1;
	u32 mod_px = (fb_modulus > 0 ? fb_modulus - 1 : 0) << (wide ? 0 : 1);
	u32 bpp = wide ? 4 : 2;
	u32 cpy_w = src_w > FB2D_W ? FB2D_W : src_w;
	u32 cpy_h = src_h > FB2D_H ? FB2D_H : src_h;

	u32 addr = FB_R_SOF1;
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

	// Mark what is now on screen; the next write by the game clears the mark.
	vwi(FB_R_SOF1, FB_PRESENT_MAGIC);
	s_stat_fb_presents++;
}

bool InitRenderer()
{
	reset_all_contexts();
	// Keep running headless if the RSX fails: the UDP log still shows why.
	if (!ps3_video_init())
		printf("[ps3rend] RSX init failed, running headless\n");
	return TileAccel.Init();
}

void TermRenderer()
{
	TileAccel.Term();
	ps3tex_reset();
	s_fb_tex = 0;
	ps3_video_term();
}

void ResetRenderer(bool Manual)
{
	TileAccel.Reset(Manual);
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

	double host_fpscr;
	__asm__ volatile("mffs %0" : "=f"(host_fpscr) :: "memory");
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	present_framebuffer();
	__asm__ volatile("mtfsf 0xff,%0" :: "f"(host_fpscr) : "memory");
	FrameCount++;
}

void StartRender()
{
	s_rc = find_context(PARAM_BASE & 0xF00000);
	if (!s_rc)
	{
		static u32 s_warned;
		if (s_warned++ < 5)
			printf("[ps3rend] no TA context for PARAM_BASE=%08X, drawing TA_ISP_BASE's\n", (u32)PARAM_BASE);
		s_rc = s_ta;
	}
	u32 vtx = s_rc->nvtx;
	VertexCount += vtx;

	// Same render-done timing as gxRend.cpp StartRender(); games pace off these IRQs.
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
	if (FB_W_SOF1 & 0x1000000)
	{
		s_stat_rtt++;
		double rtt_fpscr;
		__asm__ volatile("mffs %0" : "=f"(rtt_fpscr) :: "memory");
		__asm__ volatile("mtfsfi 7,0" ::: "memory");
		rtt_render();
		__asm__ volatile("mtfsf 0xff,%0" :: "f"(rtt_fpscr) : "memory");
		return;
	}

	FrameCount++;
	s_stat_renders++;
	for (int k = 0; k < 5; k++)
		s_stat_mv[k] += s_rc->mv_tris[k];
	s_stat_vtx += vtx;

	// Host FPSCR follows the SH4 rounding mode (sh4_registers.cpp); draw with round-to-nearest.
	double host_fpscr;
	__asm__ volatile("mffs %0" : "=f"(host_fpscr) :: "memory");
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	if (ps3_video_begin_frame())
	{
		draw_frame();
		ps3_video_end_frame();
	}
	// Mark the framebuffer the PVR just produced, so VBlank knows it is on screen.
	fb_stamp_magic();
	__asm__ volatile("mtfsf 0xff,%0" :: "f"(host_fpscr) : "memory");

	double now = os_GetSeconds();
	if (now - s_stat_t0 >= 5.0)
	{
		u32 r = s_stat_renders ? s_stat_renders : 1;
		u32 decodes, live;
		ps3tex_stats(&decodes, &live);
		printf("[rend] renders=%u rtt=%u/%u fb=%u sort=%s avg/render: vtx=%u strips op=%u pt=%u tr=%u (built op=%u pt=%u tr=%u) mv_tris op=%u tr=%u | tex decodes=%u live=%u | overflow=%u\n",
		       s_stat_renders, s_stat_rtt_copies, s_stat_rtt, s_stat_fb_presents,
		       s_autosort ? "auto" : "pre", s_stat_vtx / r,
		       s_stat_strips[ListType_Opaque] / r, s_stat_strips[ListType_Punch_Through] / r,
		       s_stat_strips[ListType_Translucent] / r,
		       s_stat_built[ListType_Opaque] / r, s_stat_built[ListType_Punch_Through] / r,
		       s_stat_built[ListType_Translucent] / r,
		       s_stat_mv[1] / r, s_stat_mv[3] / r, decodes, live, s_overflow);
		memset(s_stat_mv, 0, sizeof(s_stat_mv));
		s_stat_renders = s_stat_rtt = s_stat_rtt_copies = s_stat_fb_presents = s_stat_vtx = 0;
		memset(s_stat_strips, 0, sizeof(s_stat_strips));
		memset(s_stat_built, 0, sizeof(s_stat_built));
		s_overflow = 0;
		s_stat_t0 = now;
	}
}

void EndRender() {}

void ListCont() { TileAccel.ListCont(); }
void ListInit() { TileAccel.ListInit(); }
void SoftReset() { TileAccel.SoftReset(); }
