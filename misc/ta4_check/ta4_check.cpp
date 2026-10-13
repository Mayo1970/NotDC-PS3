// Bundles 4 and 17 (plan section 7) PC check, WSL: the PPU replay and the SPU decoder with the decode options, against
// each other, the frozen decoders of before bundle 4 (options off), and a model of the ported rules. See run.sh.
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "ta4.h"
#include "ps3/spu/spe_bell.h"

typedef uint32_t u32;
typedef int32_t s32;

static void die(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static u32 g_frame;
static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL (frame %u): ", g_frame);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static u32 rnd()
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (u32)(rs >> 32);
}
static u32 rndn(u32 n) { return rnd() % n; }
static u32 fb(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static float bf(u32 u) { float f; memcpy(&f, &u, 4); return f; }

// ---- the splitter's type table (FifoSplitter::Init) ---------------------------

static u32 type_lut[256];
static void build_lut()
{
	for (u32 i = 0; i < 256; i++)
	{
		const u32 uv16b = i & 1, tex = (i >> 3) & 1, col = (i >> 4) & 3, vol = (i >> 6) & 1, offs = (i >> 2) & 1;
		u32 rv;
		if (tex)
			rv = !vol ? (col == 0 ? 3 : col == 1 ? 5 : 7) + uv16b : (col == 0) ? 11 + uv16b : (col == 1) ? 0xFFFFFFFFu : 13 + uv16b;
		else
			rv = !vol ? ((col == 0) ? 0 : (col == 1) ? 1 : 2) : (col == 0) ? 9 : (col == 1) ? 0xFFFFFFFFu : 10;
		u32 type;
		if (!vol)
			type = (col < 2 || col == 3) ? 0 : (tex && offs) ? (2 | 0x80) : 1;
		else
			type = (col == 0 || col == 3) ? 3 : (col == 2) ? (4 | 0x80) : 0xFFDDEEAAu;
		rv |= (type & 0x80) ? (2u << 30) : (1u << 30);
		rv |= (type & 0x7F) << 8;
		type_lut[i] = rv;
	}
}
static bool is64(u32 t) { return t == 5 || t == 6 || t == 11 || t == 12 || t == 13 || t == 14; }

// ---- the model ----------------------------------------------------------------

// clamp_byte: the saturation both decoders use for every float colour.
static u32 m_byte(u32 bits)
{
	const float f = bf(bits);
	if (f != f)
		return 255;
	const s32 v = (s32)(f * 255.0f);
	return v < 0 ? 0 : v > 255 ? 255 : (u32)v;
}
static u32 m_argb(u32 a, u32 r, u32 g, u32 b) { return m_byte(a) << 24 | m_byte(r) << 16 | m_byte(g) << 8 | m_byte(b); }

// Decoder state that lives across frames: the face colour (floats and Flycast's bytes) and the clip rect.
static u32 m_face[4] = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u };
static u32 m_face8 = 0xFFFFFFFFu;
static u32 m_clip;
static void m_set_face(const u32* f)
{
	memcpy(m_face, f, 16);
	m_face8 = m_argb(f[0], f[1], f[2], f[3]);
}
// An intensity vertex. P8: Flycast's face byte * intensity byte / 256, alpha = the face alpha. Else the old rule.
static u32 m_int(u32 opt, u32 ibits)
{
	if (opt & TAOPT_INT_CLAMP)
	{
		const u32 s = m_byte(ibits);
		return (m_face8 & 0xFF000000u) | ((((m_face8 >> 16) & 0xFF) * s / 256) << 16) |
		       ((((m_face8 >> 8) & 0xFF) * s / 256) << 8) | ((m_face8 & 0xFF) * s / 256);
	}
	const float i = bf(ibits);
	return m_byte(m_face[0]) << 24 | m_byte(fb(bf(m_face[1]) * i)) << 16 | m_byte(fb(bf(m_face[2]) * i)) << 8 |
	       m_byte(fb(bf(m_face[3]) * i));
}

// Bundle 17: the face offset colour (PolyParam 2B), and an intensity vertex's offset colour from it.
static u32 m_foffs[4] = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u };
static u32 m_foffs8 = 0xFFFFFFFFu;
static void m_set_foffs(const u32* f)
{
	memcpy(m_foffs, f, 16);
	m_foffs8 = m_argb(f[0], f[1], f[2], f[3]);
}
static u32 m_ioffs(u32 opt, u32 ibits)
{
	if (opt & TAOPT_INT_CLAMP)
	{
		const u32 s = m_byte(ibits);
		return (m_foffs8 & 0xFF000000u) | ((((m_foffs8 >> 16) & 0xFF) * s / 256) << 16) |
		       ((((m_foffs8 >> 8) & 0xFF) * s / 256) << 8) | ((m_foffs8 & 0xFF) * s / 256);
	}
	const float i = bf(ibits);
	return m_byte(m_foffs[0]) << 24 | m_byte(fb(bf(m_foffs[1]) * i)) << 16 | m_byte(fb(bf(m_foffs[2]) * i)) << 8 |
	       m_byte(fb(bf(m_foffs[3]) * i));
}

struct MVol { u32 first, cull, depth, vl, list; };
// A sprite: its first vertex in the lists (corner D), and the record's x, y of A, B, C, D and z, u, v of A, B, C.
struct Spr { u32 first, x[4], y[4], z[3], u[3], v[3]; };

