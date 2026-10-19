#pragma once
// RSX Direct (G17): the SPU's jobs as plain C, for spe_tex.c and misc/rsxdirect_check. The includer defines the DMA
// layer: rj_get(ls, ea, n) waits; rj_put/rj_putf(ls, ea, n, tag) (putf fenced) do not; rj_wait(tag); rj_ticks().
#include "rsxd_emit.h"

static void rj_get(void* ls, uint32_t ea, uint32_t bytes);
static void rj_put(const void* ls, uint32_t ea, uint32_t bytes, uint32_t tag);
static void rj_putf(const void* ls, uint32_t ea, uint32_t bytes, uint32_t tag);
static void rj_wait(uint32_t tag);
static uint32_t rj_ticks(void);

typedef char rsxd_job_sizes[(sizeof(RsxdJob) % 16 == 0 && sizeof(RsxdDone) % 16 == 0 && sizeof(RsxdOp) == 48 &&
                             sizeof(RsxdTex) == 48 && sizeof(RsxdTriParam) == 48 && sizeof(RsxdProg) % 16 == 0) ? 1 : -1];

#define RJ_TAG_OUT 10  // 10, 11: the word windows
#define RJ_TAG_VB  12  // 12, 13: the vertex windows
#define RJ_TAG_IN  14
#define RJ_DMA_MAX 16384u

#define RJ_OUT_WORDS 1024
static uint32_t rj_out[2][RJ_OUT_WORDS] __attribute__((aligned(128)));
static RsxdProg rj_prog __attribute__((aligned(128)));
static uint32_t rj_prog_gen;
static RsxdJob rj_job __attribute__((aligned(128)));
static RsxdDone rj_done __attribute__((aligned(128)));
static RsxdSec rj_sec;
static RsxdTex rj_tex[RSXD_TRI_TEX] __attribute__((aligned(128)));  // TRIS: the job's textures

static struct
{
	uint32_t ea, cur, used, limit, full;
	uint32_t inval;     // EMIT: the cache clear of the draw in progress, sent once
	uint8_t* tex_due;   // TRIS: per texture table entry, 1 while its cache clear is due
	uint32_t cur_tex;   // TRIS: the texture table entry of the GL state
} rj;

static inline uint32_t rj_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

// Whole quadwords go out; the 0-3 words after the last one start the other window, which is free once its put is.
static int rsxd_reserve(RsxdState* s, uint32_t n)
{
	uint32_t* const base = rj_out[rj.cur];
	const uint32_t in = (uint32_t)(s->op - base);
	if (rj.used + in + n + 1 > rj.limit)
	{
		rj.full = 1;
		return 0;
	}
	if ((uint32_t)(s->oend - s->op) >= n)
		return 1;
	const uint32_t q = in & ~3u, tail = in - q;
	rj_put(base, rj.ea, q * 4, RJ_TAG_OUT + rj.cur);
	const uint32_t nx = rj.cur ^ 1;
	rj_wait(RJ_TAG_OUT + nx);
	uint32_t* const nb = rj_out[nx];
	for (uint32_t k = 0; k < tail; k++)
		nb[k] = base[q + k];
	rj.ea += q * 4;
	rj.used += q;
	rj.cur = nx;
	s->op = nb + tail;
	s->oend = nb + RJ_OUT_WORDS;
	return 1;
}

static uint32_t rsxd_tex_dirty(RsxdState* s, uint32_t obj, int clear)
{
	(void)s;
	(void)obj;
	(void)clear;
	if (rj.tex_due)
	{
		// The entries of one texture share the first one's flag (pad[1]).
		const uint32_t i = rj_tex[rj.cur_tex].pad[1];
		if (!rj.tex_due[i])
			return 0;
		rj.tex_due[i] = 0;
		rj_done.cleared[i >> 5] |= 1u << (i & 31);
		return 1;
	}
	const uint32_t v = rj.inval;
	rj.inval = 0;
	return v;
}

static void rj_open(RsxdState* s, uint32_t ea, uint32_t words)
{
	rj.ea = ea;
	rj.cur = 0;
	rj.used = 0;
	rj.limit = words;
	rj.full = 0;
	s->op = rj_out[0];
	s->oend = rj_out[0] + RJ_OUT_WORDS;
}

