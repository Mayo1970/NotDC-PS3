// Bundle 4 PC check, one build of the draw code (run.sh cuts bg_eval..draw_scene out of ps3_rend.cpp) over a GL that
// records every call. Three builds: the tree, R4_REF (before bundle 4), R4_OLD (before bundle 14). No SPU build.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "rend4.h"
#ifdef R4_REF
#define SIDE(x) r4_ref_##x
#elif defined(R4_OLD)
#define SIDE(x) r4_old_##x
#else
#define SIDE(x) r4_new_##x
#endif
// Each build has its own render registers.
#define g_rr SIDE(g_rr)
#include "types.h"
#include "ps3/ps3_rend_regs.h"
#include "ps3/ps3_depth.h"
#include "spe_vb.h"

RenderRegs g_rr;

namespace
{
// ---- the recording GL -----------------------------------------------------------

typedef unsigned GLenum;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef double GLclampd;
enum
{
	GL_TRIANGLES = 4, GL_TRIANGLE_STRIP = 5, GL_NEVER = 0x200, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL,
	GL_GEQUAL, GL_ALWAYS, GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
	GL_FRONT = 0x404, GL_BACK = 0x405, GL_CCW = 0x901, GL_CULL_FACE = 0xB44, GL_DEPTH_TEST = 0xB71,
	GL_STENCIL_TEST = 0xB90, GL_ALPHA_TEST = 0xBC0, GL_BLEND = 0xBE2, GL_SCISSOR_TEST = 0xC11, GL_TEXTURE_2D = 0xDE1,
	GL_FLAT = 0x1D00, GL_SMOOTH = 0x1D01, GL_KEEP = 0x1E00, GL_REPLACE = 0x1E01, GL_INVERT = 0x150A,
	GL_MODELVIEW = 0x1700, GL_PROJECTION = 0x1701, GL_MODULATE = 0x2100, GL_DECAL = 0x2101,
	GL_TEXTURE_ENV_MODE = 0x2200, GL_TEXTURE_ENV = 0x2300, GL_FALSE = 0, GL_TRUE = 1,
};
enum
{
	OP_ENABLE, OP_DISABLE, OP_BLENDFUNC, OP_DEPTHFUNC, OP_DEPTHMASK, OP_CULLFACE, OP_STENCILFUNC, OP_STENCILMASK,
	OP_STENCILOP, OP_COLORMASK, OP_TEXENV, OP_ALPHAFUNC, OP_SHADEMODEL, OP_FRONTFACE, OP_MATRIXMODE, OP_LOADMATRIX,
	OP_LOADIDENTITY, OP_DEPTHRANGE, OP_SCISSOR, OP_TEXBIND, OP_TEXBUMP, OP_TEX1, OP_DEPTHFLOAT, OP_ALLOC,
};

std::vector<R4Call> g_calls;
std::vector<float> g_tri_x;
R4Call g_st;  // the state fields only
std::vector<uint32_t> g_imm;
uint32_t g_imm_prim;
bool g_in_begin;
uint32_t g_cur[3];  // colour, u, v of the next glVertex

[[noreturn]] void r4_die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}
uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

