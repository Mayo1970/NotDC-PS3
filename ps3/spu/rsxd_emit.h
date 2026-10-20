#pragma once
// RSX Direct (G17): the run draws' RSX words without ps3gl. apply_param_body and draw_vb of ps3_rend.cpp as plain C,
// for the PPU (ps3/ps3_rsxd.cpp), the SPU (spe_tex.c) and misc/rsxdirect_check. The includer defines rsxd_reserve().
#include "rsxd.h"

// The large steps: inlined on the PPU, one copy each on the SPU (spe_tex.c sets it), where local store is short.
#ifndef RSXD_BIG
#define RSXD_BIG inline
#endif

// Provided by the includer: room for n more words at op (0 = none; the draw is dropped), and the dirty flag of a
// texture object (clear: also clear it).
static int rsxd_reserve(RsxdState* s, uint32_t n);
static uint32_t rsxd_tex_dirty(RsxdState* s, uint32_t obj, int clear);

// No register known, no program: the next draw sends all it needs.
static inline void rsxd_forget(RsxdState* s)
{
	s->rok[0] = s->rok[1] = 0;
	s->rch[0] = s->rch[1] = 0;
	s->fp_key = -1;
	s->vp_id = -1;
	s->fog_up = 0;
	memset(s->xt_up, 0, sizeof(s->xt_up));
}

// ---- apply_param_body ----

static inline uint32_t rx_bits(uint32_t w, int lo, int n) { return (w >> lo) & ((1u << n) - 1u); }

// ISP and TSP fields by their DC bit numbers (ta_structs.h).
#define P_PCW(p) ((p)[0])
#define P_ISP(p) ((p)[1])
#define P_TSP(p) ((p)[2])
#define P_TCW(p) ((p)[3])

static const uint16_t rx_blend_src[8] = { 0x0000, 0x0001, 0x0306, 0x0307, 0x0302, 0x0303, 0x0304, 0x0305 };
static const uint16_t rx_blend_dst[8] = { 0x0000, 0x0001, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305 };
static const uint8_t rx_tex_env[4] = { RXE_REPLACE, RXE_MODULATE, RXE_DECAL, RXE_MODULATE };
static const unsigned char rx_depth_cmp[2][8] = { { 0, 4, 2, 6, 1, 5, 3, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 } };
// tril_weight by FilterMode 2 / 3 and MipMapD & 3, as float bits.
static const uint32_t rx_tril[2][4] = { { 0x3F800000u, 0x3F400000u, 0x3F000000u, 0x3E800000u },
                                        { 0x00000000u, 0x3E800000u, 0x3F000000u, 0x3F400000u } };
#define RX_F_ONE   0x3F800000u
#define RX_F_M1E9  0xCE6E6B28u  // -1e9f
#define RX_F_P1E9  0x4E6E6B28u  // 1e9f

static inline uint32_t rx_depth(const RsxdState* s, uint32_t mode)
{
	return 0x0200u + rx_depth_cmp[(s->sc->flags & RSXD_S_ZFLOAT) ? 1 : 0][mode & 7];
}

static inline uint32_t rx_eff_cull(const RsxdState* s, const uint32_t* p)
{
	return rx_bits(P_ISP(p), 27, 2) ^ (uint32_t)((s->sc->flags & RSXD_S_SPRCULL) && (P_PCW(p) >> 29) == 5);
}

static inline void rx_set_cull(RsxdState* s, uint32_t mode)
{
	if (mode < 2)
	{
		s->gl.cull_en = 0;
		return;
	}
	s->gl.cull_en = 1;
	s->gl.cull_face = mode == 2 ? RX_CULL_FRONT : RX_CULL_BACK;
}

// glScissor: ps3gl stores the RSX y, through the casts of its rs fields.
static inline void rx_scissor(RsxdState* s, int x, int y, int w, int h)
{
	const int rsx_y = (int)s->sc->screen_h - y - h;
	s->gl.sc_x = (int16_t)x;
	s->gl.sc_y = (int16_t)rsx_y;
	s->gl.sc_w = (uint16_t)w;
	s->gl.sc_h = (uint16_t)h;
}