struct Frame
{
	u32 opt;
	bool bad;
	std::vector<u32> w;             // 8 words per record
	std::vector<u32> marks;         // record indexes of the TA_LIST_CONT writes, ascending
	std::vector<u32> strips_after;  // strips and volume records in the lists after each record
	std::vector<u32> mvols_after;
	std::vector<u32> col;           // the colour of each vertex
	std::vector<u32> offs;          // its offset colour, as TAOPT_OFFS keeps it
	std::vector<Spr> spr;
	std::vector<u32> param;         // pcw, isp of each param
	std::vector<MVol> mvol;
	u32 nmtri, nstrip, nbad;
	u32 mv_tris[5];
};
static Frame F;
static u32 cur_list;   // the splitter's CurrentList, 0xFFFFFFFF = none
static u32 strip_vtx;  // vertices of the strip in progress

static bool m_kept(u32 list) { return list == 1 || (list == 3 && (F.opt & TAOPT_TR_VOL)); }
static u32 m_cap() { return (F.opt & TAOPT_VOL_8K) ? 8192 : 4096; }

static void rec(const u32* r)
{
	F.w.insert(F.w.end(), r, r + 8);
	F.strips_after.push_back(F.nstrip);
	F.mvols_after.push_back((u32)F.mvol.size());
}

// ---- value generators -----------------------------------------------------------

// Colour and intensity floats the SPU's exact multiply takes: 0, [1e-3, 100], negative, NaN. No Inf, no huge value:
// the PPU's float-to-int of those is PPC-only (the PC's differs), and the HW compare (SPE Doorbell) covers them.
static u32 rnd_colf()
{
	const u32 r = rndn(100);
	if (r < 10) return 0;
	if (r < 70) return fb((float)(1 + rndn(1250)) / 1000.0f);
	if (r < 80) return fb((float)(1 + rndn(100000)) / 1000.0f);
	if (r < 90) return fb(-(float)(1 + rndn(1000)) / 100.0f);
	return 0x7FC00000u | rndn(0x400000);
}
static u32 rnd_xy() { return rndn(10) ? fb((float)rndn(64000) / 100.0f - 40.0f) : rnd(); }
// 1/W: mostly a usable range; also 0, negative, NaN, +Inf, under the floor. Never in [2^126, Inf): W is a denormal.
static u32 rnd_z()
{
	const u32 r = rndn(100);
	if (r < 80) return fb((float)(1 + rndn(100000)) / 1000.0f);
	if (r < 84) return 0;
	if (r < 88) return fb(-(float)(1 + rndn(1000)) / 10.0f);
	if (r < 92) return 0x7FC00000u | rndn(0x400000);
	if (r < 94) return 0x7F800000u;
	if (r < 97) return fb(1e-12f);
	return fb((float)(1 + rndn(1000)) * 1000.0f);
}
static u32 rnd_zs() { return fb((float)(1 + rndn(100000)) / 1000.0f); }
// A 16-bit UV: the top half of a float in [1/256, 4), or 0.
static u32 rnd_uv16() { return rndn(8) ? fb((float)(1 + rndn(1023)) / 256.0f) >> 16 : 0; }

static u32 mk(u32 para, u32 list, u32 objc, u32 eos)
{
	return para << 29 | eos << 28 | list << 24 | (objc & 0xFF);
}

// ---- records ---------------------------------------------------------------------

static void strip_done()
{
	if (strip_vtx >= 3 && !F.param.empty() && F.nstrip < T4_MAX_STRIPS)
		F.nstrip++;
	strip_vtx = 0;
}

static void gen_vertex(u32 pdid, bool eos)
{
	u32 a[8], b[8];
	for (int k = 0; k < 8; k++)
	{
		a[k] = rnd();
		b[k] = rnd();
	}
	a[0] = mk(7, rndn(8), rnd(), eos) | (rnd() & 0x00FFFF00u);
	a[1] = rnd_xy();
	a[2] = rnd_xy();
	a[3] = rnd_z();
	u32 col = 0;
	switch (pdid)
	{
	case 0: case 3: case 4: case 11: case 12: col = a[6]; break;
	case 9: col = a[4]; break;
	case 1:
		for (int k = 4; k < 8; k++)
			a[k] = rnd_colf();
		col = m_argb(a[4], a[5], a[6], a[7]);
		break;
	case 5: case 6:
		for (int k = 0; k < 4; k++)
			b[k] = rnd_colf();
		col = m_argb(b[0], b[1], b[2], b[3]);
		break;
	case 2: case 7: case 8: case 13: case 14:
		a[6] = rnd_colf();
		col = m_int(F.opt, a[6]);
		break;
	case 10:
		a[4] = rnd_colf();
		col = m_int(F.opt, a[4]);
		break;
	}
	F.col.push_back(col);
	// Bundle 17: the offset colour of the vertex; a type with none gets 0.
	u32 off = 0;
	switch (pdid)
	{
	case 3: case 4: case 11: case 12: off = a[7]; break;
	case 5: case 6:
		for (int k = 4; k < 8; k++)
			b[k] = rnd_colf();
		off = m_argb(b[4], b[5], b[6], b[7]);
		break;
	case 7: case 8: case 13: case 14:
		a[7] = rnd_colf();
		off = m_ioffs(F.opt, a[7]);
		break;
	}
	F.offs.push_back(off);
	strip_vtx++;
	if (!is64(pdid))
	{
		if (eos)
			strip_done();
		rec(a);
		return;
	}
	rec(a);
	if (eos)
		strip_done();
	rec(b);
}

