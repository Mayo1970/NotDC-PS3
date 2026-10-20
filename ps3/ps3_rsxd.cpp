// RSX Direct (G17): spu/rsxd_emit.h on the PPU, its CHECK against ps3gl, the boot self-test against librsx, and the
// SPE Tex Hash SPU's jobs (spu/rsxd_job.h). Built with -mminimal-toc (Makefile): its statics take one TOC entry.
#include "ps3_rsxd.h"
#include "spu/rsxd_emit.h"
#include "rsxd_dec.h"
#include "ps3_presets.h"
#include "ps3_tex.h"
#include "ps3_spetex.h"
#include "ps3_video.h"
#include "ps3_timebase.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <malloc.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
extern "C" {
#include "ps3gl.h"
extern uint32_t ps3gl_frame_flush_count, ps3gl_frame_flush_vertex_count, ps3gl_frame_tex_binds;
extern double ps3gl_frame_fence_wait_time;
s32 rsxContextCallback(gcmContextData* context, u32 count);
uint32_t* ps3gl_tex_direct_begin(GLsizei width, GLsizei height, uint32_t* pitch);
void ps3gl_tex_direct_end(void);
uint8_t* ps3gl_tex_direct_level0(GLsizei width, GLsizei height, uint32_t* pitch, int* swizzled);
void ps3gl_tex_convert_check(void* dst, GLsizei width, GLsizei height, const void* rgba);
int ps3gl_tex_chain_levels(void);
uint8_t* ps3gl_tex_direct_level(int level);
void ps3gl_tex_direct_level_end(int level);
}
#pragma GCC diagnostic pop
#include "rsxd_tex.h"

#ifndef RSXD_SYNC
#define RSXD_SYNC() __asm__ volatile("sync" ::: "memory")
#endif
#ifndef RSXD_LWSYNC
#define RSXD_LWSYNC() __asm__ volatile("lwsync" ::: "memory")
#endif
// db16cyc x4: the SH4 thread on the same core takes these dispatch cycles.
#ifndef RSXD_YIELD
#define RSXD_YIELD() __asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31")
#endif
#ifndef RSXD_MEMALIGN
#define RSXD_MEMALIGN(a, n) memalign(a, n)
#endif

#define SCRATCH_WORDS 8192
#define CHECK_LINES   24
#ifndef TEST_CASES
#define TEST_CASES    3000
#endif

// The SPU's jobs: in the spare host block, a command ring of RING_BYTES and a TR vertex block of TRVB_BYTES for each
// vring segment (labels 64 and 66 fence them).
#define RING_BYTES (1u << 20)
#define TRVB_BYTES (2u << 20)
#define JOB_OPS    4096
#define JOB_TEX    2048
#define JOB_MIN    4096          // ring words a job needs before it starts
#define SPU_WORDS  (64u * 1024)  // CHECK: the SPU's words, never run
#define PMAP_N     8192          // param slots (MAX_PARAMS)
// Internal section modes past RSXD_SEC_CHECK: the SPU writes the words, or (SPE CHECK) a copy beside the PPU's.
enum { SEC_SPU = 3, SEC_SPU_CHECK = 4 };
// A game frame (plan 9, stage A): ps3gl's; the emitter's (FRAME); ps3gl's, the emitter beside it (FRAME CHECK).
enum { FR_OFF, FR_LIVE, FR_CHECK };
// Stage E: the frame's textures are ps3gl's objects, the table's (+ TEX), or both (TEX CHECK: ps3gl draws with its
// own; the shadow of each slot takes the same uploads and is compared).
enum { TM_GL, TM_OWN, TM_PAIR };
#define T_NAME  0x40000000u                 // the table's GL names: T_NAME | entry (ps3gl's count up from 1)
#define T_OBJ   0x10000u                    // RsxdTex.obj of an entry: T_OBJ | entry, past ps3gl's slots
#define T_WRAP  (RT_MAX - 1)                // + TEX: the entry over memory a ps3gl texture owns (rglTexWrap)
#define T_PROXY (PS3GL_MAX_TEXTURES - 2)    // + TEX, a lent frame: ps3gl's slots that carry an entry, units 0 and 1
#define T_PNAME 0x7FFFFFF0u                 // their GL names, which no glGenTextures gives
// Stage F: the emitter's vertex ring, two segments as ps3gl's, each fenced by a label of its own (ps3gl: 64, 66).
#define RING_SEG    (16u << 20)
#define RING_LABEL0 68
#define RING_LABEL1 70
#define RX_FP_ADDR  0x08e4u                 // NV40TCL_FP_ADDRESS: (location + 1) | the FP's offset

// The frame's GL state outside RsxdGl, as ps3gl holds it (rs.vp_* with its casts, depth range, polygon offset, clear
// values, the surface, the matrices, the immediate vertex), and what the RSX holds of it (the *k flags: known).
struct Rg
{
	int vp_x, vp_y, vp_w, vp_h;
	float zn, zf;
	int poly_on;
	float poly_factor, poly_units;
	u32 clear_color, clear_stencil;
	float clear_depth;
	u32 sw, sh;
	u32 mmode, tmu;
	float proj[16], mv[16];
	bool mvp_new;
	float fog_scale;
	// ps3gl.imm: the colour (RGBA) and the texcoords as bits; the primitive in progress and its vertex count (in vbuf).
	u32 color, u0, v0, u1, v1;
	u32 prim, n;
	// Sent: the viewport (rsxSetViewport's values and the clip), polygon offset, clear values, texture unit 1.
	bool vk, pk, cck, zk, u1k, u1_stale;
	u32 vw[14], pw[3], cc, zs, u1w[8];
	// FOG (stage C): the texture object on the emitter's unit 1, or none.
	u32 u1_obj;
};

static struct
{
	RsxdProg prog __attribute__((aligned(128)));
	bool ok;
	u32 test_bad;
	int frame_mode, mode;
	u32 sec_mode;
	RsxdScene sc;
	RsxdSec sec;
	RsxdState st;
	RsxdRend rr;
	gcmContextData* ctx;
	// CHECK: ps3gl's stream (the FIFO from the frame start) and the emitter's; the FIFO's decode position.
	RdRegs* P;
	RdRegs* E;
	const u32* ppos;
	RdCall pcs;
	bool p_ok;
	u32* scratch;
	// CHECK: the texture whose cache clear the emitter sent before ps3gl's draw, which clears the flag itself.
	u32 virt_clear;
	u32 last_param[4], last_list, lines;
	u32 n_check, n_bad, n_lost, n_glbad, n_alpha;
	// Per stats window.
	u32 n_scene, n_sec, n_draw, n_vtx, n_tex, n_inval, n_drop;
	u64 n_words;

	// SPU jobs: the job, its answer, the ops and textures of a section, the TR SORT strips and params.
	bool spu_init, spu_dead;
	RsxdJob* job;
	RsxdDone* done;
	RsxdOp* ops;
	RsxdTex* texs;
	u32 nops, ntex, seq;
	RsxdTriStrip* tstrips;
	RsxdTriParam* tparams;
	u32 ntstrips, ntparams, ntvtx;
	u16* pmap;
	u16* pgen;
	u16 pgen_cur;
	u8* host;
	u32 host_io;
	u32 ring_half, ring_pos, trvb_pos;  // the frame's half; words and vertices used in it
	u32* spu_words;
	RsxdGl job_gl;  // the GL state at the job's start
	// SEC_SPU: what the recorded params leave for draw_vb's tests (a texture on unit 0, the clip).
	u32 r_tmu0, r_obj, r_clip_mode;
	int32_t r_clip_r[4];
	bool replay;
	u32 replay_inval;
	// SPE CHECK: the section's first FIFO word; ops past JOB_OPS are not compared.
	const u32* sec_fifo;
	bool spuc_lost;
	// TR SORT.
	RsxdTrisIn tin;
	bool tris_on, tris_big, tris_check;
	const u32* tris_fifo;
	u64 t_tris0;
	// Per stats window: jobs and their words, draws, PPU replays, late SPU, waits; TR SORT jobs, PPU sorts, too big;
	// SPE / TR SORT CHECK draws, mismatches, compares lost.
	u32 n_jobs, n_jwords, n_jdraw, n_fall, n_late, n_tjobs, n_tppu, n_tbig;
	u32 n_scheck, n_sbad, n_slost;
	u64 t_wait;

	// Plan 9: the row's value; the frame's FR_*, the emitter has the RSX now, FRAME's self-test passed, the scene's rules
	// came this frame (the background asks first); its volumes (B), unit 1 (C) and quads (D) are the emitter's.
	int row;
	u32 fr;
	bool fr_own, fr_ok, scene_set, vols, fog, screen;
	Rg rg;
	// rgl's vertices of a glBegin (9 words each, ps3gl_vertex_t), up to ps3gl's PS3GL_MAX_VERTS.
	u32* vbuf;
	// Per stats window: frames, lends, takes, clears, volume sections, fog tables on the emitter's unit 1, quads out of
	// a section; FRAME CHECK: clears compared, frame state mismatches at a lend.
	u32 n_frames, n_lends, n_takes, n_clears, n_vols, n_fog, n_quads, n_fclear, n_fgl;
	// Stage E: texture mode (TM_*); bound entry + 1 (0 none), ps3gl's bound; proxy slots' entry + 1; proxies free.
	// Per stats window: proxy loads, mode changes; TEX CHECK binds and levels compared, mismatches, unshadowed textures.
	u32 tm, tcur, pent[2];
	bool tgl, proxy_ok;
	u32 n_tproxy, n_tswitch, n_tbind, n_tlevel, n_tbad, n_tmiss;

	// Stage F: the programs this frame's words select (prog, or prog_own with the copies' FP addresses), the copies.
	RsxdProg prog_own __attribute__((aligned(128)));
	const RsxdProg* pg;
	u8* fpm;
	u32 fp_own[RSXD_KEYS], fp_gl[RSXD_KEYS];
	// The ring: memory, segment in use, its head, the labels' last values; the ring is this frame's (between its
	// begin and the label); the row the own frame began with; RING has not given ps3gl the RSX yet.
	u8* rb;
	u32 rb_off, rseg, rhead, rval[2], vb_row;
	volatile u32* rlabel[2];
	bool ring_ok, rused, ring_mode, vb_set, gl_off;
	// Per stats window: the ring's peak, failed blocks, label wait, timeouts; frames, handoffs to ps3gl, draws of the
	// RING CHECK compare with a copy's FP.
	u32 rpeak, n_rdrop, n_rtimeout, n_rframes, n_handoff, n_fpown;
	u64 t_rwait;
} S;

static inline gcmContextData* rctx() { return S.ctx; }

// ---- the includer's part of rsxd_emit.h ----

static int rsxd_reserve(RsxdState* s, uint32_t n)
{
	if (s->check)
		return s->oend - s->op >= (int)n;
	gcmContextData* c = rctx();
	c->current = s->op;
	if (c->current + n > c->end && rsxContextCallback(c, n) != 0)
	{
		s->op = c->current;
		s->oend = c->end;
		S.n_drop++;
		return 0;
	}
	s->op = c->current;
	s->oend = c->end;
	return 1;
}

// A texture object of the emitter: ps3gl's slot, or (T_OBJ) the table's entry. Its texels the texture cache lacks:
static inline bool tobj_mine(u32 obj)
{
	return (obj >> 16) == (T_OBJ >> 16) && (obj & 0xFFFFu) < RT_MAX;
}

static inline bool tobj_dirty(u32 obj)
{
	if (tobj_mine(obj))
		return RT.t[obj & 0xFFFFu].dirty != 0;
	return obj < PS3GL_MAX_TEXTURES && ps3gl.textures[obj].content_dirty;
}

static inline void tobj_clean(u32 obj)
{
	if (tobj_mine(obj))
		RT.t[obj & 0xFFFFu].dirty = 0;
	else if (obj < PS3GL_MAX_TEXTURES)
		ps3gl.textures[obj].content_dirty = 0;
}

static uint32_t rsxd_tex_dirty(RsxdState* s, uint32_t obj, int clear)
{
	(void)s;
	// A replay of recorded ops: the draw's own clear, decided (and the flag cleared) at the recording.
	if (S.replay)
	{
		const u32 v = S.replay_inval;
		S.replay_inval = 0;
		return v;
	}
	if (!tobj_dirty(obj))
		return 0;
	if (!clear)
	{
		if (obj == S.virt_clear)
			return 0;
		S.virt_clear = obj;
		return 1;
	}
	// ps3gl's order: the texels' stores before the clear the RSX reads.
	RSXD_SYNC();
	tobj_clean(obj);
	return 1;
}

// ---- ps3gl's state <-> the emitter's ----

static inline u32 fbits(float f)
{
	u32 u;
	memcpy(&u, &f, 4);
	return u;
}

static void tex_obj(const ps3gl_texture_t* t, RsxdTex* o)
{
	const gcmTexture& g = t->gcm_tex;
	o->offset = g.offset;
	o->format = (g.location + 1) | (g.cubemap << 2) | (g.dimension << 4) | (g.format << 8) | (g.mipmap << 16) | (1u << 3) |
	            0x8000u;
	o->remap = g.remap;
	o->size0 = ((u32)g.width << 16) | g.height;
	o->size1 = ((u32)g.depth << 20) | g.pitch;
	const u16 maxlod = (u16)(((t->num_levels ? t->num_levels : 1) - 1) << 8);
	o->ctrl = (1u << 31) | ((u32)maxlod << 7);
	o->filter = o->wrap = 0;
	o->obj = (u32)(t - ps3gl.textures);
	o->valid = t->data != 0;
	o->pad[0] = o->pad[1] = 0;
}

static inline bool t_name(u32 name)
{
	return (name & T_NAME) && (name & ~T_NAME) < T_WRAP;
}

// TEX CHECK: the shadow of ps3gl's texture t against the words tex_obj gave (o); only the memory and offset differ.
static void pair_words(const ps3gl_texture_t* t, const RsxdTex* o)
{
	const u32 i = (u32)(t - ps3gl.textures);
	const RtTex* const s = i < T_WRAP ? &RT.t[i] : 0;
	if (!s || !s->used || s->name != (u32)t->glname)
	{
		S.n_tmiss++;
		return;
	}
	RsxdTex x;
	rt_words(s, T_OBJ | i, 0, &x);
	S.n_tbind++;
	const u32 a[] = { o->format, o->remap, o->size0, o->size1, o->ctrl, o->valid };
	const u32 b[] = { x.format, x.remap, x.size0, x.size1, x.ctrl, x.valid };
	for (u32 k = 0; k < 6; k++)
		if (a[k] != b[k])
		{
			S.n_tbad++;
			if (S.lines++ < CHECK_LINES)
				printf("[rsxd] tex MISMATCH word %u ps3gl %08X table %08X | slot %u name %u\n", k, a[k], b[k], i,
				       (u32)t->glname);
			return;
		}
}

void ps3rsxd_tex_name(u32 name, RsxdTex* o)
{
	if (t_name(name))
	{
		const u32 i = name & ~T_NAME;
		rt_words(&RT.t[i], T_OBJ | i, 0, o);
		return;
	}
	const ps3gl_texture_t* t = ps3gl_texture_find(name);
	if (!t)
	{
		// As glBindTexture would draw: a new empty slot, or none when the pool is full.
		glBindTexture(GL_TEXTURE_2D, name);
		t = ps3gl.tmu[ps3gl.active_tmu].bound;
	}
	if (!t)
	{
		memset(o, 0, sizeof(*o));
		o->obj = 0xFFFFFFFFu;
		return;
	}
	tex_obj(t, o);
	if (S.tm == TM_PAIR)
		pair_words(t, o);
}

// A bound texture with the sampler words its own fields give (apply_textures' rsxTextureControl/Filter/WrapMode).
static void tex_from_fields(const ps3gl_texture_t* t, RsxdTex* o)
{
	tex_obj(t, o);
	o->ctrl |= (u32)t->max_aniso << 4;
	o->filter = ((u32)t->mag_filter << 24) | ((u32)t->min_filter << 16) | (1u << 13);
	o->wrap = t->wrap_s | ((u32)t->wrap_t << 8) | (3u << 16);
}

// obj's words with its own sampler fields; false: it has no texels.
static bool tobj_fields(u32 obj, RsxdTex* o)
{
	if (tobj_mine(obj))
	{
		rt_words(&RT.t[obj & 0xFFFFu], obj, 1, o);
		return o->valid != 0;
	}
	if (obj >= PS3GL_MAX_TEXTURES || !ps3gl.textures[obj].data)
		return false;
	tex_from_fields(&ps3gl.textures[obj], o);
	return true;
}

// The entry proxy slot u carried gets back the cache clear ps3gl did not take (a new upload since set its own).
static void t_back(u32 u)
{
	if (!S.proxy_ok)
		return;
	const u32 e = S.pent[u];
	S.pent[u] = 0;
	ps3gl_texture_t& p = ps3gl.textures[T_PROXY + u];
	if (e && RT.t[e - 1].used && RT.t[e - 1].data == p.data && p.content_dirty)
		RT.t[e - 1].dirty = 1;
	p.content_dirty = 0;
}

// + TEX, ps3gl drawing (a lent frame): ps3gl's proxy slot of unit u carries entry t, as build_gcm_texture would. Its
// pending texture cache clear moves along, so ps3gl clears the cache as often as for its own textures.
static ps3gl_texture_t* t_carry(u32 u, RtTex* t)
{
	t_back(u);
	ps3gl_texture_t* const p = &ps3gl.textures[T_PROXY + u];
	p->data = t->data;
	p->offset = t->offset;
	p->width = t->w;
	p->height = t->h;
	p->bpp = 4;
	p->num_levels = t->levels;
	p->swizzled = t->swz;
	p->alloc_levels = t->alloc;
	p->wrap_s = t->wrap_s;
	p->wrap_t = t->wrap_t;
	p->min_filter = t->min;
	p->mag_filter = t->mag;
	p->max_aniso = t->aniso;
	p->fixed_pitch = t->fixed_pitch;
	gcmTexture& g = p->gcm_tex;
	memset(&g, 0, sizeof(g));
	g.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | (t->swz ? 0 : GCM_TEXTURE_FORMAT_LIN);
	g.mipmap = t->levels ? t->levels : 1;
	g.dimension = GCM_TEXTURE_DIMS_2D;
	g.cubemap = GCM_FALSE;
	g.remap = PS3GL_TEX_REMAP_IDENTITY;
	g.width = t->w;
	g.height = t->h;
	g.depth = 1;
	g.location = GCM_LOCATION_RSX;
	g.pitch = t->fixed_pitch ? t->fixed_pitch : rt_pitch(t->w);
	g.offset = t->offset;
	p->dirty = 1;
	p->content_dirty = t->dirty;
	t->dirty = 0;
	S.pent[u] = (u32)(t - RT.t) + 1;
	return p;
}

static void gl_from_ps3gl(RsxdGl* g)
{
	const ps3gl_state_t& q = ps3gl;
	memset(g, 0, sizeof(*g));
	g->blend_en = (u32)q.rs.blend_enable;
	g->blend_src = q.rs.blend_src;
	g->blend_dst = q.rs.blend_dst;
	g->alpha_en = (u32)q.rs.alpha_test_enable;
	g->alpha_func = q.rs.alpha_func;
	g->alpha_ref = q.rs.alpha_ref;
	g->depth_test = (u32)q.rs.depth_test_enable;
	g->depth_func = q.rs.depth_func;
	g->depth_mask = (u32)q.rs.depth_mask;
	g->cull_en = (u32)q.rs.cull_enable;
	g->cull_face = q.rs.cull_face;
	g->front_face = q.rs.front_face;
	g->scissor_en = (u32)q.rs.scissor_enable;
	g->sc_x = q.rs.scissor_x;
	g->sc_y = q.rs.scissor_y;
	g->sc_w = q.rs.scissor_w;
	g->sc_h = q.rs.scissor_h;
	g->color_mask = (q.rs.color_mask_r ? 1u : 0) | (q.rs.color_mask_g ? 2u : 0) | (q.rs.color_mask_b ? 4u : 0) |
	                (q.rs.color_mask_a ? 8u : 0);
	g->shade = q.rs.shade_model;
	g->st_en = (u32)q.rs.stencil_enable;
	g->st_func = q.rs.stencil_func;
	g->st_ref = q.rs.stencil_ref;
	g->st_mask = q.rs.stencil_mask;
	g->st_fail = q.rs.stencil_fail;
	g->st_zfail = q.rs.stencil_zfail;
	g->st_zpass = q.rs.stencil_zpass;
	g->st_wmask = q.rs.stencil_writemask;
	const ps3gl_tmu_t& t0 = q.tmu[0];
	g->tex_en = (u32)t0.enabled;
	g->texenv = (u32)t0.texenv;
	g->bump = (u32)t0.bump;
	if (t0.bound)
		tex_from_fields(t0.bound, &g->tex);
	else
		g->tex.obj = 0xFFFFFFFFu;
	g->has_tex = t0.bound && t0.bound->data;
	g->fog = (u32)q.dc_fog;
	g->offs = (u32)q.dc_offs;
	g->xt = (u32)q.dc_xt;
	memcpy(g->xt_v, q.dc_xt_v, sizeof(g->xt_v));
}