static inline void rx_clip_base(RsxdState* s)
{
	if (s->gl.clip_base)
		return;
	s->gl.clip_base = 1;
	if (s->sc->base_scissor)
		rx_scissor(s, s->sc->bx, s->sc->by, s->sc->bw, s->sc->bh);
	else
		s->gl.scissor_en = 0;
}

static inline void rx_clip_apply(RsxdState* s, const RsxdClip* c)
{
	s->gl.clip_mode = RXC_NONE;
	if (c->mode == RXC_BASE)
	{
		rx_clip_base(s);
		return;
	}
	memcpy(s->gl.clip_r, c->r, sizeof(c->r));
	s->gl.clip_base = 0;
	s->gl.scissor_en = 1;
	s->gl.clip_mode = c->mode;
	if (c->mode == RXC_INSIDE)
		rx_scissor(s, c->r[0], c->r[1], c->r[2] - c->r[0], c->r[3] - c->r[1]);
}

// bump_offs, fogv_offs and offc_offs of ps3_rend.cpp are set: the lists carry offset colours.
static inline int rx_bump_offs(const RsxdState* s, const uint32_t* p)
{
	return (s->sc->flags & RSXD_S_BUMP) && (s->sc->flags & RSXD_S_LISTS_OFFS) && rx_bits(P_PCW(p), 3, 1) &&
	       rx_bits(P_TCW(p), 27, 3) == 4;
}
static inline int rx_fogv_param(const uint32_t* p)
{
	return rx_bits(P_PCW(p), 3, 1) && rx_bits(P_PCW(p), 2, 1) && rx_bits(P_TSP(p), 22, 2) == 1 &&
	       rx_bits(P_TCW(p), 27, 3) != 4;
}
static inline int rx_offc_offs(const RsxdState* s, const uint32_t* p)
{
	return (s->sc->flags & RSXD_S_OFFS) && (s->sc->flags & RSXD_S_LISTS_OFFS) && rx_bits(P_PCW(p), 3, 1) &&
	       rx_bits(P_PCW(p), 2, 1) && rx_bits(P_TCW(p), 27, 3) != 4;
}

static inline uint32_t rx_fog_mode(const RsxdState* s, const uint32_t* p, uint32_t textured, uint32_t bg)
{
	switch (rx_bits(P_TSP(p), 22, 2))
	{
	case 0: return RXF_TABLE;
	case 1: return textured && !bg && s->sc->fog >= 2 && (s->sc->flags & RSXD_S_LISTS_OFFS) && rx_fogv_param(p) ? RXF_VERTEX : RXF_NONE;
	case 3: return textured && rx_bits(P_TSP(p), 6, 2) == 1 ? RXF_BASE_TA : RXF_BASE;
	default: return RXF_NONE;
	}
}

static inline uint32_t rx_tril_bits(const uint32_t* p, uint32_t list, uint32_t textured)
{
	const uint32_t fm = rx_bits(P_TSP(p), 13, 2);
	if (!textured || fm < 2 || list == RXL_PUNCH || !rx_bits(P_TCW(p), 31, 1))
		return RX_F_ONE;
	return rx_tril[fm - 2][rx_bits(P_TSP(p), 8, 2)];
}

static inline void rx_xt_apply(RsxdState* s, const uint32_t* p, uint32_t list, uint32_t textured)
{
	const uint32_t clamp = (s->sc->flags & RSXD_S_CLAMP) && rx_bits(P_TSP(p), 21, 1);
	const uint32_t tril = (s->sc->flags & RSXD_S_TRIL) ? rx_tril_bits(p, list, textured) : RX_F_ONE;
	if (!clamp && tril == RX_F_ONE)
	{
		s->gl.xt = 0;
		return;
	}
	s->gl.xt = 1;
	for (int k = 0; k < 4; k++)
	{
		s->gl.xt_v[k] = clamp ? s->sc->clamp_min[k] : RX_F_M1E9;
		s->gl.xt_v[4 + k] = clamp ? s->sc->clamp_max[k] : RX_F_P1E9;
	}
	s->gl.xt_v[8] = tril;
	s->n_cclamp += clamp;
	s->n_tril += tril != RX_F_ONE;
}

