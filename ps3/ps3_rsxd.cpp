// RSX Direct (G17): spu/rsxd_emit.h on the PPU for every frame (rgl), its programs, texture table and vertex ring, and
// the SPE Tex Hash SPU's jobs (spu/rsxd_job.h). Built with -mminimal-toc (Makefile): its statics take one TOC entry.
#include "ps3_rsxd.h"
#include "spu/rsxd_emit.h"
#include "ps3_tex.h"
#include "ps3_spetex.h"
#include "ps3_video.h"
#include "ps3_timebase.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <malloc.h>
#include <unistd.h>
#include <rsx/rsx.h>

extern "C" s32 rsxContextCallback(gcmContextData* context, u32 count);
#include "rsxd_prog.h"
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

// The SPU's jobs: in the spare host block, a command ring of RING_BYTES and a TR vertex block of TRVB_BYTES for each
// ring segment (its label fences them).
#define RING_BYTES (1u << 20)
#define TRVB_BYTES (2u << 20)
#define JOB_OPS    4096
#define JOB_TEX    2048
#define JOB_MIN    4096          // ring words a job needs before it starts
#define PMAP_N     8192          // param slots (MAX_PARAMS)
// A section: none, the PPU emitter writes its words, or the ops are recorded for an SPU job.
enum { SEC_NONE, SEC_LIVE, SEC_SPU };
#define T_NAME    0x40000000u           // the table's names: T_NAME | entry
#define T_OBJ     0x10000u              // RsxdTex.obj of an entry: T_OBJ | entry
#define T_WRAP    (RT_MAX - 1)          // the entry over memory another side writes (rglTexWrap)
#define MAX_VERTS 16384                 // vertices of one glBegin
// The vertex ring: two segments, each fenced by a label of its own.
#define RING_SEG    (16u << 20)
#define RING_LABEL0 68
#define RING_LABEL1 70

// The frame's GL state outside RsxdGl (rs.vp_* with its casts, depth range, polygon offset, clear values, the surface,
// the matrices, the immediate vertex), and what the RSX holds of it (the *k flags: known).
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
	// The immediate colour (RGBA) and texcoords as bits; the primitive in progress and its vertex count (in vbuf).
	u32 color, u0, v0, u1, v1;
	u32 prim, n;
	// Sent: the viewport (rsxSetViewport's values and the clip), polygon offset, clear values, texture unit 1.
	bool vk, pk, cck, zk, u1k, u1_stale;
	u32 vw[14], pw[3], cc, zs, u1w[8];
	// The fog table's object on unit 1, or none.
	u32 u1_obj;
};

static struct
{
	RsxdProg prog __attribute__((aligned(128)));
	bool ok;
	u32 sec_mode;
	RsxdScene sc;
	RsxdSec sec;
	RsxdState st;
	RsxdRend rr;
	gcmContextData* ctx;
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
	RsxdGl job_gl;  // the GL state at the job's start
	// SEC_SPU: what the recorded params leave for draw_vb's tests (a texture on unit 0, the clip).
	u32 r_tmu0, r_obj, r_clip_mode;
	int32_t r_clip_r[4];
	bool replay;
	u32 replay_inval;
	// TR SORT.
	RsxdTrisIn tin;
	bool tris_on, tris_big;
	u64 t_tris0;
	// Per stats window: jobs and their words, draws, PPU replays, late SPU, waits; TR SORT jobs, PPU sorts, too big.
	u32 n_jobs, n_jwords, n_jdraw, n_fall, n_late, n_tjobs, n_tppu, n_tbig;
	u64 t_wait;

	// The scene's rules came this frame (the background asks first).
	bool scene_set;
	Rg rg;
	// rgl's vertices of a glBegin (9 words each, RsxdVertex), up to MAX_VERTS.
	u32* vbuf;
	// Per stats window: frames, clears, volume sections, fog tables on unit 1, quads out of a section.
	u32 n_frames, n_clears, n_vols, n_fog, n_quads;
	// The bound table entry + 1 (0: none).
	u32 tcur;
	// The frame's draws, vertices and texture sends (ps3_rsxwatch.c).
	u32 f_draws, f_verts, f_binds;

	// The ring: memory, segment in use, its head, the labels' last values; the ring is this frame's (between its begin
	// and the label).
	u8* rb;
	u32 rb_off, rseg, rhead, rval[2];
	volatile u32* rlabel[2];
	bool rused;
	// Per stats window: the ring's peak, failed blocks, label wait, timeouts.
	u32 rpeak, n_rdrop, n_rtimeout;
	u64 t_rwait;
} S;

#ifdef RSXD_ALL_RUNS
extern int g_rsxd_all_runs;
#endif
#ifdef RSXD_TEST_PPU
extern int g_rsxd_test_ppu;
#endif

static inline gcmContextData* rctx() { return S.ctx; }

// ---- the includer's part of rsxd_emit.h ----

