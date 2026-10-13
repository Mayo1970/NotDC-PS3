// OPTIMIZATION_PLAN 5.12 S2: Ps3VertexDecoder and the drkPvr FifoSplitter (ps3_rend.cpp, ta.h) on the SPU.
// Same lists, bit for bit, into per-context shadow arrays. Floats are handled as bits: SPU single math truncates.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <string.h>
#include <stddef.h>
#include "spe_dec.h"
#include "spe_sprite.h"

#define LIST_NONE 0xFFFFFFFFu
#define F_ONE     0x3F800000u
#define F_1E30    0x7149F2CAu
#define F_FLOOR   0x3089705Fu  // INV_W_FLOOR, 1e-9f
#define PCW_EOS   0x10000000u

// TaCmd of the splitter: what the next record is.
enum { TC_MAIN, TC_PARAM_B, TC_VTX_B, TC_VTX_B_END, TC_MODVOL_B, TC_SPRITE_B };
// VerxexDataFP: who takes vertex records.
enum { VD_NONE, VD_POLY, VD_MODVOL, VD_SPRITE };

typedef struct
{
	uint32_t nvtx, nstrip, nparam, strip_first, nmtri, nmvol;
	uint32_t min_w, max_w, max_inv_w;
	uint32_t mv_tris[5];
	uint32_t opt;  // TAOPT_* of the context's last RESET
} DecCtx;

// An LS window over one shadow array. Bytes below the write index are final, so every put of them,
// in any order, writes the same data; only the window being reused waits for its DMA.
typedef struct
{
	uint8_t* ls[2];
	uint32_t win, esize, off, cur, base, tag;
} Stream;

enum { S_VTX, S_STRIP, S_PARAM, S_MTRI, S_MVOL, S_OFFS, S_COUNT };

static uint8_t ls_vtx[2][16384] __attribute__((aligned(128)));
static uint8_t ls_strip[2][4096] __attribute__((aligned(128)));
static uint8_t ls_param[2][2048] __attribute__((aligned(128)));
static uint8_t ls_mtri[2][4096] __attribute__((aligned(128)));
static uint8_t ls_mvol[2][1024] __attribute__((aligned(128)));
static uint8_t ls_offs[2][4096] __attribute__((aligned(128)));
static Stream st[S_COUNT];

static DecCtx dctx[SPE_CTX];
static DecCtx* d;  // the context whose windows are open
static uint32_t out_ea[SPE_CTX];
static uint64_t out_base;

// Splitter and decoder state. As on the PPU it is global, not per context; zero/one starts mirror the statics.
static uint32_t tc, tc_type, vd, vd_type, strip_started;
static uint32_t cur_list;          // FifoSplitter::CurrentList
static uint32_t dec_list, mv_list; // s_cur_list, s_mv_list
static uint32_t face_a = F_ONE, face_r = F_ONE, face_g = F_ONE, face_b = F_ONE;
static uint32_t face8 = 0xFFFFFFFFu;  // P8: the face colour as saturated ARGB bytes (s_face8)
// 3b: the face offset colour of PolyParam 2B (s_foffs_*, s_foffs8) and the sprite's offset colour.
static uint32_t foffs_a = F_ONE, foffs_r = F_ONE, foffs_g = F_ONE, foffs_b = F_ONE;
static uint32_t foffs8 = 0xFFFFFFFFu;
static uint32_t sprite_offs;
static uint32_t clip_rect;            // P10: the last User_Tile_Clip rect (s_clip_rect)
static uint32_t sprite_col = 0xFFFFFFFFu;
static uint32_t spr_x[4], spr_y[4], spr_z[3];
static uint32_t type_lut[256];

// ---- float helpers -------------------------------------------------------

// clamp_byte: f * 255 rounded to 24 bits (fmuls), truncated (fctiwz), clamped. NaN gives 255.
static uint32_t clamp_byte(uint32_t b)
{
	const uint32_t e = (b >> 23) & 0xFF;
	if (e == 0xFF)
		return ((b & 0x7FFFFF) || !(b >> 31)) ? 255 : 0;
	if ((b >> 31) || !e)
		return 0;
	if (b >= F_ONE)
		return 255;
	const uint32_t p = ((b & 0x7FFFFF) | 0x800000) * 255u;
	const uint32_t s = (p >> 31) ? 8 : 7;
	uint32_t q = p >> s;
	const uint32_t rem = p & ((1u << s) - 1), half = 1u << (s - 1);
	if (rem > half || (rem == half && (q & 1)))
		q++;
	const uint32_t sh = 150 - e - s;
	return sh >= 32 ? 0 : q >> sh;
}