static inline uint32_t rx_tri_covers(const uint32_t* p)
{
	const uint32_t src = rx_bits(P_TSP(p), 29, 3), dst = rx_bits(P_TSP(p), 26, 3);
	if (src == 1 && dst == 0)
		return 2;
	return src == 4 && dst == 5;
}

// apply_param_body: p = pcw, isp, tsp, tcw. textured: the Texture bit and a bind that worked; tex its words. clip
// is read only with a Tile Clip level. Returns the OR mask that forces vertex alpha to 255. The background (RXL_BG)
// is smooth and has no offset colours.
static RSXD_BIG uint32_t rsxd_apply(RsxdState* s, const uint32_t* p, uint32_t list, uint32_t textured, const RsxdTex* tex,
                                  const RsxdClip* clip)
{
	RsxdGl* g = &s->gl;
	const RsxdScene* sc = s->sc;
	const RsxdSec* se = s->sec;
	const uint32_t tsp = P_TSP(p), isp = P_ISP(p);
	const uint32_t bg = list & RXL_BG;
	list &= ~(uint32_t)RXL_BG;
	const uint32_t zwd = rx_bits(isp, 26, 1);
	if (se->depth_pass)
	{
		g->blend_en = 0;
		g->alpha_en = 0;
		g->tex_en = 0;
		g->bump = 0;
		if (sc->fog)
			g->fog = RXF_NONE;
		if (sc->flags & RSXD_S_OFFS)
			g->offs = 0;
		if (sc->flags & (RSXD_S_CLAMP | RSXD_S_TRIL))
			g->xt = 0;
		g->depth_func = rx_depth(s, 6);
		g->depth_mask = !zwd;
		rx_set_cull(s, rx_eff_cull(s, p));
		if (sc->clip)
			rx_clip_apply(s, clip);
		return 0;
	}
	const uint32_t src_i = rx_bits(tsp, 29, 3), dst_i = rx_bits(tsp, 26, 3);
	if (list == RXL_TRANSLUCENT)
	{
		g->blend_en = 1;
		g->blend_src = rx_blend_src[src_i];
		g->blend_dst = rx_blend_dst[dst_i];
		g->alpha_en = src_i == 4 && (dst_i == 1 || dst_i == 5);
		g->depth_func = rx_depth(s, se->autosort ? 6 : rx_bits(isp, 29, 3));
	}
	else
	{
		if ((list == RXL_PUNCH && sc->pt_blend) || (list == RXL_OPAQUE && se->pass_nz))
		{
			uint32_t src = src_i, dst = dst_i;
			const uint32_t one = list == RXL_PUNCH && sc->pt_blend == 2;
			if (one)
			{
				src = src == 4 ? 1 : src == 5 ? 0 : src;
				dst = dst == 4 ? 1 : dst == 5 ? 0 : dst;
			}
			if (one && src == 1 && dst == 0)
				g->blend_en = 0;
			else
			{
				g->blend_en = 1;
				g->blend_src = rx_blend_src[src];
				g->blend_dst = rx_blend_dst[dst];
			}
		}
		else
			g->blend_en = 0;
		g->depth_func = rx_depth(s, list == RXL_PUNCH ? 6 : rx_bits(isp, 29, 3));
	}
	uint32_t zwrite = !zwd;
	if (list == RXL_PUNCH && (sc->flags & RSXD_S_PT_DEPTH))
		zwrite = 1;
	else if (list == RXL_TRANSLUCENT && se->autosort && ((sc->flags & RSXD_S_TR_NODEPTH) || se->tri_mode == RXT_NODEPTH))
		zwrite = 0;
	g->depth_mask = zwrite;
	rx_set_cull(s, rx_eff_cull(s, p));
	if (sc->flat)
	{
		const uint32_t flat = !bg && !rx_bits(P_PCW(p), 1, 1);
		g->shade = flat ? RX_FLAT : RX_SMOOTH;
		g->flat_shift = (flat && sc->flat == 2) ? 2 : 0;
	}
	if (sc->clip)
		rx_clip_apply(s, clip);
	g->st_func = RX_ALWAYS;
	g->st_ref = rx_bits(P_PCW(p), 7, 1) ? 0x80 : 0;
	g->st_mask = 0xFF;

	if (textured)
	{
		g->tex_en = 1;
		g->texenv = rx_tex_env[rx_bits(tsp, 6, 2)];
		g->tex = *tex;
		g->has_tex = tex->valid;
	}
	else
		g->tex_en = 0;
	g->bump = textured && !bg && rx_bump_offs(s, p);
	if (sc->fog)
		g->fog = rx_fog_mode(s, p, textured, bg);
	if (sc->flags & RSXD_S_OFFS)
	{
		const uint32_t offc = textured && !bg && rx_offc_offs(s, p);
		g->offs = offc;
		s->n_offc += offc;
	}
	if (sc->flags & (RSXD_S_CLAMP | RSXD_S_TRIL))
		rx_xt_apply(s, p, list, textured);

	if (se->tri_mode == RXT_OPAQUE)
	{
		const uint32_t covers = rx_tri_covers(p);
		g->blend_en = 0;
		g->alpha_en = covers != 2;
		g->depth_mask = covers && !zwd;
	}
	const uint32_t force = !rx_bits(tsp, 20, 1) || (textured && rx_bits(tsp, 6, 2) == 1);
	return force ? 0xFF000000u : 0;
}