static int rsxd_reserve(RsxdState* s, uint32_t n)
{
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

// A texture object of the emitter: the table's entry. Its texels the texture cache lacks:
static inline bool tobj_mine(u32 obj)
{
	return (obj >> 16) == (T_OBJ >> 16) && (obj & 0xFFFFu) < RT_MAX;
}

static inline bool tobj_dirty(u32 obj)
{
	return tobj_mine(obj) && RT.t[obj & 0xFFFFu].dirty != 0;
}

static inline void tobj_clean(u32 obj)
{
	if (tobj_mine(obj))
		RT.t[obj & 0xFFFFu].dirty = 0;
}

static uint32_t rsxd_tex_dirty(RsxdState* s, uint32_t obj, int clear)
{
	(void)s;
	(void)clear;
	// A replay of recorded ops: the draw's own clear, decided (and the flag cleared) at the recording.
	if (S.replay)
	{
		const u32 v = S.replay_inval;
		S.replay_inval = 0;
		return v;
	}
	if (!tobj_dirty(obj))
		return 0;
	// The texels' stores before the clear the RSX reads.
	RSXD_SYNC();
	tobj_clean(obj);
	return 1;
}

static inline u32 fbits(float f)
{
	u32 u;
	memcpy(&u, &f, 4);
	return u;
}

static inline bool t_name(u32 name)
{
	return (name & T_NAME) && (name & ~T_NAME) < T_WRAP;
}

void ps3rsxd_tex_name(u32 name, RsxdTex* o)
{
	if (t_name(name))
	{
		const u32 i = name & ~T_NAME;
		rt_words(&RT.t[i], T_OBJ | i, 0, o);
		return;
	}
	memset(o, 0, sizeof(*o));
	o->obj = 0xFFFFFFFFu;
}

// obj's words with its own sampler fields; false: it has no texels.
static bool tobj_fields(u32 obj, RsxdTex* o)
{
	if (!tobj_mine(obj))
		return false;
	rt_words(&RT.t[obj & 0xFFFFu], obj, 1, o);
	return o->valid != 0;
}

// ---- the programs: rsxd_prog.h's keys, the FP selects and VP loads of librsx ----

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

static u16 cslot(const rsxVertexProgram* vp, const rsxProgramConst* c)
{
	return c ? (u16)(c->index + vp->const_start) : RSXD_NONE;
}

static bool capture()
{
	RsxdProg& pg = S.prog;
	memset(&pg, 0, sizeof(pg));
	for (u32 i = 0; i < sizeof(pg.key_lut); i++)
	{
		const u32 env = (i >> 1) & 7, fog = (i >> 5) & 7;
		// Only the texenvs and fog modes the emitter sets.
		pg.key_lut[i] = env > RXE_DECAL || fog > RXF_BASE_TA ? (u8)RXE_DISABLED
		              : (u8)rk_key(i & 1, env, (i >> 4) & 1, fog, (i >> 8) & 1, (i >> 9) & 1, (i >> 10) & 1);
	}
	const rsxVertexProgram* vps[RSXD_VPS] = { 0, 0, 0, 0 };
	u32 nvp = 0, at = 0;
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		const RkProg& p = RK.k[k];
		cap_begin();
		rsxLoadFragmentProgramLocation(&s_cap, p.fp, p.fp_off, GCM_LOCATION_RSX);
		const u32 n = cap_words();
		if (at + n > RSXD_FP_POOL)
			return false;
		memcpy(&pg.fp_words[at], s_capbuf, n * 4);
		pg.fp_at[k] = (u16)at;
		pg.fp_n[k] = (u16)n;
		at += n;
		u32 v = 0;
		while (v < nvp && vps[v] != p.vp)
			v++;
		if (v == nvp)
		{
			if (nvp == RSXD_VPS)
				return false;
			vps[nvp++] = p.vp;
			cap_begin();
			rsxLoadVertexProgram(&s_cap, p.vp, p.vp_ucode);
			const u32 w = cap_words();
			if (w > RSXD_VP_WORDS)
				return false;
			memcpy(pg.vp_words[v], s_capbuf, w * 4);
			pg.vp_n[v] = w;
			// Constant slots: index + const_start; mvp as count rows, the others as one FLOAT4 slot.
			pg.vp_mvp[v] = cslot(p.vp, p.mvp);
			pg.vp_mvp_n[v] = p.mvp ? p.mvp->count : 0;
			pg.vp_fog[v] = cslot(p.vp, p.fogp);
			pg.vp_cmin[v] = cslot(p.vp, p.cmin);
			pg.vp_cmax[v] = cslot(p.vp, p.cmax);
			if ((p.mvp && (p.mvp->type != PARAM_FLOAT4x4 || p.mvp->count != 4)) ||
			    (p.fogp && p.fogp->type != PARAM_FLOAT4) || (p.cmin && p.cmin->type != PARAM_FLOAT4) ||
			    (p.cmax && p.cmax->type != PARAM_FLOAT4))
				return false;
		}
		pg.key_vp[k] = (u8)v;
	}
	// Every key of one VP must name the same constants (they come from the VP).
	for (u32 k = 0; k < RSXD_KEYS; k++)
	{
		const RkProg& p = RK.k[k];
		const u32 v = pg.key_vp[k];
		if (cslot(p.vp, p.fogp) != pg.vp_fog[v] || cslot(p.vp, p.cmin) != pg.vp_cmin[v] ||
		    cslot(p.vp, p.cmax) != pg.vp_cmax[v])
			return false;
	}
	return true;
}