// The word of a double that holds its sign: the first one on the SPU. misc/ta4_check sets DEC_HI 1 for x86.
#ifndef DEC_HI
#define DEC_HI 0
#endif
static inline vec_double2 to_d(uint32_t b) { return spu_extend((vec_float4)spu_splats(b)); }
static inline uint32_t to_f(vec_double2 v) { return spu_extract((vec_uint4)spu_roundtf(v), 0); }

// clamp_byte(a * b): the exact product in double, rounded once to single as fmuls does.
static uint32_t mul_byte(uint32_t a, uint32_t b)
{
	const uint32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
	const uint32_t neg = (a ^ b) >> 31;
	if (ea == 0xFF || eb == 0xFF)
	{
		if ((a << 1) > 0xFF000000u || (b << 1) > 0xFF000000u || !(a << 1) || !(b << 1))
			return 255;  // NaN in, or Inf * 0
		return neg ? 0 : 255;
	}
	if (!ea || !eb || neg)
		return 0;
	if (ea + eb >= 254)
		return 255;  // |a * b| >= 1
	return clamp_byte(to_f(spu_mul(to_d(a), to_d(b))));
}

// (a + b) - c in single steps. Double rounding through double is exact for + and - (53 >= 2*24+1).
static uint32_t add_sub(uint32_t a, uint32_t b, uint32_t c)
{
	const uint32_t t = to_f(spu_add(to_d(a), to_d(b)));
	return to_f(spu_sub(to_d(t), to_d(c)));
}

// 1/x as fdivs gives it, for x in [INV_W_FLOOR, +Inf]. q starts within one ulp; the fused residual
// at each neighbouring midpoint has an exact sign and picks the side (1/x is never a midpoint).
static uint32_t recip(uint32_t x)
{
	if (x >= 0x7F800000u)
		return 0;
	const vec_float4 xf = (vec_float4)spu_splats(x);
	const vec_double2 xd = spu_extend(xf);
	const vec_double2 one = spu_splats(1.0), half = spu_splats(0.5);
	vec_double2 y = spu_extend(spu_re(xf));
	y = spu_madd(y, spu_nmsub(xd, y, one), y);
	y = spu_madd(y, spu_nmsub(xd, y, one), y);
	const uint32_t q = to_f(y);
	if (x >= 0x7E800000u)
	{
		echo.wden++;
		return q;
	}
	const vec_double2 qd = to_d(q);
	const vec_double2 ru = spu_nmsub(xd, spu_mul(spu_add(qd, to_d(q + 1)), half), one);
	if (!(spu_extract((vec_uint4)ru, DEC_HI) >> 31))
		return q + 1;
	const vec_double2 rd = spu_nmsub(xd, spu_mul(spu_add(qd, to_d(q - 1)), half), one);
	if (spu_extract((vec_uint4)rd, DEC_HI) >> 31)
		return q - 1;
	return q;
}

static inline uint32_t float_argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
	return (clamp_byte(a) << 24) | (clamp_byte(r) << 16) | (clamp_byte(g) << 8) | clamp_byte(b);
}

static inline uint32_t intensity_argb(uint32_t i)
{
	if (d->opt & TAOPT_INT_CLAMP)
	{
		// P8, as int_scale on the PPU: each channel is face * intensity / 256, alpha is the face alpha.
		const uint32_t s = clamp_byte(i);
		return (face8 & 0xFF000000u) | (((((face8 >> 16) & 0xFF) * s) >> 8) << 16) |
		       (((((face8 >> 8) & 0xFF) * s) >> 8) << 8) | (((face8 & 0xFF) * s) >> 8);
	}
	return (clamp_byte(face_a) << 24) | (mul_byte(face_r, i) << 16) | (mul_byte(face_g, i) << 8) | mul_byte(face_b, i);
}