// ---- the draw: ps3gl's apply_states, apply_textures, apply_shader, bind_vertex_attribs, rsxDrawVertexArray ----

// No branch: the SPU has no predictor, and slot is a constant here, so this is a few selects.
static inline void rx_want(RsxdState* s, int slot, uint32_t v)
{
	const uint32_t w = (uint32_t)slot >> 5, bit = 1u << (slot & 31);
	const uint32_t same = (uint32_t)((s->rok[w] & bit) != 0) & (uint32_t)(s->rv[slot] == v);
	s->rv[slot] = v;
	s->rok[w] |= bit;
	s->rch[w] |= bit & (same - 1u);
}

// ps3gl_apply_states' scissor: a scissor clamped to the screen, or the whole screen.
static inline void rx_want_scissor(RsxdState* s)
{
	const RsxdGl* g = &s->gl;
	const int scw = (int)s->sc->screen_w, sch = (int)s->sc->screen_h;
	int sx = 0, sy = 0, sw = scw, sh = sch;
	if (g->scissor_en)
	{
		sx = g->sc_x;
		sy = g->sc_y;
		sw = g->sc_w;
		sh = g->sc_h;
		if (sx < 0) { sw += sx; sx = 0; }
		if (sy < 0) { sh += sy; sy = 0; }
		if (sw < 1) sw = 1;
		if (sh < 1) sh = 1;
		if (sx + sw > scw) sw = scw - sx;
		if (sy + sh > sch) sh = sch - sy;
		if (sw < 1) sw = 1;
		if (sh < 1) sh = 1;
	}
	rx_want(s, RX_SC_H, ((uint32_t)(uint16_t)sw << 16) | (uint16_t)sx);
	rx_want(s, RX_SC_V, ((uint32_t)(uint16_t)sh << 16) | (uint16_t)sy);
}