// The RETURN, then the last words; the quadword after the RETURN is zero padded. Returns the words with the RETURN.
static uint32_t rj_close(RsxdState* s)
{
	uint32_t* const base = rj_out[rj.cur];
	*s->op++ = RX_RETURN;
	const uint32_t in = (uint32_t)(s->op - base), q = (in + 3) & ~3u;
	for (uint32_t k = in; k < q; k++)
		base[k] = 0;
	rj_put(base, rj.ea, q * 4, RJ_TAG_OUT + rj.cur);
	rj_wait(RJ_TAG_OUT);
	rj_wait(RJ_TAG_OUT + 1);
	return rj.used + in;
}

static void rj_state(RsxdState* s)
{
	memset(s, 0, sizeof(*s));
	rj_sec = rj_job.sec;
	s->gl = rj_job.gl;
	s->sc = &rj_job.sc;
	s->sec = &rj_sec;
	s->prog = &rj_prog;
	rsxd_forget(s);
}

static void rj_finish(RsxdState* s)
{
	RsxdDone* d = &rj_done;
	d->status = rj.full ? RSXD_DONE_ROOM : d->status;
	d->words = rj_close(s);
	d->n_draw = s->n_draw;
	d->n_vtx = s->n_vtx;
	d->n_tex = s->n_tex;
	d->n_inval = s->n_inval;
	d->n_offc = s->n_offc;
	d->n_cclamp = s->n_cclamp;
	d->n_tril = s->n_tril;
	d->gl = s->gl;
}

// ---- EMIT: a section's ops ----

#define RJ_OPS  64
#define RJ_TEXS 64
static RsxdOp rj_ops[RJ_OPS] __attribute__((aligned(128)));
static RsxdTex rj_texw[RJ_TEXS] __attribute__((aligned(128)));

static void rj_emit(void)
{
	const RsxdJob* j = &rj_job;
	RsxdState st;
	rj_state(&st);
	rj.tex_due = 0;
	rj.inval = 0;
	rj_open(&st, j->out_ea, j->out_words);
	uint32_t ow = 0, on = 0, tw = 0, tn = 0;
	for (uint32_t i = 0; i < j->nops && !rj.full; i++)
	{
		if (i - ow >= on)
		{
			ow = i;
			on = rj_min(RJ_OPS, j->nops - i);
			rj_get(rj_ops, j->ops_ea + i * sizeof(RsxdOp), on * sizeof(RsxdOp));
		}
		const RsxdOp* o = &rj_ops[i - ow];
		if ((o->kind & 0xFF) == RSXD_OP_PARAM)
		{
			const uint32_t textured = (o->kind >> 16) & 1;
			const RsxdTex* t = 0;
			if (textured)
			{
				if (o->tex - tw >= tn)
				{
					tw = o->tex;
					tn = rj_min(RJ_TEXS, j->ntex - tw);
					rj_get(rj_texw, j->tex_ea + tw * sizeof(RsxdTex), tn * sizeof(RsxdTex));
				}
				t = &rj_texw[o->tex - tw];
			}
			rsxd_apply(&st, o->w, (o->kind >> 8) & 0xFF, textured, t, &o->clip);
		}
		else
		{
			rj_sec.vb_off = o->w[0];
			rj.inval = (o->kind >> 16) & 1;
			rsxd_draw(&st, (o->kind >> 8) & 0xFF, o->w[1], o->w[2], o->w[3]);
		}
	}
	rj_finish(&st);
}

// ---- TRIS: draw_tr_tris of ps3_rend.cpp (tri_collect, tri_sort, tri_runs, vb_emit) ----

typedef struct { uint32_t x, y, w, col, u, v; } RjVtx;  // DcVertex as bits
typedef struct { uint32_t key, id; } RjTri;             // id: vertex slot | odd corner << 12 | param << 13

#define RJ_F_1E25  0x69045951u  // 1e25f
#define RJ_F_INF_W 0x01202205u  // 1.0f / 3.4e37f
#define RJ_F_INF   0x7F800000u
#define RJ_RUN_MAX 16000        // RUN_MAX_VTX
#define RJ_VBW     84           // vertices per vertex window (4032 bytes)

