// Bundle 4 (plan section 7) PC check, WSL: the draw rules. Rows off: draw_scene makes the frozen file's GL calls.
// Rows on: a model of the rules; bundle 14: with TR First Pass at OLD, also the calls of the file of before it.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <set>
#include "rend4.h"

typedef uint32_t u32;

static u32 g_scene;
static void die(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL (scene %u): ", g_scene);
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

enum { GL_NEVER = 0x200, GL_FRONT = 0x404, GL_BACK = 0x405, GL_FLAT = 0x1D00, GL_SMOOTH = 0x1D01 };
static const u32 k_src[8] = { 0, 1, 0x306, 0x307, 0x302, 0x303, 0x304, 0x305 };
static const u32 k_dst[8] = { 0, 1, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305 };
static const unsigned char k_cmp[2][8] = { { 0, 4, 2, 6, 1, 5, 3, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 } };

// ---- scenes -------------------------------------------------------------------

static std::vector<u32> g_vtx, g_strip, g_param, g_mtri, g_mvol, g_offs;
static R4Scene S;

// A clip rect in the reserved bits: tiles, sometimes over the whole area, sometimes off the canvas.
static u32 rnd_rect()
{
	const u32 r = rndn(10);
	if (r == 0)
		return 0 | 19u << 6 | 0 << 12 | 14u << 17;   // the whole 640x480
	if (r == 1)
		return 0 | 39u << 6 | 0 << 12 | 31u << 17;
	u32 x0 = rndn(22), x1 = rndn(22), y0 = rndn(17), y1 = rndn(17);
	if (rndn(8) && x1 < x0) { const u32 t = x0; x0 = x1; x1 = t; }
	if (rndn(8) && y1 < y0) { const u32 t = y0; y0 = y1; y1 = t; }
	return x0 | x1 << 6 | y0 << 12 | y1 << 17;
}

static void gen_scene(bool rows)
{
	memset(&S, 0, sizeof(S));
	S.opt = rows ? rnd() & 31 : 0;
	const bool rects = (S.opt & 16) != 0;
	// Params from a small pool, so strips merge.
	u32 base[6][4];
	const u32 pool = 1 + rndn(6);
	for (u32 k = 0; k < pool; k++)
	{
		const u32 uc = rndn(3) ? 0 : rndn(4);
		const u32 rect = rects ? (uc >= 2 ? rnd_rect() : 0) : rnd();
		base[k][0] = (rnd() & 0xFF) | uc << 16 | (rndn(3) ? 4u : 5u) << 29 | ((rect >> 20) & 3) << 8;
		base[k][1] = (rnd() & 0xFFF00000u) | (rect & 0xFFFFFu);
		// The blend: any, or often the two that hide what is behind (SRC_ALPHA with INV_SRC_ALPHA; ONE with ZERO).
		const u32 bl = rndn(4);
		base[k][2] = bl == 0 ? rnd() : (rnd() & 0x03FFFFFFu) | (bl == 3 ? 1u << 29 : 4u << 29 | 5u << 26);
		base[k][3] = (rnd() & 0x07FFFFFFu) | (rndn(2) << 31);  // no bump texture; often MipMapped
	}
	S.nparam = 1 + rndn(40);
	g_param.resize(S.nparam * 4);
	for (u32 i = 0; i < S.nparam; i++)
	{
		u32 q[4];
		memcpy(q, base[rndn(pool)], 16);
		const u32 r = rndn(100);
		if (r < 20)
			q[1] ^= 0x18000000u;           // the cull direction
		else if (r < 30)
			q[0] ^= 1u << rndn(8);
		else if (r < 36)
			q[1] ^= 1u << (26 + rndn(6));
		else if (r < 40)
			q[2] ^= 1u << rndn(32);
		else if (r < 46 && rects && ((q[0] >> 16) & 3) >= 2)
		{
			const u32 rect = rnd_rect();
			q[0] = (q[0] & ~0x300u) | ((rect >> 20) & 3) << 8;
			q[1] = (q[1] & 0xFFF00000u) | (rect & 0xFFFFFu);
		}
		memcpy(&g_param[i * 4], q, 16);
	}
	S.nstrip = rndn(8) ? 1 + rndn(120) : 0;
	// Bundle 14 GAME ORDER: long translucent strips alone do not fit the block twice, so a second block is asked for.
	const bool all_tr = rows && rndn(6) == 0;
	g_strip.resize(S.nstrip * 4);
	g_vtx.clear();
	u32 prm = 0;
	for (u32 i = 0; i < S.nstrip; i++)
	{
		const u32 count = 3 + rndn(all_tr ? 12 : 6);
		const u32 l = all_tr ? 60 : rndn(100);
		if (rndn(3) == 0)
			prm = rndn(S.nparam);
		g_strip[i * 4] = (u32)(g_vtx.size() / 6);
		g_strip[i * 4 + 1] = count;
		g_strip[i * 4 + 2] = l < 50 ? 0 : l < 80 ? 2 : 4;
		g_strip[i * 4 + 3] = prm;
		// 3k: at most one inf corner (W = 0) in a translucent strip; the sorted triangles leave its triangles out.
		const u32 inf_at = rows && g_strip[i * 4 + 2] == 2 && rndn(6) == 0 ? rndn(count) : ~0u;
		for (u32 k = 0; k < count; k++)
		{
			const u32 v[6] = { fb((float)rndn(6400) / 10.0f), fb((float)rndn(4800) / 10.0f),
			                   k == inf_at ? 0 : fb(1.0f + (float)rndn(990) / 10.0f), rnd(), fb((float)i), fb((float)k) };
			g_vtx.insert(g_vtx.end(), v, v + 6);
		}
	}
	S.nvtx = (u32)(g_vtx.size() / 6);
	// Bundle 17: one offset colour per vertex.
	g_offs.resize(S.nvtx + 1);
	for (u32 i = 0; i < S.nvtx; i++)
		g_offs[i] = rnd();
	S.offs = g_offs.data();
	// Volumes: runs of records of one list, each run closed by its end record. A triangle's id is every x of it.
	g_mvol.clear();
	g_mtri.clear();
	// The opaque list's runs come first: the area sum of a volume group spans every triangle from the group's
	// first one (Flycast's too), so a run of the other list inside a group would be drawn with it.
	const u32 nlists = rndn(4), tr_from = rndn(4);
	for (u32 l = 0; l < nlists; l++)
	{
		const u32 tag = rows && (S.opt & 4) ? l >= tr_from : 0;
		const u32 nv = rndn(5);
		for (u32 v = 0; v <= nv; v++)
		{
			const u32 e[5] = { (u32)(g_mtri.size() / 9), v < nv ? rndn(4) : 0, v < nv ? rndn(8) : 0, v < nv ? rndn(2) : 0, tag };
			g_mvol.insert(g_mvol.end(), e, e + 5);
			const u32 nt = v < nv ? rndn(4) : 0;
			for (u32 t = 0; t < nt; t++)
			{
				const float id = (float)(g_mtri.size() / 9);
				const u32 tri[9] = { fb(id), fb((float)rndn(480)), fb(1.0f), fb(id), fb((float)rndn(480)), fb(1.0f),
				                     fb(id), fb((float)rndn(480)), fb(1.0f) };
				g_mtri.insert(g_mtri.end(), tri, tri + 9);
			}
		}
	}
	S.nmvol = (u32)(g_mvol.size() / 5);
	S.nmtri = (u32)(g_mtri.size() / 9);
	// The pass marks of the PPU replay (the lists carry them with the rows off too).
	S.npass = rndn(3) ? 0 : 1 + rndn(4);
	u32 ps = 0, pm = 0;
	for (u32 k = 0; k < S.npass; k++)
	{
		if (rndn(4))
			ps += rndn(S.nstrip - ps + 1);
		pm += rndn(S.nmvol - pm + 1);
		S.pass_strip[k] = ps;
		S.pass_mvol[k] = pm;
	}
	static const u32 none[9] = { 0 };
	S.vtx = g_vtx.empty() ? none : g_vtx.data();
	S.strip = g_strip.empty() ? none : g_strip.data();
	S.param = g_param.data();
	S.mtri = g_mtri.empty() ? none : g_mtri.data();
	S.mvol = g_mvol.empty() ? none : g_mvol.data();
	S.max_inv_w = fb(1.0f);
	S.isp_backgnd_d = fb(0.001f);
	S.pt_alpha_ref = rnd() & 0xFF;
	S.fpu_shad_scale = rnd() & 0x1FF;
	const u32 as = rndn(2);
	for (int k = 0; k < R4_PASSES; k++)
		S.pass_autosort[k] = rows ? rndn(2) : as;
	S.mv_shared = rows ? rndn(2) : 0;
	S.glob_clip_x = 640;
	S.glob_clip_y = 480;
	S.bg_param[0] = (rnd() & 0x8E);
	S.bg_param[1] = rnd() & 0xFFF00000u;
	S.bg_param[2] = rnd();
	S.bg_param[3] = rnd() & 0x07FFFFFFu;
	const float bx[3] = { 0, 640, 0 }, by[3] = { 0, 0, 480 };
	for (int k = 0; k < 3; k++)
	{
		S.bg_vtx[k][0] = bx[k];
		S.bg_vtx[k][1] = by[k];
		S.bg_vtx[k][2] = (float)rndn(100) / 100.0f;
		S.bg_vtx[k][3] = (float)rndn(100) / 100.0f;
		S.bg_col[k] = rnd();
	}
	if (rndn(4))
	{
		// The display: a 4:3 viewport in 1280x720, scissored.
		S.vx = 160; S.vy = 0; S.vw = 960; S.vh = 720; S.scissor = 1;
		S.cx0 = 0; S.cy0 = 0; S.cw = 640; S.ch = 480;
	}
	else
	{
		// A render to texture: the clip window at its own size, no scissor.
		const u32 w = 64 + rndn(577), h = 64 + rndn(417);
		S.vx = 0; S.vy = 0; S.vw = (int)w; S.vh = (int)h; S.scissor = 0;
		S.cx0 = (float)rndn(641 - w); S.cy0 = (float)rndn(481 - h); S.cw = (float)w; S.ch = (float)h;
	}
	S.depth_float = rndn(2);
	S.merge = rows ? 1 : rndn(8) != 0;
	S.vertex_arrays = rows ? 1 : rndn(8) != 0;
	if (rows)
	{
		S.pt_depth = rndn(2);
		S.tr_nodepth = rndn(2);
		S.pt_blend = rndn(3) ? rndn(2) : 2;
		S.sprite_cull = rndn(2);
		S.flat = rndn(3);
		S.passes = rndn(3);
		S.clip_level = rndn(3);
		S.tri_sort = rndn(4);
		S.first_pass = rndn(3);
		// Bundle 17: the registers clamp in two scenes of three with the row on.
		S.offs_row = rndn(3) == 0;
		S.clamp_row = rndn(3) == 0;
		S.tril_row = rndn(3) == 0;
		S.fog_clamp_min = rndn(3) ? rnd() : 0;
		S.fog_clamp_max = rndn(3) ? rnd() : 0xFFFFFFFFu;
	}
}

// ---- the model -----------------------------------------------------------------

static uint64_t n_draws, n_depth, n_clip_in, n_clip_out, n_clip_all, n_flat, n_shift, n_multi, n_trvol, n_spr, n_tris;
static uint64_t n_sorted, n_sorted_swap, n_opaque;
// Bundle 14: depth-only triangles in submission order, triangles SKIP left out, scenes with a second vertex block.
static uint64_t n_game, n_skipped, n_blocks, n_applied, n_old;
// Bundle 17: PT draws that ALPHA 1 makes blend-free and those it blends with new factors; draws with the offset
// colour program, with the clamp, with a weight under 1; vertices that carry their offset colour.
static uint64_t n_pt_one, n_pt_fac, n_dc_offs, n_dc_clamp, n_dc_tril, n_offs_vtx;

static bool m_offc(const u32* p) { return ((p[0] >> 3) & 1) && ((p[0] >> 2) & 1) && ((p[3] >> 27) & 7) != 4; }

// Bundle 17: the offset colour program and the clamp and weight program at a draw of a strip of list L with param p.
// off: a depth pass of List Passes, which draws with neither. bg: the background, which has no offset colours.
static void model_dc(const R4Call& d, const u32* p, u32 L, u32 i, u32 sid, bool off, bool bg = false)
{
	const bool tex = (p[0] >> 3) & 1;
	const bool offc = !off && !bg && S.offs_row && m_offc(p);
	if (d.dc_offs != (u32)offc)
		die("draw %u strip %u: offset colour program %u, want %u", i, sid, d.dc_offs, (u32)offc);
	const bool regs = S.fog_clamp_min != 0 || S.fog_clamp_max != 0xFFFFFFFFu;
	const bool cl = !off && S.clamp_row && regs && ((p[2] >> 21) & 1);
	float w = 1.0f;
	const u32 fm = (p[2] >> 13) & 3;
	if (!off && S.tril_row && tex && fm >= 2 && L != 4 && (p[3] >> 31))
	{
		const float f = 0.25f * (float)((p[2] >> 8) & 3);
		w = fm == 2 ? 1.0f - f : f;
	}
	const bool xt = cl || w != 1.0f;
	if (d.dc_xt != (u32)xt)
		die("draw %u strip %u: clamp and weight program %u, want %u (clamp %d weight %g)", i, sid, d.dc_xt, (u32)xt, cl, (double)w);
	if (xt)
	{
		static const int sh[4] = { 16, 8, 0, 24 };
		for (int k = 0; k < 4; k++)
		{
			const u32 lo = cl ? fb((float)((S.fog_clamp_min >> sh[k]) & 0xFF) / 255.0f) : fb(-1e9f);
			const u32 hi = cl ? fb((float)((S.fog_clamp_max >> sh[k]) & 0xFF) / 255.0f) : fb(1e9f);
			if (d.xt[k] != lo || d.xt[4 + k] != hi)
				die("draw %u strip %u: clamp channel %d is %08X..%08X, want %08X..%08X", i, sid, k, d.xt[k], d.xt[4 + k], lo, hi);
		}
		if (d.xt[8] != fb(w))
			die("draw %u strip %u: weight %08X, want %08X", i, sid, d.xt[8], fb(w));
	}
	n_dc_offs += offc;
	n_dc_clamp += cl;
	n_dc_tril += w != 1.0f;
}

// The largest W of a triangle's corners as bits (W is positive): the sort's key, the farthest corner.
static u32 tri_w(u32 sid, u32 kc)
{
	const u32 first = S.strip[sid * 4];
	u32 w = 0;
	for (u32 k = kc - 2; k <= kc; k++)
		if (S.vtx[(first + k) * 6 + 2] > w)
			w = S.vtx[(first + k) * 6 + 2];
	return w;
}

static u32 pass_of(u32 sid, bool multi)
{
	u32 p = 0;
	if (multi)
		for (u32 k = 0; k < S.npass; k++)
			if (S.pass_strip[k] <= sid)
				p = k + 1;
	return p;
}

struct Rect { int x0, y0, x1, y1; };
static int area(const Rect& r) { return r.x1 > r.x0 && r.y1 > r.y0 ? (r.x1 - r.x0) * (r.y1 - r.y0) : 0; }
static bool meet(const Rect& a, const Rect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

// The clip of a param: 0 none, 2 inside, 3 outside; r: the tile rect in GL pixels, cut to the viewport.
static u32 clip_of(const u32* p, Rect* r)
{
	const u32 level = (S.opt & 16) ? S.clip_level : 0;
	const u32 mode = (p[0] >> 16) & 3;
	if (!level || mode < 2 || (mode == 3 && level < 2))
		return 0;
	const u32 rect = (p[1] & 0xFFFFFu) | ((p[0] >> 8) & 3) << 20;
	const double xs = (rect & 63) * 32.0, xe = (((rect >> 6) & 63) + 1) * 32.0;
	const double ys = ((rect >> 12) & 31) * 32.0, ye = (((rect >> 17) & 31) + 1) * 32.0;
	if (xs == 0 && ys == 0 && xe >= S.glob_clip_x && ye >= S.glob_clip_y)
		return 0;
	const double sx = (double)S.vw / S.cw, sy = (double)S.vh / S.ch;
	Rect q = { S.vx + (int)lround((xs - S.cx0) * sx), S.vy + S.vh - (int)lround((ye - S.cy0) * sy),
	           S.vx + (int)lround((xe - S.cx0) * sx), S.vy + S.vh - (int)lround((ys - S.cy0) * sy) };
	const Rect v = { S.vx, S.vy, S.vx + S.vw, S.vy + S.vh };
	q.x0 = q.x0 < v.x0 ? v.x0 : q.x0 > v.x1 ? v.x1 : q.x0;
	q.x1 = q.x1 < v.x0 ? v.x0 : q.x1 > v.x1 ? v.x1 : q.x1;
	q.y0 = q.y0 < v.y0 ? v.y0 : q.y0 > v.y1 ? v.y1 : q.y0;
	q.y1 = q.y1 < v.y0 ? v.y0 : q.y1 > v.y1 ? v.y1 : q.y1;
	*r = q;
	// Outside a rect with no pixel is everywhere: no clip (Flycast's test never cuts then).
	if (mode == 3 && !area(q))
		return 0;
	return mode;
}

static void model(const R4Call* c, u32 n, const u32* vb)
{
	const bool multi = S.passes && S.npass != 0;
	const u32 last_pass = multi ? S.npass : 0;
	std::vector<u32> colour(S.nstrip, 0), depth(S.nstrip, 0), tris(S.nstrip, 0), pre(S.nstrip, 0);
	// + OPAQUE DEPTH: the pass whose colour draws have begun (its depth-only draws must all come before them).
	u32 colour_pass = ~0u;
	// 3k: the last triangle drawn in each pass, of the colour draws and of the depth-only draws.
	struct Last { u32 pass, w, sid, kc; } last_col = { ~0u, 0, 0, 0 }, last_pre = { ~0u, 0, 0, 0 };
	// The last pass that has strips: the TR depth pass runs after every pass but the last one.
	u32 prev_key = 0, n_imm = 0;
	for (u32 i = 0; i < n;)
	{
		if (c[i].kind != R4_DRAW)
		{
			// The background, the volumes and their shade quad draw under the viewport's own scissor.
			if (c[i].kind == R4_IMM)
			{
				const u32 view[4] = { (u32)S.vx, (u32)S.vy, (u32)S.vw, (u32)S.vh };
				if (c[i].scissor != (u32)S.scissor || (S.scissor && memcmp(c[i].sc, view, 16)))
					die("call %u: a glBegin draw under a clip scissor", i);
				// Bundle 17: the background (the first of them) takes the clamp and weight of its own param; the
				// volumes and their shade quad take no program of the polygon before them.
				if (n_imm++ == 0)
					model_dc(c[i], S.bg_param, 0, i, ~0u, false, true);
				else if (c[i].dc_offs || c[i].dc_xt)
					die("call %u: a volume draw with the offset colour program %u, clamp and weight program %u", i,
					    c[i].dc_offs, c[i].dc_xt);
			}
			i++;
			continue;
		}
		// A group: the draws of one range. An outside clip sends its range once per side of the rect.
		u32 j = i;
		while (j < n && (c[j].kind != R4_DRAW || (c[j].first == c[i].first && c[j].count == c[i].count &&
		       c[j].color_mask == c[i].color_mask)))
		{
			if (c[j].kind == R4_CALL && c[j].op != 18 /* OP_SCISSOR */)
				break;
			j++;
		}
		const R4Call& d = c[i];
		const bool dpass = d.color_mask == 0;
		std::set<u32> ids;
		for (u32 k = d.first; k < d.first + d.count; k++)
			ids.insert((u32)bf(vb[(size_t)k * 9 + 4]));
		const u32 first_id = (u32)bf(vb[(size_t)d.first * 9 + 4]);
		std::vector<Rect> scs;
		for (u32 k = i; k < j; k++)
			if (c[k].kind == R4_DRAW)
			{
				const Rect r = { (int)c[k].sc[0], (int)c[k].sc[1], (int)(c[k].sc[0] + c[k].sc[2]), (int)(c[k].sc[1] + c[k].sc[3]) };
				scs.push_back(r);
				if (memcmp(&c[k].blend, &d.blend, (char*)&d.scissor - (char*)&d.blend) || c[k].shift != d.shift)
					die("the draws of one range differ in state");
			}
		n_draws++;
		// Bundle 17, 3b: each vertex of the range carries its strip's offset colour in u1 when the row is on and the
		// polygon has one, else 0. u and v name the strip and the vertex in it.
		for (u32 k = d.first; k < d.first + d.count; k++)
		{
			const u32 sid = (u32)bf(vb[(size_t)k * 9 + 4]), kk = (u32)bf(vb[(size_t)k * 9 + 5]);
			if (sid >= S.nstrip || kk >= S.strip[sid * 4 + 1])
				die("draw %u: a vertex of strip %u, corner %u", i, sid, kk);
			const u32* p = &S.param[S.strip[sid * 4 + 3] * 4];
			const u32 o = S.offs[S.strip[sid * 4] + kk];
			const u32 want = S.offs_row && m_offc(p) ? o << 8 | o >> 24 : 0;
			if (vb[(size_t)k * 9 + 6] != want)
				die("draw %u: strip %u corner %u has u1 %08X, want %08X", i, sid, kk, vb[(size_t)k * 9 + 6], want);
			n_offs_vtx += want != 0;
		}
		// 3k: sorted triangles, each one three corners of a strip, wound as the strip; farthest W first, then submission
		// order. + OPAQUE DEPTH's first pass: the same ones before the colour; GAME ORDER: in submission order.
		if (d.prim == 4)
		{
			const u32 fsid = (u32)bf(vb[(size_t)d.first * 9 + 4]);
			const u32* fp = &S.param[S.strip[fsid * 4 + 3] * 4];
			const u32 run_cull = ((fp[1] >> 27) & 3) ^ (u32)(S.sprite_cull && (fp[0] >> 29) == 5);
			if (d.count % 3)
				die("draw %u: %u vertices in a triangle run", i, d.count);
			for (u32 k = d.first; k < d.first + d.count; k += 3)
			{
				u32 sid[3], kk[3];
				for (int c3 = 0; c3 < 3; c3++)
				{
					sid[c3] = (u32)bf(vb[(size_t)(k + c3) * 9 + 4]);
					kk[c3] = (u32)bf(vb[(size_t)(k + c3) * 9 + 5]);
				}
				if (sid[0] != sid[2] || sid[1] != sid[2] || sid[2] >= S.nstrip || kk[2] < 2 || kk[2] >= S.strip[sid[2] * 4 + 1])
					die("draw %u: a triangle of strips %u %u %u corners %u %u %u", i, sid[0], sid[1], sid[2], kk[0], kk[1], kk[2]);
				const u32* p = &S.param[S.strip[sid[2] * 4 + 3] * 4];
				const u32 eff = ((p[1] >> 27) & 3) ^ (u32)(S.sprite_cull && (p[0] >> 29) == 5);
				const u32 swap = (kk[2] & 1) ^ (u32)(eff >= 2 && eff != run_cull);
				if (kk[0] != (swap ? kk[2] - 1 : kk[2] - 2) || kk[1] != (swap ? kk[2] - 2 : kk[2] - 1))
					die("draw %u: strip %u triangle %u has corners %u %u (swap %u)", i, sid[2], kk[2], kk[0], kk[1], swap);
				// Each corner is the strip's vertex: its W, and its colour with the alpha the polygon forces.
				const bool force = !((p[2] >> 20) & 1) || (((p[0] >> 3) & 1) && ((p[2] >> 6) & 3) == 1);
				for (int c3 = 0; c3 < 3; c3++)
				{
					const u32* v = &S.vtx[(size_t)(S.strip[sid[2] * 4] + kk[c3]) * 6];
					const u32 col = v[3] | (force ? 0xFF000000u : 0);
					const u32* o = &vb[(size_t)(k + c3) * 9];
					if (o[2] != v[2] || o[8] != (col << 8 | col >> 24))
						die("draw %u: strip %u corner %u is W %08X colour %08X, want %08X %08X", i, sid[2], kk[c3], o[2], o[8],
						    v[2], col << 8 | col >> 24);
				}
				const u32 pass = pass_of(sid[2], multi), w = tri_w(sid[2], kk[2]);
				Last& l = dpass ? last_pre : last_col;
				const bool later = sid[2] > l.sid || (sid[2] == l.sid && kk[2] > l.kc);
				const bool game = dpass && S.first_pass == 2;
				if (pass == l.pass && (game ? !later : w > l.w || (w == l.w && !later)))
					die("draw %u: %s triangle %u/%u (W %08X) after %u/%u (W %08X)", i, dpass ? "depth-only" : "colour", sid[2],
					    kk[2], w, l.sid, l.kc, l.w);
				const Last now = { pass, w, sid[2], kk[2] };
				l = now;
				if (dpass)
				{
					if (pass == colour_pass)
						die("draw %u: a depth-only triangle of strip %u after the colour of its pass", i, sid[2]);
					pre[sid[2]]++;
					n_opaque++;
					n_game += game;
					continue;
				}
				colour_pass = pass;
				tris[sid[2]]++;
				n_sorted++;
				n_sorted_swap += swap;
			}
		}
		else if (d.prim != 5)
			die("draw %u: primitive %u", i, d.prim);
		for (u32 sid : ids)
		{
			if (sid >= S.nstrip)
				die("draw %u: vertex of strip %u", i, sid);
			const u32* st = &S.strip[sid * 4];
			const u32* p = &S.param[st[3] * 4];
			const u32 L = st[2], pass = pass_of(sid, multi);
			const bool as = S.pass_autosort[pass < R4_PASSES ? pass : R4_PASSES - 1] != 0;
			const u32 zwd = (p[1] >> 26) & 1, dm = p[1] >> 29, gouraud = (p[0] >> 1) & 1;
			const u32 eff = ((p[1] >> 27) & 3) ^ (u32)(S.sprite_cull && (p[0] >> 29) == 5);
			const u32 sec = L == 0 ? 0 : L == 4 ? 1 : 2;
			// The sorted triangles are the autosorted translucent strips of a pass, with the row ON.
			const bool trilist = S.tri_sort && L == 2 && as;
			const bool trimode = trilist && !dpass;
			// A depth-only draw is the List Passes depth pass (strips) or + OPAQUE DEPTH's first pass (triangles).
			const bool opq = dpass && d.prim == 4;
			if ((d.prim == 4) != (trimode || opq) || (opq && !(trilist && S.tri_sort == 3)))
				die("draw %u strip %u (list %u, autosort %d): primitive %u with TR Triangle Sort %u", i, sid, L, as, d.prim, S.tri_sort);
			const u32 key = pass * 4 + (dpass && !opq ? 3 : sec);
			if (key < prev_key)
				die("draw %u: strip %u (pass %u, list %u) after a later section", i, sid, pass, L);
			prev_key = key;
			if (d.cull != (u32)(eff >= 2))
				die("draw %u strip %u: cull %u, want %u", i, sid, d.cull, eff >= 2);
			if (sid == first_id && eff >= 2 && d.cull_face != (eff == 2 ? GL_FRONT : GL_BACK))
				die("draw %u strip %u: cull face %X for mode %u", i, sid, d.cull_face, eff);
			if ((p[0] >> 29) == 5 && ((p[1] >> 27) & 3) >= 2 && S.sprite_cull)
				n_spr++;
			if (opq)
			{
				// Only a polygon whose blend hides what is behind it is in this pass: at full alpha (the alpha test at
				// the opaque level), or at any alpha (no alpha test). No blend, depth as the game asks.
				const u32 si = (p[2] >> 29) & 7, di = (p[2] >> 26) & 7;
				const u32 covers = si == 1 && di == 0 ? 2 : si == 4 && di == 5 ? 1 : 0;
				if (!covers || (S.first_pass && zwd) || d.blend || d.depth_func != GL_NEVER + k_cmp[S.depth_float][6] ||
				    d.depth_mask != !zwd ||
				    d.alpha_test != (u32)(covers == 1) || d.alpha_func != 0x206 || d.alpha_ref != fb(240 / 255.0f))
					die("draw %u strip %u: the opaque depth pass state (covers %u blend %u mask %u test %u func %X)", i, sid,
					    covers, d.blend, d.depth_mask, d.alpha_test, d.alpha_func);
				model_dc(d, p, L, i, sid, false);
			}
			else if (dpass)
			{
				if (!(multi && S.passes == 2 && L == 2 && as && pass < last_pass))
					die("draw %u: a depth pass over strip %u (pass %u of %u, list %u)", i, sid, pass, last_pass, L);
				if (d.blend || d.depth_func != GL_NEVER + k_cmp[S.depth_float][6] || d.depth_mask != !zwd || d.texture)
					die("draw %u strip %u: the depth pass state", i, sid);
				model_dc(d, p, L, i, sid, true);
				depth[sid]++;
				n_depth++;
			}
			else
			{
				// P79, PT Blend + ALPHA 1: in the punch-through list SRC_ALPHA is ONE and INV_SRC_ALPHA is ZERO; ONE with
				// ZERO is no blend.
				u32 si = (p[2] >> 29) & 7, di = (p[2] >> 26) & 7;
				const bool one = L == 4 && S.pt_blend == 2;
				if (one)
				{
					si = si == 4 ? 1 : si == 5 ? 0 : si;
					di = di == 4 ? 1 : di == 5 ? 0 : di;
				}
				const bool blend = L == 2 || (L == 4 && S.pt_blend && !(one && si == 1 && di == 0)) || (L == 0 && pass != 0);
				if (d.blend != (u32)blend)
					die("draw %u strip %u (list %u pass %u): blend %u", i, sid, L, pass, d.blend);
				if (blend && (d.src != k_src[si] || d.dst != k_dst[di]))
					die("draw %u strip %u: blend factors %X %X", i, sid, d.src, d.dst);
				n_pt_one += one && !blend;
				n_pt_fac += one && blend && (si != ((p[2] >> 29) & 7) || di != ((p[2] >> 26) & 7));
				model_dc(d, p, L, i, sid, false);
				bool zw = !zwd;
				if (L == 4 && S.pt_depth)
					zw = true;
				else if (L == 2 && as && (S.tr_nodepth || S.tri_sort == 1 || S.tri_sort == 3))
					zw = false;
				// The translucent list's own alpha test: above 0, where the polygon asks for it.
				if (L == 2 && (d.alpha_func != 0x204 || d.alpha_ref != 0))
					die("draw %u strip %u: alpha func %X ref %08X in the translucent list", i, sid, d.alpha_func, d.alpha_ref);
				if (d.depth_mask != (u32)zw)
					die("draw %u strip %u (list %u, autosort %d): depth mask %u, want %d", i, sid, L, as, d.depth_mask, zw);
				const u32 cmp = L == 4 ? 6 : L == 2 && as ? 6 : dm;
				if (d.depth_func != GL_NEVER + k_cmp[S.depth_float][cmp])
					die("draw %u strip %u: depth func %X", i, sid, d.depth_func);
				const bool flat = S.flat && !gouraud;
				if (d.shade != (flat ? GL_FLAT : GL_SMOOTH) || d.shift != (flat && S.flat == 2 ? 2u : 0u))
					die("draw %u strip %u: shade %X shift %u (flat %u, gouraud %u)", i, sid, d.shade, d.shift, S.flat, gouraud);
				n_flat += flat;
				n_shift += d.shift != 0;
				if (!trimode)
					colour[sid]++;
			}
			// The clip: every strip of the range has the range's scissors.
			Rect r;
			const u32 mode = clip_of(p, &r);
			const Rect v = { S.vx, S.vy, S.vx + S.vw, S.vy + S.vh };
			if (mode == 0)
			{
				if (scs.size() != 1 || d.scissor != (u32)S.scissor || (S.scissor && memcmp(&scs[0], &v, sizeof(v))))
					die("draw %u strip %u: no clip, but scissor %u %d,%d-%d,%d x%zu", i, sid, d.scissor, scs[0].x0, scs[0].y0,
					    scs[0].x1, scs[0].y1, scs.size());
			}
			else if (mode == 2)
			{
				if (scs.size() != 1 || !d.scissor || memcmp(&scs[0], &r, sizeof(r)) || !area(r))
					die("draw %u strip %u: inside clip %d,%d-%d,%d, scissor %d,%d-%d,%d x%zu", i, sid, r.x0, r.y0, r.x1, r.y1,
					    scs[0].x0, scs[0].y0, scs[0].x1, scs[0].y1, scs.size());
				n_clip_in++;
			}
			else
			{
				int sum = 0;
				for (size_t a = 0; a < scs.size(); a++)
				{
					if (!d.scissor || !area(scs[a]) || (area(r) && meet(scs[a], r)) || scs[a].x0 < v.x0 || scs[a].y0 < v.y0 ||
					    scs[a].x1 > v.x1 || scs[a].y1 > v.y1)
						die("draw %u strip %u: outside clip %d,%d-%d,%d in %d,%d-%d,%d: scissor %zu of %zu is %d,%d-%d,%d (on %u)", i,
						    sid, r.x0, r.y0, r.x1, r.y1, v.x0, v.y0, v.x1, v.y1, a, scs.size(), scs[a].x0, scs[a].y0, scs[a].x1,
						    scs[a].y1, d.scissor);
					for (size_t b = 0; b < a; b++)
						if (meet(scs[a], scs[b]))
							die("draw %u strip %u: outside clip, two scissors overlap", i, sid);
					sum += area(scs[a]);
				}
				if (sum != area(v) - area(r))
					die("draw %u strip %u: outside clip covers %d pixels, want %d", i, sid, sum, area(v) - area(r));
				n_clip_out++;
			}
		}
		i = j;
	}
	// Every strip once, but one an inside clip cuts whole, or an outside clip over the whole viewport.
	for (u32 sid = 0; sid < S.nstrip; sid++)
	{
		const u32* st = &S.strip[sid * 4];
		const u32* p = &S.param[st[3] * 4];
		const u32 pass = pass_of(sid, multi);
		const bool as = S.pass_autosort[pass < R4_PASSES ? pass : R4_PASSES - 1] != 0;
		Rect r;
		const u32 mode = clip_of(p, &r);
		const Rect v = { S.vx, S.vy, S.vx + S.vw, S.vy + S.vh };
		const bool cut = (mode == 2 && !area(r)) || (mode == 3 && area(r) == area(v));
		n_clip_all += cut;
		if (S.tri_sort && st[2] == 2 && as)
		{
			// The triangles with no inf corner.
			u32 want_tris = 0;
			for (u32 k = 2; k < st[1] && !cut; k++)
				want_tris += S.vtx[(st[0] + k) * 6 + 2] && S.vtx[(st[0] + k - 1) * 6 + 2] && S.vtx[(st[0] + k - 2) * 6 + 2];
			const u32 si = (p[2] >> 29) & 7, di = (p[2] >> 26) & 7;
			const bool covers = (si == 1 && di == 0) || (si == 4 && di == 5);
			// TR First Pass: a polygon that writes no depth is not in the first pass.
			const bool skip = S.first_pass && ((p[1] >> 26) & 1);
			if (S.tri_sort == 3 && covers && skip)
				n_skipped += want_tris;
			if (colour[sid] || tris[sid] != want_tris || pre[sid] != (S.tri_sort == 3 && covers && !skip ? want_tris : 0u))
				die("strip %u: %u sorted triangles (%u depth-only), want %u (and %u strip draws)", sid, tris[sid], pre[sid],
				    want_tris, colour[sid]);
		}
		else if (colour[sid] != (cut ? 0u : 1u) || tris[sid] || pre[sid])
			die("strip %u drawn %u times (clip mode %u)", sid, colour[sid], mode);
		const bool want_depth = multi && S.passes == 2 && st[2] == 2 && as && pass < last_pass && !cut;
		// A later pass with no strip is not drawn; then the pass before it is the last one drawn, with a depth pass.
		if (depth[sid] != (want_depth ? 1u : 0u))
			die("strip %u: %u depth passes, want %d (pass %u of %u)", sid, depth[sid], want_depth, pass, last_pass);
	}
	// The volumes: the records of each pass drawn (an empty later pass gives its records to the next), of one list.
	// Each volume's triangles once, and the group's again where a volume closes it (DepthMode 1 or 2).
	std::vector<u32> want(S.nmtri, 0);
	u32 cnt[2] = { 0, 0 };
	for (u32 i = 0; i + 1 < S.nmvol; i++)
		if (S.mvol[(i + 1) * 5] > S.mvol[i * 5])
			cnt[S.mvol[i * 5 + 4] & 1]++;
	const u32 vlist = (S.opt & 4) && S.mv_shared && cnt[1] > cnt[0];
	n_trvol += vlist;
	if (S.nstrip && S.nmvol >= 2 && S.nmtri)
	{
		u32 mv_lo = 0;
		const u32 np = multi ? S.npass + 1 : 1;
		for (u32 k = 0; k < np; k++)
		{
			const u32 lo = k ? S.pass_strip[k - 1] : 0, hi = k + 1 < np ? S.pass_strip[k] : S.nstrip;
			const u32 mv_hi = k + 1 < np ? S.pass_mvol[k] : S.nmvol;
			if (k && lo == hi)
				continue;
			int base = -1;
			for (u32 i = mv_lo; i < mv_hi && i + 1 < S.nmvol; i++)
			{
				const u32 first = S.mvol[i * 5], end = S.mvol[(i + 1) * 5], depth = S.mvol[i * 5 + 2];
				if (end == first || S.mvol[i * 5 + 4] != vlist)
					continue;
				if (base < 0)
					base = (int)first;
				for (u32 t = first; t < end; t++)
					want[t]++;
				if (depth == 1 || depth == 2)
				{
					for (u32 t = (u32)base; t < end; t++)
						want[t]++;
					base = -1;
				}
			}
			mv_lo = mv_hi;
		}
	}
	const float* tx;
	const u32 nt = r4_new_tris(&tx);
	std::vector<u32> got(S.nmtri, 0);
	for (u32 k = 0; k < nt; k++)
	{
		if ((u32)tx[k] >= S.nmtri)
			die("a volume vertex of triangle %u", (u32)tx[k]);
		got[(u32)tx[k]]++;
	}
	for (u32 t = 0; t < S.nmtri; t++)
	{
		if (got[t] != want[t] * 3)
			die("volume triangle %u drawn %u/3 times, want %u (list %u)", t, got[t], want[t], vlist);
		n_tris += want[t];
	}
	n_multi += multi;
	// TR First Pass: with no clip rect in the scene each run the first pass applies is one draw, so a run that is
	// left out is not applied. The pass is between the colour mask off with the opaque alpha level, and the mask on.
	if (S.first_pass && !(S.opt & 16))
		for (u32 i = 0; i + 1 < n; i++)
		{
			if (c[i].kind != R4_CALL || c[i].op != 9 /* OP_COLORMASK */ || c[i].a[0] || c[i + 1].kind != R4_CALL ||
			    c[i + 1].op != 11 /* OP_ALPHAFUNC */ || c[i + 1].a[0] != 0x206)
				continue;
			u32 applied = 0, drawn = 0;
			for (i += 2; i < n && !(c[i].kind == R4_CALL && c[i].op == 9); i++)
			{
				applied += c[i].kind == R4_CALL && c[i].op == 6 /* OP_STENCILFUNC */;
				drawn += c[i].kind == R4_DRAW;
			}
			if (applied > drawn)
				die("the first pass applied %u states for %u draws", applied, drawn);
			n_applied += applied;
		}
	u32 allocs = 0, last_alloc = 0;
	for (u32 i = 0; i < n; i++)
		if (c[i].kind == R4_CALL && c[i].op == 23 /* OP_ALLOC */)
		{
			allocs++;
			last_alloc = c[i].a[0];
		}
	n_blocks += allocs > 1;
	// GAME ORDER asks for a second block of the room left + 3 vertices per triangle, and only when the first one has
	// no room for 6 per triangle. With one pass the triangles are those of every sorted strip.
	if (allocs > 1 && !multi)
	{
		u32 tri = 0;
		for (u32 sid = 0; sid < S.nstrip; sid++)
			for (u32 k = 2; S.strip[sid * 4 + 2] == 2 && k < S.strip[sid * 4 + 1]; k++)
				tri += S.vtx[(S.strip[sid * 4] + k) * 6 + 2] && S.vtx[(S.strip[sid * 4] + k - 1) * 6 + 2] &&
				       S.vtx[(S.strip[sid * 4] + k - 2) * 6 + 2];
		const u32 room = last_alloc - 3 * tri - (S.flat == 2 ? 2 : 0);
		if (allocs != 2 || S.tri_sort != 3 || S.first_pass != 2 || 6 * tri <= room || 3 * tri > room)
			die("a second vertex block of %u vertices for %u triangles (%u blocks)", last_alloc, tri, allocs);
	}
}

int main(int argc, char** argv)
{
	const u32 scenes = argc > 1 ? (u32)atoi(argv[1]) : 4000;
	if (argc > 2)
		rs ^= (uint64_t)atoi(argv[2]) * 0x9E3779B97F4A7C15ull;
	uint64_t calls = 0;
	for (g_scene = 0; g_scene < scenes; g_scene++)
	{
		gen_scene(false);
		const R4Call *cn, *cr;
		const u32 *vn, *vr;
		u32 un, ur;
		const u32 nn = r4_new_run(&S, &cn, &vn, &un);
		const u32 nr = r4_ref_run(&S, &cr, &vr, &ur);
		if (nn != nr)
			die("rows off: %u GL calls, the old code makes %u", nn, nr);
		for (u32 k = 0; k < nn; k++)
			if (memcmp(&cn[k], &cr[k], sizeof(R4Call)))
				die("rows off: call %u differs (kind %u/%u op %u/%u a %X/%X first %u/%u count %u/%u hash %X/%X)", k,
				    cn[k].kind, cr[k].kind, cn[k].op, cr[k].op, cn[k].a[0], cr[k].a[0], cn[k].first, cr[k].first,
				    cn[k].count, cr[k].count, cn[k].hash, cr[k].hash);
		if (un != ur || (un && memcmp(vn, vr, (size_t)un * 36)))
			die("rows off: the vertex block differs");
		calls += nn;
	}
	printf("phase 1: %u scenes with the rows off make the old code's GL calls (%llu calls)\n", scenes, (unsigned long long)calls);
	for (g_scene = 0; g_scene < scenes; g_scene++)
	{
		gen_scene(true);
		const R4Call* cn;
		const u32* vn;
		u32 un;
		const u32 nn = r4_new_run(&S, &cn, &vn, &un);
		model(cn, nn, vn);
		// The code of before bundle 14 has none of bundle 17's rows either.
		if (S.first_pass || S.pt_blend == 2 || S.offs_row || S.clamp_row || S.tril_row)
			continue;
		const R4Call* co;
		const u32* vo;
		u32 uo;
		const u32 no = r4_old_run(&S, &co, &vo, &uo);
		if (nn != no)
			die("TR First Pass OLD: %u GL calls, the code of before bundle 14 makes %u", nn, no);
		for (u32 k = 0; k < nn; k++)
			if (memcmp(&cn[k], &co[k], sizeof(R4Call)))
				die("TR First Pass OLD: call %u differs (kind %u/%u op %u/%u a %X/%X first %u/%u count %u/%u hash %X/%X)", k,
				    cn[k].kind, co[k].kind, cn[k].op, co[k].op, cn[k].a[0], co[k].a[0], cn[k].first, co[k].first,
				    cn[k].count, co[k].count, cn[k].hash, co[k].hash);
		if (un != uo || (un && memcmp(vn, vo, (size_t)un * 36)))
			die("TR First Pass OLD: the vertex block differs");
		n_old++;
	}
	printf("PASS: %u scenes per phase; %llu draws; %llu depth-pass strips, %llu multi-pass scenes; clip: %llu inside, "
	       "%llu outside, %llu strips cut whole; %llu flat strips (%llu with the colour shift); %llu sprite cull flips; "
	       "%llu scenes with the TR volumes, %llu volume triangles; %llu sorted triangles (%llu with swapped corners, %llu in an opaque depth pass); "
	       "TR First Pass: %llu triangles left out, %llu in submission order, %llu scenes with a second block, "
	       "%llu scenes at OLD equal to the code of before\n",
	       scenes, (unsigned long long)n_draws,
	       (unsigned long long)n_depth, (unsigned long long)n_multi, (unsigned long long)n_clip_in,
	       (unsigned long long)n_clip_out, (unsigned long long)n_clip_all, (unsigned long long)n_flat,
	       (unsigned long long)n_shift, (unsigned long long)n_spr, (unsigned long long)n_trvol, (unsigned long long)n_tris,
	       (unsigned long long)n_sorted, (unsigned long long)n_sorted_swap, (unsigned long long)n_opaque,
	       (unsigned long long)n_skipped, (unsigned long long)n_game, (unsigned long long)n_blocks,
	       (unsigned long long)n_old);
	if (!n_skipped || !n_game || !n_blocks || !n_applied)
		die("a TR First Pass case never ran");
	printf("bundle 17: PT Blend + ALPHA 1: %llu draws with no blend, %llu with new factors; %llu draws with the offset "
	       "colour program (%llu vertices with a colour), %llu with the clamp, %llu with a trilinear weight\n",
	       (unsigned long long)n_pt_one, (unsigned long long)n_pt_fac, (unsigned long long)n_dc_offs,
	       (unsigned long long)n_offs_vtx, (unsigned long long)n_dc_clamp, (unsigned long long)n_dc_tril);
	if (!n_pt_one || !n_pt_fac || !n_dc_offs || !n_offs_vtx || !n_dc_clamp || !n_dc_tril)
		die("a bundle 17 case never ran");
	return 0;
}