static void gl_to_ps3gl(const RsxdGl* g)
{
	ps3gl_state_t& q = ps3gl;
	q.rs.blend_enable = (int)g->blend_en;
	q.rs.blend_src = (uint16_t)g->blend_src;
	q.rs.blend_dst = (uint16_t)g->blend_dst;
	q.rs.alpha_test_enable = (int)g->alpha_en;
	q.rs.depth_test_enable = (int)g->depth_test;
	q.rs.depth_func = g->depth_func;
	q.rs.depth_mask = (int)g->depth_mask;
	q.rs.cull_enable = (int)g->cull_en;
	q.rs.cull_face = g->cull_face;
	q.rs.scissor_enable = (int)g->scissor_en;
	q.rs.scissor_x = (int16_t)g->sc_x;
	q.rs.scissor_y = (int16_t)g->sc_y;
	q.rs.scissor_w = (uint16_t)g->sc_w;
	q.rs.scissor_h = (uint16_t)g->sc_h;
	q.rs.shade_model = g->shade;
	q.rs.stencil_func = g->st_func;
	q.rs.stencil_ref = g->st_ref;
	q.rs.stencil_mask = g->st_mask;
	ps3gl_tmu_t& t0 = q.tmu[0];
	t0.enabled = (int)g->tex_en;
	t0.texenv = (int)g->texenv;
	t0.bump = (int)g->bump;
	if (g->tex.obj < PS3GL_MAX_TEXTURES)
		t0.bound = &q.textures[g->tex.obj];
	else if (tobj_mine(g->tex.obj) && S.proxy_ok)
		t0.bound = t_carry(0, &RT.t[g->tex.obj & 0xFFFFu]);
	q.dc_fog = (int)g->fog;
	q.dc_offs = (int)g->offs;
	q.dc_xt = (int)g->xt;
	memcpy(q.dc_xt_v, g->xt_v, sizeof(q.dc_xt_v));
	// What ps3gl believes the RSX holds is not known now: its next draw sends all, as after ps3gl_begin_frame.
	q.dirty = PS3GL_DIRTY_ALL;
	q.active_shader = -1;
	q.active_vp = NULL;
	q.vattr_valid = 0;
	for (int i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		q.tmu[i].dirty = 1;
		q.tmu[i].sent = 0;
	}
}

// CHECK: the emitter's GL state against ps3gl's after the same calls, and (rules) ps3_rend.cpp's rule state. 0 =
// equal; else the field's offset << 8 | word + 1, with the emitter's and ps3gl's values.
static u32 gl_diff(const RsxdGl* e, u32* ev, u32* gv, bool rules = true)
{
	RsxdGl g;
	gl_from_ps3gl(&g);
	// FRAME: ps3tex and Native Size bind and delete on ps3gl's unit 0 outside rgl; with the unit off, no word uses it.
	// No object (a TR SORT job that drew nothing): gl_to_ps3gl keeps ps3gl's binding, which the uploads moved.
	if ((S.fr_own && !e->tex_en && !g.tex_en) || e->tex.obj >= PS3GL_MAX_TEXTURES)
	{
		g.has_tex = e->has_tex;
		g.tex = e->tex;
	}
	if (rules)
	{
		g.clip_base = *S.rr.clip_base;
		g.clip_mode = *S.rr.clip_mode;
		memcpy(g.clip_r, S.rr.clip_r, sizeof(g.clip_r));
		g.flat_shift = *S.rr.flat_shift;
	}
	static const struct { u16 off; u8 n; } k_f[] = {
		{ offsetof(RsxdGl, blend_en), 3 }, { offsetof(RsxdGl, alpha_en), 3 }, { offsetof(RsxdGl, depth_test), 3 },
		{ offsetof(RsxdGl, cull_en), 3 }, { offsetof(RsxdGl, scissor_en), 5 }, { offsetof(RsxdGl, color_mask), 2 },
		{ offsetof(RsxdGl, st_en), 8 }, { offsetof(RsxdGl, tex_en), 4 }, { offsetof(RsxdGl, fog), 12 },
		{ offsetof(RsxdGl, clip_base), 7 },
	};
	// The rule state is the last entry.
	const u32 nf = sizeof(k_f) / sizeof(k_f[0]) - (rules ? 0 : 1);
	for (u32 i = 0; i < nf; i++)
	{
		const u32* a = (const u32*)((const u8*)e + k_f[i].off);
		const u32* b = (const u32*)((const u8*)&g + k_f[i].off);
		for (u32 k = 0; k < k_f[i].n; k++)
			if (a[k] != b[k])
			{
				*ev = a[k];
				*gv = b[k];
				return (k_f[i].off << 8) | (k + 1);
			}
	}
	// Unit 0's texture: the bound slot when a texture is on.
	*ev = e->tex.obj;
	*gv = g.tex.obj;
	if (g.has_tex && e->tex.obj != g.tex.obj)
		return 0xFFFF00u;
	return 0;
}

// ---- the programs: ps3gl_shader_key, the FP selects and VP loads of librsx ----

static gcmContextData s_cap;
static u32 s_capbuf[4096] __attribute__((aligned(128)));

static void cap_begin()
{
	s_cap.begin = s_capbuf;
	s_cap.current = s_capbuf;
	s_cap.end = s_capbuf + 4096 - 4;
	s_cap.callback = 0;
}

static u32 cap_words()
{
	return (u32)(s_cap.current - s_cap.begin);
}

static bool capture()
{
	RsxdProg& pg = S.prog;
	ps3gl_state_t& q = ps3gl;
	memset(&pg, 0, sizeof(pg));
	// ps3gl_shader_key over each input it reads, on two free texture slots that stand in for bound textures.
	const ps3gl_tmu_t t0 = q.tmu[0], t1 = q.tmu[1];
	const int fog = q.dc_fog, offs = q.dc_offs, xt = q.dc_xt;
	ps3gl_texture_t* const fake = &q.textures[PS3GL_MAX_TEXTURES - 1];
	const ps3gl_texture_t save = *fake;
	fake->data = (uint8_t*)fake;
	for (u32 i = 0; i < sizeof(pg.key_lut); i++)
	{
		// Only the texenvs and fog modes the emitter sets: ps3gl's tables end there.
		if (((i >> 1) & 7) > RXE_DECAL || ((i >> 5) & 7) > RXF_BASE_TA)
		{
			pg.key_lut[i] = PS3GL_TENV_DISABLED;
			continue;
		}
		q.tmu[0].enabled = i & 1;
		q.tmu[0].bound = (i & 1) ? fake : NULL;
		q.tmu[0].texenv = (i >> 1) & 7;
		q.tmu[0].bump = (i >> 4) & 1;
		q.dc_fog = (i >> 5) & 7;
		q.dc_offs = (i >> 8) & 1;
		q.dc_xt = (i >> 9) & 1;
		q.tmu[1].enabled = (i >> 10) & 1;
		q.tmu[1].aux = 0;
		q.tmu[1].bound = ((i >> 10) & 1) ? fake : NULL;
		int k = ps3gl_shader_key();
		if (k < 0 || k >= PS3GL_TENV_COUNT)
			k = PS3GL_TENV_DISABLED;
		pg.key_lut[i] = (u8)k;
	}
	*fake = save;
	q.tmu[0] = t0;
	q.tmu[1] = t1;
	q.dc_fog = fog;
	q.dc_offs = offs;
	q.dc_xt = xt;
	if (PS3GL_TENV_COUNT != RSXD_KEYS)
		return false;
	// Each key's FP select and VP, as ps3gl's apply_shader sends them.
	const rsxVertexProgram* vps[RSXD_VPS] = { 0, 0, 0, 0 };
	u32 nvp = 0, at = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		const ps3gl_shader_t& sh = q.shaders[k];
		cap_begin();
		if (sh.fp_ucode)
			rsxLoadFragmentProgramLocation(&s_cap, sh.fp, sh.fp_offset, GCM_LOCATION_RSX);
		const u32 n = cap_words();
		if (at + n > RSXD_FP_POOL)
			return false;
		memcpy(&pg.fp_words[at], s_capbuf, n * 4);
		pg.fp_at[k] = (u16)at;
		pg.fp_n[k] = (u16)n;
		at += n;
		u32 v = 0;
		while (v < nvp && vps[v] != sh.vp)
			v++;
		if (v == nvp)
		{
			if (nvp == RSXD_VPS)
				return false;
			vps[nvp++] = sh.vp;
			cap_begin();
			rsxLoadVertexProgram(&s_cap, sh.vp, sh.vp_ucode);
			const u32 w = cap_words();
			if (w > RSXD_VP_WORDS)
				return false;
			memcpy(pg.vp_words[v], s_capbuf, w * 4);
			pg.vp_n[v] = w;
			// Constant slots: index + const_start; mvp as count rows, the others as one FLOAT4 slot.
			const u32 base = sh.vp->const_start;
			pg.vp_mvp[v] = sh.mvp_const ? (u16)(sh.mvp_const->index + base) : RSXD_NONE;
			pg.vp_mvp_n[v] = sh.mvp_const ? sh.mvp_const->count : 0;
			pg.vp_fog[v] = sh.fog_const ? (u16)(sh.fog_const->index + base) : RSXD_NONE;
			pg.vp_cmin[v] = sh.cmin_const ? (u16)(sh.cmin_const->index + base) : RSXD_NONE;
			pg.vp_cmax[v] = sh.cmax_const ? (u16)(sh.cmax_const->index + base) : RSXD_NONE;
			if ((sh.mvp_const && (sh.mvp_const->type != PARAM_FLOAT4x4 || sh.mvp_const->count != 4)) ||
			    (sh.fog_const && sh.fog_const->type != PARAM_FLOAT4) ||
			    (sh.cmin_const && sh.cmin_const->type != PARAM_FLOAT4) ||
			    (sh.cmax_const && sh.cmax_const->type != PARAM_FLOAT4) || sh.clip_plane_const)
				return false;
		}
		pg.key_vp[k] = (u8)v;
	}
	// Every key of one VP must name the same constants (they come from the VP).
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		const ps3gl_shader_t& sh = q.shaders[k];
		const u32 v = pg.key_vp[k], base = sh.vp->const_start;
		if ((sh.fog_const ? sh.fog_const->index + base : RSXD_NONE) != pg.vp_fog[v] ||
		    (sh.cmin_const ? sh.cmin_const->index + base : RSXD_NONE) != pg.vp_cmin[v] ||
		    (sh.cmax_const ? sh.cmax_const->index + base : RSXD_NONE) != pg.vp_cmax[v])
			return false;
	}
	return true;
}

// ---- the frame's words (plan 9, stage A) ----

// Outside a section the emitter writes at the FIFO's current word (FRAME), or after its scratch words (FRAME CHECK).
static void fr_op_begin()
{
	if (S.fr != FR_LIVE)
		return;
	gcmContextData* c = rctx();
	S.st.op = c->current;
	S.st.oend = c->end;
}

static void fr_op_end()
{
	if (S.fr == FR_LIVE)
		rctx()->current = S.st.op;
}

// ps3gl_apply_states' viewport: the rect clamped to the surface, scale and offset from the whole rect, then the clip.
static void fr_viewport_words(u32* v)
{
	const Rg& r = S.rg;
	const int vx = r.vp_x, vy = r.vp_y, vw = r.vp_w, vh = r.vp_h;
	const float zn = r.zn, zf = r.zf;
	const float scale[4] = { vw * 0.5f, vh * -0.5f, (zf - zn) * 0.5f, 0.0f };
	const float offset[4] = { vx + vw * 0.5f, vy + vh * 0.5f, (zf + zn) * 0.5f, 0.0f };
	int cx = vx, cy = vy, cw = vw, ch = vh;
	if (cx < 0) { cw += cx; cx = 0; }
	if (cy < 0) { ch += cy; cy = 0; }
	if (cw < 1) cw = 1;
	if (ch < 1) ch = 1;
	if (cx + cw > (int)r.sw) cw = (int)r.sw - cx;
	if (cy + ch > (int)r.sh) ch = (int)r.sh - cy;
	if (cw < 1) cw = 1;
	if (ch < 1) ch = 1;
	v[0] = ((u32)(u16)cw << 16) | (u16)cx;
	v[1] = ((u32)(u16)ch << 16) | (u16)cy;
	v[2] = fbits(zn);
	v[3] = fbits(zf);
	for (int k = 0; k < 4; k++)
	{
		v[4 + k] = fbits(offset[k]);
		v[8 + k] = fbits(scale[k]);
	}
	v[12] = (u32)((u16)r.sw - 1) << 16;
	v[13] = (u32)((u16)r.sh - 1) << 16;
}

// The viewport and polygon offset words that differ from the RSX's, as rsxSetViewport, rsxSetViewportClip and
// rsxSetPolygonOffset* write them (offset and scale go twice). Needs 32 words.
static void fr_regs()
{
	Rg& r = S.rg;
	u32* o = S.st.op;
	u32 v[14];
	fr_viewport_words(v);
	if (!r.vk || memcmp(v, r.vw, sizeof(v)))
	{
		o[0] = RX_HDR(0x0a00, 2);
		o[1] = v[0];
		o[2] = v[1];
		o[3] = RX_HDR(0x0394, 2);
		o[4] = v[2];
		o[5] = v[3];
		o[6] = RX_HDR(0x0a20, 8);
		memcpy(o + 7, v + 4, 32);
		o[15] = RX_HDR(0x0a20, 8);
		memcpy(o + 16, v + 4, 32);
		o[24] = RX_HDR(0x02c0, 2);
		o[25] = v[12];
		o[26] = v[13];
		o += 27;
		memcpy(r.vw, v, sizeof(v));
		r.vk = true;
	}
	const u32 p[3] = { (u32)r.poly_on, fbits(r.poly_factor), fbits(r.poly_units) };
	if (!r.pk || p[0] != r.pw[0] || (p[0] && memcmp(p + 1, r.pw + 1, 8)))
	{
		o[0] = RX_HDR(0x0a68, 1);
		o[1] = p[0];
		o += 2;
		if (p[0])
		{
			o[0] = RX_HDR(0x0a78, 2);
			o[1] = p[1];
			o[2] = p[2];
			o += 3;
		}
		memcpy(r.pw, p, sizeof(p));
		r.pk = true;
	}
	S.st.op = o;
}

// ps3gl_apply_matrices' MVP: projection times model view, as rows.
static void fr_mvp()
{
	Rg& r = S.rg;
	float m[16];
	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 4; k++)
			m[c * 4 + k] = r.proj[0 * 4 + k] * r.mv[c * 4 + 0] + r.proj[1 * 4 + k] * r.mv[c * 4 + 1] +
			               r.proj[2 * 4 + k] * r.mv[c * 4 + 2] + r.proj[3 * 4 + k] * r.mv[c * 4 + 3];
	for (int k = 0; k < 4; k++)
		for (int c = 0; c < 4; c++)
			S.sc.mvp[k * 4 + c] = fbits(m[c * 4 + k]);
}

// ps3gl's texture unit 1 (the fog table, stage C) as ps3gl_apply_textures binds it, when the RSX holds other words;
// the cache clear of its new texels goes with the next draw, or here before an SPU section (spu). Needs 17 words.
static void fr_unit1(bool spu)
{
	RsxdState& st = S.st;
	Rg& r = S.rg;
	r.u1_stale = false;
	// FOG (stage C): the emitter's own unit 1; else ps3gl's.
	const ps3gl_tmu_t& u = ps3gl.tmu[1];
	const u32 obj = S.fog ? r.u1_obj : u.bound ? (u32)(u.bound - ps3gl.textures) : 0xFFFFFFFFu;
	RsxdTex x;
	const bool on = (S.fog || u.enabled) && tobj_fields(obj, &x);
	// offset, format, wrap, control, remap, filter, size0, size1
	u32 w[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	if (on)
	{
		const u32 v[8] = { x.offset, x.format, x.wrap, x.ctrl, x.remap, x.filter, x.size0, x.size1 };
		memcpy(w, v, sizeof(w));
	}
	u32* o = st.op;
	if (on && (!r.u1k || memcmp(w, r.u1w, sizeof(w))))
	{
		o[0] = RX_HDR(0x1a20, 2);
		o[1] = w[0];
		o[2] = w[1];
		o[3] = RX_HDR(0x1a30, 1);
		o[4] = w[4];
		o[5] = RX_HDR(0x1a38, 1);
		o[6] = w[6];
		o[7] = RX_HDR(0x1844, 1);
		o[8] = w[7];
		o[9] = RX_HDR(0x1a2c, 1);
		o[10] = w[3];
		o[11] = RX_HDR(0x1a34, 1);
		o[12] = w[5];
		o[13] = RX_HDR(0x1a28, 1);
		o[14] = w[2];
		o += 15;
		memcpy(r.u1w, w, sizeof(w));
		r.u1k = true;
	}
	else if (!on && (!r.u1k || r.u1w[3]))
	{
		o[0] = RX_HDR(0x1a2c, 1);
		o[1] = 0;
		o += 2;
		r.u1w[3] = 0;
		r.u1k = true;
	}
	if (on && tobj_dirty(obj))
	{
		// ps3gl's order: the texels' stores before the clear the RSX reads.
		RSXD_SYNC();
		if (!st.check)
			tobj_clean(obj);
		if (spu)
		{
			o[0] = RX_HDR(RXM_TEX_CACHE, 1);
			o[1] = 1;
			o += 2;
			st.n_inval++;
		}
		else
			st.inval_due = 1;
	}
	st.op = o;
}

// ps3gl_apply_states: the state the RSX lacks, with the viewport and polygon offset.
static void fr_apply()
{
	RsxdState& st = S.st;
	fr_op_begin();
	if (rsxd_reserve(&st, rsxd_draw_room(0)))
	{
		rx_want_state(&st);
		fr_regs();
		rx_emit_regs(&st);
	}
	fr_op_end();
}

// glClear: the clear values the RSX lacks, then the clear. As ps3gl's, it sends no pending state.
static void fr_clear(u32 mask)
{
	RsxdState& st = S.st;
	Rg& r = S.rg;
	fr_op_begin();
	if (!rsxd_reserve(&st, 12))
	{
		fr_op_end();
		return;
	}
	u32* o = st.op;
	u32 gm = 0;
	if (mask & GL_COLOR_BUFFER_BIT)
	{
		if (!r.cck || r.cc != r.clear_color)
		{
			o[0] = RX_HDR(0x1d90, 1);
			o[1] = r.clear_color;
			o += 2;
			r.cc = r.clear_color;
			r.cck = true;
		}
		gm |= GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A;
	}
	if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))
	{
		const u32 z24 = (u32)(r.clear_depth * 0xFFFFFF);
		const u32 ds = (z24 << 8) | (r.clear_stencil & 0xFF);
		if (!r.zk || r.zs != ds)
		{
			o[0] = RX_HDR(0x1d8c, 1);
			o[1] = ds;
			o += 2;
			r.zs = ds;
			r.zk = true;
		}
		if (mask & GL_DEPTH_BUFFER_BIT)
			gm |= GCM_CLEAR_Z;
		if (mask & GL_STENCIL_BUFFER_BIT)
			gm |= GCM_CLEAR_S;
	}
	if (gm)
	{
		o[0] = RX_HDR(0x1d94, 1);
		o[1] = gm;
		o[2] = RX_HDR(0x0100, 1);
		o[3] = 0;
		o += 4;
		S.n_clears++;
	}
	st.op = o;
	fr_op_end();
}

