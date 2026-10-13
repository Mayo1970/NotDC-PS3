// Plan 3j (WSL): Ps3VertexDecoder's offset colours (ps3_rend.cpp, extracted by run.sh) through the real FIFO splitter,
// against a model of Flycast's rules: every textured vertex type, the face offset colour, sprites, split 64-byte records.
#include "plugs/drkPvr/ta.h"
#include "ps3/ps3_rend_regs.h"
#include "ps3/spu/spe_bell.h"
#include "ps3/spu/spe_sprite.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace TASplitter;

namespace TASplitter { TaListFP* TaCmd = 0; void TA_ListInit() {} void TA_ListCont() {} void TA_SoftReset() {} }
int msgboxf(const char*, unsigned int, ...) { return 0; }
void spg_SchedListEndIrq(u32) {}
u32 StripCount;
// Bundle 4: ta.h's TA Bad Records, and the decode options (all off here; misc/ta4_check runs them).
int g_ta_bad_rec_preset;
u32 g_ta_bad_records;

#include "defines.inc"
#include "structs.inc"

// What the region reads from the rest of ps3_rend.cpp, with its start values.
static TaContext s_ctx;
static TaContext* s_dec = &s_ctx;
static u32 s_cur_list, s_mv_list, s_stat_built[5], s_stat_wtotal, s_stat_wclamp, s_overflow;
static float s_face_a = 1, s_face_r = 1, s_face_g = 1, s_face_b = 1;
static u32 s_face8 = 0xFFFFFFFF, s_foffs8 = 0xFFFFFFFF;
static u32 s_dopt;
static u32 s_clip_rect;
static u32 s_sprite_col = 0xFFFFFFFF;
static float s_foffs_a = 1, s_foffs_r = 1, s_foffs_g = 1, s_foffs_b = 1;
static u32 s_sprite_offs;
static bool s_bump_param;
static u32 s_stat_spr_plane;
static bool s_bump_on;
// Plan 3a: off here, so only the bump gate keeps offset colours.
static u32 s_fog;

static void reset_lists(TaContext* c)
{
	c->nvtx = c->nstrip = c->nparam = c->strip_first = c->nmtri = c->nmvol = c->npass = 0;
	c->min_w = 1e30f;
	c->max_w = 0.0f;
	c->max_inv_w = 1.0f;
	memset(c->mv_tris, 0, sizeof(c->mv_tris));
}

#include "region.inc"

FifoSplitter<Ps3VertexDecoder, false> TileAccel;

// ---- the model --------------------------------------------------------------