// A polygon header and its strips.
static void gen_poly(u32 list)
{
	u32 objc;
	do
		objc = rndn(256);
	while (type_lut[objc] == 0xFFFFFFFFu);
	const u32 uid = type_lut[objc], pdid = uid & 0xFF, ppid = (uid >> 8) & 0xFF, psz = uid >> 30;
	u32 h[16];
	for (int k = 0; k < 16; k++)
		h[k] = rnd();
	const u32 uc = rndn(3) ? 0 : rndn(4);
	h[0] = mk(4, cur_list == 0xFFFFFFFFu ? list : rndn(8), objc, 0) | uc << 16 | (rnd() & 0x00FCFF00u);
	if (ppid == 1)
		for (int k = 4; k < 8; k++)
			h[k] = rnd_colf();
	if (psz == 2)
		for (int k = 8; k < 16; k++)
			h[k] = rnd_colf();
	if (cur_list == 0xFFFFFFFFu)
		cur_list = list;
	u32 pcw = h[0], isp = h[1];
	if (F.opt & TAOPT_TILE_CLIP)
	{
		const u32 clip = uc >= 2 ? m_clip : 0;
		isp = (isp & 0xFFF00000u) | (clip & 0xFFFFFu);
		pcw = (pcw & ~0x300u) | (clip >> 20) << 8;
	}
	F.param.push_back(pcw);
	F.param.push_back(isp);
	if (ppid == 1)
		m_set_face(h + 4);
	else if (ppid == 2 || ppid == 4)
		m_set_face(h + 8);
	if (ppid == 2)
		m_set_foffs(h + 12);
	rec(h);
	if (psz == 2)
		rec(h + 8);
	const u32 nstrips = rndn(4);
	for (u32 s = 0; s < nstrips; s++)
	{
		const u32 n = rndn(10) ? 3 + rndn(6) : 1 + rndn(2);
		for (u32 v = 0; v < n; v++)
			gen_vertex(pdid, v + 1 == n);
	}
}

static void gen_sprite(u32 list)
{
	u32 h[8];
	for (int k = 0; k < 8; k++)
		h[k] = rnd();
	const u32 uc = rndn(3) ? 0 : rndn(4);
	h[0] = mk(5, cur_list == 0xFFFFFFFFu ? list : rndn(8), rnd(), 0) | uc << 16 | (rnd() & 0x00FCFF00u);
	if (cur_list == 0xFFFFFFFFu)
		cur_list = list;
	u32 pcw = h[0], isp = h[1];
	if (F.opt & TAOPT_TILE_CLIP)
	{
		const u32 clip = uc >= 2 ? m_clip : 0;
		isp = (isp & 0xFFF00000u) | (clip & 0xFFFFFu);
		pcw = (pcw & ~0x300u) | (clip >> 20) << 8;
	}
	F.param.push_back(pcw);
	F.param.push_back(isp);
	rec(h);
	const u32 n = rndn(4);
	for (u32 s = 0; s < n; s++)
	{
		u32 a[8], b[8];
		a[0] = mk(7, rndn(8), rnd(), rndn(2)) | (rnd() & 0x00FFFF00u);
		for (int k = 1; k < 8; k++)
			a[k] = (k == 3 || k == 6) ? rnd_zs() : rnd_xy();
		b[0] = rnd_xy();
		b[1] = rnd_zs();
		b[2] = rnd_xy();
		b[3] = rnd_xy();
		b[4] = rnd();
		for (int k = 5; k < 8; k++)
			b[k] = rnd_uv16() << 16 | rnd_uv16();
		// Bundle 17: shape 0 is any four corners; else a rectangle, a trapezoid or a sheared quad of screen size.
		const u32 shape = rndn(4);
		if (shape)
		{
			const float x0 = (float)rndn(6000) / 10.0f, y0 = (float)rndn(4000) / 10.0f;
			const float w = (float)(1 + rndn(3000)) / 10.0f, hh = (float)(1 + rndn(2000)) / 10.0f;
			const float sh = shape == 1 ? 0.0f : (float)rndn(400) / 10.0f - 20.0f;
			const float in = shape == 2 ? (float)(1 + rndn(200)) / 10.0f : 0.0f;
			a[1] = fb(x0);
			a[2] = fb(y0);
			a[4] = fb(x0 + w);
			a[5] = fb(y0);
			a[7] = fb(x0 + w + sh - in);
			b[0] = fb(y0 + hh);
			b[2] = fb(x0 + sh + in);
			b[3] = fb(y0 + hh);
		}
		Spr sp;
		sp.first = (u32)F.col.size();
		sp.x[0] = a[1]; sp.y[0] = a[2]; sp.z[0] = a[3];
		sp.x[1] = a[4]; sp.y[1] = a[5]; sp.z[1] = a[6];
		sp.x[2] = a[7]; sp.y[2] = b[0]; sp.z[2] = b[1];
		sp.x[3] = b[2]; sp.y[3] = b[3];
		for (int k = 0; k < 3; k++)
		{
			sp.u[k] = b[5 + k] & 0xFFFF0000u;
			sp.v[k] = b[5 + k] << 16;
		}
		F.spr.push_back(sp);
		for (int k = 0; k < 4; k++)
		{
			F.col.push_back(h[4]);
			F.offs.push_back(h[5]);
		}
		rec(a);
		// The quad is a strip of 4 vertices, taken at the second record.
		if (F.nstrip < T4_MAX_STRIPS)
			F.nstrip++;
		rec(b);
	}
}

static void gen_clip()
{
	u32 r[8];
	for (int k = 0; k < 8; k++)
		r[k] = rnd();
	r[0] = mk(1, rndn(8), rnd(), rndn(2)) | (rnd() & 0x00FFFF00u);
	// The TA's User Tile Clip record: X min, Y min, X max, Y max in words 4 to 7 (tiles).
	m_clip = (r[4] & 63) | (r[6] & 63) << 6 | (r[5] & 31) << 12 | (r[7] & 31) << 17;
	rec(r);
}

