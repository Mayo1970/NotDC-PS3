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
s32 rsxContextCallback(gcmContextData* context, u32 count);
}
#pragma GCC diagnostic pop

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
	if (obj >= PS3GL_MAX_TEXTURES)
		return 0;
	ps3gl_texture_t* t = &ps3gl.textures[obj];
	if (!t->content_dirty)
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
	t->content_dirty = 0;
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

void ps3rsxd_tex_name(u32 name, RsxdTex* o)
{
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
}

// A bound texture with the sampler words its own fields give (apply_textures' rsxTextureControl/Filter/WrapMode).
static void tex_from_fields(const ps3gl_texture_t* t, RsxdTex* o)
{
	tex_obj(t, o);
	o->ctrl |= (u32)t->max_aniso << 4;
	o->filter = ((u32)t->mag_filter << 24) | ((u32)t->min_filter << 16) | (1u << 13);
	o->wrap = t->wrap_s | ((u32)t->wrap_t << 8) | (3u << 16);
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

// CHECK: the emitter's GL state against ps3gl's after the same calls, and ps3_rend.cpp's rule state. 0 = equal;
// else the field's offset << 8 | word + 1, with the emitter's and ps3gl's values.
static u32 gl_diff(const RsxdGl* e, u32* ev, u32* gv)
{
	RsxdGl g;
	gl_from_ps3gl(&g);
	g.clip_base = *S.rr.clip_base;
	g.clip_mode = *S.rr.clip_mode;
	memcpy(g.clip_r, S.rr.clip_r, sizeof(g.clip_r));
	g.flat_shift = *S.rr.flat_shift;
	static const struct { u16 off; u8 n; } k_f[] = {
		{ offsetof(RsxdGl, blend_en), 3 }, { offsetof(RsxdGl, alpha_en), 3 }, { offsetof(RsxdGl, depth_test), 3 },
		{ offsetof(RsxdGl, cull_en), 3 }, { offsetof(RsxdGl, scissor_en), 5 }, { offsetof(RsxdGl, color_mask), 2 },
		{ offsetof(RsxdGl, st_en), 8 }, { offsetof(RsxdGl, tex_en), 4 }, { offsetof(RsxdGl, fog), 12 },
		{ offsetof(RsxdGl, clip_base), 7 },
	};
	for (u32 i = 0; i < sizeof(k_f) / sizeof(k_f[0]); i++)
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

void ps3rsxd_init()
{
	if (!ps3gl_ptr)
		return;
	S.ctx = ps3gl_get_ctx();
	const bool cap = capture();
	S.ok = cap && selftest();
	printf("[rsxd] RSX Direct %s (programs %s, %u VP words, %u FP words)\n", S.ok ? "ready" : "OFF: the emitter fails",
	       cap ? "captured" : "NOT captured", S.prog.vp_n[0] + S.prog.vp_n[1] + S.prog.vp_n[2] + S.prog.vp_n[3],
	       S.prog.fp_at[RSXD_KEYS - 1] + S.prog.fp_n[RSXD_KEYS - 1]);
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

void ps3rsxd_frame()
{
	int m = g_rsx_direct_preset;
	if (m < RSXD_OFF || m > RSXD_TRIS_CHECK || !S.ok)
		m = RSXD_OFF;
	S.frame_mode = m;
	// ps3gl_begin_frame has waited for this vring segment's fence: its halves of the ring and TR block are free.
	S.ring_half = ps3gl.vring.cur_seg & 1;
	S.ring_pos = 0;
	S.trvb_pos = 0;
	if (m != RSXD_CHECK)
		return;
	if (!check_bufs())
	{
		S.frame_mode = RSXD_OFF;
		return;
	}
	rd_reset(S.P);
	S.ppos = rctx()->current;
	S.pcs.in_call = 0;
	S.p_ok = true;
}

int ps3rsxd_scene(const RsxdSceneIn* in, const RsxdRend* rend)
{
	S.mode = S.frame_mode;
	if (S.mode == RSXD_OFF)
		return RSXD_OFF;
	S.rr = *rend;
	RsxdScene& sc = S.sc;
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
	S.n_scene++;
	return S.mode;
}

// The section's start: with takeover, ps3gl sends what it holds, so the registers the emitter leaves alone are
// current; unit 0 and its pending texture cache clear are the section's (ps3gl sends unit 0 again at its next draw).
static void sec_open(bool takeover)
{
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
	st.prog = &S.prog;
	gl_from_ps3gl(&st.gl);
	st.gl.clip_base = *S.rr.clip_base;
	st.gl.clip_mode = *S.rr.clip_mode;
	memcpy(st.gl.clip_r, S.rr.clip_r, sizeof(st.gl.clip_r));
	st.gl.flat_shift = *S.rr.flat_shift;
	rsxd_forget(&st);
	st.n_draw = st.n_vtx = st.n_tex = st.n_inval = st.n_offc = st.n_cclamp = st.n_tril = 0;
	S.job_gl = st.gl;
}

// ps3gl and ps3_rend.cpp take the emitter's state back; the counts go to their stats.
static void sec_release()
{
	RsxdState& st = S.st;
	gl_to_ps3gl(&st.gl);
	*S.rr.clip_base = st.gl.clip_base != 0;
	*S.rr.clip_mode = st.gl.clip_mode;
	memcpy(S.rr.clip_r, st.gl.clip_r, sizeof(st.gl.clip_r));
	*S.rr.flat_shift = st.gl.flat_shift;
	*S.rr.cache_valid = false;
#ifdef RSXD_RELEASE_CHECK
	// misc/rsxdirect_check: ps3gl and ps3_rend.cpp hold the emitter's state after the write-back.
	u32 ev, gv;
	const u32 d = gl_diff(&st.gl, &ev, &gv);
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
	j->prog_ea = ea32(&S.prog);
	j->prog_gen = 1;
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
	kind = RSXD_K_RUNS;
#endif
	gcmContextData* c = rctx();
	sec_open(true);
	RsxdState& st = S.st;
	S.n_sec++;
	if (S.mode == RSXD_CHECK)
	{
		S.sec_mode = RSXD_SEC_CHECK;
		st.check = 1;
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
	rsxd_draw(&st, rp, stride, first, count);
	// A draw that moved on to a new FIFO segment ends below op0; its words are left out.
	if (st.op >= op0)
		S.n_words += (u64)(st.op - op0);
	if (S.sec_mode != RSXD_SEC_CHECK)
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
		const u32 da[] = { S.P->prim, S.P->batch_first, S.P->batch_count, S.P->n_inval };
		const u32 db[] = { S.E->prim, S.E->batch_first, S.E->batch_count, S.E->n_inval };
		for (u32 i = 0; i < 4; i++)
			if (da[i] != db[i])
			{
				check_report(0, 3, 0xD000 + i, da[i], db[i]);
				bad++;
			}
		S.E->n_inval = S.P->n_inval;
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
					t.pad[0] = t.obj < PS3GL_MAX_TEXTURES && ps3gl.textures[t.obj].content_dirty;
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
		if ((d.cleared[i >> 5] >> (i & 31)) & 1 && S.texs[i].obj < PS3GL_MAX_TEXTURES)
			ps3gl.textures[S.texs[i].obj].content_dirty = 0;
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
	static const char* const k_mode[] = { "OFF", "PPU", "CHECK", "SPE", "SPE CHECK", "TR SORT", "TR SORT CHECK" };
	const u32 d = S.n_draw ? S.n_draw : 1;
	printf("[rsxd] mode=%s ok=%u | scenes=%u sections=%u draws=%u (%.0f/s) vtx=%u words/draw=%.1f tex=%u inval=%u"
	       " drop=%u | check draws=%u bad=%u alpha=%u gl=%u lost=%u\n",
	       k_mode[S.frame_mode], (u32)S.ok, S.n_scene, S.n_sec, S.n_draw, secs > 0 ? S.n_draw / secs : 0.0, S.n_vtx,
	       (double)S.n_words / d, S.n_tex, S.n_inval, S.n_drop, S.n_check, S.n_bad, S.n_alpha, S.n_glbad, S.n_lost);
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