static inline void rx_want_state(RsxdState* s)
{
	const RsxdGl* g = &s->gl;
	rx_want(s, RX_BLEND_EN, g->blend_en);
	if (g->blend_en)
	{
		rx_want(s, RX_BLEND_SRC, (g->blend_src << 16) | g->blend_src);
		rx_want(s, RX_BLEND_DST, (g->blend_dst << 16) | g->blend_dst);
		rx_want(s, RX_BLEND_EQ, (RX_FUNC_ADD << 16) | RX_FUNC_ADD);
	}
	rx_want(s, RX_ALPHA_EN, g->alpha_en);
	if (g->alpha_en)
	{
		rx_want(s, RX_ALPHA_FUNC, g->alpha_func);
		rx_want(s, RX_ALPHA_REF, g->alpha_ref);
	}
	rx_want(s, RX_D_TEST, g->depth_test);
	rx_want(s, RX_D_FUNC, g->depth_func);
	rx_want(s, RX_D_WRITE, g->depth_mask);
	rx_want(s, RX_CULL_EN, g->cull_en);
	if (g->cull_en)
	{
		rx_want(s, RX_CULL_FACE, g->cull_face);
		rx_want(s, RX_FRONT_FACE, g->front_face);
	}
	rx_want_scissor(s);
	rx_want(s, RX_COLOR_MASK, ((g->color_mask & 4) ? 0x00000001u : 0) | ((g->color_mask & 2) ? 0x00000100u : 0) |
	                          ((g->color_mask & 1) ? 0x00010000u : 0) | ((g->color_mask & 8) ? 0x01000000u : 0));
	rx_want(s, RX_SHADE, g->shade);
	rx_want(s, RX_ST_EN, g->st_en);
	if (g->st_en)
	{
		rx_want(s, RX_ST_FUNC, g->st_func);
		rx_want(s, RX_ST_REF, g->st_ref);
		rx_want(s, RX_ST_MASK, g->st_mask);
		rx_want(s, RX_ST_FAIL, g->st_fail);
		rx_want(s, RX_ST_ZFAIL, g->st_zfail);
		rx_want(s, RX_ST_ZPASS, g->st_zpass);
		rx_want(s, RX_ST_WMASK, g->st_wmask);
	}
}

// Changed registers in method order, a run of neighbouring methods under one header (no run crosses slot 31/32).
static inline void rx_emit_regs(RsxdState* s)
{
	uint32_t* o = s->op;
	for (int w = 0; w < 2; w++)
	{
		uint32_t m = s->rch[w];
		s->rch[w] = 0;
		while (m)
		{
			const int i = 31 - __builtin_clz(m & -m), base = w * 32;
			int j = i;
			while (j < 31 && ((m >> (j + 1)) & 1) && rx_method[base + j + 1] == rx_method[base + j] + 4)
				j++;
			*o++ = RX_HDR(rx_method[base + i], j - i + 1);
			for (int k = i; k <= j; k++)
				*o++ = s->rv[base + k];
			m &= ~((2u << j) - (1u << i));
		}
	}
	s->op = o;
}

static inline void rx_copy(RsxdState* s, const uint32_t* w, uint32_t n)
{
	memcpy(s->op, w, n * 4);
	s->op += n;
}

// LoadVertexProgramParameterBlock for one constant slot.
static inline void rx_const(RsxdState* s, uint32_t slot, const uint32_t* v, uint32_t n)
{
	uint32_t* o = s->op;
	o[0] = RX_HDR(RXM_VP_CONST_ID, 1 + 4 * n);
	o[1] = slot;
	memcpy(o + 2, v, n * 16);
	s->op = o + 2 + 4 * n;
}

// Room for a draw of count vertices with every state change, so no reserve splits a draw's words.
static inline uint32_t rsxd_draw_room(uint32_t count)
{
	return 360 + (count >> 8);
}