// Records with no meaning: Object_List_Set, the two reserved types, a header with Volume = 1 and Col_Type = 1.
static void gen_bad(u32 list)
{
	u32 r[8];
	for (int k = 0; k < 8; k++)
		r[k] = rnd();
	const u32 kind = rndn(4);
	if (kind < 3)
		r[0] = mk(kind == 0 ? 2 : kind == 1 ? 3 : 6, rndn(8), rnd(), rndn(2)) | (rnd() & 0x00FFFF00u);
	else
	{
		// The splitter opens the list before it looks at the type.
		r[0] = mk(4, cur_list == 0xFFFFFFFFu ? list : rndn(8), 0x50 | (rnd() & 0x8F), 0) | (rnd() & 0x00FFFF00u);
		if (cur_list == 0xFFFFFFFFu)
			cur_list = list;
	}
	F.nbad++;
	rec(r);
}

static void gen_end()
{
	u32 r[8];
	for (int k = 0; k < 8; k++)
		r[k] = rnd();
	// With no list open the splitter takes the record's list type as an index of 5 flags (ta.h, not checked there).
	r[0] = mk(0, cur_list == 0xFFFFFFFFu ? rndn(5) : rndn(8), rnd(), rndn(2)) | (rnd() & 0x00FFFF00u);
	if (cur_list != 0xFFFFFFFFu && m_kept(cur_list) && F.mvol.size() < T4_MAX_MODVOL)
		F.mvol.push_back({ F.nmtri, 0, 0, 0, (u32)(cur_list == 3) });
	cur_list = 0xFFFFFFFFu;
	rec(r);
}

static void gen_poly_list(u32 list, u32 items)
{
	for (u32 i = 0; i < items; i++)
	{
		const u32 r = rndn(100);
		if (r < 72)
			gen_poly(list);
		else if (r < 87)
			gen_sprite(list);
		else if (r < 95)
			gen_clip();
		else if (F.bad)
			gen_bad(list);
	}
	gen_end();
}

static void gen_modvol_list(u32 list, u32 vols, u32 max_tris)
{
	for (u32 i = 0; i < vols; i++)
	{
		u32 h[8];
		for (int k = 0; k < 8; k++)
			h[k] = rnd();
		h[0] = mk(4, cur_list == 0xFFFFFFFFu ? list : rndn(8), rnd(), 0) | (rnd() & 0x00FFFF00u);
		if (cur_list == 0xFFFFFFFFu)
			cur_list = list;
		const u32 mvl = cur_list;
		if (m_kept(mvl) && F.mvol.size() < T4_MAX_MODVOL)
			F.mvol.push_back({ F.nmtri, (h[1] >> 27) & 3, h[1] >> 29, (h[0] >> 6) & 1, (u32)(mvl == 3) });
		rec(h);
		const u32 tris = rndn(max_tris + 1);
		for (u32 t = 0; t < tris; t++)
		{
			u32 a[8], b[8];
			for (int k = 0; k < 8; k++)
			{
				a[k] = rnd_xy();
				b[k] = rnd();
			}
			a[0] = mk(7, rndn(8), rnd(), rndn(2)) | (rnd() & 0x00FFFF00u);
			a[3] = rnd_z();
			a[6] = rnd_z();
			b[0] = rnd_xy();
			b[1] = rnd_z();
			F.mv_tris[mvl % 5]++;
			if (m_kept(mvl) && F.nmtri < m_cap())
				F.nmtri++;
			rec(a);
			rec(b);
		}
	}
	gen_end();
}

static void gen_frame(u32 opt, bool bad, u32 kind)
{
	F.opt = opt;
	F.bad = bad;
	F.w.clear();
	F.marks.clear();
	F.strips_after.clear();
	F.mvols_after.clear();
	F.col.clear();
	F.offs.clear();
	F.spr.clear();
	F.param.clear();
	F.mvol.clear();
	F.nmtri = F.nstrip = F.nbad = 0;
	memset(F.mv_tris, 0, sizeof(F.mv_tris));
	// The list init: the splitter has no list, P63 makes the face white, P10 puts the clip rect back.
	cur_list = 0xFFFFFFFFu;
	strip_vtx = 0;
	if (opt & TAOPT_FACE_RESET)
	{
		for (int k = 0; k < 4; k++)
			m_face[k] = m_foffs[k] = 0x3F800000u;
		m_face8 = m_foffs8 = 0xFFFFFFFFu;
	}
	if (opt & TAOPT_TILE_CLIP)
		m_clip = TA_CLIP_RESET;

	const u32 nlists = 1 + rndn(6);
	for (u32 l = 0; l < nlists; l++)
	{
		// A frame stays under the vertex and param caps: past them the two decoders part by design.
		if (F.col.size() > 30000 || F.param.size() / 2 > 5000)
			break;
		const u32 r = rndn(100);
		const u32 list = r < 35 ? 0 : r < 55 ? 2 : r < 70 ? 4 : r < 86 ? 1 : 3;
		if (list & 1)
		{
			// kind 1: many triangles, past the two caps; kind 2: many volumes, past the volume cap.
			if (kind == 1)
				gen_modvol_list(list, 2 + rndn(6), 1500);
			else if (kind == 2)
				gen_modvol_list(list, 100 + rndn(120), 3);
			else
				gen_modvol_list(list, rndn(6), 12);
		}
		else
			gen_poly_list(list, kind == 3 ? 200 + rndn(500) : rndn(40));
	}
	if (F.col.size() > T4_MAX_VTX - 16 || F.param.size() / 2 > T4_MAX_PARAMS - 4)
		die("the generator passed a list cap (%zu vertices, %zu params)", F.col.size(), F.param.size() / 2);
	// TA_LIST_CONT writes: any record boundary, the middle of a 64-byte record too.
	const u32 nrec = (u32)(F.w.size() / 8);
	const u32 nmarks = rndn(4) ? 0 : 1 + rndn(11);
	for (u32 k = 0; k < nmarks; k++)
		F.marks.push_back(rndn(nrec + 1));
	for (size_t i = 1; i < F.marks.size(); i++)
		for (size_t j = i; j > 0 && F.marks[j] < F.marks[j - 1]; j--)
		{
			const u32 t = F.marks[j];
			F.marks[j] = F.marks[j - 1];
			F.marks[j - 1] = t;
		}
}