// intensity_offs of the PPU: the face offset colour by the vertex's offset intensity, alpha unscaled.
static uint32_t intensity_offs(uint32_t i)
{
	if (d->opt & TAOPT_INT_CLAMP)
	{
		const uint32_t s = clamp_byte(i);
		return (foffs8 & 0xFF000000u) | (((((foffs8 >> 16) & 0xFF) * s) >> 8) << 16) |
		       (((((foffs8 >> 8) & 0xFF) * s) >> 8) << 8) | (((foffs8 & 0xFF) * s) >> 8);
	}
	return (clamp_byte(foffs_a) << 24) | (mul_byte(foffs_r, i) << 16) | (mul_byte(foffs_g, i) << 8) | mul_byte(foffs_b, i);
}

// track_w + track_inv_w; returns W. Signed compares on the bits, as the PPU's float compares order them.
static uint32_t track_w(uint32_t z)
{
	const uint32_t safe = ((int32_t)z >= (int32_t)F_FLOOR && z <= 0x7F800000u) ? z : F_FLOOR;
	const uint32_t w = recip(safe);
	if (w < d->min_w) d->min_w = w;
	if (w > d->max_w) d->max_w = w;
	if ((int32_t)d->max_inv_w < (int32_t)z && (int32_t)z < 0x49800000)
		d->max_inv_w = z;
	return w;
}

// ---- output streams ------------------------------------------------------

static void st_wait(uint32_t mask)
{
	echo.stage = SPE_STAGE_OUT;
	mfc_write_tag_mask(mask);
	mfc_read_tag_status_all();
	echo.stage = SPE_STAGE_RUN;
}

// Puts [base, end) of the open window: one 16-byte-aligned block, then an 8- and a 4-byte tail.
static void st_put(Stream* s, uint32_t end)
{
	const uint32_t tag = s->tag + s->cur;
	uint8_t* src = s->ls[s->cur];
	const uint64_t ea = out_base + s->off + s->base;
	uint32_t t = end & ~15u;
	if (t > s->base)
		mfc_put(src, ea, t - s->base, tag, 0, 0);
	t -= s->base;
	end -= s->base;
	if (end - t >= 8)
	{
		mfc_put(src + t, ea + t, 8, tag, 0, 0);
		t += 8;
	}
	if (end - t >= 4)
		mfc_put(src + t, ea + t, 4, tag, 0, 0);
}

// The write index left the window: put the final bytes, carry the last partial line to the other window.
static void st_slide(Stream* s, uint32_t b)
{
	st_put(s, b);
	const uint32_t nb = b & ~127u;
	const uint32_t o = s->cur ^ 1;
	st_wait(1u << (s->tag + o));
	memcpy(s->ls[o], s->ls[s->cur] + (nb - s->base), b - nb);
	s->cur = o;
	s->base = nb;
}

// Indexes only grow, so idx is never below the window.
static inline void* st_at(Stream* s, uint32_t idx)
{
	const uint32_t b = idx * s->esize;
	if (b + s->esize > s->base + s->win)
		st_slide(s, b);
	return s->ls[s->cur] + (b - s->base);
}

static void st_counts(const DecCtx* c, uint32_t* n)
{
	n[S_VTX] = c->nvtx;
	n[S_STRIP] = c->nstrip;
	n[S_PARAM] = c->nparam;
	n[S_MTRI] = c->nmtri;
	n[S_MVOL] = c->nmvol;
	// 3b: one offset colour per vertex, only for a context that keeps them.
	n[S_OFFS] = (c->opt & TAOPT_OFFS) ? c->nvtx : 0;
}

static void st_flush(void)
{
	uint32_t n[S_COUNT];
	st_counts(d, n);
	for (int i = 0; i < S_COUNT; i++)
		st_put(&st[i], n[i] * st[i].esize);
}

// Opens the windows at the lists' ends; the partial line there comes back from main memory.
static void st_open(void)
{
	uint32_t n[S_COUNT];
	st_counts(d, n);
	out_base = out_ea[d - dctx];
	for (int i = 0; i < S_COUNT; i++)
	{
		Stream* s = &st[i];
		const uint32_t b = n[i] * s->esize;
		s->cur = 0;
		s->base = b & ~127u;
		if (b > s->base)
			mfc_get(s->ls[0], out_base + s->off + s->base, ((b + 15) & ~15u) - s->base, s->tag, 0, 0);
	}
	st_wait(TAG_OUT_MASK);
}