#ifdef RSXD_TEST_HOOKS
// misc/rsxdirect_check: the programs of key k and the RSX offset of its FP.
const rsxVertexProgram* ps3rsxd_test_vp(int k)
{
	return RK.k[k].vp;
}

const rsxFragmentProgram* ps3rsxd_test_fp(int k, u32* off)
{
	*off = RK.k[k].fp_off;
	return RK.k[k].fp;
}
#endif

// ---- the frame's words ----

// Outside a section the emitter writes at the FIFO's current word.
static void fr_op_begin()
{
	gcmContextData* c = rctx();
	S.st.op = c->current;
	S.st.oend = c->end;
}

static void fr_op_end()
{
	rctx()->current = S.st.op;
}

// rsxSetViewport's rect clamped to the surface, scale and offset from the whole rect, then the clip.
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

// The MVP: projection times model view, as rows.
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

// Texture unit 1 (the fog table) when the RSX holds other words; the cache clear of its new texels goes with the next
// draw, or here before an SPU section (spu). Needs 17 words.
static void fr_unit1(bool spu)
{
	RsxdState& st = S.st;
	Rg& r = S.rg;
	r.u1_stale = false;
	const u32 obj = r.u1_obj;
	RsxdTex x;
	const bool on = tobj_fields(obj, &x);
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
		// The texels' stores before the clear the RSX reads.
		RSXD_SYNC();
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

// The state the RSX lacks, with the viewport and polygon offset.
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

// glClear: the clear values the RSX lacks, then the clear. It sends no pending state.
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

// Before the emitter's draws (spu: an SPU section's): the viewport, unit 1, a new MVP.
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

// ---- the boot ----

static const float k_identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

// The GL state of a w x h display before any call (glInit's defaults): no test, blend or mask, unit 0 in MODULATE.
static void gl_defaults(u32 w, u32 h)
{
	RsxdGl& g = S.st.gl;
	memset(&g, 0, sizeof(g));
	g.blend_src = GCM_ONE;
	g.blend_dst = GCM_ZERO;
	g.alpha_func = GCM_ALWAYS;
	g.depth_func = GCM_LESS;
	g.depth_mask = 1;
	g.cull_face = GCM_CULL_BACK;
	g.front_face = GCM_FRONTFACE_CCW;
	g.sc_w = (int32_t)(uint16_t)w;
	g.sc_h = (int32_t)(uint16_t)h;
	g.color_mask = 15;
	g.shade = GCM_SHADE_MODEL_SMOOTH;
	g.st_func = GCM_ALWAYS;
	g.st_mask = 0xFFFFFFFFu;
	g.st_fail = g.st_zfail = g.st_zpass = GCM_KEEP;
	g.st_wmask = 0xFFFFFFFFu;
	g.texenv = RXE_MODULATE;
	g.tex.obj = 0xFFFFFFFFu;
	g.clip_base = 1;
	Rg& r = S.rg;
	memset(&r, 0, sizeof(r));
	r.vp_w = (int)(uint16_t)w;
	r.vp_h = (int)(uint16_t)h;
	r.zf = 1.0f;
	r.clear_depth = 1.0f;
	r.sw = w;
	r.sh = h;
	r.mmode = GL_MODELVIEW;
	memcpy(r.proj, k_identity, sizeof(r.proj));
	memcpy(r.mv, k_identity, sizeof(r.mv));
	r.color = 0xFFFFFFFFu;
	r.u1_obj = 0xFFFFFFFFu;
	S.sc.screen_w = w;
	S.sc.screen_h = h;
}

// The ring's memory and labels.
static bool ring_mem()
{
	S.rb = (u8*)rsxMemalign(128, 2 * RING_SEG);
	if (!S.rb)
		return false;
	rsxAddressToOffset(S.rb, &S.rb_off);
	S.rlabel[0] = (volatile u32*)gcmGetLabelAddress(RING_LABEL0);
	S.rlabel[1] = (volatile u32*)gcmGetLabelAddress(RING_LABEL1);
	if (!S.rlabel[0] || !S.rlabel[1])
		return false;
	*S.rlabel[0] = *S.rlabel[1] = 0;
	S.rval[0] = S.rval[1] = 0;
	S.rseg = S.rhead = 0;
	S.rused = false;
	return true;
}

extern "C" int ps3rsxd_init(void* ctx, u32 w, u32 h)
{
	S.ctx = (gcmContextData*)ctx;
	gl_defaults(w, h);
	RsxdState& st = S.st;
	st.sc = &S.sc;
	st.sec = &S.sec;
	st.prog = &S.prog;
	rsxd_forget(&st);
	const bool prog = rk_load() && capture();
	if (!S.vbuf)
		S.vbuf = (u32*)RSXD_MEMALIGN(128, MAX_VERTS * sizeof(RsxdVertex));
	const bool ring = ring_mem();
	S.ok = prog && ring && S.vbuf;
	printf("[rsxd] RSX Direct %s (programs %s, %u VP words, %u FP words, ring %s)\n", S.ok ? "ready" : "FAILED",
	       prog ? "loaded" : "NOT loaded", S.prog.vp_n[0] + S.prog.vp_n[1] + S.prog.vp_n[2] + S.prog.vp_n[3],
	       S.prog.fp_at[RSXD_KEYS - 1] + S.prog.fp_n[RSXD_KEYS - 1], ring ? "ready" : "NOT ready");
	return S.ok;
}

// ---- the emitter's vertex ring ----

// The frame's segment gets its label, which the RSX writes once past every draw before it.
static void ring_end()
{
	if (!S.rused)
		return;
	S.rused = false;
	S.rval[S.rseg]++;
	rsxSetWriteBackendLabel(rctx(), S.rseg ? RING_LABEL1 : RING_LABEL0, S.rval[S.rseg]);
}

// The frame takes the other segment once the RSX is past the frame that used it last.
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
}