static u32 fbits(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static float bitsf(u32 u) { float f; memcpy(&f, &u, 4); return f; }

static u32 m_byte(float f)
{
	if (f != f)
		return 255;
	const s32 v = (s32)(f * 255.0f);
	return v < 0 ? 0 : v > 255 ? 255 : (u32)v;
}
static u32 m_argb(float a, float r, float g, float b) { return m_byte(a) << 24 | m_byte(r) << 16 | m_byte(g) << 8 | m_byte(b); }

// Flycast: the face offset colour (PolyParam 2B) times the vertex intensity; its alpha is not scaled.
static float m_foffs[4];
static u32 m_int(u32 ibits)
{
	const float i = bitsf(ibits);
	return m_argb(m_foffs[0], m_foffs[1] * i, m_foffs[2] * i, m_foffs[3] * i);
}

// ---- streams -----------------------------------------------------------------

static u32 rs;
static u32 rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rndn(u32 n) { return rnd() % n; }

// Colour and intensity floats: mostly [0, 1.25], some negative, NaN, huge, or any bits.
static u32 rnd_colf()
{
	const u32 r = rndn(100);
	if (r < 80) return fbits((float)rndn(1001) / 800.0f);
	if (r < 85) return fbits(-(float)rndn(1000) / 100.0f);
	if (r < 90) return 0x7FC00000u | rndn(0x400000);
	if (r < 95) return fbits(1e30f);
	return rnd();
}
static u32 rnd_pos() { return fbits((float)rndn(64000) / 100.0f - 40.0f); }
static u32 rnd_inv_w() { return fbits((float)(1 + rndn(100000)) / 1000.0f); }

static std::vector<u32> g_words;
struct Expect { bool check; u32 offs; };
static std::vector<Expect> g_exp;   // one per vertex the decoder appends

static u32 mk(u32 para, u32 list, u32 objc, u32 eos) { return para << 29 | eos << 28 | list << 24 | (objc & 0xFF); }

static void push(const u32* w, u32 n) { g_words.insert(g_words.end(), w, w + n); }

static u32 rnd_tcw() { return (rnd() & ~(7u << 27)) | (rndn(2) ? 4u : rndn(8)) << 27; }

// A polygon header and its strip; pdid from the splitter's own table, so both agree on every record's size.
static void gen_poly(u32 list)
{
	u32 objc;
	do
		objc = rndn(256);
	while ((objc & 0x40) && ((objc >> 4) & 3) == 1);
	const u32 uid = TileAccel.ta_type_lut[objc];
	const u32 pdid = uid & 0xFF, ppid = (uid >> 8) & 0x7F;
	const u32 tcw = rnd_tcw();
	const bool bump = s_bump_on && (objc & 8) && ((tcw >> 27) & 7) == 4;
	u32 h[16] = { mk(4, list, objc, 0), rnd(), rnd(), tcw, rnd(), rnd(), rnd(), rnd() };
	if (ppid == 1)
		for (int k = 4; k < 8; k++)
			h[k] = rnd_colf();
	if (ppid == 2 || ppid == 4)
	{
		for (int k = 8; k < 16; k++)
			h[k] = rnd_colf();
		if (ppid == 2)
			for (int k = 0; k < 4; k++)
				m_foffs[k] = bitsf(h[12 + k]);
	}
	push(h, (uid >> 30) == SZ64 ? 16 : 8);

	const u32 n = 1 + rndn(7);
	const bool big = pdid == 5 || pdid == 6 || pdid == 11 || pdid == 12 || pdid == 13 || pdid == 14;
	for (u32 i = 0; i < n; i++)
	{
		u32 w[16] = { mk(7, list, objc, i + 1 == n), rnd_pos(), rnd_pos(), rnd_inv_w(), rnd(), rnd(), rnd(), rnd() };
		for (int k = 8; k < 16; k++)
			w[k] = rnd();
		if (pdid == 7 || pdid == 8 || pdid == 13 || pdid == 14 || pdid == 2 || pdid == 10)
			w[6] = rnd_colf(), w[7] = rnd_colf();
		if (pdid == 1)
			for (int k = 4; k < 8; k++)
				w[k] = rnd_colf();
		if (pdid == 5 || pdid == 6)
			for (int k = 8; k < 16; k++)
				w[k] = rnd_colf();
		Expect e = { bump, 0 };
		switch (pdid)
		{
		case 3: case 4: case 11: case 12: e.offs = w[7]; break;
		case 7: case 8: case 13: case 14: e.offs = m_int(w[7]); break;
		case 5: case 6: e.offs = m_argb(bitsf(w[12]), bitsf(w[13]), bitsf(w[14]), bitsf(w[15])); break;
		default: e.check = false; break;  // untextured: never a bump polygon
		}
		push(w, big ? 16 : 8);
		g_exp.push_back(e);
	}
}

// A sprite header and its quads: all four corners take the header's offset colour.
static void gen_sprite(u32 list)
{
	const u32 objc = rndn(256);
	const u32 tcw = rnd_tcw();
	const bool bump = s_bump_on && (objc & 8) && ((tcw >> 27) & 7) == 4;
	const u32 h[8] = { mk(5, list, objc, 0), rnd(), rnd(), tcw, rnd(), rnd(), rnd(), rnd() };
	push(h, 8);
	const u32 n = 1 + rndn(3);
	for (u32 i = 0; i < n; i++)
	{
		u32 w[16] = { mk(7, list, objc, i + 1 == n) };
		for (int k = 1; k < 16; k++)
			w[k] = (k == 3 || k == 6 || k == 9) ? rnd_inv_w() : (k < 12 ? rnd_pos() : rnd());
		push(w, 16);
		for (int c = 0; c < 4; c++)
			g_exp.push_back({ bump, h[5] });
	}
}

static void gen_stream()
{
	g_words.clear();
	g_exp.clear();
	static const u32 kinds[3] = { 0, 2, 4 };
	u32 first = rndn(3);
	const u32 lists = 1 + rndn(3);
	for (u32 l = 0; l < lists; l++)
	{
		const u32 list = kinds[(first + l) % 3];
		const u32 groups = 1 + rndn(6);
		for (u32 g = 0; g < groups; g++)
		{
			if (rndn(6) == 0)
				gen_sprite(list);
			else
				gen_poly(list);
		}
		const u32 eol[8] = { mk(0, list, 0, 0) };
		push(eol, 8);
	}
}

// The replay as decode_range runs it, in blocks of 1-4 records, so a 64-byte record may split across calls.
static void decode()
{
	TileAccel.ListInit();
	const u32 total = (u32)g_words.size() / 8;
	for (u32 r = 0; r < total;)
	{
		u32 n = 1 + rndn(4);
		if (r + n > total)
			n = total - r;
		Ta_Dma* p = (Ta_Dma*)&g_words[r * 8];
		Ta_Dma* end = p + n - 1;
		do
			p = TileAccel.TaCmd(p, end);
		while (p <= end);
		r += n;
	}
}

int main(int argc, char** argv)
{
	const u32 streams = argc > 1 ? (u32)atoi(argv[1]) : 20000;
	u64 checked = 0, kept = 0;
	for (u32 seed = 1; seed <= streams; seed++)
	{
		s_bump_on = seed & 1;
		rs = seed * 2654435761u | 1;
		s_foffs_a = s_foffs_r = s_foffs_g = s_foffs_b = 1.0f;
		m_foffs[0] = m_foffs[1] = m_foffs[2] = m_foffs[3] = 1.0f;
		gen_stream();
		for (u32 i = 0; i < MAX_VTX; i++)
			s_ctx.offs[i] = 0xDEADBEEFu;
		decode();
		if (s_ctx.nvtx != g_exp.size())
		{
			printf("FAIL: seed %u: %u vertices decoded, the stream has %zu\n", seed, s_ctx.nvtx, g_exp.size());
			return 1;
		}
		for (u32 i = 0; i < s_ctx.nvtx; i++)
		{
			const u32 want = g_exp[i].check ? g_exp[i].offs : 0xDEADBEEFu;
			if (s_ctx.offs[i] != want)
			{
				printf("FAIL: seed %u (bump %s): vertex %u offs %08X, want %08X%s\n", seed, s_bump_on ? "on" : "off", i,
				       s_ctx.offs[i], want, g_exp[i].check ? "" : " (no write)");
				return 1;
			}
			checked += g_exp[i].check;
			kept += !g_exp[i].check;
		}
	}
	printf("PASS: %u streams, %llu bump vertices checked, %llu other vertices left alone\n", streams,
	       (unsigned long long)checked, (unsigned long long)kept);
	return 0;
}