void call(uint32_t op, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0, uint32_t a5 = 0)
{
	if (g_in_begin)
		r4_die("GL state call %u inside glBegin", op);
	R4Call c;
	memset(&c, 0, sizeof(c));
	c.kind = R4_CALL;
	c.op = op;
	c.a[0] = a0; c.a[1] = a1; c.a[2] = a2; c.a[3] = a3; c.a[4] = a4; c.a[5] = a5;
	g_calls.push_back(c);
}
void cap(GLenum what, uint32_t on)
{
	switch (what)
	{
	case GL_BLEND: g_st.blend = on; break;
	case GL_DEPTH_TEST: g_st.depth_test = on; break;
	case GL_CULL_FACE: g_st.cull = on; break;
	case GL_ALPHA_TEST: g_st.alpha_test = on; break;
	case GL_TEXTURE_2D: g_st.texture = on; break;
	case GL_SCISSOR_TEST: g_st.scissor = on; break;
	case GL_STENCIL_TEST: g_st.stencil_test = on; break;
	default: r4_die("glEnable/glDisable of %X", what);
	}
}
void glEnable(GLenum c) { call(OP_ENABLE, c); cap(c, 1); }
void glDisable(GLenum c) { call(OP_DISABLE, c); cap(c, 0); }
void glBlendFunc(GLenum s, GLenum d) { call(OP_BLENDFUNC, s, d); g_st.src = s; g_st.dst = d; }
void glDepthFunc(GLenum f) { call(OP_DEPTHFUNC, f); g_st.depth_func = f; }
void glDepthMask(GLboolean m) { call(OP_DEPTHMASK, m); g_st.depth_mask = m; }
void glCullFace(GLenum f) { call(OP_CULLFACE, f); g_st.cull_face = f; }
void glStencilFunc(GLenum f, int ref, unsigned mask)
{
	call(OP_STENCILFUNC, f, (uint32_t)ref, mask);
	g_st.stencil_func[0] = f; g_st.stencil_func[1] = (uint32_t)ref; g_st.stencil_func[2] = mask;
}
void glStencilMask(unsigned m) { call(OP_STENCILMASK, m); g_st.stencil_mask = m; }
void glStencilOp(GLenum a, GLenum b, GLenum c)
{
	call(OP_STENCILOP, a, b, c);
	g_st.stencil_op[0] = a; g_st.stencil_op[1] = b; g_st.stencil_op[2] = c;
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { call(OP_COLORMASK, r, g, b, a); g_st.color_mask = r; }
void glTexEnvi(GLenum t, GLenum n, int v) { call(OP_TEXENV, t, n, (uint32_t)v); }
void glAlphaFunc(GLenum f, float ref)
{
	call(OP_ALPHAFUNC, f, fbits(ref));
	g_st.alpha_func = f;
	g_st.alpha_ref = fbits(ref);
}
void glShadeModel(GLenum m) { call(OP_SHADEMODEL, m); g_st.shade = m; }
void glFrontFace(GLenum m) { call(OP_FRONTFACE, m); }
void glMatrixMode(GLenum m) { call(OP_MATRIXMODE, m); }
void glLoadMatrixf(const float* m)
{
	uint32_t h = 0;
	for (int k = 0; k < 16; k++)
		h = h * 31 + fbits(m[k]);
	call(OP_LOADMATRIX, h, fbits(m[0]), fbits(m[5]), fbits(m[8]), fbits(m[9]), fbits(m[14]));
}
void glLoadIdentity() { call(OP_LOADIDENTITY); }
void glDepthRange(double n, double f) { call(OP_DEPTHRANGE, fbits((float)n), fbits((float)f)); }
void glScissor(int x, int y, int w, int h)
{
	call(OP_SCISSOR, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
	g_st.sc[0] = (uint32_t)x; g_st.sc[1] = (uint32_t)y; g_st.sc[2] = (uint32_t)w; g_st.sc[3] = (uint32_t)h;
}
void ps3gl_tex_bump(int on) { call(OP_TEXBUMP, (uint32_t)on); }
// Plan 3a Fog is off in this check: the region must make none of these calls.
enum { PS3GL_FOG_NONE, PS3GL_FOG_TABLE, PS3GL_FOG_VERTEX, PS3GL_FOG_BASE, PS3GL_FOG_BASE_TA };
void ps3gl_dc_fog(int) { r4_die("ps3gl_dc_fog with Fog off"); }
void ps3gl_tex1_bits(uint32_t b) { g_cur[1] ^= 0; (void)b; }
void ps3_video_depth_float(int on) { call(OP_DEPTHFLOAT, (uint32_t)on); }
bool ps3tex_bind(TSP tsp, TCW tcw, u32 slot, bool pt)
{
	call(OP_TEXBIND, tsp.full, tcw.full, slot, pt);
	return true;
}

R4Call snap(uint32_t kind)
{
	R4Call c = g_st;
	c.kind = kind;
	return c;
}
void glBegin(GLenum prim)
{
	g_in_begin = true;
	g_imm_prim = prim;
	g_imm.clear();
}
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { g_cur[0] = (uint32_t)r << 24 | g << 16 | b << 8 | a; }
void glTexCoord2f(float u, float v) { g_cur[1] = fbits(u); g_cur[2] = fbits(v); }
void glVertex3f(float x, float y, float z)
{
	if (!g_in_begin)
		r4_die("glVertex outside glBegin");
	const uint32_t w[6] = { fbits(x), fbits(y), fbits(z), g_cur[0], g_cur[1], g_cur[2] };
	g_imm.insert(g_imm.end(), w, w + 6);
	if (g_imm_prim == GL_TRIANGLES)
		g_tri_x.push_back(x);
}
void glEnd()
{
	g_in_begin = false;
	R4Call c = snap(R4_IMM);
	c.prim = g_imm_prim;
	c.nv = (uint32_t)(g_imm.size() / 6);
	uint32_t h = 0;
	for (uint32_t w : g_imm)
		h = h * 0x01000193u + w;
	c.hash = h;
	g_calls.push_back(c);
}

#pragma pack(push, 1)
typedef struct
{
	float x, y, z, w;
	float u0, v0;
	float u1, v1;
	uint32_t color;
} ps3gl_vertex_t;
#pragma pack(pop)

// Every block of the scene, one after the other with a spare vertex between: bundle 14 GAME ORDER can ask for a
// second one. The region writes only the newest block, so a move of the older words is safe.
std::vector<uint32_t> g_vbmem;
uint32_t g_vb_alloc, g_vb_start;  // the end of the newest block and its first vertex, counted over all blocks
ps3gl_vertex_t* ps3gl_vring_alloc(int count, uint32_t* off)
{
	call(OP_ALLOC, (uint32_t)count);
	g_vb_start = (uint32_t)(g_vbmem.size() / 9);
	g_vbmem.resize(g_vbmem.size() + (size_t)count * 9 + 9, 0xEEEEEEEEu);
	g_vb_alloc = g_vb_start + (uint32_t)count;
	*off = 0x1000 + g_vb_start * 36;
	return (ps3gl_vertex_t*)(g_vbmem.data() + (size_t)g_vb_start * 9);
}
void* ps3gl_vring_alloc_raw(uint32_t, uint32_t*) { r4_die("the SPU's vertex block with the SPU build off"); }

void draw_range(GLenum prim, uint32_t off, uint32_t stride, int first, int count, uint32_t shift)
{
	if (off != 0x1000 + g_vb_start * 36 || first < 0 || count <= 0 ||
	    g_vb_start + (uint32_t)(first + count) + shift > g_vb_alloc + (shift ? 2 : 0))
		r4_die("draw of %d+%d (offset %X, shift %u) outside the block %u..%u", first, count, off, shift, g_vb_start, g_vb_alloc);
	first += (int)g_vb_start;
	R4Call c = snap(R4_DRAW);
	c.prim = prim;
	c.first = (uint32_t)first;
	c.count = (uint32_t)count;
	c.stride = stride;
	c.shift = shift;
	uint32_t h = 0;
	for (size_t k = (size_t)first * 9; k < (size_t)(first + count) * 9; k++)
		h = h * 0x01000193u + g_vbmem[k];
	c.hash = h;
	g_calls.push_back(c);
}
void ps3gl_draw_vb(GLenum prim, uint32_t off, int first, int count) { draw_range(prim, off, 36, first, count, 0); }
void ps3gl_draw_vb_stride(GLenum prim, uint32_t off, uint32_t stride, int first, int count)
{
	draw_range(prim, off, stride, first, count, 0);
}
void ps3gl_draw_vb_shift(GLenum prim, uint32_t off, uint32_t stride, int first, int count, uint32_t shift)
{
	draw_range(prim, off, stride, first, count, shift);
}

// ---- what the region reads from the rest of ps3_rend.cpp --------------------------

const u32 ListType_Opaque = 0, ListType_Opaque_Modifier_Volume = 1, ListType_Translucent = 2,
          ListType_Translucent_Modifier_Volume = 3, ListType_Punch_Through = 4;
#define MAX_VTX    (64 * 1024)
#define MAX_STRIPS (16 * 1024)
#define MAX_PARAMS (8 * 1024)
#define MAX_PASSES 8
#define INV_W_FLOOR 1e-9f
#define STAT_WINDOW_S 5.0
#include "structs.inc"

DrawLists s_rl;
int s_rl_spe = -1;
u32 s_stat_strips[5];
enum { DT_PARAM, DT_TEX, DT_GLEND, DT_SORT, DT_MODVOL, DT_COUNT };
u64 s_dt[DT_COUNT];
inline u64 PERF_TICKS() { return 0; }
bool s_bump_on;
u32 s_fog;
void fog_scene_begin() {}
void fog_scene_end() {}
float s_stat_w_lo, s_stat_w_hi, s_stat_near_lo, s_stat_bg_inv_w;
u32 s_stat_bg_zw;
double s_stat_t0;
double os_GetSeconds() { return 0.0; }
int g_merge, g_varrays;
int get_strip_merge_preset() { return g_merge; }
int get_vertex_array_preset() { return g_varrays; }
enum { SPETEX_BG = 0xFFFFFFFEu, SPETEX_NONE = 0xFFFFFFFFu };

// The SPU build is off in this check.
enum { SPEVB_OFF, SPEVB_ON, SPEVB_CHECK };
enum { SPEVB_SEC_OP, SPEVB_SEC_PT, SPEVB_SEC_TR };
int ps3spevb_mode() { return SPEVB_OFF; }
bool ps3spevb_request(u32, u32, u32, u32, void*, u32, u32) { r4_die("SPU build request"); }
const SpeRun* ps3spevb_run(u32, u32) { r4_die("SPU run"); }
u32 ps3spevb_sec_start(u32) { r4_die("SPU run"); }
u32 ps3spevb_strips(u32) { r4_die("SPU run"); }
bool ps3spevb_tr_ppu() { r4_die("SPU run"); }
u32 ps3spevb_vb_used() { r4_die("SPU run"); }
void ps3spevb_note_alpha(bool) {}
void ps3spevb_end_frame() {}
bool ps3spevb_check_bufs(u32**, u32**, SpeRun**, u32*) { return false; }
const SpeRun* ps3spevb_runs() { return 0; }
const volatile SpeVbEcho* ps3spevb_wait_final() { return 0; }
void ps3spevb_check_result(bool) {}

#include "region.inc"

std::vector<DcVertex> g_v;
std::vector<DcStrip> g_s;
std::vector<DcParam> g_p;
std::vector<ModTri> g_mt;
std::vector<ModVolParam> g_mv;
}

uint32_t SIDE(run)(const R4Scene* s, const R4Call** calls, const uint32_t** vb, uint32_t* vb_used)
{
	// One spare element each: an empty vector has no data pointer.
	g_v.resize(s->nvtx + 1);
	memcpy(g_v.data(), s->vtx, (size_t)s->nvtx * 24);
	g_s.resize(s->nstrip + 1);
	for (u32 i = 0; i < s->nstrip; i++)
	{
		g_s[i].first = s->strip[4 * i];
		g_s[i].count = (u16)s->strip[4 * i + 1];
		g_s[i].list = (u8)s->strip[4 * i + 2];
		g_s[i].param = (u16)s->strip[4 * i + 3];
	}
	g_p.resize(s->nparam + 1);
	memcpy(g_p.data(), s->param, (size_t)s->nparam * 16);
	g_mt.resize(s->nmtri + 1);
	if (s->nmtri)
		memcpy(g_mt.data(), s->mtri, (size_t)s->nmtri * 36);
	g_mv.resize(s->nmvol + 1);
	for (u32 i = 0; i < s->nmvol; i++)
	{
		g_mv[i].first = s->mvol[5 * i];
		g_mv[i].cull = (u8)s->mvol[5 * i + 1];
		g_mv[i].depth = (u8)s->mvol[5 * i + 2];
		g_mv[i].volume_last = (u8)s->mvol[5 * i + 3];
#ifndef R4_REF
		g_mv[i].list = (u8)s->mvol[5 * i + 4];
#endif
	}
	memset(&s_rl, 0, sizeof(s_rl));
	s_rl.vtx = g_v.data();
	s_rl.strip = g_s.data();
	s_rl.param = g_p.data();
	s_rl.mtri = g_mt.data();
	s_rl.mvol = g_mv.data();
	s_rl.nvtx = s->nvtx;
	s_rl.nstrip = s->nstrip;
	s_rl.nparam = s->nparam;
	s_rl.nmtri = s->nmtri;
	s_rl.nmvol = s->nmvol;
	s_rl.min_w = 1.0f;
	s_rl.max_w = 100.0f;
	memcpy(&s_rl.max_inv_w, &s->max_inv_w, 4);

	memset(&g_rr, 0, sizeof(g_rr));
	g_rr.isp_backgnd_d = s->isp_backgnd_d;
	g_rr.pt_alpha_ref = s->pt_alpha_ref;
	g_rr.fpu_shad_scale = s->fpu_shad_scale;
	g_rr.fb_scale_x = g_rr.fb_scale_y = 1.0f;
	g_rr.autosort = s->pass_autosort[0] != 0;
	memcpy(&g_rr.bg_param, s->bg_param, 16);
	for (int k = 0; k < 3; k++)
	{
		g_rr.bg_vtx[k].x = s->bg_vtx[k][0];
		g_rr.bg_vtx[k].y = s->bg_vtx[k][1];
		g_rr.bg_vtx[k].u = s->bg_vtx[k][2];
		g_rr.bg_vtx[k].v = s->bg_vtx[k][3];
		g_rr.bg_vtx[k].col = s->bg_col[k];
	}
	g_merge = (int)s->merge;
	g_varrays = (int)s->vertex_arrays;
	s_zfloat = s->depth_float != 0;
#ifndef R4_REF
	// What rth_job_decode, StartRender and draw_frame latch before draw_scene.
	for (int k = 0; k < R4_PASSES; k++)
		g_rr.pass_autosort[k] = s->pass_autosort[k] != 0;
	g_rr.mv_shared = s->mv_shared != 0;
	g_rr.glob_clip_x = s->glob_clip_x;
	g_rr.glob_clip_y = s->glob_clip_y;
	s_rl.opt = s->opt;
	s_rl.npass = s->npass;
	s_rl.pass_strip = s->pass_strip;
	s_rl.pass_mvol = s->pass_mvol;
	s_do.pt_depth = s->pt_depth != 0;
	s_do.tr_nodepth = s->tr_nodepth != 0;
	s_do.pt_blend = s->pt_blend != 0;
	s_do.flat = (u8)s->flat;
	s_do.passes = (u8)s->passes;
	s_do.tri_sort = (u8)s->tri_sort;
#ifndef R4_OLD
	s_do.first_pass = (u8)s->first_pass;
#endif
	s_mo_sprite = s->sprite_cull != 0;
	const bool rects = (s_rl.opt & TAOPT_TILE_CLIP) != 0;
	s_do.clip = rects ? (u8)s->clip_level : 0;
	s_do.tr_vol = (s_rl.opt & TAOPT_TR_VOL) != 0;
	s_key_pcw = rects ? 0x303CE : 0x300CE;
	s_key_isp = rects ? 0xF40FFFFF : 0xF4000000;
	clip_view(s->vx, s->vy, s->vw, s->vh, s->scissor != 0);
#endif

	// The state draw_frame and rtt_render leave: reset_gl_state, the clear, the viewport and its scissor.
	g_calls.clear();
	g_tri_x.clear();
	g_vbmem.clear();
	g_vb_alloc = g_vb_start = 0;
	memset(&g_st, 0, sizeof(g_st));
	g_st.color_mask = 1;
	g_st.depth_mask = 1;
	g_st.stencil_mask = 0xFF;
	g_st.shade = GL_SMOOTH;
	g_st.scissor = s->scissor;
	g_st.sc[0] = (uint32_t)s->vx; g_st.sc[1] = (uint32_t)s->vy; g_st.sc[2] = (uint32_t)s->vw; g_st.sc[3] = (uint32_t)s->vh;
	draw_scene(s->cx0, s->cy0, s->cw, s->ch);
	*calls = g_calls.data();
	*vb = g_vbmem.data();
	*vb_used = g_vb_alloc;
	return (uint32_t)g_calls.size();
}

#if !defined(R4_REF) && !defined(R4_OLD)
uint32_t r4_new_tris(const float** x)
{
	*x = g_tri_x.data();
	return (uint32_t)g_tri_x.size();
}
#endif