// Before the emitter's draws in a frame it has (spu: an SPU section's): what ps3gl's draw would also apply.
static void fr_pre_draw(bool spu)
{
	RsxdState& st = S.st;
	Rg& r = S.rg;
	if (st.oend - st.op < 64 && !rsxd_reserve(&st, 64))
		return;
	fr_regs();
	if (r.u1_stale)
		fr_unit1(spu);
	// A new MVP: the VP loads again, with its constants.
	if (r.mvp_new)
	{
		fr_mvp();
		st.vp_id = -1;
		r.mvp_new = false;
	}
}

// ---- the boot self-test ----

static u32 s_rng = 0x2545F491u;
static u32 rnd()
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 17;
	s_rng ^= s_rng << 5;
	return s_rng;
}
static u32 rndn(u32 n) { return rnd() % n; }

// The registers of a draw both sides must agree on (rx_method, the FP and VP registers).
static const u16 k_test_extra[] = { 0x08e4, 0x0b40, 0x0b44, 0x0b48, 0x0b4c, 0x0b50, 0x0b54, 0x0b58, 0x0b5c, 0x1d60,
                                    0x1ea0, 0x1ef8, 0x1ff0, 0x1ff4 };

static void test_report(u32 c, const char* what, u32 a, u32 b, u32 x)
{
	if (S.test_bad++ < 8)
		printf("[rsxd] selftest MISMATCH case %u %s %X: ps3gl %08X emitter %08X\n", c, what, x, a, b);
}

static u32 test_compare(u32 c, const RdRegs* a, const RdRegs* b)
{
	const u32 bad0 = S.test_bad;
	for (u32 i = 0; i < RX_N + sizeof(k_test_extra) / 2; i++)
	{
		const u32 m = i < RX_N ? rx_method[i] : k_test_extra[i - RX_N], k = m >> 2;
		if (a->set[k] != b->set[k] || (a->set[k] && a->reg[k] != b->reg[k]))
			test_report(c, a->set[k] != b->set[k] ? "set" : "reg", a->set[k] ? a->reg[k] : 0xDEAD, b->set[k] ? b->reg[k] : 0xDEAD, m);
	}
	for (u32 s = 0; s < RD_VPI; s++)
		if (a->vpi_set[s] != b->vpi_set[s] || memcmp(&a->vpi[s * 4], &b->vpi[s * 4], 16))
			test_report(c, "vp inst", a->vpi[s * 4], b->vpi[s * 4], s);
	for (u32 s = 0; s < RD_VPC; s++)
		if (a->vpc_set[s] != b->vpc_set[s] || memcmp(&a->vpc[s * 4], &b->vpc[s * 4], 16))
			test_report(c, "vp const", a->vpc[s * 4], b->vpc[s * 4], s);
	const u32 da[] = { a->n_draws, a->prim, a->batch_first, a->batch_count, a->n_inval, a->n_vinval, a->err };
	const u32 db[] = { b->n_draws, b->prim, b->batch_first, b->batch_count, b->n_inval, b->n_vinval, b->err };
	for (u32 i = 0; i < 7; i++)
		if (da[i] != db[i])
			test_report(c, "draw", da[i], db[i], i);
	return S.test_bad - bad0;
}

static void test_tex(ps3gl_texture_t* t, u32 dirty)
{
	const u32 lw = 3 + rndn(8), lh = 3 + rndn(8);
	t->width = (u16)(1u << lw);
	t->height = (u16)(1u << lh);
	t->num_levels = (u8)(1 + rndn(10));
	t->swizzled = rndn(2);
	t->wrap_s = (u8)(1 + rndn(3));
	t->wrap_t = (u8)(1 + rndn(3));
	t->min_filter = (u8)(1 + rndn(6));
	t->mag_filter = (u8)(1 + rndn(2));
	t->max_aniso = (u8)rndn(8);
	t->data = rndn(16) ? (uint8_t*)t : NULL;
	t->offset = rnd() & 0x0FFFFF80u;
	gcmTexture& g = t->gcm_tex;
	memset(&g, 0, sizeof(g));
	g.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | (t->swizzled ? 0 : GCM_TEXTURE_FORMAT_LIN);
	g.mipmap = t->num_levels;
	g.dimension = GCM_TEXTURE_DIMS_2D;
	g.remap = PS3GL_TEX_REMAP_IDENTITY;
	g.width = t->width;
	g.height = t->height;
	g.depth = 1;
	g.location = GCM_LOCATION_RSX;
	g.pitch = ((t->width * 4u) + 63u) & ~63u;
	g.offset = t->offset;
	t->content_dirty = (int)dirty;
	t->dirty = 1;
}

static bool selftest()
{
	ps3gl_state_t& q = ps3gl;
	RdRegs* A = (RdRegs*)malloc(sizeof(RdRegs));
	RdRegs* B = (RdRegs*)malloc(sizeof(RdRegs));
	u32* wa = (u32*)memalign(128, SCRATCH_WORDS * 4);
	u32* wb = (u32*)memalign(128, SCRATCH_WORDS * 4);
	if (!A || !B || !wa || !wb)
	{
		free(A);
		free(B);
		free(wa);
		free(wb);
		printf("[rsxd] selftest: no memory\n");
		return false;
	}
	gcmContextData* const ctx0 = q.ctx;
	const ps3gl_tmu_t t0 = q.tmu[0], t1 = q.tmu[1];
	const int fog = q.dc_fog, offs = q.dc_offs, xt = q.dc_xt, fast = q.fast, atmu = q.active_tmu;
	const uint32_t fl = ps3gl_frame_flush_count, fv = ps3gl_frame_flush_vertex_count, fb = ps3gl_frame_tex_binds;
	const float fog_scale = q.dc_fog_scale;
	float xt_v[9];
	memcpy(xt_v, q.dc_xt_v, sizeof(xt_v));
	const ps3gl_matstack_t mv = q.mv, proj = q.proj;
	const GLenum mm = q.matrix_mode;
	ps3gl_texture_t* const ft[2] = { &q.textures[PS3GL_MAX_TEXTURES - 1], &q.textures[PS3GL_MAX_TEXTURES - 2] };
	const ps3gl_texture_t fsave[2] = { *ft[0], *ft[1] };
	const decltype(q.rs) rs = q.rs;
	gcmContextData cap;
	static const u16 k_blend[] = { 0, 1, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307 };
	static const u32 k_sop[] = { GCM_KEEP, GCM_ZERO, GCM_REPLACE, GCM_INVERT };
	static const u32 k_count[] = { 1, 2, 3, 4, 255, 256, 257, 511, 512, 513, 4000, 16000 };
	u32 bad_cases = 0;
	for (u32 c = 0; c < TEST_CASES; c++)
	{
		q.rs.blend_enable = rndn(2);
		q.rs.blend_src = k_blend[rndn(10)];
		q.rs.blend_dst = k_blend[rndn(10)];
		q.rs.alpha_test_enable = rndn(2);
		q.rs.alpha_func = GCM_NEVER + rndn(8);
		q.rs.alpha_ref = rndn(256);
		q.rs.depth_test_enable = rndn(2);
		q.rs.depth_func = GCM_NEVER + rndn(8);
		q.rs.depth_mask = rndn(2);
		q.rs.cull_enable = rndn(2);
		q.rs.cull_face = rndn(2) ? GCM_CULL_FRONT : GCM_CULL_BACK;
		q.rs.front_face = rndn(2) ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW;
		q.rs.scissor_enable = rndn(2);
		q.rs.scissor_x = (int16_t)((int)rndn(1500) - 200);
		q.rs.scissor_y = (int16_t)((int)rndn(900) - 100);
		q.rs.scissor_w = (uint16_t)rndn(1400);
		q.rs.scissor_h = (uint16_t)rndn(800);
		q.rs.color_mask_r = rndn(2);
		q.rs.color_mask_g = rndn(2);
		q.rs.color_mask_b = rndn(2);
		q.rs.color_mask_a = rndn(2);
		q.rs.shade_model = rndn(2) ? GCM_SHADE_MODEL_FLAT : GCM_SHADE_MODEL_SMOOTH;
		q.rs.stencil_enable = rndn(2);
		q.rs.stencil_func = GCM_NEVER + rndn(8);
		q.rs.stencil_ref = rndn(256);
		q.rs.stencil_mask = rndn(2) ? 0xFF : rnd();
		q.rs.stencil_fail = k_sop[rndn(4)];
		q.rs.stencil_zfail = k_sop[rndn(4)];
		q.rs.stencil_zpass = k_sop[rndn(4)];
		q.rs.stencil_writemask = rndn(2) ? 0xFF : rndn(4);
		test_tex(ft[0], rndn(3) == 0);
		test_tex(ft[1], 0);
		q.active_tmu = 0;
		q.tmu[0].enabled = rndn(4) != 0;
		q.tmu[0].bound = rndn(8) ? ft[0] : NULL;
		q.tmu[0].texenv = 1 + rndn(3);
		q.tmu[0].bump = rndn(3) == 0;
		q.tmu[0].aux = 0;
		q.tmu[1].enabled = rndn(2);
		q.tmu[1].aux = rndn(2);
		q.tmu[1].bound = rndn(2) ? ft[1] : NULL;
		q.dc_fog = rndn(5);
		q.dc_offs = rndn(2);
		q.dc_xt = rndn(2);
		for (int k = 0; k < 9; k++)
			q.dc_xt_v[k] = (float)((int)rndn(2001) - 1000) / 250.0f;
		q.dc_fog_scale = (float)(1 + rndn(100000)) / 64.0f;
		if (rndn(4) == 0)
		{
			float m[16];
			for (int k = 0; k < 16; k++)
				m[k] = (float)((int)rndn(2001) - 1000) / 100.0f;
			glMatrixMode(GL_PROJECTION);
			glLoadMatrixf(m);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
		}
		q.dirty = PS3GL_DIRTY_ALL;
		q.active_shader = -1;
		q.active_vp = NULL;
		q.vattr_valid = 0;
		q.fast = rndn(2) ? PS3GL_FAST_COMPARE : 0;
		for (int i = 0; i < PS3GL_MAX_TMUS; i++)
		{
			q.tmu[i].dirty = 1;
			q.tmu[i].sent = 0;
		}
		const GLenum prim = rndn(2) ? GL_TRIANGLES : GL_TRIANGLE_STRIP;
		const u32 vb_off = rnd() & 0x0FFFFFF0u, stride = rndn(2) ? 36 : 48, first = rndn(70000);
		const u32 count = k_count[rndn(sizeof(k_count) / 4)], shift = rndn(3) ? 0 : 2;

		// The emitter first: ps3gl's draw clears the dirty flag the emitter reads.
		ps3gl_apply_matrices();
		RsxdScene sc;
		memset(&sc, 0, sizeof(sc));
		sc.screen_w = q.screen_w;
		sc.screen_h = q.screen_h;
		sc.fog_scale = fbits(q.dc_fog_scale);
		memcpy(sc.mvp, ps3gl_get_mvp(), sizeof(sc.mvp));
		const ps3gl_tmu_t& u1 = q.tmu[1];
		sc.flags = (u1.enabled && !u1.aux && u1.bound && u1.bound->data) ? RSXD_S_TMU1 : 0;
		RsxdSec sec;
		memset(&sec, 0, sizeof(sec));
		sec.vb_off = vb_off;
		RsxdState st;
		memset(&st, 0, sizeof(st));
		st.sc = &sc;
		st.sec = &sec;
		st.prog = &S.prog;
		gl_from_ps3gl(&st.gl);
		st.gl.clip_mode = RXC_NONE;
		st.gl.flat_shift = shift;
		rsxd_forget(&st);
		st.check = 1;
		st.op = wb;
		st.oend = wb + SCRATCH_WORDS;
		S.virt_clear = 0xFFFFFFFFu;
		rsxd_draw(&st, prim == GL_TRIANGLES ? RX_TRIANGLES : RX_TRI_STRIP, stride, first, count);

		cap.begin = wa;
		cap.current = wa;
		cap.end = wa + SCRATCH_WORDS - 4;
		cap.callback = 0;
		q.ctx = &cap;
		ps3gl_draw_vb_shift(prim, vb_off, stride, (int)first, (int)count, shift);
		q.ctx = ctx0;

		rd_reset(A);
		rd_reset(B);
		const u32* pa = wa;
		const u32* pb = wb;
		rd_stream(A, &pa, cap.current, 0);
		rd_stream(B, &pb, st.op, 0);
		bad_cases += test_compare(c, A, B) != 0;
	}
	*ft[0] = fsave[0];
	*ft[1] = fsave[1];
	q.rs = rs;
	q.tmu[0] = t0;
	q.tmu[1] = t1;
	q.dc_fog = fog;
	q.dc_offs = offs;
	q.dc_xt = xt;
	q.dc_fog_scale = fog_scale;
	memcpy(q.dc_xt_v, xt_v, sizeof(xt_v));
	q.fast = fast;
	q.active_tmu = atmu;
	ps3gl_frame_flush_count = fl;
	ps3gl_frame_flush_vertex_count = fv;
	ps3gl_frame_tex_binds = fb;
	q.mv = mv;
	q.proj = proj;
	q.matrix_mode = mm;
	q.mv.dirty = q.proj.dirty = 1;
	q.dirty = PS3GL_DIRTY_ALL;
	q.active_shader = -1;
	q.active_vp = NULL;
	q.vattr_valid = 0;
	for (int i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		q.tmu[i].dirty = 1;
		q.tmu[i].sent = 0;
	}
	free(A);
	free(B);
	free(wa);
	free(wb);
	printf("[rsxd] selftest: %u cases, %u bad\n", (u32)TEST_CASES, bad_cases);
	return bad_cases == 0;
}

// ps3gl's frame state outside RsxdGl.
static void rg_read(Rg* out)
{
	const ps3gl_state_t& q = ps3gl;
	Rg& r = *out;
	r.vp_x = q.rs.vp_x;
	r.vp_y = q.rs.vp_y;
	r.vp_w = q.rs.vp_w;
	r.vp_h = q.rs.vp_h;
	r.zn = q.rs.depth_near;
	r.zf = q.rs.depth_far;
	r.poly_on = q.rs.polyoffset_fill;
	r.poly_factor = q.rs.polyoffset_factor;
	r.poly_units = q.rs.polyoffset_units;
	r.clear_color = q.rs.clear_color;
	r.clear_depth = q.rs.clear_depth;
	r.clear_stencil = q.rs.clear_stencil;
	r.sw = q.screen_w;
	r.sh = q.screen_h;
	r.mmode = q.matrix_mode;
	memcpy(r.proj, q.proj.stack[q.proj.depth], sizeof(r.proj));
	memcpy(r.mv, q.mv.stack[q.mv.depth], sizeof(r.mv));
	r.mvp_new = true;
	r.fog_scale = q.dc_fog_scale;
	r.tmu = (u32)q.active_tmu;
	r.color = q.imm.color;
	memcpy(&r.u0, &q.imm.u0, 4);
	memcpy(&r.v0, &q.imm.v0, 4);
	memcpy(&r.u1, &q.imm.u1, 4);
	memcpy(&r.v1, &q.imm.v1, 4);
	r.n = 0;
	r.u1_obj = 0xFFFFFFFFu;
}

// ... into the emitter's, whose RSX words of it are not known.
static void rg_from_ps3gl()
{
	Rg& r = S.rg;
	rg_read(&r);
	r.vk = r.pk = r.cck = r.zk = r.u1k = false;
	r.u1_stale = true;
	S.sc.screen_w = r.sw;
	S.sc.screen_h = r.sh;
	S.sc.fog_scale = fbits(r.fog_scale);
}

#ifndef FRAME_CASES
#define FRAME_CASES 1000
#endif

// Plan 9, stage A: the frame's words against librsx's through ps3gl for random states: ps3gl_apply_states with every
// group dirty, glClear, and texture unit 1 in ps3gl_apply_textures.
static bool selftest_frame()
{
	ps3gl_state_t& q = ps3gl;
	RdRegs* A = (RdRegs*)malloc(sizeof(RdRegs));
	RdRegs* B = (RdRegs*)malloc(sizeof(RdRegs));
	u32* wa = (u32*)memalign(128, SCRATCH_WORDS * 4);
	u32* wb = (u32*)memalign(128, SCRATCH_WORDS * 4);
	if (!A || !B || !wa || !wb)
	{
		free(A);
		free(B);
		free(wa);
		free(wb);
		printf("[rsxd] selftest frame: no memory\n");
		return false;
	}
	gcmContextData* const ctx0 = q.ctx;
	const decltype(q.rs) rs = q.rs;
	const ps3gl_tmu_t t0 = q.tmu[0], t1 = q.tmu[1];
	const int atmu = q.active_tmu, fast = q.fast;
	const uint32_t sw0 = q.screen_w, sh0 = q.screen_h;
	ps3gl_texture_t* const ft = &q.textures[PS3GL_MAX_TEXTURES - 2];
	const ps3gl_texture_t fsave = *ft;
	const u32 fr0 = S.fr;
	gcmContextData cap;
	static const u32 k_mask[] = { GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, GL_STENCIL_BUFFER_BIT,
	                              GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
	                              GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, 0 };
	static const u32 k_sop[] = { GCM_KEEP, GCM_ZERO, GCM_REPLACE, GCM_INVERT };
	u32 bad_cases = 0;
	for (u32 c = 0; c < FRAME_CASES; c++)
	{
		q.rs.blend_enable = rndn(2);
		q.rs.blend_src = (u16)(rndn(2) ? GCM_ONE : GCM_SRC_ALPHA);
		q.rs.blend_dst = (u16)(rndn(2) ? GCM_ZERO : GCM_ONE_MINUS_SRC_ALPHA);
		q.rs.alpha_test_enable = rndn(2);
		q.rs.alpha_func = GCM_NEVER + rndn(8);
		q.rs.alpha_ref = rndn(256);
		q.rs.depth_test_enable = rndn(2);
		q.rs.depth_func = GCM_NEVER + rndn(8);
		q.rs.depth_mask = rndn(2);
		q.rs.cull_enable = rndn(2);
		q.rs.cull_face = rndn(2) ? GCM_CULL_FRONT : GCM_CULL_BACK;
		q.rs.front_face = rndn(2) ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW;
		q.rs.scissor_enable = rndn(2);
		q.rs.scissor_x = (int16_t)((int)rndn(2200) - 200);
		q.rs.scissor_y = (int16_t)((int)rndn(1300) - 100);
		q.rs.scissor_w = (uint16_t)rndn(2100);
		q.rs.scissor_h = (uint16_t)rndn(1200);
		q.rs.vp_x = (int16_t)((int)rndn(2200) - 200);
		q.rs.vp_y = (int16_t)((int)rndn(1300) - 100);
		q.rs.vp_w = (uint16_t)rndn(2100);
		q.rs.vp_h = (uint16_t)rndn(1200);
		q.rs.depth_near = (float)rndn(1001) / 1000.0f;
		q.rs.depth_far = rndn(2) ? 1.0f : (float)rndn(1001) / 1000.0f;
		q.rs.polyoffset_fill = rndn(4) == 0;
		q.rs.polyoffset_factor = (float)((int)rndn(201) - 100) / 8.0f;
		q.rs.polyoffset_units = (float)((int)rndn(201) - 100) / 8.0f;
		q.rs.color_mask_r = rndn(2);
		q.rs.color_mask_g = rndn(2);
		q.rs.color_mask_b = rndn(2);
		q.rs.color_mask_a = rndn(2);
		q.rs.shade_model = rndn(2) ? GCM_SHADE_MODEL_FLAT : GCM_SHADE_MODEL_SMOOTH;
		q.rs.stencil_enable = rndn(2);
		q.rs.stencil_func = GCM_NEVER + rndn(8);
		q.rs.stencil_ref = rndn(256);
		q.rs.stencil_mask = rndn(2) ? 0xFF : rnd();
		q.rs.stencil_fail = k_sop[rndn(4)];
		q.rs.stencil_zfail = k_sop[rndn(4)];
		q.rs.stencil_zpass = k_sop[rndn(4)];
		q.rs.stencil_writemask = rndn(2) ? 0xFF : rndn(4);
		q.rs.clear_color = rnd();
		q.rs.clear_depth = rndn(2) ? 1.0f : (float)rndn(100001) / 100000.0f;
		q.rs.clear_stencil = rnd();
		q.screen_w = 64 + rndn(1857);
		q.screen_h = 64 + rndn(1017);
		test_tex(ft, rndn(3) == 0);
		q.active_tmu = 0;
		q.tmu[0].enabled = 0;
		q.tmu[0].dirty = 0;
		q.tmu[1].enabled = rndn(4) != 0;
		q.tmu[1].bound = rndn(8) ? ft : NULL;
		q.tmu[1].aux = 1;
		q.tmu[1].dirty = 1;
		q.tmu[1].sent = 0;
		q.dirty = PS3GL_DIRTY_ALL;
		q.fast = rndn(2) ? PS3GL_FAST_COMPARE : 0;
		const u32 mask = k_mask[rndn(sizeof(k_mask) / 4)];

		// The emitter first: ps3gl's bind clears the texel flag the emitter reads.
		RsxdState& st = S.st;
		memset(&st, 0, sizeof(st));
		st.sc = &S.sc;
		st.sec = &S.sec;
		st.prog = &S.prog;
		gl_from_ps3gl(&st.gl);
		rg_from_ps3gl();
		rsxd_forget(&st);
		S.fr = FR_CHECK;
		st.check = 1;
		st.op = wb;
		st.oend = wb + SCRATCH_WORDS;
		fr_apply();
		fr_clear(mask);
		fr_unit1(true);

		cap.begin = wa;
		cap.current = wa;
		cap.end = wa + SCRATCH_WORDS - 4;
		cap.callback = 0;
		q.ctx = &cap;
		ps3gl_apply_states();
		glClear(mask);
		ps3gl_apply_textures();
		q.ctx = ctx0;

		rd_reset(A);
		rd_reset(B);
		const u32* pa = wa;
		const u32* pb = wb;
		rd_stream(A, &pa, cap.current, 0);
		rd_stream(B, &pb, st.op, 0);
		const u32 bad0 = S.test_bad;
		for (u32 k = 0; k < RD_REGS; k++)
			if (A->set[k] != B->set[k] || (A->set[k] && A->reg[k] != B->reg[k]))
				test_report(c, A->set[k] != B->set[k] ? "frame set" : "frame reg", A->set[k] ? A->reg[k] : 0xDEAD,
				            B->set[k] ? B->reg[k] : 0xDEAD, k * 4);
		const u32 ea[] = { A->n_clears, A->clear_mask, A->n_inval, A->err };
		const u32 eb[] = { B->n_clears, B->clear_mask, B->n_inval, B->err };
		for (u32 i = 0; i < 4; i++)
			if (ea[i] != eb[i])
				test_report(c, "frame event", ea[i], eb[i], i);
		bad_cases += S.test_bad != bad0;
	}
	*ft = fsave;
	q.rs = rs;
	q.tmu[0] = t0;
	q.tmu[1] = t1;
	q.active_tmu = atmu;
	q.fast = fast;
	q.screen_w = sw0;
	q.screen_h = sh0;
	q.dirty = PS3GL_DIRTY_ALL;
	q.active_shader = -1;
	q.active_vp = NULL;
	q.vattr_valid = 0;
	for (int i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		q.tmu[i].dirty = 1;
		q.tmu[i].sent = 0;
	}
	S.fr = fr0;
	S.n_clears = 0;
	memset(&S.st, 0, sizeof(S.st));
	memset(&S.rg, 0, sizeof(S.rg));
	free(A);
	free(B);
	free(wa);
	free(wb);
	printf("[rsxd] selftest frame: %u cases, %u bad\n", (u32)FRAME_CASES, bad_cases);
	return bad_cases == 0;
}