// One draw of [first, first + count) under the GL state; ps3gl_draw_vb_shift's words, minus the ones the RSX holds.
static RSXD_BIG void rx_draw1(RsxdState* s, uint32_t prim, uint32_t stride, uint32_t first, uint32_t count)
{
	if (s->oend - s->op < (int)rsxd_draw_room(count) && !rsxd_reserve(s, rsxd_draw_room(count)))
		return;
	const RsxdGl* g = &s->gl;
	const RsxdProg* pg = s->prog;
	rx_want_state(s);
	// The texture cache clears before a draw that samples new texels (content_dirty), of unit 0 or (inval_due) unit 1.
	const uint32_t tmu0 = g->tex_en && g->has_tex;
	uint32_t inval = s->inval_due;
	s->inval_due = 0;
	if (tmu0)
	{
		inval |= rsxd_tex_dirty(s, g->tex.obj, !s->check);
		rx_want(s, RX_T_OFFSET, g->tex.offset);
		rx_want(s, RX_T_FORMAT, g->tex.format);
		rx_want(s, RX_T_SWIZZLE, g->tex.remap);
		rx_want(s, RX_T_SIZE0, g->tex.size0);
		rx_want(s, RX_T_SIZE1, g->tex.size1);
		rx_want(s, RX_T_ENABLE, g->tex.ctrl);
		rx_want(s, RX_T_FILTER, g->tex.filter);
		rx_want(s, RX_T_WRAP, g->tex.wrap);
	}
	else
		rx_want(s, RX_T_ENABLE, 0);
	const uint32_t key = pg->key_lut[rsxd_key_index(tmu0, g->texenv, tmu0 && g->bump, g->fog, g->offs, g->xt,
	                                                (s->sc->flags & RSXD_S_TMU1) != 0)];
	const uint32_t vb = s->sec->vb_off, loc = s->sec->vb_loc << 31;
	const uint32_t st8 = stride << 8;
	rx_want(s, RX_F_POS, st8 | (4 << 4) | 2);
	rx_want(s, RX_A_POS, loc | vb);
	rx_want(s, RX_F_TC0, st8 | (2 << 4) | 2);
	rx_want(s, RX_A_TC0, loc | (vb + 16));
	rx_want(s, RX_F_TC1, key >= RSXD_KEY_TC1U8 ? st8 | (4 << 4) | 4 : st8 | (2 << 4) | 2);
	rx_want(s, RX_A_TC1, loc | (vb + 24));
	rx_want(s, RX_F_COL, st8 | (4 << 4) | 4);
	rx_want(s, RX_A_COL, loc | (vb + 32 + g->flat_shift * stride));
	if (inval)
	{
		*s->op++ = RX_HDR(RXM_TEX_CACHE, 1);
		*s->op++ = 1;
		s->n_inval++;
	}
	if (tmu0 && (s->rch[1] >> (RX_T_OFFSET - 32)))
		s->n_tex++;
	rx_emit_regs(s);
	if ((int32_t)key != s->fp_key)
	{
		rx_copy(s, &pg->fp_words[pg->fp_at[key]], pg->fp_n[key]);
		s->fp_key = (int32_t)key;
	}
	const uint32_t vp = pg->key_vp[key];
	const int reload = (int32_t)vp != s->vp_id;
	if (reload)
	{
		rx_copy(s, pg->vp_words[vp], pg->vp_n[vp]);
		s->vp_id = (int32_t)vp;
		if (pg->vp_mvp[vp] != RSXD_NONE)
			rx_const(s, pg->vp_mvp[vp], s->sc->mvp, pg->vp_mvp_n[vp]);
	}
	// The first draw after rsxd_forget() always loads a VP, so the compares below only see values it sent.
	if (pg->vp_cmin[vp] != RSXD_NONE && pg->vp_cmax[vp] != RSXD_NONE && pg->vp_fog[vp] != RSXD_NONE)
	{
		if (reload || s->fog_up != s->sc->fog_scale || memcmp(s->xt_up, g->xt_v, sizeof(s->xt_up)) != 0)
		{
			const uint32_t fogp[4] = { s->sc->fog_scale, g->xt_v[8], 0, 0 };
			rx_const(s, pg->vp_fog[vp], fogp, 1);
			rx_const(s, pg->vp_cmin[vp], &g->xt_v[0], 1);
			rx_const(s, pg->vp_cmax[vp], &g->xt_v[4], 1);
			s->fog_up = s->sc->fog_scale;
			memcpy(s->xt_up, g->xt_v, sizeof(s->xt_up));
		}
	}
	else if (pg->vp_fog[vp] != RSXD_NONE && (reload || s->fog_up != s->sc->fog_scale))
	{
		const uint32_t fogp[4] = { s->sc->fog_scale, 0, 0, 0 };
		rx_const(s, pg->vp_fog[vp], fogp, 1);
		s->fog_up = s->sc->fog_scale;
	}
	// rsxDrawVertexArray.
	uint32_t* o = s->op;
	o[0] = RX_HDR_NI(RXM_VTX_INVAL, 3);
	o[1] = o[2] = o[3] = 0;
	o[4] = RX_HDR(RXM_BEGIN_END, 1);
	o[5] = prim;
	uint32_t c = count - 1;
	const uint32_t lcount = c & 0xff;
	c >>= 8;
	o[6] = RX_HDR(RXM_VB_BATCH, 1);
	o[7] = (lcount << 24) | first;
	o += 8;
	uint32_t start = first + lcount + 1;
	while (c)
	{
		const uint32_t n = c < 0x7ff ? c : 0x7ff;
		*o++ = RX_HDR_NI(RXM_VB_BATCH, n);
		for (uint32_t k = 0; k < n; k++, start += 0x100)
			*o++ = 0xff000000u | start;
		c -= n;
	}
	o[0] = RX_HDR(RXM_BEGIN_END, 1);
	o[1] = 0;
	s->op = o + 2;
	s->n_draw++;
	s->n_vtx += count;
}