static void bind(uint32_t ctx)
{
	DecCtx* c = &dctx[ctx & (SPE_CTX - 1)];
	if (c == d)
		return;
	if (d)
		st_flush();
	st_wait(TAG_OUT_MASK);
	d = c;
	st_open();
}

// ---- decoder (Ps3VertexDecoder) ------------------------------------------

static void reset_lists(DecCtx* c)
{
	memset(c, 0, sizeof(*c));
	c->min_w = F_1E30;
	c->max_inv_w = F_ONE;
}

static void add_param(const uint32_t* r)
{
	if (d->nparam >= SPE_MAX_PARAMS)
	{
		echo.pfull++;
		return;
	}
	SpeParam* p = (SpeParam*)st_at(&st[S_PARAM], d->nparam++);
	p->pcw = r[0];
	p->isp = r[1];
	p->tsp = r[2];
	p->tcw = r[3];
	if (d->opt & TAOPT_TILE_CLIP)
	{
		// P10, as add_param on the PPU: the clip rect in the reserved bits, 0 without a clip mode.
		const uint32_t clip = ((r[0] >> 16) & 3) >= 2 ? clip_rect : 0;
		p->isp = (r[1] & 0xFFF00000u) | (clip & 0xFFFFFu);
		p->pcw = (r[0] & ~0x300u) | ((clip >> 20) << 8);
	}
}

static inline void set_face(const uint32_t* f)
{
	face_a = f[0];
	face_r = f[1];
	face_g = f[2];
	face_b = f[3];
	face8 = float_argb(f[0], f[1], f[2], f[3]);
}

// PolyParam 2B: the face offset colour follows the face colour.
static inline void set_foffs(const uint32_t* f)
{
	foffs_a = f[0];
	foffs_r = f[1];
	foffs_g = f[2];
	foffs_b = f[3];
	foffs8 = float_argb(f[0], f[1], f[2], f[3]);
}

// 3b: vertex i's offset colour, as VTX_OFFS on the PPU under TAOPT_OFFS; a type with none gets 0.
static inline void put_offs(uint32_t i, uint32_t argb)
{
	*(uint32_t*)st_at(&st[S_OFFS], i) = argb;
}

// mv_kept and mv_cap of the PPU: the opaque volume list, with P27 the translucent one too; P66's cap.
static inline int mv_kept(uint32_t list)
{
	return list == 1 || (list == 3 && (d->opt & TAOPT_TR_VOL));
}

static inline uint32_t mv_cap(void)
{
	return (d->opt & TAOPT_VOL_8K) ? SPE_MAX_MODTRI : SPE_MODTRI_OLD;
}

static void end_list(uint32_t list)
{
	if (!mv_kept(list) || d->nmvol >= SPE_MAX_MODVOL)
		return;
	SpeModVol* p = (SpeModVol*)st_at(&st[S_MVOL], d->nmvol++);
	p->first = d->nmtri;
	p->cull = p->depth = p->volume_last = 0;
	p->list = (uint8_t)(list == 3);
}

static void strip_end(void)
{
	const uint32_t count = d->nvtx - d->strip_first;
	if (count < 3 || !d->nparam || d->nstrip >= SPE_MAX_STRIPS)
		return;
	SpeStrip* s = (SpeStrip*)st_at(&st[S_STRIP], d->nstrip++);
	s->first = d->strip_first;
	s->count = (uint16_t)(count > 0xFFFF ? 0xFFFF : count);
	s->list = (uint8_t)dec_list;
	s->pad = 0;
	s->param = (uint16_t)(d->nparam - 1);
	s->pad2 = 0;
}

static inline int is64(uint32_t t)
{
	return t == 5 || t == 6 || t == 11 || t == 12 || t == 13 || t == 14;
}

static inline void uv16(SpeVtx* v, uint32_t w)
{
	v->u = w & 0xFFFF0000u;
	v->v = w << 16;
}