// Stage F: the emitter's own copies of ps3gl's fragment programs (the prebuilt ucode; prog_own selects them), checked as
// the RSX reads them, and its ring's labels. False: RING draws as TEX.
static bool own_memory()
{
	const ps3gl_state_t& q = ps3gl;
	u32 total = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
		if (q.shaders[k].fp_ucode)
			total += (q.shaders[k].fp_ucode_size + 63u) & ~63u;
	if (!S.fpm && total)
		S.fpm = (u8*)rsxMemalign(64, total);
	if (!S.fpm)
		return false;
	S.prog_own = S.prog;
	u32 at = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		const ps3gl_shader_t& sh = q.shaders[k];
		S.fp_own[k] = S.fp_gl[k] = 0;
		if (!sh.fp_ucode)
			continue;
		void* uc;
		u32 size;
		rsxFragmentProgramGetUCode(sh.fp, &uc, &size);
		if (size != sh.fp_ucode_size || (size & 3))
			return false;
		u8* const d = S.fpm + at;
		at += (size + 63u) & ~63u;
		memcpy(d, uc, size);
		u32 off;
		rsxAddressToOffset(d, &off);
		const volatile u32* const v = (const volatile u32*)d;
		for (u32 i = 0; i < size / 4; i++)
		{
			u32 x;
			memcpy(&x, (const u8*)uc + i * 4, 4);
			if (v[i] != x)
				return false;
		}
		// The select differs from ps3gl's in the one word that names the program.
		const u32 gl = (GCM_LOCATION_RSX + 1) | (sh.fp_offset & 0x1fffffffu), own = (GCM_LOCATION_RSX + 1) | (off & 0x1fffffffu);
		u32* const w = &S.prog_own.fp_words[S.prog_own.fp_at[k]];
		u32 found = 0;
		for (u32 i = 0; i + 1 < S.prog_own.fp_n[k]; i++)
			if (w[i] == RX_HDR(RX_FP_ADDR, 1) && w[i + 1] == gl)
			{
				w[i + 1] = own;
				found++;
			}
		if (found != 1)
			return false;
		S.fp_own[k] = own;
		S.fp_gl[k] = gl;
	}
	S.rlabel[0] = (volatile u32*)gcmGetLabelAddress(RING_LABEL0);
	S.rlabel[1] = (volatile u32*)gcmGetLabelAddress(RING_LABEL1);
	if (!S.rlabel[0] || !S.rlabel[1])
		return false;
	*S.rlabel[0] = *S.rlabel[1] = 0;
	S.rval[0] = S.rval[1] = 0;
	S.rseg = S.rhead = 0;
	S.rused = S.ring_mode = false;
	return true;
}

void ps3rsxd_init()
{
	if (!ps3gl_ptr)
		return;
	S.ctx = ps3gl_get_ctx();
	const bool cap = capture();
	S.ok = cap && selftest();
	if (!S.vbuf)
		S.vbuf = (u32*)RSXD_MEMALIGN(128, PS3GL_MAX_VERTS * sizeof(ps3gl_vertex_t));
	S.fr_ok = S.ok && S.vbuf && selftest_frame();
	// Stage E: two of ps3gl's slots that no texture takes carry the table's textures to ps3gl in a lent + TEX frame.
	S.proxy_ok = true;
	for (u32 u = 0; u < 2; u++)
	{
		const int gn = ps3gl.textures[T_PROXY + u].glname;
		S.proxy_ok = S.proxy_ok && (gn == -1 || gn == (int)(T_PNAME + u));
	}
	for (u32 u = 0; S.proxy_ok && u < 2; u++)
	{
		memset(&ps3gl.textures[T_PROXY + u], 0, sizeof(ps3gl_texture_t));
		ps3gl.textures[T_PROXY + u].glname = (int)(T_PNAME + u);
	}
	S.tgl = true;
	S.pg = &S.prog;
	S.ring_ok = S.fr_ok && own_memory();
	printf("[rsxd] RSX Direct %s (programs %s, %u VP words, %u FP words)%s%s%s\n", S.ok ? "ready" : "OFF: the emitter fails",
	       cap ? "captured" : "NOT captured", S.prog.vp_n[0] + S.prog.vp_n[1] + S.prog.vp_n[2] + S.prog.vp_n[3],
	       S.prog.fp_at[RSXD_KEYS - 1] + S.prog.fp_n[RSXD_KEYS - 1], S.ok && !S.fr_ok ? ", FRAME OFF" : "",
	       S.proxy_ok ? "" : ", TEX OFF", S.ring_ok ? "" : ", RING OFF");
}

// ---- the emitter's vertex ring (plan 9, stage F) ----

// Its memory, taken at the first RING or RING CHECK frame: the other values keep the RSX memory they had.
static bool ring_mem()
{
	if (S.rb)
		return true;
	S.rb = (u8*)rsxMemalign(128, 2 * RING_SEG);
	if (S.rb)
	{
		rsxAddressToOffset(S.rb, &S.rb_off);
		return true;
	}
	S.ring_ok = false;
	printf("[rsxd] RING OFF: no RSX memory for the ring\n");
	return false;
}

// The frame's segment gets its label, which the RSX writes once past every draw before it.
static void ring_end()
{
	if (!S.rused)
		return;
	S.rused = false;
	S.rval[S.rseg]++;
	rsxSetWriteBackendLabel(rctx(), S.rseg ? RING_LABEL1 : RING_LABEL0, S.rval[S.rseg]);
}

// The frame takes the other segment once the RSX is past the frame that used it last (as ps3gl_begin_frame's wait).
static void ring_begin()
{
	ring_end();
	S.rseg ^= 1;
	volatile u32* const l = S.rlabel[S.rseg];
	const u64 t0 = gettime();
	int waited = 0;
	while (*l != S.rval[S.rseg] && waited < 20000)
	{
		usleep(10);
		waited++;
	}
	if (*l != S.rval[S.rseg])
	{
		S.n_rtimeout++;
		printf("[rsxd] WARNING: ring segment %u fence timed out (val=%u label=%u)\n", S.rseg, S.rval[S.rseg], *l);
	}
	S.t_rwait += gettime() - t0;
	S.rhead = 0;
	S.rused = true;
	S.n_rframes++;
}

// ps3gl_vring_alloc's rules: no wrap onto a segment the RSX may read; each block starts 16-byte aligned.
static void* ring_alloc(u32 bytes, u32* off)
{
	if (S.rhead + bytes > RING_SEG)
	{
		S.n_rdrop++;
		return 0;
	}
	const u32 at = S.rseg * RING_SEG + S.rhead;
	*off = S.rb_off + at;
	S.rhead = (S.rhead + bytes + 15u) & ~15u;
	if (S.rhead > S.rpeak)
		S.rpeak = S.rhead;
	return S.rb + at;
}

void* rglVringAlloc(int count, u32* off)
{
	if (!S.rused)
		return ps3gl_vring_alloc(count, off);
	return ring_alloc((u32)count * (u32)sizeof(ps3gl_vertex_t), off);
}

void* rglVringAllocRaw(u32 bytes, u32* off)
{
	if (!S.rused)
		return ps3gl_vring_alloc_raw(bytes, off);
	return bytes ? ring_alloc(bytes, off) : 0;
}

extern "C" int ps3rsxd_gl_off(void)
{
	return S.gl_off;
}

bool ps3rsxd_ring_next()
{
	return g_rsx_direct_preset == RSXD_RING && S.ok && S.ring_ok;
}

extern "C" void ps3rsxd_ring_end(void)
{
	ring_end();
}

u32 ps3rsxd_fp_map(u32 addr)
{
	for (u32 k = 0; k < RSXD_KEYS; k++)
		if (S.fp_own[k] && addr == S.fp_own[k])
			return S.fp_gl[k];
	return addr;
}

// RING: ps3gl's frame starts at the frame's first lend (the UI's): its segment, fence and state, the bundle 14 bits.
static void t_handoff()
{
	if (!S.gl_off)
		return;
	S.gl_off = false;
	S.n_handoff++;
	ps3gl_begin_frame();
	ps3gl_fast_path((g_tex_bind_table_preset ? PS3GL_FAST_BIND : 0) | (g_gl_compare_preset ? PS3GL_FAST_COMPARE : 0) |
	                (g_tex_recycle_preset ? PS3GL_FAST_GEN : 0));
}

// ---- frames, scenes, sections ----

static const u32* io_addr(void* ctx, uint32_t off)
{
	(void)ctx;
	void* a = 0;
	if (gcmIoOffsetToAddress(off, &a) != 0)
		return 0;
	return (const u32*)a;
}

static bool check_bufs()
{
	if (!S.P)
	{
		S.P = (RdRegs*)malloc(sizeof(RdRegs));
		S.E = (RdRegs*)malloc(sizeof(RdRegs));
		S.scratch = (u32*)memalign(128, SCRATCH_WORDS * 4);
	}
	return S.P && S.E && S.scratch;
}

static inline u32 ea32(const void* p) { return (u32)(uintptr_t)p; }
static inline u8* ring_base() { return S.host + S.ring_half * RING_BYTES; }
static inline u32 ring_room() { return RING_BYTES / 4 - S.ring_pos; }
static inline u8* trvb_base() { return S.host + 2 * RING_BYTES + S.ring_half * TRVB_BYTES; }

static void spu_setup()
{
	if (S.spu_init)
		return;
	S.spu_init = true;
	u32 bytes = 0;
	S.host = (u8*)ps3_video_host_spare(&bytes, &S.host_io);
	S.job = (RsxdJob*)RSXD_MEMALIGN(128, sizeof(RsxdJob));
	S.done = (RsxdDone*)RSXD_MEMALIGN(128, sizeof(RsxdDone));
	S.ops = (RsxdOp*)RSXD_MEMALIGN(128, JOB_OPS * sizeof(RsxdOp));
	S.texs = (RsxdTex*)RSXD_MEMALIGN(128, JOB_TEX * sizeof(RsxdTex));
	S.tstrips = (RsxdTriStrip*)RSXD_MEMALIGN(128, RSXD_TRI_STRIPS * sizeof(RsxdTriStrip));
	S.tparams = (RsxdTriParam*)RSXD_MEMALIGN(128, RSXD_TRI_PARAMS * sizeof(RsxdTriParam));
	S.pmap = (u16*)RSXD_MEMALIGN(128, PMAP_N * 2);
	S.pgen = (u16*)RSXD_MEMALIGN(128, PMAP_N * 2);
	S.spu_words = (u32*)RSXD_MEMALIGN(128, SPU_WORDS * 4);
	S.spu_dead = !S.host || bytes < 2 * RING_BYTES + 2 * TRVB_BYTES || !S.job || !S.done || !S.ops || !S.texs ||
	             !S.tstrips || !S.tparams || !S.pmap || !S.pgen || !S.spu_words;
	if (S.done)
		memset(S.done, 0, sizeof(RsxdDone));
	if (S.pgen)
		memset(S.pgen, 0, PMAP_N * 2);
	printf("[rsxd] SPU jobs %s | host block %p io %08X, %u KB\n", S.spu_dead ? "OFF (no memory)" : "ready", S.host,
	       S.host_io, bytes >> 10);
}

static bool spu_ready()
{
	spu_setup();
	return !S.spu_dead && ps3spetex_mode() != SPETEX_OFF;
}

static void fr_take();
static void t_switch(u32 tm);
static void t_unproxy();
#ifdef RSXD_FRAME_SECTIONS
extern int g_rsxd_frame_sections;
#endif
#ifdef RSXD_ALL_RUNS
extern int g_rsxd_all_runs;
#endif

void ps3rsxd_frame(u32 kind)
{
	// A frame that kept the RSX to its end.
	ps3rsxd_lend();
	// An own frame keeps the row its begin saw; only its begin leaves ps3gl alone.
	int row = S.vb_set ? (int)S.vb_row : g_rsx_direct_preset;
	if (!S.vb_set)
		S.gl_off = false;
	S.vb_set = false;
	if (row < RSXD_OFF || row > RSXD_RING_CHECK || !S.ok)
		row = RSXD_OFF;
	S.row = row;
	// Stage F: the emitter's ring and program copies at RING (its begin left ps3gl alone) and RING CHECK. A change waits
	// for the RSX to be idle: the frees and the SPU's host blocks change the fence they follow.
	const bool ring = S.ring_ok && (S.gl_off || (row == RSXD_RING_CHECK && ring_mem()));
	if (ring != S.ring_mode)
	{
		ring_end();
		ps3_video_drain();
		rt_frame(0);
		rt_frame(1);
		S.ring_mode = ring;
	}
	if (ring)
		ring_begin();
	S.pg = ring ? &S.prog_own : &S.prog;
	const u32 seg = ring ? S.rseg : (u32)ps3gl.vring.cur_seg;
	// Stage E: the frame's begin waited for its segment's fence, so the table's memory given up in it before goes. A new
	// texture mode drops the names of the old one.
	rt_frame(seg);
	t_unproxy();
	const bool tex = row == RSXD_TEX || row == RSXD_RING;
	const u32 tm = row == RSXD_TEX_CHECK || row == RSXD_RING_CHECK ? TM_PAIR : tex && S.fr_ok && S.proxy_ok ? TM_OWN : TM_GL;
	// RING: the old mode's names go through ps3gl, so a change hands it the frame here (ps3gl's frame begins).
	if (tm != S.tm)
	{
		t_handoff();
		t_switch(tm);
	}
	// FRAME to RING draw their runs as SPE + TR SORT; their CHECKs as CHECK.
	const bool live = row == RSXD_FRAME || row == RSXD_VOLS || row == RSXD_FOG || row == RSXD_SCREEN || tex;
	const bool check = row == RSXD_FRAME_CHECK || row == RSXD_VOLS_CHECK || row == RSXD_FOG_CHECK ||
	                   row == RSXD_SCREEN_CHECK || row == RSXD_TEX_CHECK || row == RSXD_RING_CHECK;
	int m = live ? RSXD_TRIS : check ? RSXD_CHECK : row;
#ifdef RSXD_FRAME_SECTIONS
	// misc/rsxdirect_check: the frame values with the run draws of another value.
	if (live && g_rsxd_frame_sections)
		m = g_rsxd_frame_sections;
#endif
	S.frame_mode = m;
	S.screen = row >= RSXD_SCREEN;
	const bool take = kind == RSXD_F_GAME || S.screen;
	S.fr = !take || !S.fr_ok ? FR_OFF : live ? FR_LIVE : check ? FR_CHECK : FR_OFF;
	S.fog = row == RSXD_FOG || row == RSXD_FOG_CHECK || S.screen;
	S.vols = row == RSXD_VOLS || row == RSXD_VOLS_CHECK || S.fog;
	S.scene_set = false;
	// The frame's begin has waited for this segment's fence: its halves of the ring and TR block are free.
	S.ring_half = seg & 1;
	S.ring_pos = 0;
	S.trvb_pos = 0;
	if (m == RSXD_CHECK)
	{
		if (!check_bufs())
		{
			S.frame_mode = RSXD_OFF;
			S.fr = FR_OFF;
			return;
		}
		rd_reset(S.P);
		S.ppos = rctx()->current;
		S.pcs.in_call = 0;
		S.p_ok = true;
	}
	if (S.fr != FR_OFF)
	{
		S.n_frames++;
		fr_take();
	}
}