static RsxdTriStrip rj_strip[RSXD_TRI_STRIPS] __attribute__((aligned(128)));
static RsxdTriParam rj_par[RSXD_TRI_PARAMS] __attribute__((aligned(128)));
static uint8_t rj_due[RSXD_TRI_TEX];
static RjVtx rj_vtx[RSXD_TRI_VTX] __attribute__((aligned(128)));
static uint32_t rj_offs[RSXD_TRI_VTX] __attribute__((aligned(128)));
static RjTri rj_tri[2][RSXD_TRI_VTX] __attribute__((aligned(128)));
static uint32_t rj_vb[2][RJ_VBW * 12] __attribute__((aligned(128)));

static struct
{
	uint32_t n;          // vertices written
	uint32_t ea, cur;    // EA of window cur's first vertex
	uint32_t in;         // vertices in window cur
	const uint32_t* cache;  // apply_param's cache: the param's words, or 0
	uint32_t cache_or;
} rv;

// vertex_inf on the bits: NaN or |x|, |y| > 1e25, or W below 1/3.4e37 (a NaN W is not).
static inline int rj_inf(const RjVtx* v)
{
	if ((v->x & 0x7FFFFFFFu) > RJ_F_1E25 || (v->y & 0x7FFFFFFFu) > RJ_F_1E25)
		return 1;
	if ((v->w & 0x7FFFFFFFu) > RJ_F_INF)
		return 0;
	return (v->w >> 31) || v->w < RJ_F_INF_W;
}

// a > b as the PPU compares two W that passed vertex_inf (positive or NaN).
static inline int rj_wgt(uint32_t a, uint32_t b)
{
	return (a & 0x7FFFFFFFu) <= RJ_F_INF && (b & 0x7FFFFFFFu) <= RJ_F_INF && a > b;
}

// a * b as fmuls (round to nearest even, denormals kept, PPC NaN rules): spe_vb_build.h's fmul_rn.
static uint32_t rj_fmul_rn(uint32_t a, uint32_t b)
{
	const uint32_t s = (a ^ b) & 0x80000000u;
	int32_t ea = (int32_t)((a >> 23) & 0xFF), eb = (int32_t)((b >> 23) & 0xFF);
	uint32_t ma = a & 0x7FFFFFu, mb = b & 0x7FFFFFu;
	if (ea == 0xFF || eb == 0xFF)
	{
		if (ea == 0xFF && ma)
			return a | 0x400000u;
		if (eb == 0xFF && mb)
			return b | 0x400000u;
		if ((ea == 0xFF && !eb && !mb) || (eb == 0xFF && !ea && !ma))
			return 0x7FC00000u;
		return s | RJ_F_INF;
	}
	if ((!ea && !ma) || (!eb && !mb))
		return s;
	if (ea)
		ma |= 0x800000u;
	else
		for (ea = 1; !(ma & 0x800000u); ea--)
			ma <<= 1;
	if (eb)
		mb |= 0x800000u;
	else
		for (eb = 1; !(mb & 0x800000u); eb--)
			mb <<= 1;
	const uint64_t p = (uint64_t)ma * mb;
	int32_t e = ea + eb - 127;
	uint32_t sh = 23;
	if (p >> 47)
	{
		e++;
		sh = 24;
	}
	if (e <= 0)
	{
		sh += (uint32_t)(1 - e);
		e = 0;
	}
	if (sh > 48)
		return s;
	uint64_t m = p >> sh;
	const uint64_t rem = p & ((1ull << sh) - 1), half = 1ull << (sh - 1);
	if (rem > half || (rem == half && (m & 1)))
		m++;
	if (!e)
		return s | (uint32_t)m;
	if (m >> 24)
	{
		m >>= 1;
		e++;
	}
	if (e >= 255)
		return s | RJ_F_INF;
	return s | ((uint32_t)e << 23) | ((uint32_t)m & 0x7FFFFFu);
}