// The sides of an outside clip's rect r in the base scissor (x0, y0, x1, y1); a bit per side with pixels.
static inline uint32_t rsxd_sides(const RsxdScene* sc, const int32_t* r, int side[4][4])
{
	const int x1 = sc->bx + sc->bw, y1 = sc->by + sc->bh;
	const int t[4][4] = { { sc->bx, sc->by, r[0], y1 }, { r[2], sc->by, x1, y1 }, { r[0], sc->by, r[2], r[1] },
	                      { r[0], r[3], r[2], y1 } };
	uint32_t m = 0;
	for (int k = 0; k < 4; k++)
	{
		memcpy(side[k], t[k], sizeof(t[k]));
		m |= (uint32_t)(t[k][2] > t[k][0] && t[k][3] > t[k][1]) << k;
	}
	return m;
}

// Whether draw_vb sends a draw of count vertices under clip mode and rect r.
static inline int rsxd_draws(const RsxdScene* sc, uint32_t mode, const int32_t* r, uint32_t count)
{
	int side[4][4];
	return count && mode != RXC_ALL && (mode != RXC_OUTSIDE || rsxd_sides(sc, r, side));
}

// draw_vb: an outside clip draws once per side of the rect, a clip that cuts all draws nothing. count 0 sends
// nothing, as ps3gl_draw_vb_shift.
static inline void rsxd_draw(RsxdState* s, uint32_t prim, uint32_t stride, uint32_t first, uint32_t count)
{
	RsxdGl* g = &s->gl;
	if (g->clip_mode == RXC_ALL || count == 0)
		return;
	if (g->clip_mode != RXC_OUTSIDE)
	{
		rx_draw1(s, prim, stride, first, count);
		return;
	}
	int side[4][4];
	const uint32_t m = rsxd_sides(s->sc, g->clip_r, side);
	for (int k = 0; k < 4; k++)
	{
		if (!((m >> k) & 1))
			continue;
		rx_scissor(s, side[k][0], side[k][1], side[k][2] - side[k][0], side[k][3] - side[k][1]);
		rx_draw1(s, prim, stride, first, count);
	}
}