int ps3rsxd_scene(const RsxdSceneIn* in, const RsxdRend* rend)
{
	S.mode = S.frame_mode;
	if (S.mode == RSXD_OFF)
		return RSXD_OFF;
	S.rr = *rend;
	RsxdScene& sc = S.sc;
	// The frame's own values the memset below drops (the emitter has the frame: they are not ps3gl's).
	const u32 fsw = sc.screen_w, fsh = sc.screen_h, ffog = sc.fog_scale;
	memset(&sc, 0, sizeof(sc));
	sc.flags = in->flags & ~RSXD_S_TMU1;
	sc.pt_blend = in->pt_blend;
	sc.flat = in->flat;
	sc.clip = in->clip;
	sc.fog = in->fog;
	for (int k = 0; k < 4; k++)
	{
		sc.clamp_min[k] = fbits(in->clamp_min[k]);
		sc.clamp_max[k] = fbits(in->clamp_max[k]);
	}
	sc.bx = in->bx;
	sc.by = in->by;
	sc.bw = in->bw;
	sc.bh = in->bh;
	sc.base_scissor = in->base_scissor;
	sc.screen_w = ps3gl.screen_w;
	sc.screen_h = ps3gl.screen_h;
	sc.cv_x = in->cv_x;
	sc.cv_y = in->cv_y;
	sc.cv_w = in->cv_w;
	sc.cv_h = in->cv_h;
	sc.cv_cx0 = in->cx0;
	sc.cv_cy0 = in->cy0;
	sc.cv_cw = in->cw;
	sc.cv_ch = in->ch;
	sc.glob_clip_x = in->glob_clip_x;
	sc.glob_clip_y = in->glob_clip_y;
	if (S.fr_own)
	{
		sc.screen_w = fsw;
		sc.screen_h = fsh;
		sc.fog_scale = ffog;
		const ps3gl_tmu_t& u1 = ps3gl.tmu[1];
		sc.flags |= (u1.enabled && !u1.aux && u1.bound && u1.bound->data) ? RSXD_S_TMU1 : 0;
		fr_mvp();
	}
	// FRAME asks before the background and again with the vertex block: one scene.
	S.n_scene += !(S.fr_own && S.scene_set);
	S.scene_set = S.fr_own;
	return S.mode;
}

// A section in a frame the emitter has: its state and the RSX's go on; the rule state is ps3_rend.cpp's.
static void fr_sec()
{
	RsxdSec& se = S.sec;
	se.pass_nz = *S.rr.pass != 0;
	se.autosort = *S.rr.autosort;
	se.tri_mode = *S.rr.tri_mode;
	se.depth_pass = *S.rr.depth_pass;
	se.vb_off = 0;
	se.vb_loc = GCM_LOCATION_RSX;
	RsxdState& st = S.st;
	st.sc = &S.sc;
	st.sec = &se;
	st.prog = S.pg;
	st.gl.clip_base = *S.rr.clip_base;
	st.gl.clip_mode = *S.rr.clip_mode;
	memcpy(st.gl.clip_r, S.rr.clip_r, sizeof(st.gl.clip_r));
	st.gl.flat_shift = *S.rr.flat_shift;
	st.n_draw = st.n_vtx = st.n_tex = st.n_inval = st.n_offc = st.n_cclamp = st.n_tril = 0;
	S.job_gl = st.gl;
}

// The section's start: with takeover, ps3gl sends what it holds, so the registers the emitter leaves alone are
// current; unit 0 and its pending texture cache clear are the section's (ps3gl sends unit 0 again at its next draw).
static void sec_open(bool takeover)
{
	if (S.fr_own)
	{
		fr_sec();
		return;
	}
	if (takeover)
	{
		ps3gl_flush_polygon_batch();
		ps3gl_apply_states();
		ps3gl_apply_matrices();
		ps3gl_tmu_t& u0 = ps3gl.tmu[0];
		const int en0 = u0.enabled;
		u0.enabled = 0;
		ps3gl_apply_textures();
		u0.enabled = en0;
		u0.dirty = 1;
		u0.sent = 0;
		ps3gl_apply_shader();
	}
	else
		ps3gl_apply_matrices();
	RsxdScene& sc = S.sc;
	memcpy(sc.mvp, ps3gl_get_mvp(), sizeof(sc.mvp));
	sc.fog_scale = fbits(ps3gl.dc_fog_scale);
	const ps3gl_tmu_t& u1 = ps3gl.tmu[1];
	sc.flags = (sc.flags & ~RSXD_S_TMU1) | ((u1.enabled && !u1.aux && u1.bound && u1.bound->data) ? RSXD_S_TMU1 : 0);
	RsxdSec& se = S.sec;
	se.pass_nz = *S.rr.pass != 0;
	se.autosort = *S.rr.autosort;
	se.tri_mode = *S.rr.tri_mode;
	se.depth_pass = *S.rr.depth_pass;
	se.vb_off = 0;
	se.vb_loc = GCM_LOCATION_RSX;
	RsxdState& st = S.st;
	st.sc = &sc;
	st.sec = &se;
	st.prog = S.pg;
	gl_from_ps3gl(&st.gl);
	st.gl.clip_base = *S.rr.clip_base;
	st.gl.clip_mode = *S.rr.clip_mode;
	memcpy(st.gl.clip_r, S.rr.clip_r, sizeof(st.gl.clip_r));
	st.gl.flat_shift = *S.rr.flat_shift;
	rsxd_forget(&st);
	st.n_draw = st.n_vtx = st.n_tex = st.n_inval = st.n_offc = st.n_cclamp = st.n_tril = 0;
	S.job_gl = st.gl;
}

// ps3gl (not in a frame the emitter has) and ps3_rend.cpp take the emitter's state back; the counts go to their stats.
static void sec_release()
{
	RsxdState& st = S.st;
	if (!S.fr_own)
		gl_to_ps3gl(&st.gl);
	*S.rr.clip_base = st.gl.clip_base != 0;
	*S.rr.clip_mode = st.gl.clip_mode;
	memcpy(S.rr.clip_r, st.gl.clip_r, sizeof(st.gl.clip_r));
	*S.rr.flat_shift = st.gl.flat_shift;
	*S.rr.cache_valid = false;
#ifdef RSXD_RELEASE_CHECK
	// misc/rsxdirect_check: ps3gl and ps3_rend.cpp hold the emitter's state after the write-back.
	u32 ev, gv;
	const u32 d = S.fr_own ? 0 : gl_diff(&st.gl, &ev, &gv);
	if (d && S.lines++ < CHECK_LINES)
		printf("[rsxd] release MISMATCH gl field %X.%u emitter %X ps3gl %X\n", d >> 8, d & 0xFF, ev, gv);
#endif
	*S.rr.offc += st.n_offc;
	*S.rr.cclamp += st.n_cclamp;
	*S.rr.tril += st.n_tril;
	ps3gl_frame_flush_count += st.n_draw;
	ps3gl_frame_flush_vertex_count += st.n_vtx;
	ps3gl_frame_tex_binds += st.n_tex;
	S.n_draw += st.n_draw;
	S.n_vtx += st.n_vtx;
	S.n_tex += st.n_tex;
	S.n_inval += st.n_inval;
}

// ---- the SPU's jobs ----

static bool job_start(RsxdJob* j)
{
	j->seq = ++S.seq;
	j->prog_ea = ea32(S.pg);
	j->prog_gen = S.pg == &S.prog_own ? 2 : 1;
	j->done_ea = ea32(S.done);
	RSXD_SYNC();
	return ps3spetex_job(ea32(j), j->seq);
}

// The SPU's answer: a short spin with yields, then 20 us sleeps. 50 ms late: the SPU jobs stop for the boot.
static bool job_wait(u32 seq)
{
	const volatile u32* d = &S.done->seq;
	const u64 t0 = gettime();
	while (*d != seq)
	{
		const u64 dt = gettime() - t0;
		if (dt > ps3_timebase_hz / 20)
		{
			S.spu_dead = true;
			S.n_late++;
			printf("[rsxd] SPU job %u late (50 ms): RSX Direct goes on on the PPU\n", seq);
			return false;
		}
		if (dt < ps3_timebase_hz / 10000)
			RSXD_YIELD();
		else
			usleep(20);
	}
	RSXD_LWSYNC();
	S.t_wait += gettime() - t0;
	return true;
}

static void job_fill(RsxdJob* j, u32 kind, u32 check)
{
	memset(j, 0, sizeof(*j));
	j->kind = kind;
	j->check = check;
	if (check)
	{
		j->out_ea = ea32(S.spu_words);
		j->out_words = SPU_WORDS;
	}
	else
	{
		j->out_ea = ea32(ring_base()) + S.ring_pos * 4;
		j->out_words = ring_room();
	}
	j->sc = S.sc;
	j->sec = S.sec;
	j->gl = S.job_gl;
}

// A live job's words: the CALL, and the job's state and counts as the PPU emitter would leave them.
static void job_take()
{
	const RsxdDone& d = *S.done;
	RsxdState& st = S.st;
	RSXD_SYNC();
	rsxSetCallCommand(rctx(), S.host_io + S.ring_half * RING_BYTES + S.ring_pos * 4);
	S.ring_pos += (d.words + 31) & ~31u;
	S.n_jobs++;
	S.n_jwords += d.words;
	S.n_words += d.words;
	S.n_jdraw += d.n_draw;
	st.gl = d.gl;
	st.n_draw += d.n_draw;
	st.n_vtx += d.n_vtx;
	st.n_tex += d.n_tex;
	st.n_inval += d.n_inval;
	st.n_offc += d.n_offc;
	st.n_cclamp += d.n_cclamp;
	st.n_tril += d.n_tril;
	S.job_gl = st.gl;
	// The SPU's words changed what the RSX holds.
	if (S.fr_own)
		rsxd_forget(&st);
}

static bool emit_job(u32 check)
{
	RsxdJob* j = S.job;
	job_fill(j, RSXD_JOB_EMIT, check);
	j->ops_ea = ea32(S.ops);
	j->nops = S.nops;
	j->tex_ea = ea32(S.texs);
	j->ntex = S.ntex;
	if (!job_start(j) || !job_wait(j->seq))
		return false;
	if (S.done->status != RSXD_DONE_OK)
	{
		S.n_fall++;
		return false;
	}
	return true;
}

// ---- recording (SEC_SPU, SEC_SPU_CHECK) ----

static void rec_begin()
{
	S.nops = S.ntex = 0;
	S.spuc_lost = false;
	const RsxdGl& g = S.st.gl;
	S.r_tmu0 = g.tex_en && g.has_tex;
	S.r_obj = g.tex.obj;
	S.r_clip_mode = g.clip_mode;
	memcpy(S.r_clip_r, g.clip_r, sizeof(S.r_clip_r));
}

// The ops through the PPU emitter, in the FIFO: a job that failed, or no SPU.
static void rec_replay()
{
	RsxdState& st = S.st;
	st.gl = S.job_gl;
	rsxd_forget(&st);
	st.check = 0;
	gcmContextData* c = rctx();
	st.op = c->current;
	st.oend = c->end;
	S.replay = true;
	for (u32 i = 0; i < S.nops; i++)
	{
		const RsxdOp& o = S.ops[i];
		if ((o.kind & 0xFF) == RSXD_OP_PARAM)
		{
			const u32 textured = (o.kind >> 16) & 1;
			rsxd_apply(&st, o.w, (o.kind >> 8) & 0xFF, textured, textured ? &S.texs[o.tex] : 0, &o.clip);
		}
		else
		{
			S.sec.vb_off = o.w[0];
			S.replay_inval = (o.kind >> 16) & 1;
			rsxd_draw(&st, (o.kind >> 8) & 0xFF, o.w[1], o.w[2], o.w[3]);
		}
	}
	S.replay = false;
	c->current = st.op;
	S.nops = S.ntex = 0;
	S.job_gl = st.gl;
}

// SEC_SPU: the ops so far to the SPU; without room or SPU the PPU draws them, and the rest of the section.
static void rec_flush()
{
	if (!S.nops)
		return;
	if (ring_room() >= JOB_MIN && !S.spu_dead && emit_job(0))
	{
		job_take();
		S.nops = S.ntex = 0;
		return;
	}
	rec_replay();
	S.sec_mode = RSXD_SEC_LIVE;
}

static void rec_param(const u32* w, u32 list, u32 textured, const RsxdTex* tex, const RsxdClip* clip)
{
	RsxdOp& o = S.ops[S.nops++];
	o.kind = RSXD_OP_PARAM | (list << 8) | (textured << 16);
	memcpy(o.w, w, 16);
	o.tex = 0;
	o.clip = *clip;
	o.pad = 0;
	if (textured)
	{
		if (!S.ntex || memcmp(&S.texs[S.ntex - 1], tex, sizeof(*tex)))
			S.texs[S.ntex++] = *tex;
		o.tex = S.ntex - 1;
	}
}

static void rec_draw(u32 prim, u32 vb_off, u32 stride, u32 first, u32 count, u32 inval)
{
	RsxdOp& o = S.ops[S.nops++];
	o.kind = RSXD_OP_DRAW | (prim << 8) | (inval << 16);
	o.w[0] = vb_off;
	o.w[1] = stride;
	o.w[2] = first;
	o.w[3] = count;
	o.tex = 0;
	memset(&o.clip, 0, sizeof(o.clip));
	o.pad = 0;
}

u32 ps3rsxd_begin(u32 kind)
{
	if (S.mode == RSXD_OFF)
		return RSXD_SEC_NONE;
#ifdef RSXD_ALL_RUNS
	if (g_rsxd_all_runs && kind == RSXD_K_PPU)
		kind = RSXD_K_RUNS;
#endif
	gcmContextData* c = rctx();
	sec_open(true);
	RsxdState& st = S.st;
	S.n_sec++;
	S.n_vols += kind == RSXD_K_VOL;
	if (S.mode == RSXD_CHECK)
	{
		S.sec_mode = RSXD_SEC_CHECK;
		st.check = 1;
		// FRAME CHECK: the emitter's scratch words and the compare go on from the frame's start.
		if (S.fr_own)
			return RSXD_SEC_CHECK;
		st.op = S.scratch;
		st.oend = S.scratch + SCRATCH_WORDS;
		if (S.p_ok)
		{
			rd_fifo(S.P, &S.ppos, c->current, io_addr, 0, &S.pcs, 1u << 21, 0);
			if (S.P->err)
			{
				S.p_ok = false;
				S.n_lost++;
			}
		}
		rd_clean(S.P);
		rd_copy(S.E, S.P);
		S.virt_clear = 0xFFFFFFFFu;
		return RSXD_SEC_CHECK;
	}
	S.sec_mode = RSXD_SEC_LIVE;
	*S.rr.cache_valid = false;
	st.check = 0;
	st.op = c->current;
	st.oend = c->end;
	if (kind == RSXD_K_RUNS && (S.mode == RSXD_SPE || S.mode == RSXD_TRIS || S.mode == RSXD_SPE_CHECK) && spu_ready() &&
	    ring_room() >= JOB_MIN)
	{
		// FRAME: what the SPU's draws need from the frame goes before its words.
		if (S.fr_own)
		{
			fr_pre_draw(true);
			c->current = st.op;
		}
		S.sec_mode = S.mode == RSXD_SPE_CHECK ? SEC_SPU_CHECK : SEC_SPU;
		S.sec_fifo = c->current;
		rec_begin();
	}
	return RSXD_SEC_LIVE;
}

static void spu_compare(const u32* fifo0, const u32* fifo1, const u32* spu, u32 words);

void ps3rsxd_end()
{
	RsxdState& st = S.st;
	if (S.sec_mode == RSXD_SEC_CHECK)
	{
		u32 ev, gv;
		const u32 d = gl_diff(&st.gl, &ev, &gv);
		if (d)
		{
			S.n_glbad++;
			if (S.lines++ < CHECK_LINES)
				printf("[rsxd] check MISMATCH gl field %X.%u emitter %X ps3gl %X at a section end | last param %08X %08X "
				       "%08X %08X list %u\n", d >> 8, d & 0xFF, ev, gv, S.last_param[0], S.last_param[1], S.last_param[2],
				       S.last_param[3], S.last_list);
		}
		S.n_draw += st.n_draw;
		S.n_vtx += st.n_vtx;
		S.n_tex += st.n_tex;
		S.n_inval += st.n_inval;
	}
	else if (S.sec_mode == SEC_SPU)
	{
		rec_flush();
		if (S.sec_mode == RSXD_SEC_LIVE)
			rctx()->current = st.op;
		sec_release();
	}
	else if (S.sec_mode == RSXD_SEC_LIVE || S.sec_mode == SEC_SPU_CHECK)
	{
		rctx()->current = st.op;
		sec_release();
		// SPE CHECK: the SPU's words for the same ops, against the PPU's words of the section.
		if (S.sec_mode == SEC_SPU_CHECK && S.nops)
		{
			if (S.spuc_lost || !emit_job(1))
				S.n_slost++;
			else
				spu_compare(S.sec_fifo, rctx()->current, S.spu_words, S.done->words);
		}
	}
	S.sec_mode = RSXD_SEC_NONE;
}

// clip_apply's rect for one param, with ps3_rend.cpp's float steps (PPU only: the SPU rounds otherwise).
static void clip_of(const u32* w, RsxdClip* c)
{
	const RsxdScene& sc = S.sc;
	const u32 pcw = w[0], isp = w[1];
	const u32 mode = (pcw >> 16) & 3;
	c->mode = RXC_BASE;
	if (mode < 2 || (mode == 3 && sc.clip < 2))
		return;
	const u32 rect = (isp & 0xFFFFFu) | (((pcw >> 8) & 3) << 20);
	const float csx = (float)((rect & 63) * 32), cex = (float)((((rect >> 6) & 63) + 1) * 32);
	const float csy = (float)(((rect >> 12) & 31) * 32), cey = (float)((((rect >> 17) & 31) + 1) * 32);
	if (csx == 0 && csy == 0 && cex >= (float)sc.glob_clip_x && cey >= (float)sc.glob_clip_y)
		return;
	const float sx = (float)sc.cv_w / sc.cv_cw, sy = (float)sc.cv_h / sc.cv_ch;
	int r[4] = { sc.cv_x + (int)lroundf((csx - sc.cv_cx0) * sx), sc.cv_y + sc.cv_h - (int)lroundf((cey - sc.cv_cy0) * sy),
	             sc.cv_x + (int)lroundf((cex - sc.cv_cx0) * sx), sc.cv_y + sc.cv_h - (int)lroundf((csy - sc.cv_cy0) * sy) };
	const int lim[4] = { sc.bx, sc.by, sc.bx + sc.bw, sc.by + sc.bh };
	for (int k = 0; k < 4; k++)
	{
		const int lo = lim[k & 1], hi = lim[2 + (k & 1)];
		r[k] = r[k] < lo ? lo : r[k] > hi ? hi : r[k];
	}
	const bool empty = r[2] <= r[0] || r[3] <= r[1];
	if (mode == 3 && empty)
		return;
	memcpy(c->r, r, sizeof(r));
	c->mode = mode == 3 ? RXC_OUTSIDE : empty ? RXC_ALL : RXC_INSIDE;
}

// apply_param's texture bind and clip rect for the emitter.
static u32 param_prep(const u32* w, u32 list, u32 slot, RsxdTex* tex, RsxdClip* clip)
{
	memcpy(S.last_param, w, 16);
	S.last_list = list;
	u32 textured = 0;
	// The TR depth pass binds no texture (apply_param_body).
	if (((w[0] >> 3) & 1) && !S.sec.depth_pass)
	{
		const u64 t0 = gettime();
		textured = ps3tex_bind_rsxd(w[2], w[3], slot, list == RXL_PUNCH, tex);
		// CHECK: ps3gl's bind came first and counted the time.
		if (S.sec_mode != RSXD_SEC_CHECK)
			*S.rr.t_tex += gettime() - t0;
	}
	clip->mode = RXC_BASE;
	if (S.sc.clip)
		clip_of(w, clip);
	return textured;
}

u32 ps3rsxd_param(const void* param, u32 list, u32 slot)
{
	const u32* w = (const u32*)param;
	if (S.sec_mode == SEC_SPU && S.nops >= JOB_OPS - 1)
		rec_flush();
	RsxdTex tex;
	RsxdClip clip;
	const u32 textured = param_prep(w, list, slot, &tex, &clip);
	if (S.sec_mode == SEC_SPU)
	{
		if (textured && S.ntex == JOB_TEX)
			rec_flush();
		if (S.sec_mode == SEC_SPU)
		{
			rec_param(w, list, textured, &tex, &clip);
			// What rsxd_apply leaves for the draws: unit 0 and the clip.
			if (S.sec.depth_pass || !textured)
				S.r_tmu0 = 0;
			else
			{
				S.r_tmu0 = tex.valid;
				S.r_obj = tex.obj;
			}
			if (S.sc.clip)
			{
				S.r_clip_mode = clip.mode == RXC_BASE ? RXC_NONE : clip.mode;
				if (clip.mode != RXC_BASE)
					memcpy(S.r_clip_r, clip.r, sizeof(S.r_clip_r));
			}
			if (S.sec.depth_pass)
				return 0;
			const u32 tsp = w[2];
			return (!((tsp >> 20) & 1) || (textured && ((tsp >> 6) & 3) == 1)) ? 0xFF000000u : 0;
		}
	}
	const u32 rv = rsxd_apply(&S.st, w, list, textured, &tex, &clip);
	if (S.sec_mode == SEC_SPU_CHECK)
	{
		if (S.nops >= JOB_OPS - 1 || (textured && S.ntex == JOB_TEX))
			S.spuc_lost = true;
		else
			rec_param(w, list, textured, &tex, &clip);
	}
	return rv;
}