// Normal operands with a normal product: the double product is exact, its one rounding is fmuls'.
static inline uint32_t rj_fmul(uint32_t a, uint32_t b)
{
	if ((a & 0x7F800000u) - 0x00800000u < 0x7F000000u && (b & 0x7F800000u) - 0x00800000u < 0x7F000000u)
	{
		float fa, fb;
		memcpy(&fa, &a, 4);
		memcpy(&fb, &b, 4);
		const double p = (double)fa * (double)fb;
		uint64_t pb;
		memcpy(&pb, &p, 8);
		const uint32_t hp = (uint32_t)(pb >> 32) & 0x7FFFFFFFu;
		if (hp - 0x38100000u < 0x0FD00000u)
		{
			const float f = (float)p;
			uint32_t r;
			memcpy(&r, &f, 4);
			return r;
		}
	}
	return rj_fmul_rn(a, b);
}

static void rj_vb_open(uint32_t ea)
{
	rv.n = 0;
	rv.ea = ea;
	rv.cur = 0;
	rv.in = 0;
}

static void rj_vb_flush(void)
{
	if (!rv.in)
		return;
	rj_put(rj_vb[rv.cur], rv.ea, rv.in * RSXD_TRI_STRIDE, RJ_TAG_VB + rv.cur);
	rv.ea += rv.in * RSXD_TRI_STRIDE;
	rv.cur ^= 1;
	rv.in = 0;
	rj_wait(RJ_TAG_VB + rv.cur);
}

// vb_emit in the SPU's 48-byte form.
static void rj_emit_vtx(uint32_t slot, uint32_t alpha_or, uint32_t offs)
{
	const RjVtx* v = &rj_vtx[slot];
	uint32_t* o = &rj_vb[rv.cur][rv.in * 12];
	const uint32_t c = v->col | alpha_or;
	o[0] = rj_fmul(v->x, v->w);
	o[1] = rj_fmul(v->y, v->w);
	o[2] = v->w;
	o[3] = 0x3F800000u;
	o[4] = v->u;
	o[5] = v->v;
	o[6] = (offs << 8) | (offs >> 24);
	o[7] = 0;
	o[8] = (c << 8) | (c >> 24);
	o[9] = o[10] = o[11] = 0;
	rv.n++;
	if (++rv.in == RJ_VBW)
		rj_vb_flush();
}

static inline uint32_t rj_covers(const uint32_t* w)
{
	return rx_tri_covers(w);
}

static inline int rj_equiv(const uint32_t* a, const uint32_t* b)
{
	return ((a[0] ^ b[0]) & rj_job.key_pcw) == 0 && ((a[1] ^ b[1]) & rj_job.key_isp) == 0 && a[2] == b[2] && a[3] == b[3];
}

static inline uint32_t rj_alpha_rule(const uint32_t* w)
{
	return (!rx_bits(P_TSP(w), 20, 1) || (rx_bits(P_PCW(w), 3, 1) && rx_bits(P_TSP(w), 6, 2) == 1)) ? 0xFF000000u : 0;
}

// apply_param with its cache (the same words: the same texture and clip); the cache holds no depth pass here.
static uint32_t rj_apply(RsxdState* st, const RsxdTriParam* p)
{
	if (rv.cache && !memcmp(rv.cache, p->w, 16))
		return rv.cache_or;
	const uint32_t textured = p->tex != RSXD_NONE;
	if (textured)
		rj.cur_tex = p->tex;
	rv.cache_or = rsxd_apply(st, p->w, RXL_TRANSLUCENT, textured, textured ? &rj_tex[p->tex] : 0, &p->clip);
	rv.cache = p->w;
	return rv.cache_or;
}