// No wrap onto a segment the RSX may read; each block starts 16-byte aligned.
static void* ring_alloc(u32 bytes, u32* off)
{
	if (!S.rused || S.rhead + bytes > RING_SEG)
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
	return ring_alloc((u32)count * (u32)sizeof(RsxdVertex), off);
}

void* rglVringAllocRaw(u32 bytes, u32* off)
{
	return bytes ? ring_alloc(bytes, off) : 0;
}

// ---- frames, scenes, sections ----

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
	S.spu_dead = !S.host || bytes < 2 * RING_BYTES + 2 * TRVB_BYTES || !S.job || !S.done || !S.ops || !S.texs ||
	             !S.tstrips || !S.tparams || !S.pmap || !S.pgen;
	if (S.done)
		memset(S.done, 0, sizeof(RsxdDone));
	if (S.pgen)
		memset(S.pgen, 0, PMAP_N * 2);
	printf("[rsxd] SPU jobs %s | host block %p io %08X, %u KB\n", S.spu_dead ? "OFF (no memory)" : "ready", S.host,
	       S.host_io, bytes >> 10);
}

static bool spu_ready()
{
#ifdef RSXD_TEST_PPU
	if (g_rsxd_test_ppu)
		return false;
#endif
	spu_setup();
	return !S.spu_dead && ps3spetex_mode() != SPETEX_OFF;
}

// ps3_video.c's frame begin, after the surface: the ring segment once its fence passed, the table memory given up in
// it before, and the RSX known to hold nothing (the next draw sends all it needs).
extern "C" void ps3rsxd_vring_begin(void)
{
	if (!S.ok)
		return;
	ring_begin();
	rt_frame(S.rseg);
	S.ring_half = S.rseg & 1;
	S.ring_pos = 0;
	S.trvb_pos = 0;
	rsxd_forget(&S.st);
	Rg& r = S.rg;
	r.vk = r.pk = r.cck = r.zk = r.u1k = false;
	r.u1_stale = r.mvp_new = true;
	r.u1_obj = 0xFFFFFFFFu;
	r.n = 0;
	S.sc.screen_w = r.sw;
	S.sc.screen_h = r.sh;
	S.sc.fog_scale = fbits(r.fog_scale);
	S.scene_set = false;
	S.f_draws = S.f_verts = S.f_binds = 0;
	RT.n_frame_alloc = 0;
	S.n_frames++;
}

// Its end: a unit 1 cache clear no draw took, then this segment's label.
extern "C" void ps3rsxd_vring_end(void)
{
	if (!S.ok)
		return;
	RsxdState& st = S.st;
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
		st.inval_due = 0;
	}
	ring_end();
}

bool ps3rsxd_scene(const RsxdSceneIn* in, const RsxdRend* rend)
{
	if (!S.ok)
		return false;
	S.rr = *rend;
	RsxdScene& sc = S.sc;
	// The frame's own values the memset below drops.
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
	sc.screen_w = fsw;
	sc.screen_h = fsh;
	sc.fog_scale = ffog;
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
	fr_mvp();
	// draw_scene asks before the background and again with the vertex block: one scene.
	S.n_scene += !S.scene_set;
	S.scene_set = true;
	return true;
}

// A section: the emitter's state and the RSX's go on; the rule state is ps3_rend.cpp's.
static void sec_open()
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
	st.prog = &S.prog;
	st.gl.clip_base = *S.rr.clip_base;
	st.gl.clip_mode = *S.rr.clip_mode;
	memcpy(st.gl.clip_r, S.rr.clip_r, sizeof(st.gl.clip_r));
	st.gl.flat_shift = *S.rr.flat_shift;
	st.n_draw = st.n_vtx = st.n_tex = st.n_inval = st.n_offc = st.n_cclamp = st.n_tril = 0;
	S.job_gl = st.gl;
}