// ---- runs --------------------------------------------------------------------------

static T4Lists Lp, Ls, Lrp, Lrs;

struct PpuSide
{
	void (*reset)(uint32_t, int);
	void (*data)(const uint32_t*, uint32_t);
	void (*cont)(void);
	void (*lists)(T4Lists*);
};
struct SpuSide
{
	void (*reset)(uint32_t, uint32_t);
	void (*data)(uint32_t, const uint32_t*, uint32_t);
	void (*fence)(uint32_t, T4Lists*);
};

// The replay: one decode per STARTRENDER, each split at the cont marks. raw_end values are random.
static void run_ppu(const PpuSide& p, T4Lists* l)
{
	const u32 nrec = (u32)(F.w.size() / 8);
	p.reset(F.opt, F.bad);
	u32 pos = 0, m = 0;
	while (pos < nrec || m < F.marks.size())
	{
		u32 end = rndn(4) ? nrec : pos + rndn(nrec - pos + 1);
		while (m < F.marks.size() && F.marks[m] <= end)
		{
			const u32 mark = F.marks[m++];
			if (mark > pos)
			{
				p.data(&F.w[(size_t)pos * 8], mark - pos);
				pos = mark;
			}
			p.cont();
		}
		if (end > pos)
		{
			p.data(&F.w[(size_t)pos * 8], end - pos);
			pos = end;
		}
	}
	p.lists(l);
}

// The bell: DATA commands of any length, pulled in chunks of up to 16 KB.
static void run_spu(const SpuSide& s, u32 ctx, T4Lists* l)
{
	const u32 nrec = (u32)(F.w.size() / 8);
	s.reset(ctx, F.opt);
	u32 pos = 0;
	while (pos < nrec)
	{
		u32 n = rndn(3) ? 1 + rndn(512) : 1 + rndn(8);
		if (n > nrec - pos)
			n = nrec - pos;
		s.data(ctx, &F.w[(size_t)pos * 8], n);
		pos += n;
	}
	s.fence(ctx, l);
}

static void same(const T4Lists& a, const T4Lists& b, const char* what)
{
	const u32 ha[] = { a.nvtx, a.nstrip, a.nparam, a.nmtri, a.nmvol, a.max_inv_w, a.min_w, a.max_w,
	                   a.mv_tris[0], a.mv_tris[1], a.mv_tris[2], a.mv_tris[3], a.mv_tris[4] };
	const u32 hb[] = { b.nvtx, b.nstrip, b.nparam, b.nmtri, b.nmvol, b.max_inv_w, b.min_w, b.max_w,
	                   b.mv_tris[0], b.mv_tris[1], b.mv_tris[2], b.mv_tris[3], b.mv_tris[4] };
	static const char* const hn[] = { "nvtx", "nstrip", "nparam", "nmtri", "nmvol", "max_inv_w", "min_w", "max_w",
	                                  "mv0", "mv1", "mv2", "mv3", "mv4" };
	for (u32 k = 0; k < 13; k++)
		if (ha[k] != hb[k])
			die("%s: %s %08X / %08X", what, hn[k], ha[k], hb[k]);
	for (u32 i = 0; i < a.nvtx; i++)
		if (memcmp(a.vtx[i], b.vtx[i], 24))
			die("%s: vertex %u: %08X %08X %08X %08X %08X %08X / %08X %08X %08X %08X %08X %08X", what, i,
			    a.vtx[i][0], a.vtx[i][1], a.vtx[i][2], a.vtx[i][3], a.vtx[i][4], a.vtx[i][5],
			    b.vtx[i][0], b.vtx[i][1], b.vtx[i][2], b.vtx[i][3], b.vtx[i][4], b.vtx[i][5]);
	if (a.opt & b.opt & TAOPT_OFFS)
		for (u32 i = 0; i < a.nvtx; i++)
			if (a.offs[i] != b.offs[i])
				die("%s: offset colour of vertex %u: %08X / %08X", what, i, a.offs[i], b.offs[i]);
	for (u32 i = 0; i < a.nstrip; i++)
		if (memcmp(a.strip[i], b.strip[i], 16))
			die("%s: strip %u: %u+%u l%u p%u / %u+%u l%u p%u", what, i, a.strip[i][0], a.strip[i][1], a.strip[i][2],
			    a.strip[i][3], b.strip[i][0], b.strip[i][1], b.strip[i][2], b.strip[i][3]);
	for (u32 i = 0; i < a.nparam; i++)
		if (memcmp(a.param[i], b.param[i], 16))
			die("%s: param %u: %08X %08X %08X %08X / %08X %08X %08X %08X", what, i, a.param[i][0], a.param[i][1],
			    a.param[i][2], a.param[i][3], b.param[i][0], b.param[i][1], b.param[i][2], b.param[i][3]);
	for (u32 i = 0; i < a.nmtri; i++)
		if (memcmp(a.mtri[i], b.mtri[i], 36))
			die("%s: volume triangle %u", what, i);
	for (u32 i = 0; i < a.nmvol; i++)
		if (memcmp(a.mvol[i], b.mvol[i], 20))
			die("%s: volume %u: %u c%u d%u v%u l%u / %u c%u d%u v%u l%u", what, i, a.mvol[i][0], a.mvol[i][1],
			    a.mvol[i][2], a.mvol[i][3], a.mvol[i][4], b.mvol[i][0], b.mvol[i][1], b.mvol[i][2], b.mvol[i][3],
			    b.mvol[i][4]);
}