// tri_runs(n, base, emit, own).
static void rj_runs(RsxdState* st, uint32_t n, uint32_t base, int emit, int own, uint32_t* runs)
{
	const RjTri* tri = rj_tri[0];
	const RsxdTriParam* rp = 0;
	uint32_t run_first = 0, run_cull = 0, alpha_or = 0, run_vb = base, offs = 0;
	int draws = 1;
	rv.cache = 0;
	for (uint32_t t = 0; t <= n && !rj.full; t++)
	{
		const RsxdTriParam* p = t < n ? &rj_par[tri[t].id >> 13] : 0;
		if (!p || !rp || !rj_equiv(p->w, rp->w) || (t - run_first) * 3 + 3 > RJ_RUN_MAX)
		{
			if (t != run_first && draws)
			{
				rsxd_draw(st, RX_TRIANGLES, RSXD_TRI_STRIDE, run_vb, 3 * (t - run_first));
				(*runs)++;
			}
			if (!p)
				break;
			draws = rj_sec.tri_mode != RXT_OPAQUE || (rj_covers(p->w) && !(rj_job.first_pass && rx_bits(P_ISP(p->w), 26, 1)));
			if (rj_sec.tri_mode == RXT_OPAQUE)
			{
				rj_done.n_fp_runs++;
				rj_done.n_fp_nodepth += !rj_covers(p->w) || rx_bits(P_ISP(p->w), 26, 1);
			}
			alpha_or = draws || !rj_job.first_pass ? rj_apply(st, p) : rj_alpha_rule(p->w);
			rp = p;
			run_first = t;
			run_cull = rx_eff_cull(st, p->w);
			offs = p->offs;
			run_vb = own ? rv.n : base + 3 * t;
		}
		if (!emit || (own && !draws))
			continue;
		const uint32_t c = tri[t].id & 0xFFF, cull = rx_eff_cull(st, p->w);
		const uint32_t swap = ((tri[t].id >> 12) & 1) ^ (uint32_t)(cull >= 2 && cull != run_cull);
		const uint32_t corner[3] = { swap ? c - 1 : c - 2, swap ? c - 2 : c - 1, c };
		for (uint32_t k = 0; k < 3; k++)
			rj_emit_vtx(corner[k], alpha_or, offs ? rj_offs[corner[k]] : 0);
	}
}

// tri_sort: a radix sort of the key, least significant byte first, so equal keys keep their order.
static void rj_sort(uint32_t n)
{
	RjTri* src = rj_tri[0];
	RjTri* dst = rj_tri[1];
	for (uint32_t sh = 0; sh < 32; sh += 8)
	{
		uint32_t hist[256];
		memset(hist, 0, sizeof(hist));
		for (uint32_t i = 0; i < n; i++)
			hist[(src[i].key >> sh) & 255]++;
		uint32_t sum = 0;
		for (uint32_t d = 0; d < 256; d++)
		{
			const uint32_t c = hist[d];
			hist[d] = sum;
			sum += c;
		}
		for (uint32_t i = 0; i < n; i++)
			dst[hist[(src[i].key >> sh) & 255]++] = src[i];
		RjTri* t = src;
		src = dst;
		dst = t;
	}
}

// A run of [ea, ea + bytes) into LS at ls, through rj_tri[1] (the sort's spare half, free until the sort).
static void rj_get_any(void* ls, uint32_t ea, uint32_t bytes)
{
	uint8_t* const stage = (uint8_t*)rj_tri[1];
	uint8_t* o = (uint8_t*)ls;
	while (bytes)
	{
		const uint32_t a = ea & ~15u, lead = ea - a;
		const uint32_t take = rj_min(bytes, RJ_DMA_MAX - 16 - lead);
		rj_get(stage, a, (lead + take + 15) & ~15u);
		memcpy(o, stage + lead, take);
		o += take;
		ea += take;
		bytes -= take;
	}
}