void ps3rsxd_param_check(const void* param, u32 list, u32 slot, u32 alpha_or)
{
	RsxdTex tex;
	RsxdClip clip;
	const u32* w = (const u32*)param;
	const u32 textured = param_prep(w, list, slot, &tex, &clip);
	const u32 rv = rsxd_apply(&S.st, w, list, textured, &tex, &clip);
	if (rv != alpha_or)
	{
		S.n_alpha++;
		if (S.lines++ < CHECK_LINES)
			printf("[rsxd] check MISMATCH alpha force %08X ps3gl %08X | param %08X %08X %08X %08X list %u\n", rv, alpha_or,
			       S.last_param[0], S.last_param[1], S.last_param[2], S.last_param[3], list);
	}
}

void ps3rsxd_draw(u32 prim, u32 vb_off, u32 stride, u32 first, u32 count)
{
	RsxdState& st = S.st;
	const u32 rp = prim == GL_TRIANGLES ? RX_TRIANGLES : RX_TRI_STRIP;
	if (S.sec_mode == SEC_SPU && S.nops >= JOB_OPS - 1)
		rec_flush();
	if (S.sec_mode == SEC_SPU)
	{
		// As rx_draw1 would: the texture's cache clear at the draw's first side, which clears the flag.
		const u32 inval = rsxd_draws(&S.sc, S.r_clip_mode, S.r_clip_r, count) && S.r_tmu0 && rsxd_tex_dirty(&st, S.r_obj, 1);
		rec_draw(rp, vb_off, stride, first, count, inval);
		return;
	}
	S.sec.vb_off = vb_off;
	const u32* const op0 = st.op;
	const u32 inval0 = st.n_inval;
	if (S.fr_own)
		fr_pre_draw(false);
	rsxd_draw(&st, rp, stride, first, count);
	// A draw that moved on to a new FIFO segment ends below op0; its words are left out.
	if (st.op >= op0)
		S.n_words += (u64)(st.op - op0);
	// The CHECKs write the scratch buffer, also out of a section.
	if (!st.check)
		rctx()->current = st.op;
	if (S.sec_mode == SEC_SPU_CHECK)
	{
		if (S.nops >= JOB_OPS - 1)
			S.spuc_lost = true;
		else
			rec_draw(rp, vb_off, stride, first, count, st.n_inval != inval0);
	}
}

static void check_report(void* ctx, uint32_t what, uint32_t index, uint32_t a, uint32_t b)
{
	(void)ctx;
	if (S.lines++ < CHECK_LINES)
	{
		static const char* const k_what[] = { "reg", "vp inst", "vp const", "set" };
		printf("[rsxd] check MISMATCH draw %u %s %X ps3gl %08X emitter %08X | param %08X %08X %08X %08X list %u\n",
		       S.n_check, k_what[what & 3], index, a, b, S.last_param[0], S.last_param[1], S.last_param[2],
		       S.last_param[3], S.last_list);
	}
}

void ps3rsxd_draw_check()
{
	RsxdState& st = S.st;
	const u32* ep = S.scratch;
	const u32* const eend = st.op;
	const u32* const stop = rctx()->current;
	for (;;)
	{
		int pe = 0;
		if (S.p_ok)
		{
			pe = rd_fifo(S.P, &S.ppos, stop, io_addr, 0, &S.pcs, 1u << 16, 1);
			if (S.P->err)
			{
				S.p_ok = false;
				S.n_lost++;
				pe = 0;
			}
		}
		const int ee = rd_stream(S.E, &ep, eend, 1);
		if (!S.p_ok)
			break;
		if (!pe && !ee)
			break;
		S.n_check++;
		if (pe != ee)
		{
			check_report(0, 3, 0x1808, pe, ee);
			S.n_bad++;
			break;
		}
		u32 bad = 0;
		if (pe == 2)
		{
			// FRAME CHECK: a clear.
			S.n_fclear++;
			if (S.P->clear_mask != S.E->clear_mask)
			{
				check_report(0, 3, 0x1d94, S.P->clear_mask, S.E->clear_mask);
				bad++;
			}
		}
		else
		{
			const u32 da[] = { S.P->prim, S.P->batch_first, S.P->batch_count, S.P->n_inval };
			const u32 db[] = { S.E->prim, S.E->batch_first, S.E->batch_count, S.E->n_inval };
			for (u32 i = 0; i < 4; i++)
				if (da[i] != db[i])
				{
					check_report(0, 3, 0xD000 + i, da[i], db[i]);
					bad++;
				}
		}
		S.E->n_inval = S.P->n_inval;
		// RING CHECK: the emitter selects its own copy of the program ps3gl selects.
		if (S.pg == &S.prog_own)
		{
			u32& fa = S.E->reg[RX_FP_ADDR >> 2];
			const u32 gl = ps3rsxd_fp_map(fa);
			S.n_fpown += gl != fa;
			fa = gl;
		}
		if (S.fr == FR_CHECK)
			rd_take_foreign(S.P, S.E);
		bad += rd_compare(S.P, S.E, check_report, 0);
		S.n_bad += bad != 0;
	}
	st.op = S.scratch;
	S.virt_clear = 0xFFFFFFFFu;
}

// ---- SPE CHECK and TR SORT CHECK: the SPU's words against the PPU's ----

static void spu_report(void* ctx, uint32_t what, uint32_t index, uint32_t a, uint32_t b)
{
	(void)ctx;
	if (S.lines++ < CHECK_LINES)
	{
		static const char* const k_what[] = { "reg", "vp inst", "vp const", "set" };
		printf("[rsxd] %s MISMATCH draw %u %s %X ppu %08X spu %08X\n", S.mode == RSXD_SPE_CHECK ? "spe" : "tris",
		       S.n_scheck, k_what[what & 3], index, a, b);
	}
}

// The FIFO [fifo0, fifo1) and the SPU's words [spu, spu + words - 1) (the RETURN left out), draw by draw. Both start
// from rsxd_forget, so each sends the same registers.
static void spu_compare(const u32* fifo0, const u32* fifo1, const u32* spu, u32 words)
{
	if (!check_bufs())
		return;
	RdRegs* A = S.P;
	RdRegs* B = S.E;
	rd_reset(A);
	rd_reset(B);
	const u32* pa = fifo0;
	const u32* pb = spu;
	const u32* const pe = spu + (words ? words - 1 : 0);
	RdCall cs = { 0, 0 };
	for (;;)
	{
		const int ea = rd_fifo(A, &pa, fifo1, io_addr, 0, &cs, 1u << 22, 1);
		const int eb = rd_stream(B, &pb, pe, 1);
		if (A->err || B->err)
		{
			S.n_slost++;
			return;
		}
		if (!ea && !eb)
			return;
		S.n_scheck++;
		u32 bad = 0;
		if (ea != eb)
		{
			spu_report(0, 3, 0x1808, ea, eb);
			S.n_sbad++;
			return;
		}
		const u32 da[] = { A->prim, A->batch_first, A->batch_count, A->n_inval };
		const u32 db[] = { B->prim, B->batch_first, B->batch_count, B->n_inval };
		for (u32 i = 0; i < 4; i++)
			if (da[i] != db[i])
			{
				spu_report(0, 3, 0xD000 + i, da[i], db[i]);
				bad++;
			}
		B->n_inval = A->n_inval;
		bad += rd_compare(A, B, spu_report, 0);
		S.n_sbad += bad != 0;
	}
}

// ---- TR SORT: draw_tr_tris on the SPU ----

// run_offs of ps3_rend.cpp: the run's vertices carry their offset colours.
static u32 tris_offs(const u32* w)
{
	if (!S.tin.offs)
		return 0;
	const u32 tex = (w[0] >> 3) & 1, off = (w[0] >> 2) & 1, fmt = (w[3] >> 27) & 7, fogc = (w[2] >> 22) & 3;
	if ((S.sc.flags & RSXD_S_BUMP) && tex && fmt == 4)
		return 1;
	if (S.sc.fog >= 2 && tex && off && fogc == 1 && fmt != 4)
		return 1;
	return (S.sc.flags & RSXD_S_OFFS) && tex && off && fmt != 4;
}

bool ps3rsxd_tris_begin(const RsxdTrisIn* in)
{
	S.tris_on = false;
	if ((S.mode != RSXD_TRIS && S.mode != RSXD_TRIS_CHECK) || !spu_ready() || ring_room() < JOB_MIN)
		return false;
	S.tin = *in;
	S.t_tris0 = gettime();
	S.ntstrips = S.ntparams = S.ntex = S.ntvtx = 0;
	if (++S.pgen_cur == 0)
	{
		memset(S.pgen, 0, PMAP_N * 2);
		S.pgen_cur = 1;
	}
	S.tris_big = false;
	// TR SORT CHECK: the PPU path draws and takes the RSX itself.
	sec_open(S.mode == RSXD_TRIS);
	// FRAME: what the SPU's draws need from the frame goes before its words.
	if (S.fr_own && S.mode == RSXD_TRIS)
	{
		gcmContextData* c = rctx();
		RsxdState& st = S.st;
		st.op = c->current;
		st.oend = c->end;
		fr_pre_draw(true);
		c->current = st.op;
	}
	S.tris_on = true;
	return true;
}

bool ps3rsxd_tri_strip(u32 first, u32 count, const void* param, u32 slot)
{
	if (S.tris_big)
		return false;
	if (S.ntstrips == RSXD_TRI_STRIPS || S.ntvtx + count > RSXD_TRI_VTX || slot >= PMAP_N)
	{
		S.tris_big = true;
		return false;
	}
	u32 p;
	if (S.pgen[slot] == S.pgen_cur)
		p = S.pmap[slot];
	else
	{
		if (S.ntparams == RSXD_TRI_PARAMS)
		{
			S.tris_big = true;
			return false;
		}
		p = S.ntparams++;
		RsxdTriParam& tp = S.tparams[p];
		const u32* w = (const u32*)param;
		memcpy(tp.w, w, 16);
		tp.tex = RSXD_NONE;
		if ((w[0] >> 3) & 1)
		{
			RsxdTex t;
			const u64 t0 = gettime();
			if (ps3tex_bind_rsxd(w[2], w[3], slot, false, &t))
			{
				u32 k = 0, due = 0;
				while (k < S.ntex && memcmp(&S.texs[k], &t, 40))
					k++;
				// One cache clear per texture: entries of one texture share the first one's flag (pad[1]).
				while (due < S.ntex && S.texs[due].obj != t.obj)
					due++;
				if (k == S.ntex)
				{
					if (k == RSXD_TRI_TEX)
					{
						S.tris_big = true;
						return false;
					}
					t.pad[0] = tobj_dirty(t.obj);
					t.pad[1] = due < S.ntex ? due : k;
					S.texs[S.ntex++] = t;
				}
				tp.tex = (u16)k;
			}
			*S.rr.t_tex += gettime() - t0;
		}
		tp.offs = (u16)tris_offs(w);
		tp.clip.mode = RXC_BASE;
		if (S.sc.clip)
			clip_of(w, &tp.clip);
		tp.pad[0] = tp.pad[1] = 0;
		S.pmap[slot] = (u16)p;
		S.pgen[slot] = S.pgen_cur;
	}
	RsxdTriStrip& ts = S.tstrips[S.ntstrips++];
	ts.first = first;
	ts.count = (u16)count;
	ts.p = (u16)p;
	S.ntvtx += count;
	return true;
}

bool ps3rsxd_tris_end()
{
	if (!S.tris_on)
		return false;
	S.tris_on = false;
	const bool check = S.mode == RSXD_TRIS_CHECK;
	if (!S.ntstrips && !S.tris_big)
	{
		// No TR triangle in the pass.
		if (!check)
			sec_release();
		return false;
	}
	bool ok = !S.tris_big;
	if (ok)
	{
		RsxdJob* j = S.job;
		RsxdSec sec = S.sec;
		S.sec.vb_off = S.host_io + 2 * RING_BYTES + S.ring_half * TRVB_BYTES + S.trvb_pos * RSXD_TRI_STRIDE;
		S.sec.vb_loc = GCM_LOCATION_CELL;
		job_fill(j, RSXD_JOB_TRIS, check);
		S.sec = sec;
		j->strips_ea = ea32(S.tstrips);
		j->nstrips = S.ntstrips;
		j->vtx_ea = ea32(S.tin.vtx);
		j->offs_ea = ea32(S.tin.offs);
		j->params_ea = ea32(S.tparams);
		j->nparams = S.ntparams;
		j->tex_ea = ea32(S.texs);
		j->ntex = S.ntex;
		j->tri_sort = S.tin.tri_sort;
		j->first_pass = S.tin.first_pass;
		j->vb_room = S.tin.vb_room;
		j->key_pcw = S.tin.key_pcw;
		j->key_isp = S.tin.key_isp;
		j->vb_ea = ea32(trvb_base()) + S.trvb_pos * RSXD_TRI_STRIDE;
		j->vb_io = j->sec.vb_off;
		j->vb_cap = TRVB_BYTES / RSXD_TRI_STRIDE - S.trvb_pos;
		ok = job_start(j);
		if (ok && check)
		{
			S.tris_check = true;
			S.tris_fifo = rctx()->current;
			return false;
		}
		ok = ok && job_wait(j->seq);
		if (ok && S.done->status != RSXD_DONE_OK)
		{
			S.n_tbig += S.done->status == RSXD_DONE_BIG;
			ok = false;
		}
	}
	else if (S.tris_big)
		S.n_tbig++;
	if (!ok)
	{
		S.n_tppu++;
		if (!check)
			sec_release();
		return false;
	}
	const RsxdDone& d = *S.done;
	S.trvb_pos += d.n_vb;
	S.n_tjobs++;
	job_take();
	for (u32 i = 0; i < S.ntex; i++)
		if ((d.cleared[i >> 5] >> (i & 31)) & 1)
			tobj_clean(S.texs[i].obj);
	*S.tin.strips += S.ntstrips;
	*S.tin.tris += d.n_tris;
	*S.tin.draws += d.n_runs;
	*S.tin.fp_runs += d.n_fp_runs;
	*S.tin.fp_nodepth += d.n_fp_nodepth;
	sec_release();
	*S.tin.t_sort += gettime() - S.t_tris0;
	return true;
}

// A pointer to vertex attribute word addr's memory: RSX memory in ps3gl's vring, or IO-mapped host memory.
static const u8* attr_ptr(u32 addr)
{
	const u32 off = addr & 0x7FFFFFFFu;
	if (addr >> 31)
		return (const u8*)io_addr(0, off);
	if (off < ps3gl.vring.base_off || off - ps3gl.vring.base_off >= PS3GL_VRING_SIZE)
		return 0;
	return ps3gl.vring.base + (off - ps3gl.vring.base_off);
}

// The registers both TR sort paths set at a draw: the emitter's, but the vertex attributes (other block and stride).
static bool tris_dont_care(const RdRegs* a, u32 m)
{
	const u32* r = a->reg;
	if (m >= 0x1680 && m < 0x1780)
		return true;
	if (m >= 0x0314 && m <= 0x0320 && !r[0x0310 >> 2])
		return true;
	if ((m == 0x0308 || m == 0x030c) && !r[0x0304 >> 2])
		return true;
	if (m >= 0x032c && m <= 0x0344 && !r[0x0328 >> 2])
		return true;
	if ((m == 0x1830 || m == 0x1834) && !r[0x183c >> 2])
		return true;
	if (m == 0x0a6c && !r[0x0a74 >> 2])
		return true;
	return ((m >= 0x1a00 && m < 0x1a20 && m != 0x1a0c) || m == 0x1840) && !(r[0x1a0c >> 2] >> 31);
}

void ps3rsxd_tris_check()
{
	if (!S.tris_check)
		return;
	S.tris_check = false;
	if (!job_wait(S.job->seq))
		return;
	const RsxdDone& d = *S.done;
	if (d.status != RSXD_DONE_OK)
	{
		S.n_tbig += d.status == RSXD_DONE_BIG;
		return;
	}
	if (!check_bufs())
		return;
	RdRegs* A = S.P;
	RdRegs* B = S.E;
	rd_reset(A);
	rd_reset(B);
	const u32* pa = S.tris_fifo;
	const u32* pb = S.spu_words;
	const u32* const pe = S.spu_words + (d.words ? d.words - 1 : 0);
	const u32* const stop = rctx()->current;
	RdCall cs = { 0, 0 };
	for (;;)
	{
		const int ea = rd_fifo(A, &pa, stop, io_addr, 0, &cs, 1u << 22, 1);
		const int eb = rd_stream(B, &pb, pe, 1);
		if (A->err || B->err)
		{
			S.n_slost++;
			return;
		}
		if (!ea && !eb)
			return;
		S.n_scheck++;
		if (ea != eb)
		{
			spu_report(0, 3, 0x1808, ea, eb);
			S.n_sbad++;
			return;
		}
		u32 bad = 0;
		const u32 da[] = { A->prim, A->batch_count, A->n_inval };
		const u32 db[] = { B->prim, B->batch_count, B->n_inval };
		for (u32 i = 0; i < 3; i++)
			if (da[i] != db[i])
			{
				spu_report(0, 3, 0xD000 + i, da[i], db[i]);
				bad++;
			}
		B->n_inval = A->n_inval;
		for (u32 i = 0; i < RX_N; i++)
		{
			const u32 m = rx_method[i], k = m >> 2;
			if (!tris_dont_care(A, m) && A->reg[k] != B->reg[k])
			{
				spu_report(0, 0, m, A->reg[k], B->reg[k]);
				bad++;
			}
		}
		static const u16 k_more[] = { 0x08e4, 0x1d60, 0x1ea0, 0x1ff0, 0x1ff4 };
		for (u32 i = 0; i < sizeof(k_more) / 2; i++)
			if (A->reg[k_more[i] >> 2] != B->reg[k_more[i] >> 2])
			{
				spu_report(0, 0, k_more[i], A->reg[k_more[i] >> 2], B->reg[k_more[i] >> 2]);
				bad++;
			}
		if (memcmp(A->vpc, B->vpc, 16 * 4))
		{
			spu_report(0, 2, 0, A->vpc[0], B->vpc[0]);
			bad++;
		}
		// The vertices each draw reads: 36 bytes of each, from the vring at 36 and from the host block at 48.
		const u8* va = attr_ptr(A->reg[0x1680 >> 2]);
		const u8* vb = attr_ptr(B->reg[0x1680 >> 2]);
		const u32 sa = (A->reg[0x1740 >> 2] >> 8) & 0xFF, sb = (B->reg[0x1740 >> 2] >> 8) & 0xFF;
		if (!va || !vb)
			bad++;
		else
			for (u32 v = 0; v < A->batch_count; v++)
				if (memcmp(va + (size_t)(A->batch_first + v) * sa, vb + (size_t)(B->batch_first + v) * sb, 36))
				{
					spu_report(0, 3, 0xE000 + v, A->batch_first + v, B->batch_first + v);
					bad++;
					break;
				}
		rd_clean(A);
		rd_clean(B);
		S.n_sbad += bad != 0;
	}
}