// The 32-byte types (VTX_ONE). Types without UV get 0, as the PPU writes them since S2.
static void vtx_one(uint32_t t, const uint32_t* r)
{
	if (d->nvtx >= SPE_MAX_VTX)
	{
		echo.vtx_of++;
		return;
	}
	SpeVtx* v = (SpeVtx*)st_at(&st[S_VTX], d->nvtx);
	v->x = r[1];
	v->y = r[2];
	v->w = track_w(r[3]);
	switch (t)
	{
	case 0: v->col = r[6]; v->u = v->v = 0; break;
	case 1: v->col = float_argb(r[4], r[5], r[6], r[7]); v->u = v->v = 0; break;
	case 2: v->col = intensity_argb(r[6]); v->u = v->v = 0; break;
	case 3: v->col = r[6]; v->u = r[4]; v->v = r[5]; break;
	case 4: v->col = r[6]; uv16(v, r[4]); break;
	case 7: v->col = intensity_argb(r[6]); v->u = r[4]; v->v = r[5]; break;
	case 8: v->col = intensity_argb(r[6]); uv16(v, r[4]); break;
	case 9: v->col = r[4]; v->u = v->v = 0; break;
	case 10: v->col = intensity_argb(r[4]); v->u = v->v = 0; break;
	}
	if (d->opt & TAOPT_OFFS)
		put_offs(d->nvtx, (t == 3 || t == 4) ? r[7] : (t == 7 || t == 8) ? intensity_offs(r[7]) : 0);
	d->nvtx++;
}

// First half of a 64-byte vertex (VTX_A): position, UV and the colour where it lives in part A.
static void vtx_a(uint32_t t, const uint32_t* r)
{
	if (d->nvtx >= SPE_MAX_VTX)
	{
		echo.vtx_of++;
		return;
	}
	SpeVtx* v = (SpeVtx*)st_at(&st[S_VTX], d->nvtx);
	v->x = r[1];
	v->y = r[2];
	v->w = track_w(r[3]);
	switch (t)
	{
	case 5: v->u = r[4]; v->v = r[5]; break;
	case 6: uv16(v, r[4]); break;
	case 11: v->col = r[6]; v->u = r[4]; v->v = r[5]; break;
	case 12: v->col = r[6]; uv16(v, r[4]); break;
	case 13: v->col = intensity_argb(r[6]); v->u = r[4]; v->v = r[5]; break;
	case 14: v->col = intensity_argb(r[6]); uv16(v, r[4]); break;
	}
	// Volume 0 is drawn, so its offset colour is the one kept; types 5 and 6 have theirs in the second half.
	if ((d->opt & TAOPT_OFFS) && t >= 11)
		put_offs(d->nvtx, t <= 12 ? r[7] : intensity_offs(r[7]));
}

// Second half (VTX_B / VTX_B_KEEP): b is the record at byte 32 of the vertex.
static void vtx_b(uint32_t t, const uint32_t* b)
{
	if (d->nvtx >= SPE_MAX_VTX)
		return;
	if (t == 5 || t == 6)
	{
		SpeVtx* v = (SpeVtx*)st_at(&st[S_VTX], d->nvtx);
		v->col = float_argb(b[0], b[1], b[2], b[3]);
		if (d->opt & TAOPT_OFFS)
			put_offs(d->nvtx, float_argb(b[4], b[5], b[6], b[7]));
	}
	d->nvtx++;
}

static void sprite_a(const uint32_t* r)
{
	spr_x[0] = r[1]; spr_y[0] = r[2]; spr_z[0] = r[3];
	spr_x[1] = r[4]; spr_y[1] = r[5]; spr_z[1] = r[6];
	spr_x[2] = r[7];
}