static void rj_tris(void)
{
	const RsxdJob* j = &rj_job;
	RsxdDone* d = &rj_done;
	RsxdState st;
	rj_state(&st);
	rj.inval = 0;
	if (j->nstrips > RSXD_TRI_STRIPS || j->nparams > RSXD_TRI_PARAMS || j->ntex > RSXD_TRI_TEX)
	{
		d->status = RSXD_DONE_BIG;
		return;
	}
	rj_get_any(rj_strip, j->strips_ea, j->nstrips * sizeof(RsxdTriStrip));
	rj_get_any(rj_par, j->params_ea, j->nparams * sizeof(RsxdTriParam));
	rj_get_any(rj_tex, j->tex_ea, j->ntex * sizeof(RsxdTex));
	for (uint32_t i = 0; i < j->ntex; i++)
		rj_due[i] = (uint8_t)rj_tex[i].pad[0];
	rj.tex_due = rj_due;
	rj.cur_tex = 0;

	// tri_collect: the vertices of each strip in LS, one slot each; every triangle with no inf corner.
	uint32_t nv = 0, n = 0;
	for (uint32_t s = 0; s < j->nstrips; s++)
	{
		const RsxdTriStrip* sp = &rj_strip[s];
		if (nv + sp->count > RSXD_TRI_VTX)
		{
			d->status = RSXD_DONE_BIG;
			return;
		}
		rj_get_any(&rj_vtx[nv], j->vtx_ea + sp->first * 24, sp->count * 24u);
		if (rj_par[sp->p].offs)
			rj_get_any(&rj_offs[nv], j->offs_ea + sp->first * 4, sp->count * 4u);
		const RjVtx* v = &rj_vtx[nv];
		int inf0 = rj_inf(v), inf1 = rj_inf(v + 1);
		for (uint32_t k = 2; k < sp->count; k++)
		{
			const int inf2 = rj_inf(v + k);
			if (!inf0 && !inf1 && !inf2)
			{
				uint32_t w = v[k].w;
				if (rj_wgt(v[k - 1].w, w))
					w = v[k - 1].w;
				if (rj_wgt(v[k - 2].w, w))
					w = v[k - 2].w;
				rj_tri[0][n].key = ~w;
				rj_tri[0][n].id = (nv + k) | ((k & 1) << 12) | ((uint32_t)sp->p << 13);
				n++;
			}
			inf0 = inf1;
			inf1 = inf2;
		}
		nv += sp->count;
	}
	// tri_game_room with the scene block's room (a fresh block when 6 n does not fit); the sort, then the cut.
	const int game = j->tri_sort == 3 && j->first_pass == 2 && 3 * n <= j->vb_room;
	if (!game)
	{
		rj_sort(n);
		if (n > j->vb_room / 3)
			n = j->vb_room / 3;
	}
	if ((game ? 6 : 3) * n > j->vb_cap)
	{
		d->status = RSXD_DONE_BIG;
		return;
	}
	d->n_tris = n;
	rj_open(&st, j->out_ea, j->out_words);
	rj_vb_open(j->vb_ea);
	uint32_t base = 0;
	if (j->tri_sort == 3)
	{
		rj_sec.tri_mode = RXT_OPAQUE;
		st.gl.color_mask = 0;
		st.gl.alpha_func = 0x206;
		st.gl.alpha_ref = 240;
		rj_runs(&st, n, base, 1, game, &d->n_runs);
		if (game)
		{
			rj_sort(n);
			base = rv.n;
		}
		rj_sec.tri_mode = RXT_NODEPTH;
		st.gl.color_mask = 15;
		st.gl.alpha_func = 0x204;
		st.gl.alpha_ref = 0;
		rj_runs(&st, n, base, game, 0, &d->n_runs);
	}
	else
	{
		rj_sec.tri_mode = j->tri_sort == 2 ? RXT_GAME : RXT_NODEPTH;
		rj_runs(&st, n, base, 1, 0, &d->n_runs);
	}
	rj_sec.tri_mode = RXT_NONE;
	rj_vb_flush();
	rj_wait(RJ_TAG_VB);
	rj_wait(RJ_TAG_VB + 1);
	d->n_vb = rv.n;
	rj.tex_due = 0;
	rj_finish(&st);
}

// The job at ea; its RsxdDone goes out with seq in the first quadword, after the rest.
static void rj_run(uint32_t ea)
{
	const uint32_t t0 = rj_ticks();
	rj_get(&rj_job, ea, sizeof(rj_job));
	if (rj_job.prog_gen != rj_prog_gen)
	{
		rj_get(&rj_prog, rj_job.prog_ea, sizeof(rj_prog));
		rj_prog_gen = rj_job.prog_gen;
	}
	memset(&rj_done, 0, sizeof(rj_done));
	if (rj_job.kind == RSXD_JOB_EMIT)
		rj_emit();
	else
		rj_tris();
	rj_done.seq = rj_job.seq;
	rj_done.ticks = t0 - rj_ticks();
	rj_put((uint8_t*)&rj_done + 16, rj_job.done_ea + 16, sizeof(rj_done) - 16, RJ_TAG_IN);
	rj_putf(&rj_done, rj_job.done_ea, 16, RJ_TAG_IN);
	rj_wait(RJ_TAG_IN);
}