static uint64_t n_vtx, n_int, n_param, n_clip, n_mvol, n_tr_mvol, n_capped, n_marks, n_badrec, n_rec;
// Bundle 17: offset colours checked; sprites, those at the plane's corner, at the old one, and near a limit of the
// rule (a flat sprite, a result at the range's end), where both corners pass.
static uint64_t n_offs, n_spr, n_spr_plane, n_spr_old, n_spr_either;

// set_pos of the decoders: W for a 1/W, with its floor.
static u32 m_w(float z)
{
	const float safe = (z >= 1e-9f) ? z : 1e-9f;
	return fb(1.0f / safe);
}
static bool m_plain(u32 b)
{
	const u32 e = (b >> 23) & 0xFF;
	return !b || (e >= 87 && e <= 167);
}

// P80: the sprite's corner D in the lists against the plane through A, B and C, in long double. The decoders keep
// the old corner (A + C - B) for a parallelogram and for values they do not take.
static void model_sprite(const T4Lists& l, const Spr& s)
{
	typedef long double ld;
	const u32* d = l.vtx[s.first];
	n_spr++;
	const u32 zo = fb(bf(s.z[0]) + bf(s.z[2]) - bf(s.z[1]));
	const u32 uo = fb(bf(s.u[0]) + bf(s.u[2]) - bf(s.u[1])), vo = fb(bf(s.v[0]) + bf(s.v[2]) - bf(s.v[1]));
	const bool is_old = d[2] == m_w(bf(zo)) && d[4] == uo && d[5] == vo;
	bool plain = true;
	for (int k = 0; k < 4; k++)
		plain = plain && m_plain(s.x[k]) && m_plain(s.y[k]);
	for (int k = 0; k < 3; k++)
		plain = plain && m_plain(s.z[k]) && m_plain(s.u[k]) && m_plain(s.v[k]);
	const char* why = !(F.opt & TAOPT_SPR_PLANE) ? "the option is off" : !plain ? "a value is not plain" : 0;
	ld m[3], mag[3];
	if (!why)
	{
		const ld ax = bf(s.x[0]), ay = bf(s.y[0]);
		const ld abx = (ld)bf(s.x[1]) - ax, aby = (ld)bf(s.y[1]) - ay;
		const ld acx = (ld)bf(s.x[2]) - ax, acy = (ld)bf(s.y[2]) - ay;
		const ld apx = (ld)bf(s.x[3]) - ax, apy = (ld)bf(s.y[3]) - ay;
		const ld det = abx * acy - aby * acx, n1 = apx * acy - apy * acx, n2 = abx * apy - aby * apx;
		const ld sd = fabsl(abx * acy) + fabsl(aby * acx), s1 = fabsl(apx * acy) + fabsl(apy * acx);
		const ld s2 = fabsl(abx * apy) + fabsl(aby * apx);
		if (apx == acx - abx && apy == acy - aby)
			why = "a parallelogram";
		else if (det == 0)
			why = "no area";
		else if (fabsl(det) < 1e-7L * sd || (n1 != 0 && fabsl(n1) < 1e-7L * s1) || (n2 != 0 && fabsl(n2) < 1e-7L * s2))
		{
			// A difference that loses 7 digits: the decoders' doubles and this model can part.
			n_spr_either++;
			return;
		}
		else
		{
			// AP = k1 AB + k2 AC defines k1 and k2; D's value is then A's plus k1 and k2 times the sides' steps.
			const ld k1 = n1 / det, k2 = n2 / det;
			if (fabsl(k1 * abx + k2 * acx - apx) > 1e-9L * (fabsl(k1 * abx) + fabsl(k2 * acx) + fabsl(apx)) ||
			    fabsl(k1 * aby + k2 * acy - apy) > 1e-9L * (fabsl(k1 * aby) + fabsl(k2 * acy) + fabsl(apy)))
				die("the model's k1 and k2 do not give D (sprite at vertex %u)", s.first);
			const u32* const w[3] = { s.z, s.u, s.v };
			bool edge = false;
			for (int k = 0; k < 3; k++)
			{
				const ld a = bf(w[k][0]);
				const ld t1 = k1 * ((ld)bf(w[k][1]) - a), t2 = k2 * ((ld)bf(w[k][2]) - a);
				m[k] = a + t1 + t2;
				mag[k] = fabsl(a) + fabsl(t1) + fabsl(t2);
				const ld am = fabsl(m[k]);
				// A result outside 2^-100 .. 2^101 keeps the old corner; near a limit, or lost in the sum, both pass.
				if (am != 0 && (am < 0x1p-100L || am >= 0x1p101L))
					why = "a result out of range";
				if ((am > 0x1p-102L && am < 0x1p-98L) || (am > 0x1p99L && am < 0x1p103L) || (am != 0 && am < 1e-9L * mag[k]))
					edge = true;
			}
			if (edge)
			{
				n_spr_either++;
				return;
			}
		}
	}
	if (why)
	{
		if (!is_old)
			die("model: sprite at vertex %u: the old corner is expected (%s), got W %08X u %08X v %08X", s.first, why,
			    d[2], d[4], d[5]);
		n_spr_old++;
		return;
	}
	const ld ez = 4e-6L * mag[0];
	if (fabsl(m[0] - 1e-9L) > ez)
	{
		const ld zf = m[0] >= 1e-9L ? m[0] : 1e-9L;
		if (fabsl((ld)bf(d[2]) * zf - 1) > 1e-5L + ez / zf)
			die("model: sprite at vertex %u: W %g, the plane's 1/W is %Lg", s.first, (double)bf(d[2]), m[0]);
	}
	for (int k = 1; k < 3; k++)
		if (fabsl((ld)bf(d[3 + k]) - m[k]) > 4e-6L * mag[k] + 1e-37L)
			die("model: sprite at vertex %u: %c %g, the plane's is %Lg", s.first, k == 1 ? 'u' : 'v',
			    (double)bf(d[3 + k]), m[k]);
	// The trapezoid's corner must not be the parallelogram's: a decoder that never takes the plane fails here.
	if (is_old && fabsl((ld)bf(uo) - m[1]) + fabsl((ld)bf(vo) - m[2]) > 1e-3L * (mag[1] + mag[2]))
		die("model: sprite at vertex %u: the old corner, with the plane's far from it", s.first);
	n_spr_plane++;
}