// D completes the parallelogram; strip order D, C, A, B.
static void sprite_b(const uint32_t* b)
{
	if (d->nvtx + 4 > SPE_MAX_VTX)
	{
		echo.vtx_of++;
		return;
	}
	spr_y[2] = b[0];
	spr_z[2] = b[1];
	spr_x[3] = b[2];
	spr_y[3] = b[3];
	uint32_t w[4], u[4], v[4];
	uint32_t z3 = add_sub(spr_z[0], spr_z[2], spr_z[1]);
	for (int k = 0; k < 3; k++)
	{
		u[k] = b[5 + k] & 0xFFFF0000u;
		v[k] = b[5 + k] << 16;
	}
	u[3] = add_sub(u[0], u[2], u[1]);
	v[3] = add_sub(v[0], v[2], v[1]);
	if (d->opt & TAOPT_SPR_PLANE)
	{
		// P80, as the PPU: D's own x and y on the plane through A, B and C.
		uint32_t o[3];
		if (spr_plane(spr_x, spr_y, spr_z, u, v, o))
		{
			z3 = o[0];
			u[3] = o[1];
			v[3] = o[2];
			echo.spr_plane++;
		}
	}
	w[0] = track_w(spr_z[0]);
	w[1] = track_w(spr_z[1]);
	w[2] = track_w(spr_z[2]);
	w[3] = track_w(z3);

	static const int order[4] = { 3, 2, 0, 1 };
	d->strip_first = d->nvtx;
	for (int i = 0; i < 4; i++)
	{
		const int k = order[i];
		if (d->opt & TAOPT_OFFS)
			put_offs(d->nvtx, sprite_offs);
		SpeVtx* o = (SpeVtx*)st_at(&st[S_VTX], d->nvtx++);
		o->x = spr_x[k];
		o->y = spr_y[k];
		o->w = w[k];
		o->col = sprite_col;
		o->u = u[k];
		o->v = v[k];
	}
	strip_end();
}

static void modvol_param(const uint32_t* r)
{
	mv_list = dec_list;
	if (!mv_kept(dec_list) || d->nmvol >= SPE_MAX_MODVOL)
		return;
	SpeModVol* p = (SpeModVol*)st_at(&st[S_MVOL], d->nmvol++);
	p->first = d->nmtri;
	p->cull = (uint8_t)((r[1] >> 27) & 3);
	p->depth = (uint8_t)(r[1] >> 29);
	p->volume_last = (uint8_t)((r[0] >> 6) & 1);
	p->list = (uint8_t)(dec_list == 3);
}

static void modvol_a(const uint32_t* r)
{
	d->mv_tris[mv_list % 5]++;
	if (!mv_kept(dec_list) || d->nmtri >= mv_cap())
		return;
	SpeModTri* t = (SpeModTri*)st_at(&st[S_MTRI], d->nmtri);
	for (int k = 0; k < 7; k++)
		t->v[k] = r[1 + k];
	track_w(r[3]);
	track_w(r[6]);
}

static void modvol_b(const uint32_t* b)
{
	if (!mv_kept(dec_list) || d->nmtri >= mv_cap())
		return;
	SpeModTri* t = (SpeModTri*)st_at(&st[S_MTRI], d->nmtri++);
	t->v[7] = b[0];
	t->v[8] = b[1];
	track_w(b[1]);
}

// ---- splitter (FifoSplitter) ---------------------------------------------

// ta_poly_data. Unlike the PPU loop, a non-vertex record ends the run and goes back to the main switch.
static const uint32_t* poly_data(const uint32_t* r, const uint32_t* end)
{
	const uint32_t t = vd_type;
	if (!strip_started)
	{
		d->strip_first = d->nvtx;
		strip_started = 1;
	}
	if (!is64(t))
	{
		for (; r <= end; r += 8)
		{
			if ((r[0] >> 29) != 7)
			{
				echo.odd++;
				return r;
			}
			vtx_one(t, r);
			if (r[0] & PCW_EOS)
			{
				strip_started = 0;
				strip_end();
				return r + 8;
			}
		}
		return r;
	}
	for (; r < end; r += 16)
	{
		if ((r[0] >> 29) != 7)
		{
			echo.odd++;
			return r;
		}
		vtx_a(t, r);
		vtx_b(t, r + 8);
		if (r[0] & PCW_EOS)
		{
			strip_started = 0;
			strip_end();
			return r + 16;
		}
	}
	if (r == end)
	{
		if ((r[0] >> 29) != 7)
		{
			echo.odd++;
			return r;
		}
		vtx_a(t, r);
		tc = (r[0] & PCW_EOS) ? TC_VTX_B_END : TC_VTX_B;
		tc_type = t;
		r += 8;
	}
	return r;
}

static inline void list_start(uint32_t list)
{
	cur_list = list;
	dec_list = list;
}