void ps3rsxd_stats(double secs)
{
	if (!S.n_scene && S.frame_mode == RSXD_OFF)
		return;
	static const char* const k_mode[] = { "OFF", "PPU", "CHECK", "SPE", "SPE CHECK", "TR SORT", "TR SORT CHECK",
	                                      "+ FRAME", "FRAME CHECK", "+ VOLUMES", "VOLUMES CHECK", "+ FOG", "FOG CHECK",
	                                      "+ SCREEN", "SCREEN CHECK", "+ TEX", "TEX CHECK", "+ RING", "RING CHECK" };
	const u32 d = S.n_draw ? S.n_draw : 1;
	printf("[rsxd] mode=%s ok=%u | scenes=%u sections=%u draws=%u (%.0f/s) vtx=%u words/draw=%.1f tex=%u inval=%u"
	       " drop=%u | check draws=%u bad=%u alpha=%u gl=%u lost=%u\n",
	       k_mode[S.row], (u32)S.ok, S.n_scene, S.n_sec, S.n_draw, secs > 0 ? S.n_draw / secs : 0.0, S.n_vtx,
	       (double)S.n_words / d, S.n_tex, S.n_inval, S.n_drop, S.n_check, S.n_bad, S.n_alpha, S.n_glbad, S.n_lost);
	if (S.row >= RSXD_FRAME)
		printf("[rsxd] frame ok=%u frames=%u lends=%u takes=%u clears=%u vols=%u fog=%u quads=%u | check clears=%u "
		       "gl=%u\n", (u32)S.fr_ok, S.n_frames, S.n_lends, S.n_takes, S.n_clears, S.n_vols, S.n_fog, S.n_quads,
		       S.n_fclear, S.n_fgl);
	S.n_frames = S.n_lends = S.n_takes = S.n_clears = S.n_vols = S.n_fog = S.n_quads = S.n_fclear = S.n_fgl = 0;
	if (S.row >= RSXD_TEX || S.n_tswitch)
	{
		u32 live = 0;
		for (u32 i = 0; i < RT_MAX; i++)
			live += RT.t[i].used;
		printf("[rsxd] tex mode=%u entries=%u | allocs=%u frees=%u fails=%u levels=%u MB=%.1f over=%u | proxy=%u switches=%u"
		       " | check binds=%u levels=%u bad=%u miss=%u\n", S.tm, live, RT.n_alloc, RT.n_free, RT.n_fail, RT.n_up,
		       (double)RT.n_bytes / 1048576.0, RT.n_over, S.n_tproxy, S.n_tswitch, S.n_tbind, S.n_tlevel, S.n_tbad,
		       S.n_tmiss);
	}
	RT.n_alloc = RT.n_free = RT.n_fail = RT.n_up = RT.n_over = 0;
	RT.n_bytes = 0;
	S.n_tproxy = S.n_tswitch = S.n_tbind = S.n_tlevel = S.n_tbad = S.n_tmiss = 0;
	if (S.row >= RSXD_RING || S.n_rframes)
		printf("[rsxd] ring ok=%u frames=%u peak=%u KB drops=%u wait %.2f ms timeouts=%u | handoffs=%u | check fp=%u\n",
		       (u32)S.ring_ok, S.n_rframes, S.rpeak >> 10, S.n_rdrop, (double)S.t_rwait * 1000.0 / (double)ps3_timebase_hz,
		       S.n_rtimeout, S.n_handoff, S.n_fpown);
	S.n_rframes = S.n_rdrop = S.n_rtimeout = S.n_handoff = S.n_fpown = S.rpeak = 0;
	S.t_rwait = 0;
	if (S.frame_mode >= RSXD_SPE)
		printf("[rsxd] spu up=%u | jobs=%u words=%u draws=%u ppu=%u late=%u wait %.2f ms | tris jobs=%u ppu=%u big=%u"
		       " | spu check draws=%u bad=%u lost=%u\n",
		       (u32)(!S.spu_dead && ps3spetex_mode() != SPETEX_OFF), S.n_jobs, S.n_jwords, S.n_jdraw, S.n_fall, S.n_late,
		       (double)S.t_wait * 1000.0 / (double)ps3_timebase_hz, S.n_tjobs, S.n_tppu, S.n_tbig, S.n_scheck, S.n_sbad,
		       S.n_slost);
	S.n_scene = S.n_sec = S.n_draw = S.n_vtx = S.n_tex = S.n_inval = S.n_drop = 0;
	S.n_words = 0;
	S.n_check = S.n_bad = S.n_alpha = S.n_glbad = S.n_lost = 0;
	S.n_jobs = S.n_jwords = S.n_jdraw = S.n_fall = S.n_late = S.n_tjobs = S.n_tppu = S.n_tbig = 0;
	S.n_scheck = S.n_sbad = S.n_slost = 0;
	S.t_wait = 0;
}

// ---- the frame (plan 9, stage A) ----

// The emitter's frame state into ps3gl, which draws next: as after ps3gl_begin_frame, it sends all at its next draw.
static void fr_write()
{
	ps3gl_state_t& q = ps3gl;
	const RsxdGl& g = S.st.gl;
	const Rg& r = S.rg;
	gl_to_ps3gl(&g);
	q.rs.alpha_func = g.alpha_func;
	q.rs.alpha_ref = g.alpha_ref;
	q.rs.front_face = g.front_face;
	q.rs.color_mask_r = (g.color_mask & 1) != 0;
	q.rs.color_mask_g = (g.color_mask & 2) != 0;
	q.rs.color_mask_b = (g.color_mask & 4) != 0;
	q.rs.color_mask_a = (g.color_mask & 8) != 0;
	q.rs.stencil_enable = (int)g.st_en;
	q.rs.stencil_fail = g.st_fail;
	q.rs.stencil_zfail = g.st_zfail;
	q.rs.stencil_zpass = g.st_zpass;
	q.rs.stencil_writemask = g.st_wmask;
	q.rs.vp_x = (int16_t)r.vp_x;
	q.rs.vp_y = (int16_t)r.vp_y;
	q.rs.vp_w = (uint16_t)r.vp_w;
	q.rs.vp_h = (uint16_t)r.vp_h;
	q.rs.depth_near = r.zn;
	q.rs.depth_far = r.zf;
	q.rs.polyoffset_fill = r.poly_on;
	q.rs.polyoffset_factor = r.poly_factor;
	q.rs.polyoffset_units = r.poly_units;
	q.rs.clear_color = r.clear_color;
	q.rs.clear_depth = r.clear_depth;
	q.rs.clear_stencil = r.clear_stencil;
	q.screen_w = r.sw;
	q.screen_h = r.sh;
	q.matrix_mode = r.mmode;
	memcpy(q.proj.stack[q.proj.depth], r.proj, sizeof(r.proj));
	memcpy(q.mv.stack[q.mv.depth], r.mv, sizeof(r.mv));
	q.proj.dirty = q.mv.dirty = 1;
	q.dc_fog_scale = r.fog_scale;
	q.active_tmu = (int)r.tmu;
	// FOG: ps3gl draws on with the emitter's fog table (a lend in the scene: the fallback draws).
	if (S.fog)
	{
		ps3gl_tmu_t& u1 = q.tmu[1];
		u1.bound = r.u1_obj < PS3GL_MAX_TEXTURES ? &q.textures[r.u1_obj]
		         : tobj_mine(r.u1_obj) && S.proxy_ok ? t_carry(1, &RT.t[r.u1_obj & 0xFFFFu]) : NULL;
		u1.enabled = u1.bound != NULL;
		u1.aux = u1.enabled;
	}
	q.imm.color = r.color;
	memcpy(&q.imm.u0, &r.u0, 4);
	memcpy(&q.imm.v0, &r.v0, 4);
	memcpy(&q.imm.u1, &r.u1, 4);
	memcpy(&q.imm.v1, &r.v1, 4);
}

// FRAME CHECK at a lend: the emitter's frame state against ps3gl's.
static void fr_check_gl()
{
	u32 ev = 0, gv = 0, d = gl_diff(&S.st.gl, &ev, &gv, false);
	if (!d)
	{
		Rg p;
		rg_read(&p);
		const Rg& r = S.rg;
		const u32 a[] = { (u32)r.vp_x, (u32)r.vp_y, (u32)r.vp_w, (u32)r.vp_h, fbits(r.zn), fbits(r.zf), (u32)r.poly_on,
		                  r.clear_color, fbits(r.clear_depth), r.clear_stencil, r.sw, r.sh, r.mmode, r.tmu,
		                  fbits(r.fog_scale), r.color, r.u0, r.v0, r.u1, r.v1 };
		const u32 b[] = { (u32)p.vp_x, (u32)p.vp_y, (u32)p.vp_w, (u32)p.vp_h, fbits(p.zn), fbits(p.zf), (u32)p.poly_on,
		                  p.clear_color, fbits(p.clear_depth), p.clear_stencil, p.sw, p.sh, p.mmode, p.tmu,
		                  fbits(p.fog_scale), p.color, p.u0, p.v0, p.u1, p.v1 };
		for (u32 k = 0; k < sizeof(a) / 4 && !d; k++)
			if (a[k] != b[k])
			{
				d = 0x100000u | k;
				ev = a[k];
				gv = b[k];
			}
		if (!d && (memcmp(r.proj, p.proj, sizeof(p.proj)) || memcmp(r.mv, p.mv, sizeof(p.mv))))
			d = 0x200000u;
	}
	if (!d)
		return;
	S.n_fgl++;
	if (S.lines++ < CHECK_LINES)
		printf("[rsxd] check MISMATCH frame field %X emitter %X ps3gl %X at a lend\n", d, ev, gv);
}

// ps3gl's state into the emitter's, which has the frame from here; the RSX holds nothing the emitter knows.
static void fr_take()
{
	// + TEX: the cache clears ps3gl did not take go back to the entries the proxies carried.
	t_back(0);
	t_back(1);
	RsxdState& st = S.st;
	st.sc = &S.sc;
	st.sec = &S.sec;
	st.prog = S.pg;
	gl_from_ps3gl(&st.gl);
	rg_from_ps3gl();
	st.gl.clip_base = 1;
	if (S.rr.clip_base)
	{
		st.gl.clip_base = *S.rr.clip_base;
		st.gl.clip_mode = *S.rr.clip_mode;
		memcpy(st.gl.clip_r, S.rr.clip_r, sizeof(st.gl.clip_r));
		st.gl.flat_shift = *S.rr.flat_shift;
	}
	const ps3gl_tmu_t& u1 = ps3gl.tmu[1];
	S.sc.flags = (S.sc.flags & ~RSXD_S_TMU1) | ((u1.enabled && !u1.aux && u1.bound && u1.bound->data) ? RSXD_S_TMU1 : 0);
	rsxd_forget(&st);
	S.fr_own = true;
	if (S.fr == FR_LIVE)
	{
		st.check = 0;
		return;
	}
	st.check = 1;
	st.op = S.scratch;
	st.oend = S.scratch + SCRATCH_WORDS;
	if (S.p_ok)
	{
		rd_fifo(S.P, &S.ppos, rctx()->current, io_addr, 0, &S.pcs, 1u << 21, 0);
		if (S.P->err)
		{
			S.p_ok = false;
			S.n_lost++;
		}
	}
	rd_clean(S.P);
	rd_copy(S.E, S.P);
	S.virt_clear = 0xFFFFFFFFu;
}

bool ps3rsxd_lend()
{
	if (!S.fr_own)
		return false;
	RsxdState& st = S.st;
	if (S.fr == FR_LIVE)
	{
		t_handoff();
		// A unit 1 cache clear no draw took goes before ps3gl's draws.
		if (st.inval_due)
		{
			fr_op_begin();
			if (rsxd_reserve(&st, 2))
			{
				st.op[0] = RX_HDR(RXM_TEX_CACHE, 1);
				st.op[1] = 1;
				st.op += 2;
				S.n_inval++;
			}
			fr_op_end();
		}
		fr_write();
	}
	else
	{
		fr_check_gl();
		st.op = S.scratch;
	}
	st.inval_due = 0;
	S.fr_own = false;
	S.n_lends++;
	return true;
}

void ps3rsxd_take(bool was)
{
	if (!was || S.fr_own || S.fr == FR_OFF)
		return;
	fr_take();
	S.n_takes++;
}

bool ps3rsxd_owns()
{
	return S.fr_own;
}

bool ps3rsxd_volumes()
{
	return S.fr_own && S.vols;
}

bool ps3rsxd_fog_live()
{
	return S.fr_own && S.fog && S.fr == FR_LIVE;
}

void ps3rsxd_fog_table(u32 name)
{
	if (!S.fr_own || !S.fog)
		return;
	S.rg.u1_stale = true;
	if (t_name(name))
	{
		S.rg.u1_obj = T_OBJ | (name & ~T_NAME);
		S.n_fog++;
		return;
	}
	const ps3gl_texture_t* const t = name ? ps3gl_texture_find(name) : NULL;
	S.rg.u1_obj = t ? (u32)(t - ps3gl.textures) : 0xFFFFFFFFu;
	S.n_fog += t != NULL;
	if (t && S.tm == TM_PAIR)
	{
		RsxdTex o;
		tex_obj(t, &o);
		pair_words(t, &o);
	}
}

bool ps3rsxd_quads()
{
	return S.fr_own && S.screen;
}

bool ps3rsxd_own_frame(u32 kind)
{
	const int row = g_rsx_direct_preset;
	if (kind != RSXD_F_GAME)
		return S.fr_ok && (row == RSXD_SCREEN || row == RSXD_TEX || row == RSXD_RING);
	return S.fr_ok && (row == RSXD_FRAME || row == RSXD_VOLS || row == RSXD_FOG || row == RSXD_SCREEN || row == RSXD_TEX ||
	                   row == RSXD_RING);
}

void ps3rsxd_screen(u32 w, u32 h)
{
	if (!S.fr_own)
		return;
	S.rg.sw = w;
	S.rg.sh = h;
	S.sc.screen_w = w;
	S.sc.screen_h = h;
}

// FRAME's frame begin: ps3gl_begin_frame's vring segment swap and fence wait, then the rest of it. RING leaves ps3gl
// alone up to the frame's first lend; ps3rsxd_frame takes the emitter's ring and its fence.
extern "C" void ps3rsxd_vring_begin(void)
{
	if (!ps3gl_ptr)
		return;
	S.vb_row = (u32)g_rsx_direct_preset;
	S.vb_set = true;
	S.gl_off = S.vb_row == RSXD_RING && S.ok && S.ring_ok && ring_mem();
	if (S.gl_off)
		return;
	ps3gl_vring_t& v = ps3gl.vring;
	v.cur_seg = (v.cur_seg + 1) % PS3GL_VRING_SEGMENTS;
	const int seg = v.cur_seg;
	volatile uint32_t* const label = v.fence_label[seg];
	const u64 t0 = gettime();
	if (label)
	{
		int waited = 0;
		while (*label != v.fence_val[seg] && waited < 20000)
		{
			usleep(10);
			waited++;
		}
		if (*label != v.fence_val[seg])
			printf("[rsxd] WARNING: vring segment %d fence timed out (val=%u label=%u)\n", seg, (u32)v.fence_val[seg],
			       (u32)*label);
	}
	ps3gl_frame_fence_wait_time += (double)(gettime() - t0) / (double)ps3_timebase_hz;
	ps3gl_begin_frame_state();
}

// Its end: ps3gl_end_frame up to the label, then this segment's label. RING: ps3gl's part (from the handoff) ends as
// ps3gl_end_frame, then the ring's label.
extern "C" void ps3rsxd_vring_end(void)
{
	ps3rsxd_lend();
	if (S.rused)
	{
		t_handoff();
		ps3gl_end_frame();
		ring_end();
		return;
	}
	ps3gl_end_frame_stats();
	gcmContextData* const ctx = ps3gl_get_ctx();
	if (!ctx)
		return;
	ps3gl_vring_t& v = ps3gl.vring;
	const int seg = v.cur_seg;
	if (v.fence_label[seg])
	{
		v.fence_val[seg]++;
		rsxSetWriteBackendLabel(ctx, seg == 0 ? PS3GL_LABEL_VRING_SEG0 : PS3GL_LABEL_VRING_SEG1, v.fence_val[seg]);
	}
}

// ---- rgl ----

// ps3gl gets the call too: the emitter does not have the frame, or FRAME CHECK.
static inline bool fr_gl()
{
	return !S.fr_own || S.fr == FR_CHECK;
}

// ps3gl_states.c's conversions.
static inline u32 rg_cmp(u32 f)
{
	return f >= GL_NEVER && f <= GL_ALWAYS ? f : GCM_ALWAYS;
}

static inline u32 rg_sop(u32 op)
{
	return op == GL_KEEP || op == GL_ZERO || op == GL_REPLACE || op == GL_INCR || op == GL_DECR || op == GL_INVERT ? op
	                                                                                                          : GCM_KEEP;
}

static inline u32 rg_blend(u32 f)
{
	return f == GL_ZERO || f == GL_ONE || (f >= GL_SRC_COLOR && f <= GL_SRC_ALPHA_SATURATE) ? f : GCM_ONE;
}

// The caps the emitter keeps; GL_TEXTURE_2D only on unit 0 (unit 1 is ps3gl's until stage C).
static bool fr_cap(u32 cap, u32 on)
{
	RsxdGl& g = S.st.gl;
	switch (cap)
	{
	case GL_BLEND: g.blend_en = on; return true;
	case GL_ALPHA_TEST: g.alpha_en = on; return true;
	case GL_DEPTH_TEST: g.depth_test = on; return true;
	case GL_CULL_FACE: g.cull_en = on; return true;
	case GL_SCISSOR_TEST: g.scissor_en = on; return true;
	case GL_STENCIL_TEST: g.st_en = on; return true;
	case GL_POLYGON_OFFSET_FILL: S.rg.poly_on = (int)on; return true;
	case GL_TEXTURE_2D:
		if (S.rg.tmu)
			return false;
		g.tex_en = on;
		return true;
	default: return false;
	}
}

static void rgl_cap(u32 cap, u32 on)
{
	const bool mine = S.fr_own && fr_cap(cap, on);
	if (!mine || S.fr == FR_CHECK)
	{
		if (on)
			glEnable(cap);
		else
			glDisable(cap);
	}
}

void rglEnable(u32 cap)
{
	rgl_cap(cap, 1);
}

void rglDisable(u32 cap)
{
	rgl_cap(cap, 0);
}

void rglColorMask(u8 r, u8 g, u8 b, u8 a)
{
	if (S.fr_own)
		S.st.gl.color_mask = (r ? 1u : 0) | (g ? 2u : 0) | (b ? 4u : 0) | (a ? 8u : 0);
	if (fr_gl())
		glColorMask(r, g, b, a);
}

void rglDepthMask(u8 flag)
{
	if (S.fr_own)
		S.st.gl.depth_mask = flag ? 1 : 0;
	if (fr_gl())
		glDepthMask(flag);
}

void rglAlphaFunc(u32 func, float ref)
{
	if (S.fr_own)
	{
		u32 r = (u32)(ref * 255.0f + 0.5f);
		if (r > 255)
			r = 255;
		S.st.gl.alpha_func = rg_cmp(func);
		S.st.gl.alpha_ref = r;
	}
	if (fr_gl())
		glAlphaFunc(func, ref);
}

void rglShadeModel(u32 mode)
{
	if (S.fr_own)
		S.st.gl.shade = mode == GL_FLAT ? GCM_SHADE_MODEL_FLAT : GCM_SHADE_MODEL_SMOOTH;
	if (fr_gl())
		glShadeModel(mode);
}

void rglFrontFace(u32 mode)
{
	if (S.fr_own)
		S.st.gl.front_face = mode == GL_CW ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW;
	if (fr_gl())
		glFrontFace(mode);
}

void rglStencilMask(u32 mask)
{
	if (S.fr_own)
		S.st.gl.st_wmask = mask;
	if (fr_gl())
		glStencilMask(mask);
}

void rglStencilOp(u32 fail, u32 zfail, u32 zpass)
{
	if (S.fr_own)
	{
		RsxdGl& g = S.st.gl;
		g.st_fail = rg_sop(fail);
		g.st_zfail = rg_sop(zfail);
		g.st_zpass = rg_sop(zpass);
	}
	if (fr_gl())
		glStencilOp(fail, zfail, zpass);
}

void rglStencilFunc(u32 func, int ref, u32 mask)
{
	if (S.fr_own)
	{
		RsxdGl& g = S.st.gl;
		g.st_func = rg_cmp(func);
		g.st_ref = (u32)ref;
		g.st_mask = mask;
	}
	if (fr_gl())
		glStencilFunc(func, ref, mask);
}

void rglDepthFunc(u32 func)
{
	if (S.fr_own)
		S.st.gl.depth_func = rg_cmp(func);
	if (fr_gl())
		glDepthFunc(func);
}