// The lists against the model of the frame.
static void model(const T4Lists& l)
{
	if (l.nvtx != F.col.size())
		die("model: %u vertices, want %zu", l.nvtx, F.col.size());
	for (u32 i = 0; i < l.nvtx; i++)
		if (l.vtx[i][3] != F.col[i])
			die("model: vertex %u colour %08X, want %08X (opt %02X)", i, l.vtx[i][3], F.col[i], F.opt);
	if (l.nparam != F.param.size() / 2)
		die("model: %u params, want %zu", l.nparam, F.param.size() / 2);
	for (u32 i = 0; i < l.nparam; i++)
	{
		if (l.param[i][0] != F.param[2 * i] || l.param[i][1] != F.param[2 * i + 1])
			die("model: param %u pcw %08X isp %08X, want %08X %08X (opt %02X)", i, l.param[i][0], l.param[i][1],
			    F.param[2 * i], F.param[2 * i + 1], F.opt);
		if ((F.opt & TAOPT_TILE_CLIP) && ((l.param[i][0] >> 16) & 3) >= 2)
			n_clip++;
	}
	if (l.nstrip != F.nstrip)
		die("model: %u strips, want %u", l.nstrip, F.nstrip);
	if (l.nmvol != F.mvol.size() || l.nmtri != F.nmtri)
		die("model: %u volumes %u triangles, want %zu %u (opt %02X)", l.nmvol, l.nmtri, F.mvol.size(), F.nmtri, F.opt);
	for (u32 i = 0; i < l.nmvol; i++)
	{
		const MVol& m = F.mvol[i];
		if (l.mvol[i][0] != m.first || l.mvol[i][1] != m.cull || l.mvol[i][2] != m.depth || l.mvol[i][3] != m.vl ||
		    l.mvol[i][4] != m.list)
			die("model: volume %u: %u c%u d%u v%u l%u, want %u c%u d%u v%u l%u", i, l.mvol[i][0], l.mvol[i][1],
			    l.mvol[i][2], l.mvol[i][3], l.mvol[i][4], m.first, m.cull, m.depth, m.vl, m.list);
		n_tr_mvol += m.list;
	}
	for (int k = 0; k < 5; k++)
		if (l.mv_tris[k] != F.mv_tris[k])
			die("model: mv_tris[%d] %u, want %u", k, l.mv_tris[k], F.mv_tris[k]);
	// The pass marks: the strips and volume records in the lists at each cont, the first T4_MAX_PASSES of them.
	const u32 np = F.marks.size() < T4_MAX_PASSES ? (u32)F.marks.size() : T4_MAX_PASSES;
	if (l.npass != np)
		die("model: %u pass marks, want %u", l.npass, np);
	for (u32 k = 0; k < np; k++)
	{
		const u32 at = F.marks[k];
		const u32 ws = at ? F.strips_after[at - 1] : 0, wm = at ? F.mvols_after[at - 1] : 0;
		if (l.pass_strip[k] != ws || l.pass_mvol[k] != wm)
			die("model: pass mark %u (record %u): strips %u volumes %u, want %u %u", k, at, l.pass_strip[k],
			    l.pass_mvol[k], ws, wm);
	}
	n_marks += np;
	if (F.opt & TAOPT_OFFS)
	{
		for (u32 i = 0; i < l.nvtx; i++)
			if (l.offs[i] != F.offs[i])
				die("model: vertex %u offset colour %08X, want %08X (opt %02X)", i, l.offs[i], F.offs[i], F.opt);
		n_offs += l.nvtx;
	}
	for (const Spr& s : F.spr)
		model_sprite(l, s);
	n_vtx += l.nvtx;
	n_param += l.nparam;
	n_mvol += l.nmvol;
	const u32 sent = F.mv_tris[1] + ((F.opt & TAOPT_TR_VOL) ? F.mv_tris[3] : 0);
	if (sent > F.nmtri)
		n_capped++;
}

// A normal double: any sign, an exponent of -300 to 300; one in four has a short mantissa (exact quotients).
static double rnd_dbl()
{
	uint64_t m = ((uint64_t)rnd() << 32 | rnd()) & 0xFFFFFFFFFFFFFull;
	if (rndn(4) == 0)
		m &= ~((1ull << rndn(53)) - 1);
	const uint64_t b = (uint64_t)(rnd() & 1) << 63 | (uint64_t)(723 + rndn(601)) << 52 | m;
	double d;
	memcpy(&d, &b, 8);
	return d;
}