// One step of ta_main or of the pending second half; end is the last record available.
static const uint32_t* step(const uint32_t* r, const uint32_t* end)
{
	switch (tc)
	{
	case TC_PARAM_B:
		tc = TC_MAIN;
		set_face(r);
		if (tc_type == 2)
			set_foffs(r + 4);
		return r + 8;
	case TC_VTX_B:
	case TC_VTX_B_END:
	{
		const uint32_t ends = (tc == TC_VTX_B_END);
		tc = TC_MAIN;
		vtx_b(tc_type, r);
		if (ends)
		{
			strip_started = 0;
			strip_end();
		}
		return r + 8;
	}
	case TC_MODVOL_B:
		tc = TC_MAIN;
		modvol_b(r);
		return r + 8;
	case TC_SPRITE_B:
		tc = TC_MAIN;
		sprite_b(r);
		return r + 8;
	}

	const uint32_t pcw = r[0];
	switch (pcw >> 29)
	{
	case 0:  // End_Of_List
		if (cur_list != LIST_NONE)
			end_list(cur_list);
		cur_list = LIST_NONE;
		vd = VD_NONE;
		return r + 8;
	case 1:  // User_Tile_Clip: xmin, ymin, xmax, ymax in words 4..7 (the splitter's data_32[3..6])
		clip_rect = (r[4] & 63) | ((r[6] & 63) << 6) | ((r[5] & 31) << 12) | ((r[7] & 31) << 17);
		return r + 8;
	case 4:  // Polygon or Modifier Volume
	{
		if (cur_list == LIST_NONE)
			list_start((pcw >> 24) & 7);
		if (cur_list & 1)
		{
			modvol_param(r);
			vd = VD_MODVOL;
			return r + 8;
		}
		const uint32_t uid = type_lut[pcw & 0xFF];
		const uint32_t psz = uid >> 30, pdid = uid & 0xFF, ppid = (uid >> 8) & 0xFF;
		if (pdid > 14 || ppid > 4)
		{
			echo.bad++;
			return r + 8;
		}
		vd = VD_POLY;
		vd_type = pdid;
		if (r != end || psz == 1)
		{
			add_param(r);
			if (ppid == 1)
				set_face(r + 4);
			else if (ppid == 2 || ppid == 4)
				set_face(r + 8);
			if (ppid == 2)
				set_foffs(r + 12);
			return r + psz * 8;
		}
		add_param(r);
		tc = TC_PARAM_B;
		tc_type = ppid;
		return r + 8;
	}
	case 5:  // Sprite
		if (cur_list == LIST_NONE)
			list_start((pcw >> 24) & 7);
		vd = VD_SPRITE;
		add_param(r);
		sprite_col = r[4];
		sprite_offs = r[5];
		return r + 8;
	case 7:  // Vertex
		switch (vd)
		{
		case VD_POLY:
			return poly_data(r, end);
		case VD_MODVOL:
			modvol_a(r);
			if (r == end)
			{
				tc = TC_MODVOL_B;
				return r + 8;
			}
			modvol_b(r + 8);
			return r + 16;
		case VD_SPRITE:
			sprite_a(r);
			if (r == end)
			{
				tc = TC_SPRITE_B;
				return r + 8;
			}
			sprite_b(r + 8);
			return r + 16;
		default:
			return r + 8;
		}
	default:  // Object_List_Set and the reserved types
		echo.bad++;
		return r + 8;
	}
}

// ---- entry points --------------------------------------------------------

// FifoSplitter::Init: data type, header type and size per obj_ctrl.
static void build_lut(void)
{
	for (uint32_t i = 0; i < 256; i++)
	{
		const uint32_t uv16b = i & 1, tex = (i >> 3) & 1, col = (i >> 4) & 3, vol = (i >> 6) & 1, offs = (i >> 2) & 1;
		uint32_t rv;
		if (tex)
		{
			if (!vol)
				rv = (col == 0 ? 3 : col == 1 ? 5 : 7) + uv16b;
			else
				rv = (col == 0) ? 11 + uv16b : (col == 1) ? 0xFFFFFFFFu : 13 + uv16b;
		}
		else
		{
			if (!vol)
				rv = (col == 0) ? 0 : (col == 1) ? 1 : 2;
			else
				rv = (col == 0) ? 9 : (col == 1) ? 0xFFFFFFFFu : 10;
		}
		uint32_t type;
		if (!vol)
			type = (col < 2 || col == 3) ? 0 : (tex && offs) ? (2 | 0x80) : 1;
		else
			type = (col == 0 || col == 3) ? 3 : (col == 2) ? (4 | 0x80) : 0xFFDDEEAAu;
		rv |= (type & 0x80) ? (2u << 30) : (1u << 30);
		rv |= (type & 0x7F) << 8;
		type_lut[i] = rv;
	}
}