// ps3_rend.cpp takes the rule state back; the counts go to the stats.
static void sec_release()
{
	RsxdState& st = S.st;
	*S.rr.clip_base = st.gl.clip_base != 0;
	*S.rr.clip_mode = st.gl.clip_mode;
	memcpy(S.rr.clip_r, st.gl.clip_r, sizeof(st.gl.clip_r));
	*S.rr.flat_shift = st.gl.flat_shift;
	*S.rr.cache_valid = false;
	*S.rr.offc += st.n_offc;
	*S.rr.cclamp += st.n_cclamp;
	*S.rr.tril += st.n_tril;
	S.f_draws += st.n_draw;
	S.f_verts += st.n_vtx;
	S.f_binds += st.n_tex;
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

static void job_fill(RsxdJob* j, u32 kind)
{
	memset(j, 0, sizeof(*j));
	j->kind = kind;
	j->out_ea = ea32(ring_base()) + S.ring_pos * 4;
	j->out_words = ring_room();
	j->sc = S.sc;
	j->sec = S.sec;
	j->gl = S.job_gl;
}

// A job's words: the CALL, and the job's state and counts as the PPU emitter would leave them.
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
	rsxd_forget(&st);
}

static bool emit_job()
{
	RsxdJob* j = S.job;
	job_fill(j, RSXD_JOB_EMIT);
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

// ---- recording (SEC_SPU) ----

static void rec_begin()
{
	S.nops = S.ntex = 0;
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
	if (ring_room() >= JOB_MIN && !S.spu_dead && emit_job())
	{
		job_take();
		S.nops = S.ntex = 0;
		return;
	}
	rec_replay();
	S.sec_mode = SEC_LIVE;
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

bool ps3rsxd_begin(u32 kind)
{
	if (!S.ok)
		return false;
#ifdef RSXD_ALL_RUNS
	if (g_rsxd_all_runs && kind == RSXD_K_PPU)
		kind = RSXD_K_RUNS;
#endif
	gcmContextData* c = rctx();
	sec_open();
	RsxdState& st = S.st;
	S.n_sec++;
	S.n_vols += kind == RSXD_K_VOL;
	S.sec_mode = SEC_LIVE;
	*S.rr.cache_valid = false;
	st.check = 0;
	st.op = c->current;
	st.oend = c->end;
	if (kind == RSXD_K_RUNS && spu_ready() && ring_room() >= JOB_MIN)
	{
		// What the SPU's draws need from the frame goes before its words.
		fr_pre_draw(true);
		c->current = st.op;
		S.sec_mode = SEC_SPU;
		rec_begin();
	}
	return true;
}

void ps3rsxd_end()
{
	RsxdState& st = S.st;
	if (S.sec_mode == SEC_SPU)
	{
		rec_flush();
		if (S.sec_mode == SEC_LIVE)
			rctx()->current = st.op;
		sec_release();
	}
	else if (S.sec_mode == SEC_LIVE)
	{
		rctx()->current = st.op;
		sec_release();
	}
	S.sec_mode = SEC_NONE;
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
	u32 textured = 0;
	// The TR depth pass binds no texture (apply_param_body).
	if (((w[0] >> 3) & 1) && !S.sec.depth_pass)
	{
		const u64 t0 = gettime();
		textured = ps3tex_bind_rsxd(w[2], w[3], slot, list == RXL_PUNCH, tex);
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
	return rsxd_apply(&S.st, w, list, textured, &tex, &clip);
}

void ps3rsxd_draw(u32 prim, u32 vb_off, u32 stride, u32 first, u32 count)
{
	RsxdState& st = S.st;
	const u32 rp = prim == GL_TRIANGLES ? RX_TRIANGLES : prim == GL_QUADS ? RX_QUADS : RX_TRI_STRIP;
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
	fr_pre_draw(false);
	rsxd_draw(&st, rp, stride, first, count);
	// A draw that moved on to a new FIFO segment ends below op0; its words are left out.
	if (st.op >= op0)
		S.n_words += (u64)(st.op - op0);
	rctx()->current = st.op;
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
	if (!S.ok || !spu_ready() || ring_room() < JOB_MIN)
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
	sec_open();
	// What the SPU's draws need from the frame goes before its words.
	gcmContextData* c = rctx();
	RsxdState& st = S.st;
	st.op = c->current;
	st.oend = c->end;
	fr_pre_draw(true);
	c->current = st.op;
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
	if (!S.ntstrips && !S.tris_big)
	{
		// No TR triangle in the pass.
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
		job_fill(j, RSXD_JOB_TRIS);
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
		ok = job_start(j) && job_wait(j->seq);
		if (ok && S.done->status != RSXD_DONE_OK)
		{
			S.n_tbig += S.done->status == RSXD_DONE_BIG;
			ok = false;
		}
	}
	else
		S.n_tbig++;
	if (!ok)
	{
		S.n_tppu++;
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

void ps3rsxd_stats(double secs)
{
	if (!S.n_frames)
		return;
	const u32 d = S.n_draw ? S.n_draw : 1;
	printf("[rsxd] ok=%u | scenes=%u sections=%u draws=%u (%.0f/s) vtx=%u words/draw=%.1f tex=%u inval=%u drop=%u | "
	       "frames=%u clears=%u vols=%u fog=%u quads=%u\n", (u32)S.ok, S.n_scene, S.n_sec, S.n_draw,
	       secs > 0 ? S.n_draw / secs : 0.0, S.n_vtx, (double)S.n_words / d, S.n_tex, S.n_inval, S.n_drop, S.n_frames,
	       S.n_clears, S.n_vols, S.n_fog, S.n_quads);
	u32 live = 0;
	for (u32 i = 0; i < RT_MAX; i++)
		live += RT.t[i].used;
	printf("[rsxd] tex entries=%u | allocs=%u frees=%u fails=%u levels=%u MB=%.1f over=%u | ring peak=%u KB drops=%u "
	       "wait %.2f ms timeouts=%u\n", live, RT.n_alloc, RT.n_free, RT.n_fail, RT.n_up, (double)RT.n_bytes / 1048576.0,
	       RT.n_over, S.rpeak >> 10, S.n_rdrop, (double)S.t_rwait * 1000.0 / (double)ps3_timebase_hz, S.n_rtimeout);
	printf("[rsxd] spu up=%u | jobs=%u words=%u draws=%u ppu=%u late=%u wait %.2f ms | tris jobs=%u ppu=%u big=%u\n",
	       (u32)(!S.spu_dead && ps3spetex_mode() != SPETEX_OFF), S.n_jobs, S.n_jwords, S.n_jdraw, S.n_fall, S.n_late,
	       (double)S.t_wait * 1000.0 / (double)ps3_timebase_hz, S.n_tjobs, S.n_tppu, S.n_tbig);
	S.n_frames = S.n_clears = S.n_vols = S.n_fog = S.n_quads = 0;
	RT.n_alloc = RT.n_free = RT.n_fail = RT.n_up = RT.n_over = 0;
	RT.n_bytes = 0;
	S.rpeak = S.n_rdrop = S.n_rtimeout = 0;
	S.t_rwait = 0;
	S.n_scene = S.n_sec = S.n_draw = S.n_vtx = S.n_tex = S.n_inval = S.n_drop = 0;
	S.n_words = 0;
	S.n_jobs = S.n_jwords = S.n_jdraw = S.n_fall = S.n_late = S.n_tjobs = S.n_tppu = S.n_tbig = 0;
	S.t_wait = 0;
}

extern "C" void ps3rsxd_frame_counts(u32* draws, u32* verts, u32* binds, u32* allocs)
{
	*draws = S.f_draws;
	*verts = S.f_verts;
	*binds = S.f_binds;
	*allocs = RT.n_frame_alloc;
}

void ps3rsxd_fog_table(u32 name)
{
	S.rg.u1_stale = true;
	S.rg.u1_obj = t_name(name) ? T_OBJ | (name & ~T_NAME) : 0xFFFFFFFFu;
	S.n_fog += t_name(name);
}

void ps3rsxd_screen(u32 w, u32 h)
{
	S.rg.sw = w;
	S.rg.sh = h;
	S.sc.screen_w = w;
	S.sc.screen_h = h;
}

// ---- rgl ----

// GL's compares, stencil ops and blend factors as the RSX takes them; another value: its default.
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

// The caps the emitter keeps; GL_TEXTURE_2D only on unit 0 (unit 1 holds the fog table alone).
static void rgl_cap(u32 cap, u32 on)
{
	RsxdGl& g = S.st.gl;
	switch (cap)
	{
	case GL_BLEND: g.blend_en = on; break;
	case GL_ALPHA_TEST: g.alpha_en = on; break;
	case GL_DEPTH_TEST: g.depth_test = on; break;
	case GL_CULL_FACE: g.cull_en = on; break;
	case GL_SCISSOR_TEST: g.scissor_en = on; break;
	case GL_STENCIL_TEST: g.st_en = on; break;
	case GL_POLYGON_OFFSET_FILL: S.rg.poly_on = (int)on; break;
	case GL_TEXTURE_2D:
		if (!S.rg.tmu)
			g.tex_en = on;
		break;
	default: break;
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
	S.st.gl.color_mask = (r ? 1u : 0) | (g ? 2u : 0) | (b ? 4u : 0) | (a ? 8u : 0);
}

void rglDepthMask(u8 flag)
{
	S.st.gl.depth_mask = flag ? 1 : 0;
}

void rglAlphaFunc(u32 func, float ref)
{
	u32 r = (u32)(ref * 255.0f + 0.5f);
	if (r > 255)
		r = 255;
	S.st.gl.alpha_func = rg_cmp(func);
	S.st.gl.alpha_ref = r;
}

void rglShadeModel(u32 mode)
{
	S.st.gl.shade = mode == GL_FLAT ? GCM_SHADE_MODEL_FLAT : GCM_SHADE_MODEL_SMOOTH;
}

void rglFrontFace(u32 mode)
{
	S.st.gl.front_face = mode == GL_CW ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW;
}

void rglStencilMask(u32 mask)
{
	S.st.gl.st_wmask = mask;
}

void rglStencilOp(u32 fail, u32 zfail, u32 zpass)
{
	RsxdGl& g = S.st.gl;
	g.st_fail = rg_sop(fail);
	g.st_zfail = rg_sop(zfail);
	g.st_zpass = rg_sop(zpass);
}

void rglStencilFunc(u32 func, int ref, u32 mask)
{
	RsxdGl& g = S.st.gl;
	g.st_func = rg_cmp(func);
	g.st_ref = (u32)ref;
	g.st_mask = mask;
}

void rglDepthFunc(u32 func)
{
	S.st.gl.depth_func = rg_cmp(func);
}

void rglBlendFunc(u32 src, u32 dst)
{
	S.st.gl.blend_src = rg_blend(src);
	S.st.gl.blend_dst = rg_blend(dst);
}

// glCullFace keeps its face for another mode.
void rglCullFace(u32 mode)
{
	if (mode == GL_FRONT || mode == GL_BACK || mode == GL_FRONT_AND_BACK)
		S.st.gl.cull_face = mode;
}

// The texenvs as the emitter's RXE_* (GL_ADD: the colour-only program); unit 1 has none.
void rglTexEnvMode(u32 mode)
{
	if (!S.rg.tmu)
		S.st.gl.texenv = mode == GL_REPLACE ? RXE_REPLACE : mode == GL_DECAL ? RXE_DECAL : mode == GL_ADD ? RXE_ADD
		               : RXE_MODULATE;
}

// glScissor and glViewport keep the RSX y of the surface in use, with their int16 and uint16 casts.
void rglScissor(int x, int y, int w, int h)
{
	RsxdGl& g = S.st.gl;
	g.sc_x = (int16_t)x;
	g.sc_y = (int16_t)((int)S.rg.sh - y - h);
	g.sc_w = (uint16_t)w;
	g.sc_h = (uint16_t)h;
}

void rglViewport(int x, int y, int w, int h)
{
	Rg& r = S.rg;
	r.vp_x = (int16_t)x;
	r.vp_y = (int16_t)((int)r.sh - y - h);
	r.vp_w = (uint16_t)w;
	r.vp_h = (uint16_t)h;
}

void rglDepthRange(double n, double f)
{
	S.rg.zn = (float)n;
	S.rg.zf = (float)f;
}

void rglClearColor(float r, float g, float b, float a)
{
	const u32 ri = (u32)(r * 255.0f), gi = (u32)(g * 255.0f), bi = (u32)(b * 255.0f), ai = (u32)(a * 255.0f);
	S.rg.clear_color = (ai << 24) | (ri << 16) | (gi << 8) | bi;
}

void rglClearDepth(double d)
{
	S.rg.clear_depth = (float)d;
}

void rglClearStencil(int s)
{
	S.rg.clear_stencil = (u32)s;
}

void rglClear(u32 mask)
{
	fr_clear(mask);
}

void rglApplyStates()
{
	fr_apply();
}

void rglMatrixMode(u32 mode)
{
	S.rg.mmode = mode;
}

static float* rg_mat()
{
	return S.rg.mmode == GL_PROJECTION ? S.rg.proj : S.rg.mv;
}

void rglLoadMatrixf(const float* m)
{
	memcpy(rg_mat(), m, 64);
	S.rg.mvp_new = true;
}

void rglLoadIdentity()
{
	memcpy(rg_mat(), k_identity, 64);
	S.rg.mvp_new = true;
}

void rglActiveTexture(u32 unit)
{
	const int t = (int)(unit - GL_TEXTURE0_ARB);
	if (t >= 0 && t < 2)
		S.rg.tmu = (u32)t;
	S.rg.u1_stale = true;
}

void rglTexBump(int on)
{
	if (!S.rg.tmu)
		S.st.gl.bump = on != 0;
}

void rglTex1Bits(u32 bits)
{
	S.rg.u1 = bits;
	S.rg.v1 = 0;
}

void rglDcFog(int mode)
{
	S.st.gl.fog = (u32)mode;
}

void rglDcFogScale(float scale)
{
	S.rg.fog_scale = scale;
	S.sc.fog_scale = fbits(scale);
}

void rglDcOffs(int on)
{
	S.st.gl.offs = on != 0;
}

void rglDcXt(int on, const float* cmin, const float* cmax, float tril)
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

void rglBegin(u32 prim)
{
	S.rg.prim = prim;
	S.rg.n = 0;
}

void rglColor4ub(u8 r, u8 g, u8 b, u8 a)
{
	S.rg.color = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | a;
}

void rglTexCoord2f(float u, float v)
{
	S.rg.u0 = fbits(u);
	S.rg.v0 = fbits(v);
}

void rglVertex3f(float x, float y, float z)
{
	Rg& r = S.rg;
	if (r.n >= MAX_VERTS)
		return;
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

// glEnd's draw: the vertices in a block of the frame's ring, drawn by the emitter.
void rglEnd()
{
	Rg& r = S.rg;
	const u32 n = r.n, stride = (u32)sizeof(RsxdVertex);
	r.n = 0;
	const bool sec = S.sec_mode != SEC_NONE;
	RsxdState& st = S.st;
	// A quad out of a section: no tile clip or colour shift; its texture is the one bound last.
	if (!sec)
	{
		RsxdGl& g = st.gl;
		S.n_quads++;
		g.clip_mode = RXC_NONE;
		g.flat_shift = 0;
		g.has_tex = S.tcur && tobj_fields(T_OBJ | (S.tcur - 1), &g.tex);
		if (!S.tcur)
			g.tex.obj = 0xFFFFFFFFu;
	}
	u32 off = 0;
	RsxdVertex* const b = n ? (RsxdVertex*)rglVringAlloc((int)n, &off) : NULL;
	if (!b)
		return;
	// One memcpy: RSX memory is cache-inhibited, and GCC may merge single stores.
	memcpy(b, S.vbuf, n * stride);
	if (sec)
	{
		ps3rsxd_draw(r.prim, off, stride, 0, n);
		return;
	}
	const u32 d0 = st.n_draw, v0 = st.n_vtx, t0 = st.n_tex;
	fr_op_begin();
	ps3rsxd_draw(r.prim, off, stride, 0, n);
	fr_op_end();
	S.f_draws += st.n_draw - d0;
	S.f_verts += st.n_vtx - v0;
	S.f_binds += st.n_tex - t0;
}

// ---- textures ----

static inline RtTex* t_cur()
{
	return S.tcur ? &RT.t[S.tcur - 1] : 0;
}

void rglGenTextures(u32* name)
{
	const int i = rt_new(T_WRAP);
	*name = i < 0 ? 0 : T_NAME | (u32)i;
}

void rglDeleteTexture(u32 name)
{
	if (!t_name(name))
		return;
	const u32 i = name & ~T_NAME;
	if (RT.t[i].used)
		rt_delete(i);
	if (S.tcur == i + 1)
		S.tcur = 0;
}

void rglBindTexture(u32 name)
{
	S.tcur = t_name(name) ? (name & ~T_NAME) + 1 : 0;
}

void rglTexParameteri(u32 pname, int v)
{
	if (RtTex* const t = t_cur())
		rt_param(t, pname, v);
}

void rglTexHint(u32 hint)
{
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
	if (RtTex* const t = t_cur())
		rt_image(t, level, (u32)w, (u32)h, rgba);
}

u32* rglTexDirectBegin(int w, int h, u32* pitch)
{
	RtTex* const t = t_cur();
	return t ? rt_direct_begin(t, (u32)w, (u32)h, pitch) : 0;
}

void rglTexDirectEnd()
{
	if (RtTex* const t = t_cur())
		rt_direct_end(t);
}

u8* rglTexDirectLevel0(int w, int h, u32* pitch, int* swz)
{
	RtTex* const t = t_cur();
	return t ? rt_direct_level0(t, (u32)w, (u32)h, pitch, swz) : 0;
}

u8* rglTexDirectLevel(int level)
{
	RtTex* const t = t_cur();
	return t ? rt_direct_level(t, level) : 0;
}

void rglTexDirectLevelEnd(int level)
{
	if (RtTex* const t = t_cur())
		rt_direct_level_end(t, level);
}

int rglTexChainLevels()
{
	RtTex* const t = t_cur();
	return t ? rt_chain_levels(t) : 0;
}

void rglTexConvertCheck(void* dst, int w, int h, const void* rgba)
{
	if (RtTex* const t = t_cur())
		rt_convert_check(t, dst, (u32)w, (u32)h, rgba);
}

void rglTexWrap(void* mem, u32 pitch, u32 w, u32 h)
{
	if (!mem)
	{
		S.tcur = 0;
		return;
	}
	RtTex* const t = &RT.t[T_WRAP];
	if (!t->used)
		rt_init(t);
	rt_wrap(t, (u8*)mem, pitch, w, h);
	S.tcur = T_WRAP + 1;
}

u8* ps3rsxd_fixed(u32 w, u32 h, u32* pitch)
{
	*pitch = rt_pitch(w);
	return (u8*)rsxMemalign(128, *pitch * h);
}

// The RSX is idle: the table's memory, the ring and the programs go.
extern "C" void ps3rsxd_mem_term(void)
{
	if (!S.ctx)
		return;
	for (u32 i = 0; i < RT_MAX; i++)
		if (RT.t[i].used)
			rt_delete(i);
	rt_frame(0);
	rt_frame(1);
	free(RT.stage);
	RT.stage = 0;
	RT.stage_size = 0;
	RT.swz_w = RT.swz_h = 0;
	S.tcur = 0;
	if (S.rb)
		rsxFree(S.rb);
	if (RK.fpm)
		rsxFree(RK.fpm);
	S.rb = 0;
	RK.fpm = 0;
	RK.fpm_bytes = 0;
	S.ok = S.rused = false;
}