// Bundle 17 (P80): the SPU's divide of spe_sprite.h against the host's, which rounds to nearest as the PPU's does.
static u32 div_checks(u32 n)
{
	for (u32 i = 0; i < n; i++)
	{
		double a = rnd_dbl();
		const double b = rnd_dbl();
		// One in four: an exact quotient.
		if (rndn(4) == 0)
		{
			volatile double q = (double)(1 + rndn(1u << 20)) * b;
			a = q;
		}
		const double want = a / b, got = spu_new_div(a, b);
		if (memcmp(&want, &got, 8))
			die("spr_div(%a, %a) = %a, the host's divide gives %a", a, b, got, want);
	}
	return n;
}

int main(int argc, char** argv)
{
	const u32 frames = argc > 1 ? (u32)atoi(argv[1]) : 6000;
	if (argc > 2)
		rs ^= (uint64_t)atoi(argv[2]) * 0x9E3779B97F4A7C15ull;
	g_seed = rs | 1;
	build_lut();
	spu_arena(64u << 20);
	const PpuSide pn = { ppu_new_reset, ppu_new_data, ppu_new_cont, ppu_new_lists };
	const PpuSide pr = { ppu_ref_reset, ppu_ref_data, ppu_ref_cont, ppu_ref_lists };
	const SpuSide sn = { spu_new_reset, spu_new_data, spu_new_fence };
	const SpuSide sr = { spu_ref_reset, spu_ref_data, spu_ref_fence };
	spu_new_init(0);
	spu_ref_init(0);
	const u32 n_div = div_checks(2000000);

	// Phase 1, options off: the tree's decoders against the frozen ones, and against each other.
	for (g_frame = 0; g_frame < frames; g_frame++)
	{
		gen_frame(0, false, g_frame % 23 == 5 ? 1 : g_frame % 29 == 7 ? 2 : g_frame % 31 == 9 ? 3 : 0);
		const u32 ctx = rndn(SPE_CTX);
		run_ppu(pn, &Lp);
		run_ppu(pr, &Lrp);
		run_spu(sn, ctx, &Ls);
		run_spu(sr, ctx, &Lrs);
		same(Lp, Lrp, "options off, PPU new / old");
		same(Ls, Lrs, "options off, SPU new / old");
		same(Lp, Ls, "options off, PPU / SPU");
		model(Lp);
		n_rec += F.w.size() / 8;
	}
	printf("phase 1: %u frames with the options off equal the old decoders (%llu records, %llu vertices)\n", frames,
	       (unsigned long long)n_rec, (unsigned long long)n_vtx);

	// Phase 2, any options; phase 3 adds the records with no meaning and TA Bad Records at SKIP.
	for (int phase = 2; phase <= 3; phase++)
	{
		for (g_frame = 0; g_frame < frames; g_frame++)
		{
			const u32 opt = rndn(8) ? rnd() & 127 : rndn(2) ? 127 : 1u << rndn(7);
			gen_frame(opt, phase == 3, g_frame % 23 == 5 ? 1 : g_frame % 29 == 7 ? 2 : g_frame % 31 == 9 ? 3 : 0);
			const u32 ctx = rndn(SPE_CTX);
			const u32 bp = ppu_new_bad(), bs = spu_new_bad();
			const u32 pp = ppu_new_planes(), ps = spu_new_planes();
			const uint64_t mp = n_spr_plane, me = n_spr_either;
			run_ppu(pn, &Lp);
			run_spu(sn, ctx, &Ls);
			same(Lp, Ls, "PPU / SPU");
			if (Ls.opt != opt || Lp.opt != opt)
				die("the lists say options %02X / %02X, want %02X", Lp.opt, Ls.opt, opt);
			if (ppu_new_bad() - bp != F.nbad || spu_new_bad() - bs != F.nbad)
				die("bad records counted %u / %u, want %u", ppu_new_bad() - bp, spu_new_bad() - bs, F.nbad);
			model(Lp);
			// Each decoder counts its plane corners: the same count, and it holds every sprite the model is sure of.
			const u32 dp = ppu_new_planes() - pp, ds = spu_new_planes() - ps;
			if (dp != ds || dp < n_spr_plane - mp || dp > n_spr_plane - mp + (n_spr_either - me))
				die("plane corners counted %u / %u, the model has %llu sure and %llu near a limit", dp, ds,
				    (unsigned long long)(n_spr_plane - mp), (unsigned long long)(n_spr_either - me));
			n_badrec += F.nbad;
			if (opt & TAOPT_INT_CLAMP)
				n_int++;
			n_rec += F.w.size() / 8;
		}
	}
	printf("PASS: %u frames per phase, %llu records; %llu vertices, %llu params (%llu with a clip rect), %llu volume "
	       "records (%llu of the TR list), %llu frames past a triangle cap, %llu pass marks, %llu bad records skipped, "
	       "%llu frames with Intensity Clamp\n", frames, (unsigned long long)n_rec, (unsigned long long)n_vtx,
	       (unsigned long long)n_param, (unsigned long long)n_clip, (unsigned long long)n_mvol,
	       (unsigned long long)n_tr_mvol, (unsigned long long)n_capped, (unsigned long long)n_marks,
	       (unsigned long long)n_badrec, (unsigned long long)n_int);
	printf("bundle 17: %llu offset colours; %llu sprites: %llu at the plane's corner, %llu at the old one, %llu near a "
	       "limit of the rule; %u divides\n", (unsigned long long)n_offs, (unsigned long long)n_spr,
	       (unsigned long long)n_spr_plane, (unsigned long long)n_spr_old, (unsigned long long)n_spr_either, n_div);
	if (!n_offs || !n_spr_plane || !n_spr_old)
		die("a bundle 17 case never ran");
	return 0;
}