void rglBlendFunc(u32 src, u32 dst)
{
	if (S.fr_own)
	{
		S.st.gl.blend_src = rg_blend(src);
		S.st.gl.blend_dst = rg_blend(dst);
	}
	if (fr_gl())
		glBlendFunc(src, dst);
}

// glCullFace keeps its face for another mode.
void rglCullFace(u32 mode)
{
	if (S.fr_own && (mode == GL_FRONT || mode == GL_BACK || mode == GL_FRONT_AND_BACK))
		S.st.gl.cull_face = mode;
	if (fr_gl())
		glCullFace(mode);
}

// glTexEnvf's modes (ps3gl's PS3GL_TENV_*, the emitter's RXE_*); unit 1's is ps3gl's.
void rglTexEnvMode(u32 mode)
{
	const bool mine = S.fr_own && !S.rg.tmu;
	if (mine)
		S.st.gl.texenv = mode == GL_REPLACE ? PS3GL_TENV_REPLACE : mode == GL_DECAL ? PS3GL_TENV_DECAL
		               : mode == GL_ADD ? PS3GL_TENV_ADD : PS3GL_TENV_MODULATE;
	if (!mine || S.fr == FR_CHECK)
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)mode);
}

// glScissor and glViewport keep the RSX y of the surface in use, with their int16 and uint16 casts.
void rglScissor(int x, int y, int w, int h)
{
	if (S.fr_own)
	{
		RsxdGl& g = S.st.gl;
		g.sc_x = (int16_t)x;
		g.sc_y = (int16_t)((int)S.rg.sh - y - h);
		g.sc_w = (uint16_t)w;
		g.sc_h = (uint16_t)h;
	}
	if (fr_gl())
		glScissor(x, y, w, h);
}

void rglViewport(int x, int y, int w, int h)
{
	if (S.fr_own)
	{
		Rg& r = S.rg;
		r.vp_x = (int16_t)x;
		r.vp_y = (int16_t)((int)r.sh - y - h);
		r.vp_w = (uint16_t)w;
		r.vp_h = (uint16_t)h;
	}
	if (fr_gl())
		glViewport(x, y, w, h);
}

void rglDepthRange(double n, double f)
{
	if (S.fr_own)
	{
		S.rg.zn = (float)n;
		S.rg.zf = (float)f;
	}
	if (fr_gl())
		glDepthRange(n, f);
}

void rglClearColor(float r, float g, float b, float a)
{
	if (S.fr_own)
	{
		const u32 ri = (u32)(r * 255.0f), gi = (u32)(g * 255.0f), bi = (u32)(b * 255.0f), ai = (u32)(a * 255.0f);
		S.rg.clear_color = (ai << 24) | (ri << 16) | (gi << 8) | bi;
	}
	if (fr_gl())
		glClearColor(r, g, b, a);
}

void rglClearDepth(double d)
{
	if (S.fr_own)
		S.rg.clear_depth = (float)d;
	if (fr_gl())
		glClearDepth(d);
}

void rglClearStencil(int s)
{
	if (S.fr_own)
		S.rg.clear_stencil = (u32)s;
	if (fr_gl())
		glClearStencil(s);
}

void rglClear(u32 mask)
{
	if (S.fr_own)
		fr_clear(mask);
	if (!fr_gl())
		return;
	glClear(mask);
	if (S.fr_own)
		ps3rsxd_draw_check();
}

void rglApplyStates()
{
	if (S.fr_own)
		fr_apply();
	if (fr_gl())
		ps3gl_apply_states();
}

void rglMatrixMode(u32 mode)
{
	if (S.fr_own)
		S.rg.mmode = mode;
	if (fr_gl())
		glMatrixMode(mode);
}

static float* rg_mat()
{
	return S.rg.mmode == GL_PROJECTION ? S.rg.proj : S.rg.mv;
}

void rglLoadMatrixf(const float* m)
{
	if (S.fr_own)
	{
		memcpy(rg_mat(), m, 64);
		S.rg.mvp_new = true;
	}
	if (fr_gl())
		glLoadMatrixf(m);
}

void rglLoadIdentity()
{
	if (S.fr_own)
	{
		memcpy(rg_mat(), ps3gl_identity, 64);
		S.rg.mvp_new = true;
	}
	if (fr_gl())
		glLoadIdentity();
}

// Always ps3gl's too: its texture calls go to the unit (unit 1's state is ps3gl's until stage C).
void rglActiveTexture(u32 unit)
{
	if (S.fr_own)
	{
		const int t = (int)(unit - GL_TEXTURE0_ARB);
		if (t >= 0 && t < PS3GL_MAX_TMUS)
			S.rg.tmu = (u32)t;
		S.rg.u1_stale = true;
	}
	glActiveTextureARB(unit);
}

void rglTexBump(int on)
{
	const bool mine = S.fr_own && !S.rg.tmu;
	if (mine)
		S.st.gl.bump = on != 0;
	if (!mine || S.fr == FR_CHECK)
		ps3gl_tex_bump(on);
}

void rglTex1Bits(u32 bits)
{
	if (S.fr_own)
	{
		S.rg.u1 = bits;
		S.rg.v1 = 0;
	}
	if (fr_gl())
		ps3gl_tex1_bits(bits);
}

void rglDcFog(int mode)
{
	if (S.fr_own)
		S.st.gl.fog = (u32)mode;
	if (fr_gl())
		ps3gl_dc_fog(mode);
}

void rglDcFogScale(float scale)
{
	if (S.fr_own)
	{
		S.rg.fog_scale = scale;
		S.sc.fog_scale = fbits(scale);
	}
	if (fr_gl())
		ps3gl_dc_fog_scale(scale);
}

void rglDcOffs(int on)
{
	if (S.fr_own)
		S.st.gl.offs = on != 0;
	if (fr_gl())
		ps3gl_dc_offs(on);
}

void rglDcXt(int on, const float* cmin, const float* cmax, float tril)
{
	if (S.fr_own)
	{
		RsxdGl& g = S.st.gl;
		g.xt = on != 0;
		if (on)
		{
			memcpy(&g.xt_v[0], cmin, 16);
			memcpy(&g.xt_v[4], cmax, 16);
			g.xt_v[8] = fbits(tril);
		}
	}
	if (fr_gl())
		ps3gl_dc_xt(on, cmin, cmax, tril);
}

void rglBegin(u32 prim)
{
	if (S.fr_own)
	{
		S.rg.prim = prim;
		S.rg.n = 0;
	}
	if (fr_gl())
		glBegin(prim);
}

void rglColor4ub(u8 r, u8 g, u8 b, u8 a)
{
	if (S.fr_own)
		S.rg.color = ps3gl_pack_color_ub(r, g, b, a);
	if (fr_gl())
		glColor4ub(r, g, b, a);
}

void rglTexCoord2f(float u, float v)
{
	if (S.fr_own)
	{
		S.rg.u0 = fbits(u);
		S.rg.v0 = fbits(v);
	}
	if (fr_gl())
		glTexCoord2f(u, v);
}

void rglVertex3f(float x, float y, float z)
{
	Rg& r = S.rg;
	// glVertex3f drops the vertices past PS3GL_MAX_VERTS.
	if (S.fr_own && r.n < PS3GL_MAX_VERTS)
	{
		u32* const v = S.vbuf + r.n++ * 9;
		v[0] = fbits(x);
		v[1] = fbits(y);
		v[2] = fbits(z);
		v[3] = RX_F_ONE;
		v[4] = r.u0;
		v[5] = r.v0;
		v[6] = r.u1;
		v[7] = r.v1;
		v[8] = r.color;
	}
	if (fr_gl())
		glVertex3f(x, y, z);
}

// glEnd's draw: the vertices in a block of the frame's ring, drawn by the emitter (FRAME CHECK: from the block ps3gl
// takes next in its vring, beside its draw).
void rglEnd()
{
	if (!S.fr_own)
	{
		glEnd();
		return;
	}
	Rg& r = S.rg;
	const u32 n = r.n, stride = (u32)sizeof(ps3gl_vertex_t);
	r.n = 0;
	const bool sec = S.sec_mode != RSXD_SEC_NONE;
	// A quad out of a section (stage D): no tile clip or colour shift; its texture is the one bound last (the table's
	// entry, or ps3gl's unit 0).
	if (!sec)
	{
		RsxdGl& g = S.st.gl;
		S.n_quads++;
		g.clip_mode = RXC_NONE;
		g.flat_shift = 0;
		if (!S.tgl && S.tcur)
			g.has_tex = tobj_fields(T_OBJ | (S.tcur - 1), &g.tex);
		else
		{
			const ps3gl_texture_t* const t = ps3gl.tmu[0].bound;
			if (t)
				tex_from_fields(t, &g.tex);
			else
				g.tex.obj = 0xFFFFFFFFu;
			g.has_tex = t && t->data;
		}
	}
	if (S.fr == FR_CHECK)
	{
		const ps3gl_vring_t& v = ps3gl.vring;
		if (n && v.base && v.head + n * stride <= v.seg_capacity)
			ps3rsxd_draw(r.prim, v.base_off + (u32)v.cur_seg * v.seg_capacity + v.head, stride, 0, n);
		glEnd();
		ps3rsxd_draw_check();
		return;
	}
	u32 off = 0;
	ps3gl_vertex_t* const b = n ? (ps3gl_vertex_t*)rglVringAlloc((int)n, &off) : NULL;
	if (!b)
		return;
	// One memcpy, as ps3gl's submit_ranges: RSX memory is cache-inhibited, and GCC may merge single stores.
	memcpy(b, S.vbuf, n * stride);
	if (!sec)
		fr_op_begin();
	ps3rsxd_draw(r.prim, off, stride, 0, n);
	if (!sec)
		fr_op_end();
}

// ---- textures (plan 9, stage E) ----

static inline RtTex* t_cur()
{
	return S.tcur ? &RT.t[S.tcur - 1] : 0;
}

// + TEX in a lent frame: ps3gl draws with the bound entry, which the proxy slot of its active unit carries.
static void t_proxy()
{
	const int u = ps3gl.active_tmu;
	RtTex* const t = t_cur();
	if (S.tm != TM_OWN || S.fr_own || !S.proxy_ok || !t || u < 0 || u > 1)
		return;
	ps3gl_tmu_t& m = ps3gl.tmu[u];
	m.bound = t_carry((u32)u, t);
	m.dirty = 1;
	S.n_tproxy++;
}

// At a frame's start no unit of ps3gl keeps a proxy, so the memory it carried can go.
static void t_unproxy()
{
	if (!S.proxy_ok)
		return;
	t_back(0);
	t_back(1);
	for (int u = 0; u < PS3GL_MAX_TMUS; u++)
	{
		ps3gl_tmu_t& m = ps3gl.tmu[u];
		if (m.bound == &ps3gl.textures[T_PROXY] || m.bound == &ps3gl.textures[T_PROXY + 1])
		{
			m.bound = 0;
			m.dirty = 1;
		}
	}
	ps3gl.textures[T_PROXY].data = 0;
	ps3gl.textures[T_PROXY + 1].data = 0;
}

// A new texture mode: the cache and ps3_rend.cpp give back the old mode's names, then any entry left goes.
static void t_switch(u32 tm)
{
	ps3tex_reset();
	rend_tex_reset();
	for (u32 i = 0; i < RT_MAX; i++)
		if (RT.t[i].used)
			rt_delete(i);
	S.tm = tm;
	S.tcur = 0;
	S.tgl = true;
	S.n_tswitch++;
	static const char* const k_tm[] = { "ps3gl's", "the emitter's table", "ps3gl's and a shadow of each (TEX CHECK)" };
	printf("[rsxd] textures: %s\n", k_tm[tm]);
}

// TEX CHECK: a level ps3gl and the shadow both wrote. The layout, then 32 texels across the level, read back (slow
// RSX reads; the first and the last are among them).
static void pair_level(const ps3gl_texture_t* g, const RtTex* s, int level)
{
	S.n_tlevel++;
	u32 what = 0, a = 0, b = 0;
	if (!g || (g->data != 0) != (s->data != 0))
		what = 1;
	else if (!g->data)
		return;
	else if (g->swizzled != s->swz || (g->alloc_levels ? g->alloc_levels : 1) != (s->alloc ? s->alloc : 1) ||
	         g->num_levels != s->levels || g->width != s->w || g->height != s->h)
	{
		what = 2;
		a = ((u32)g->swizzled << 28) | ((u32)g->alloc_levels << 24) | ((u32)g->num_levels << 20) | g->width;
		b = ((u32)s->swz << 28) | ((u32)s->alloc << 24) | ((u32)s->levels << 20) | s->w;
	}
	else if (level < (s->alloc ? s->alloc : 1))
	{
		const u32 w = rt_dim(s->w, level), n = w * rt_dim(s->h, level), wp = rt_pitch(s->w) / 4;
		const u32 off = rt_level_off(s->w, s->h, level, s->swz);
		const volatile u32* const pa = (const volatile u32*)(g->data + off);
		const volatile u32* const pb = (const volatile u32*)(s->data + off);
		for (u32 k = 0; k < 32 && !what; k++)
		{
			const u32 i = (u32)(((u64)(n - 1) * k) / 31);
			const u32 at = s->swz ? i : (i / w) * wp + i % w;
			a = pa[at];
			b = pb[at];
			what = a != b ? 3 : 0;
		}
	}
	if (!what)
		return;
	S.n_tbad++;
	if (S.lines++ < CHECK_LINES)
		printf("[rsxd] tex MISMATCH level %d %s ps3gl %08X table %08X | %ux%u\n", level,
		       what == 1 ? "memory" : what == 2 ? "layout" : "texel", a, b, s->w, s->h);
}

void rglGenTextures(u32* name)
{
	if (S.tm != TM_OWN)
	{
		glGenTextures(1, name);
		return;
	}
	const int i = rt_new(T_WRAP);
	*name = i < 0 ? 0 : T_NAME | (u32)i;
}

void rglDeleteTexture(u32 name)
{
	if (!name)
		return;
	const bool mine = t_name(name);
	u32 i = mine ? name & ~T_NAME : T_WRAP;
	if (!mine && S.tm == TM_PAIR)
		if (const ps3gl_texture_t* const t = ps3gl_texture_find(name))
			i = (u32)(t - ps3gl.textures);
	if (i < T_WRAP && RT.t[i].used && (mine || RT.t[i].name == name))
	{
		rt_delete(i);
		if (S.tcur == i + 1)
			S.tcur = 0;
	}
	if (!mine)
		glDeleteTextures(1, &name);
}

void rglBindTexture(u32 name)
{
	if (t_name(name))
	{
		S.tcur = (name & ~T_NAME) + 1;
		S.tgl = false;
		t_proxy();
		return;
	}
	glBindTexture(GL_TEXTURE_2D, name);
	S.tgl = true;
	S.tcur = 0;
	if (S.tm != TM_PAIR || !name)
		return;
	// TEX CHECK: the shadow of the slot, new when the slot holds another name now.
	const ps3gl_texture_t* const t = ps3gl.tmu[ps3gl.active_tmu].bound;
	const u32 i = t ? (u32)(t - ps3gl.textures) : T_WRAP;
	if (i >= T_WRAP)
		return;
	RtTex* const s = &RT.t[i];
	if (s->used && s->name != name)
		rt_delete(i);
	if (!s->used)
		rt_init(s, name);
	S.tcur = i + 1;
}

void rglTexParameteri(u32 pname, int v)
{
	if (S.tgl)
		glTexParameteri(GL_TEXTURE_2D, pname, v);
	if (RtTex* const t = t_cur())
	{
		rt_param(t, pname, v);
		t_proxy();
	}
}

void rglTexHint(u32 hint)
{
	if (S.tgl)
	{
		if (hint == RSXD_HINT_LINEAR)
			ps3gl_texture_hint_no_swizzle();
		else
			ps3gl_texture_hint_mipmaps();
	}
	if (RtTex* const t = t_cur())
	{
		if (hint == RSXD_HINT_LINEAR)
			t->nosw = 1;
		else
			t->mip = 1;
	}
}

void rglTexImage2D(int level, int w, int h, const void* rgba)
{
	if (S.tgl)
		glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	RtTex* const t = t_cur();
	if (!t)
		return;
	rt_image(t, level, (u32)w, (u32)h, rgba);
	if (S.tm == TM_PAIR)
		pair_level(ps3gl.tmu[ps3gl.active_tmu].bound, t, level);
	t_proxy();
}

// TEX CHECK: the direct writes (SPU decode, P42 YUV blocks) give no memory, so both take the same glTexImage2D words.
u32* rglTexDirectBegin(int w, int h, u32* pitch)
{
	if (S.tm == TM_PAIR)
		return 0;
	if (S.tgl)
		return ps3gl_tex_direct_begin(w, h, pitch);
	RtTex* const t = t_cur();
	return t ? rt_direct_begin(t, (u32)w, (u32)h, pitch) : 0;
}

void rglTexDirectEnd()
{
	if (S.tgl)
		ps3gl_tex_direct_end();
	else if (RtTex* const t = t_cur())
	{
		rt_direct_end(t);
		t_proxy();
	}
}

u8* rglTexDirectLevel0(int w, int h, u32* pitch, int* swz)
{
	if (S.tm == TM_PAIR)
		return 0;
	if (S.tgl)
		return ps3gl_tex_direct_level0(w, h, pitch, swz);
	RtTex* const t = t_cur();
	return t ? rt_direct_level0(t, (u32)w, (u32)h, pitch, swz) : 0;
}

u8* rglTexDirectLevel(int level)
{
	if (S.tm == TM_PAIR)
		return 0;
	if (S.tgl)
		return ps3gl_tex_direct_level(level);
	RtTex* const t = t_cur();
	return t ? rt_direct_level(t, level) : 0;
}

void rglTexDirectLevelEnd(int level)
{
	if (S.tgl)
		ps3gl_tex_direct_level_end(level);
	else if (RtTex* const t = t_cur())
	{
		rt_direct_level_end(t, level);
		t_proxy();
	}
}

int rglTexChainLevels()
{
	RtTex* const t = t_cur();
	if (!S.tgl)
		return t ? rt_chain_levels(t) : 0;
	const int n = ps3gl_tex_chain_levels();
	if (S.tm == TM_PAIR && t && rt_chain_levels(t) != n)
	{
		S.n_tbad++;
		if (S.lines++ < CHECK_LINES)
			printf("[rsxd] tex MISMATCH chain levels ps3gl %d table %d\n", n, rt_chain_levels(t));
	}
	return n;
}

void rglTexConvertCheck(void* dst, int w, int h, const void* rgba)
{
	if (S.tgl)
		ps3gl_tex_convert_check(dst, w, h, rgba);
	else if (RtTex* const t = t_cur())
		rt_convert_check(t, dst, (u32)w, (u32)h, rgba);
}

bool rglTexWrap(void* mem, u32 pitch, u32 w, u32 h)
{
	if (S.tm != TM_OWN || !mem)
	{
		S.tgl = true;
		S.tcur = 0;
		return false;
	}
	RtTex* const t = &RT.t[T_WRAP];
	if (!t->used)
		rt_init(t, 0);
	rt_wrap(t, (u8*)mem, pitch, w, h);
	S.tcur = T_WRAP + 1;
	S.tgl = false;
	t_proxy();
	return true;
}

// The RSX is idle: the table's memory, the ring and the program copies go; ps3gl's proxy slots let go first.
extern "C" void ps3rsxd_mem_term(void)
{
	if (!ps3gl_ptr)
		return;
	t_unproxy();
	for (u32 i = 0; i < RT_MAX; i++)
		if (RT.t[i].used)
			rt_delete(i);
	rt_frame(0);
	rt_frame(1);
	free(RT.stage);
	RT.stage = 0;
	RT.stage_size = 0;
	RT.swz_w = RT.swz_h = 0;
	S.tm = TM_GL;
	S.tcur = 0;
	S.tgl = true;
	if (S.rb)
		rsxFree(S.rb);
	if (S.fpm)
		rsxFree(S.fpm);
	S.rb = S.fpm = 0;
	S.ring_ok = S.rused = S.ring_mode = S.gl_off = false;
	S.pg = &S.prog;
	memset(S.fp_own, 0, sizeof(S.fp_own));
}