void dec_init(const SpeSetup* setup)
{
	static uint8_t* const win[S_COUNT][2] = {
		{ ls_vtx[0], ls_vtx[1] }, { ls_strip[0], ls_strip[1] }, { ls_param[0], ls_param[1] },
		{ ls_mtri[0], ls_mtri[1] }, { ls_mvol[0], ls_mvol[1] }, { ls_offs[0], ls_offs[1] } };
	static const uint32_t size[S_COUNT] = {
		sizeof(ls_vtx[0]), sizeof(ls_strip[0]), sizeof(ls_param[0]), sizeof(ls_mtri[0]), sizeof(ls_mvol[0]),
		sizeof(ls_offs[0]) };
	static const uint32_t esize[S_COUNT] = {
		sizeof(SpeVtx), sizeof(SpeStrip), sizeof(SpeParam), sizeof(SpeModTri), sizeof(SpeModVol), 4 };
	static const uint32_t off[S_COUNT] = {
		offsetof(SpeOut, vtx), offsetof(SpeOut, strip), offsetof(SpeOut, param),
		offsetof(SpeOut, mtri), offsetof(SpeOut, mvol), offsetof(SpeOut, offs) };
	for (int i = 0; i < S_COUNT; i++)
	{
		st[i].ls[0] = win[i][0];
		st[i].ls[1] = win[i][1];
		st[i].win = size[i];
		st[i].esize = esize[i];
		st[i].off = off[i];
		st[i].tag = TAG_OUT0 + 2 * i;
	}
	for (int i = 0; i < SPE_CTX; i++)
	{
		out_ea[i] = setup->out_ea[i];
		reset_lists(&dctx[i]);
		dctx[i].opt = setup->opt;
	}
	build_lut();
	d = &dctx[0];
	st_open();
}

// FifoSplitter::ListInit. Its EndList lands on lists that are cleared right after, so it is left out.
void dec_reset(uint32_t ctx, uint32_t opt)
{
	DecCtx* c = &dctx[ctx & (SPE_CTX - 1)];
	tc = TC_MAIN;
	strip_started = 0;
	cur_list = LIST_NONE;
	reset_lists(c);
	c->opt = opt;
	// As Ps3VertexDecoder::ListInit: P63 makes the face colour white, P10 puts the clip rect back.
	if (opt & TAOPT_FACE_RESET)
	{
		face_a = face_r = face_g = face_b = F_ONE;
		face8 = 0xFFFFFFFFu;
		foffs_a = foffs_r = foffs_g = foffs_b = F_ONE;
		foffs8 = 0xFFFFFFFFu;
	}
	if (opt & TAOPT_TILE_CLIP)
		clip_rect = TA_CLIP_RESET;
	if (c == d)
	{
		// Puts of the old lists still in flight must not land on the new ones.
		st_wait(TAG_OUT_MASK);
		st_open();
	}
}

void dec_records(uint32_t ctx, const uint32_t* rec, uint32_t n)
{
	bind(ctx);
	const uint32_t* end = rec + (n - 1) * 8;
	while (rec <= end)
		rec = step(rec, end);
}

void dec_fence(uint32_t ctx, SpeFence* res)
{
	const DecCtx* c = &dctx[ctx & (SPE_CTX - 1)];
	if (c == d)
		st_flush();
	st_wait(TAG_OUT_MASK);
	res->nvtx = c->nvtx;
	res->nstrip = c->nstrip;
	res->nparam = c->nparam;
	res->nmtri = c->nmtri;
	res->nmvol = c->nmvol;
	res->max_inv_w = c->max_inv_w;
	res->min_w = c->min_w;
	res->max_w = c->max_w;
	for (int k = 0; k < 5; k++)
		res->mv_tris[k] = c->mv_tris[k];
	res->opt = c->opt;
}
