// 5.12 #2 (WSL): spe_vb_build.h on SPU shims against the PPU merge of ps3_rend.cpp (draw_list, draw_tr_sorted,
// draw_strip, as run.sh extracts them), bit for bit: runs, vertex words, counts, and what every echo publishes.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include "ps3/ps3_rend_regs.h"
#include "spe_vb.h"

extern "C" {
extern uint8_t* g_arena;
extern double shim_re_err;
extern void (*g_land_hook)(uint64_t ea, uint32_t size);
extern uint64_t g_seed;
void spu_arena(uint32_t size);
uint64_t spu_ea(const void* p);
void spu_setup(const uint32_t* out_ea, uint32_t runs_ea, uint64_t echo_ea);
int spu_build(const SpeVbBell* req);
uint32_t t_fmul(uint32_t a, uint32_t b);
void t_mulxy(uint32_t x, uint32_t y, uint32_t w, uint32_t* ox, uint32_t* oy);
void t_recip2(uint32_t a, uint32_t b, uint32_t* qa, uint32_t* qb);
uint32_t t_add(uint32_t a, uint32_t b);
uint32_t t_div(uint32_t a, uint32_t n);
}

// ---- what the extracted PPU code needs -----------------------------------

#pragma pack(push, 1)
typedef struct
{
	float x, y, z, w;
	float u0, v0;
	float u1, v1;
	uint32_t color;
} ps3gl_vertex_t;
#pragma pack(pop)
typedef unsigned GLenum;
#define GL_TRIANGLE_STRIP 5
static const u32 ListType_Opaque = 0, ListType_Translucent = 2, ListType_Punch_Through = 4;
#define MAX_STRIPS SPE_MAX_STRIPS
#include "structs.inc"

static DrawLists s_rl;
static u32 s_stat_strips[5];
enum { DT_PARAM, DT_TEX, DT_GLEND, DT_SORT, DT_MODVOL, DT_COUNT };
static u64 s_dt[DT_COUNT];
static inline u64 PERF_TICKS() { return 0; }

static void die(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

// Plan 3j: the bump passes set it; the SPU compare runs with it off, as the PPU never builds bump scenes there.
static bool s_bump_on;
// The 3j glBegin pass (Strip Merge off) records each vertex and its offset colour; any other pass has a buffer.
struct ImmVtx { const DcVertex* v; u32 offs; };
static std::vector<ImmVtx>* g_imm;
static void emit_vertex(const DcVertex* v, u32, u32 offs)
{
	if (!g_imm)
		die("emit_vertex with a vertex buffer");
	g_imm->push_back({ v, offs });
}
static void glBegin(GLenum) { if (!g_imm) die("glBegin with a vertex buffer"); }
static void glEnd() { if (!g_imm) die("glEnd with a vertex buffer"); }

// The GL pass: apply_param opens a run (its param), the draw at run_close gives its range.
static std::vector<SpeRun> g_trace;
static u32 apply_param(const DcParam& p, u32)
{
	SpeRun r = { 0, 0, (u32)(&p - s_rl.param), 0 };
	g_trace.push_back(r);
	return (!p.tsp.UseAlpha || (p.pcw.Texture && p.tsp.ShadInstr == 1)) ? 0xFF000000u : 0;
}
// Bundle 4: draw_vb's clip and colour shift are off here (misc/rend4_check runs them); the draw is one range.
enum { CLIP_NONE, CLIP_INSIDE, CLIP_OUTSIDE, CLIP_ALL };
struct ClipView { int x, y, w, h; bool scissor; float cx0, cy0, cw, ch; };
static ClipView s_cv;
static u32 s_clip_mode, s_flat_shift;
static int s_clip_r[4];
static void glScissor(int, int, int, int) { die("glScissor with no clip"); }
static void ps3gl_draw_vb_shift(GLenum, u32, u32 stride, int first, int count, u32 shift)
{
	if (g_trace.empty())
		die("draw before apply_param");
	if (stride != sizeof(ps3gl_vertex_t) || shift)
		die("draw stride %u shift %u", stride, shift);
	g_trace.back().first = (u32)first;
	g_trace.back().count = (u32)count;
}

#include "region.inc"

// ---- helpers -------------------------------------------------------------

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd32()
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (uint32_t)(rs >> 32);
}
static float rndf(float lo, float hi) { return lo + (hi - lo) * (float)(rnd32() / 4294967296.0); }
static u32 fb(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static float bf(u32 u) { float f; memcpy(&f, &u, 4); return f; }
static bool isnanb(u32 b) { return (b & 0x7FFFFFFFu) > 0x7F800000u; }

// fmuls as the PPU gives it, NaN cases included.
static u32 ppc_mul(u32 a, u32 b)
{
	if (isnanb(a))
		return a | 0x400000u;
	if (isnanb(b))
		return b | 0x400000u;
	const u32 r = fb(bf(a) * bf(b));
	return isnanb(r) ? 0x7FC00000u : r;
}

// ---- scenes --------------------------------------------------------------

#define ARENA (24u << 20)
static SpeOut* g_out[SPE_CTX];
static SpeRun* g_runs;
static SpeVbEcho* g_echo;
static u32* g_vb;
static u32 g_vb_max;

struct Scene
{
	u32 ctx, nvtx, nstrip, nparam, cap;
	bool autosort, wild;
	// Bundle 4: Sprite Cull (P25), the clip rect bits in the merge key (P10); 3k: the TR list is the PPU's.
	bool spr, clip, notr;
};

static u32 gen_xy()
{
	const u32 r = rnd32() % 1000;
	if (r < 880) return fb(rndf(-60.f, 700.f));
	if (r < 900) return 0;
	if (r < 910) return 0x80000000u;
	if (r < 920) return rnd32() & 0x807FFFFFu;
	if (r < 930) return 0x69045951u | (rnd32() & 0x80000000u);
	if (r < 935) return 0x69045952u | (rnd32() & 0x80000000u);
	if (r < 945) return 0x7F800000u | (rnd32() & 0x80000000u);
	if (r < 955) return 0x7FC00000u | (rnd32() & 0x803FFFFFu);
	if (r < 970) return fb(rndf(-1e-30f, 1e-30f));
	if (r < 990) return fb(rndf(-1e6f, 1e6f));
	return rnd32();
}

// W as the decoder writes it ({0} or [2^-126, 1e9]); wild scenes also take any bits.
static u32 gen_w(bool wild)
{
	if (wild && rnd32() % 4 == 0)
		return rnd32();
	const u32 r = rnd32() % 1000;
	if (r < 40) return 0;
	if (r < 70) return 0x4E6E6B28u;
	if (r < 85) return 0x00800000u + rnd32() % 0x00A02205u;
	if (r < 95) return 0x01202205u + rnd32() % 64 - 32;
	if (r < 110) return 0x00800000u + rnd32() % 0x0D000000u;
	if (r < 125) return fb(rndf(1e8f, 1e9f));
	return fb(expf(rndf(-6.f, 11.f)));
}

static void gen_params(SpeParam* p, u32 n)
{
	SpeParam base[6];
	const u32 pool = 1 + rnd32() % 6;
	for (u32 k = 0; k < pool; k++)
	{
		base[k].pcw = rnd32() & 0x000300FFu;
		// A polygon or a sprite header (ParaType 4 or 5); the clip rect's two high bits.
		base[k].pcw |= ((rnd32() % 3) ? 4u : 5u) << 29;
		if (rnd32() % 4 == 0)
			base[k].pcw |= (rnd32() & 3) << 8;
		base[k].isp = rnd32();
		base[k].tsp = rnd32();
		base[k].tcw = rnd32() % 4;
		// 3j: PixelFmt 4, a bump texture when the Texture bit is set too.
		if (rnd32() % 3 == 0)
			base[k].tcw |= 4u << 27;
	}
	for (u32 i = 0; i < n; i++)
	{
		SpeParam q = base[rnd32() % pool];
		const u32 r = rnd32() % 100;
		if (r < 30)
		{
			// Bits the merge ignores: Col_Type, UV_16bit, the cull direction, ISP low bits, the list type.
			q.pcw ^= rnd32() & 0xFFFCFF31u;
			q.isp ^= rnd32() & 0x0BFFFFFFu;
		}
		else if (r < 36)
			q.pcw ^= 1u << (rnd32() % 32);
		else if (r < 40)
			q.isp ^= 0x18000000u;  // cull mode
		else if (r < 43)
			q.tsp ^= 1u << (rnd32() % 32);
		else if (r < 46)
			q.tcw ^= 1u << (rnd32() % 32);
		p[i] = q;
	}
}

static u32 tr_ppu_expected(const Scene& s)
{
	if (s.notr)
		return 1;
	if (!s.autosort)
		return 0;
	const SpeOut* o = g_out[s.ctx];
	u32 ntr = 0;
	for (u32 i = 0; i < s.nstrip; i++)
	{
		const SpeStrip& st = o->strip[i];
		if (st.list != 2)
			continue;
		ntr++;
		for (u32 k = 0; k < st.count; k++)
		{
			const u32 w = o->vtx[st.first + k].w;
			if (w && (w < 0x00800000u || w > 0x4E6E6B28u))
				return 1;
		}
	}
	return ntr > SPEVB_TR_MAX;
}

static void gen_scene(Scene& s, u32 kind)
{
	s.ctx = rnd32() % SPE_CTX;
	s.autosort = rnd32() & 1;
	s.wild = kind == 5;
	s.spr = rnd32() & 1;
	s.clip = rnd32() % 3 == 0;
	s.notr = rnd32() % 4 == 0;
	SpeOut* o = g_out[s.ctx];
	u32 max_strip = 40, max_count = 12;
	switch (kind)
	{
	case 0: max_strip = 1 + rnd32() % 50; break;
	case 1: max_strip = 100 + rnd32() % 1900; break;
	case 2: max_strip = 4000 + rnd32() % 12385; break;
	case 3: max_strip = 4500 + rnd32() % 2000; max_count = 3; s.autosort = true; break;
	case 4: max_strip = 5 + rnd32() % 40; break;
	case 5: max_strip = 50 + rnd32() % 500; break;
	case 7: max_strip = 9000; max_count = 6; break;  // one state: runs grow to RUN_MAX_VTX
	default: max_strip = 50 + rnd32() % 500; break;
	}
	s.nparam = 1 + rnd32() % (kind == 2 ? SPE_MAX_PARAMS : 400);
	gen_params(o->param, s.nparam);
	if (kind == 7)
		for (u32 i = 1; i < s.nparam; i++)
			o->param[i] = o->param[0];
	u32 v = 0, prm = 0, n = 0;
	for (; n < max_strip && n < SPE_MAX_STRIPS; n++)
	{
		u32 count = 3 + rnd32() % max_count;
		const u32 r = rnd32() % 100;
		if (kind == 4 || r < 2)
			count = (rnd32() & 1) ? 60 + rnd32() % 10 : 100 + rnd32() % 1500;
		else if (r < 8 && kind != 3)
			count = 13 + rnd32() % 70;
		const u32 gap = (rnd32() % 20 == 0) ? rnd32() % 5 : 0;
		if (v + gap + count > SPE_MAX_VTX)
			break;
		v += gap;
		u32 list = rnd32() % 100;
		list = list < 55 ? 0 : list < 80 ? 2 : list < 96 ? 4 : 6;
		if (kind == 3)
			list = (rnd32() % 10) ? 2 : 0;
		if (rnd32() % 3 == 0 && prm + 1 < s.nparam)
			prm += (rnd32() % 8 == 0) ? 1 + rnd32() % 5 : 1;
		if (prm >= s.nparam)
			prm = s.nparam - 1;
		SpeStrip& st = o->strip[n];
		st.first = v;
		st.count = (uint16_t)count;
		st.list = (uint8_t)list;
		st.pad = 0;
		st.param = (uint16_t)prm;
		st.pad2 = 0;
		v += count;
	}
	s.nstrip = n;
	s.nvtx = v;
	for (u32 i = 0; i < v; i++)
	{
		SpeVtx& x = o->vtx[i];
		x.x = gen_xy();
		x.y = gen_xy();
		x.w = gen_w(s.wild);
		x.col = rnd32();
		x.u = (rnd32() % 10) ? fb(rndf(0.f, 1.f)) : rnd32();
		x.v = (rnd32() % 10) ? fb(rndf(0.f, 1.f)) : rnd32();
	}
	s.cap = 3 * s.nvtx + 4 * s.nstrip + 16;
	if (kind == 6 || rnd32() % 20 == 0)
		s.cap = rnd32() % (s.cap / 2 + 1);
	if (s.cap > g_vb_max)
		s.cap = g_vb_max;
}

// ---- the PPU reference ---------------------------------------------------

struct RefOut
{
	std::vector<SpeRun> runs;
	u32 sec_end[3], strips[3], vtx_end[3];
	std::vector<u32> vb;
};

// 3j: the bump passes' offset colours; bump = PPU-replayed lists with Bump Maps on, else the SPU's lists.
static std::vector<u32> g_offs(SPE_MAX_VTX);

static void ref_run(const Scene& s, bool dry, RefOut& r, bool bump = false, bool offs = false)
{
	const SpeOut* o = g_out[s.ctx];
	s_rl.vtx = (const DcVertex*)o->vtx;
	s_rl.strip = (const DcStrip*)o->strip;
	s_rl.param = (const DcParam*)o->param;
	s_rl.offs = bump || offs ? g_offs.data() : 0;
	s_bump_on = bump;
	s_rl.nvtx = s.nvtx;
	s_rl.nstrip = s.nstrip;
	s_rl.nparam = s.nparam;
	s_merge = true;
	s_mo_sprite = s.spr;
	s_key_pcw = s.clip ? 0x303CE : 0x300CE;
	s_key_isp = s.clip ? 0xF40FFFFF : 0xF4000000;
	s_strip_lo = 0;
	s_strip_hi = s.nstrip;
	r.vb.assign((size_t)s.cap * 9 + 9, 0xEEEEEEEEu);
	s_vb = (ps3gl_vertex_t*)r.vb.data();
	s_vb_used = 0;
	s_vb_cap = s.cap;
	s_vb_off = 0;
	static std::vector<SpeRun> dry_buf(SPEVB_MAX_RUNS);
	s_dry_runs = dry_buf.data();
	s_dry_nruns = 0;
	g_trace.clear();
	memset(&s_run, 0, sizeof(s_run));
	static const u32 lists[3] = { ListType_Opaque, ListType_Punch_Through, ListType_Translucent };
	for (u32 k = 0; k < 3; k++)
	{
		s_dry_strips = 0;
		memset(s_stat_strips, 0, sizeof(s_stat_strips));
		if (dry)
		{
			if (k == 2 && s.autosort)
				draw_tr_sorted<true>();
			else
				draw_list<true>(lists[k]);
			r.sec_end[k] = s_dry_nruns;
			r.strips[k] = s_dry_strips;
		}
		else
		{
			if (k == 2 && s.autosort)
				draw_tr_sorted();
			else
				draw_list(lists[k]);
			r.sec_end[k] = (u32)g_trace.size();
			r.strips[k] = s_stat_strips[lists[k]];
		}
		r.vtx_end[k] = s_vb_used;
	}
	if (dry)
		r.runs.assign(dry_buf.begin(), dry_buf.begin() + s_dry_nruns);
	else
		r.runs = g_trace;
	r.vb.resize((size_t)s_vb_used * 9);
}

static void same_ref(const RefOut& a, const RefOut& b)
{
	for (int k = 0; k < 3; k++)
		if (a.sec_end[k] != b.sec_end[k] || a.strips[k] != b.strips[k] || a.vtx_end[k] != b.vtx_end[k])
			die("DRY and GL passes differ at section %d", k);
	if (a.runs.size() != b.runs.size() || memcmp(a.runs.data(), b.runs.data(), a.runs.size() * sizeof(SpeRun)) ||
	    a.vb != b.vb)
		die("DRY and GL passes differ in runs or vertices");
}

// ---- the SPU build and its publications ----------------------------------

static const RefOut* g_ref;
static u32 g_seq, g_pub_runs, g_pub_vtx, g_pubs, g_trppu_exp;
static uint64_t g_echo_ea;

// The SPU's slot: the 9 words of ps3gl_vertex_t, then 3 zero words.
#define SLOT_WORDS (SPEVB_VTX_STRIDE / 4)

static void check_vtx(u32 from, u32 to, const char* when)
{
	for (u32 i = from; i < to; i++)
	{
		const u32* a = g_vb + (size_t)i * SLOT_WORDS;
		const u32* b = g_ref->vb.data() + (size_t)i * 9;
		if (memcmp(a, b, 36) || a[9] || a[10] || a[11])
			die("%s: vertex %u spu %08X %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X ref %08X %08X %08X %08X "
			    "%08X %08X %08X %08X %08X", when, i, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10],
			    a[11], b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
	}
}

static void check_runs(u32 from, u32 to, const char* when)
{
	for (u32 i = from; i < to; i++)
		if (memcmp(&g_runs[i], &g_ref->runs[i], sizeof(SpeRun)))
			die("%s: run %u spu %u+%u p%u ref %u+%u p%u", when, i, g_runs[i].first, g_runs[i].count, g_runs[i].param,
			    g_ref->runs[i].first, g_ref->runs[i].count, g_ref->runs[i].param);
}

// Each echo that lands: every run and vertex it counts is already final in memory, and counts never go back.
static void land_hook(uint64_t ea, uint32_t)
{
	if (ea != g_echo_ea)
		return;
	const SpeVbEcho* e = g_echo;
	if (e->seq != g_seq)
		die("echo seq %u, want %u", e->seq, g_seq);
	g_pubs++;
	const u32 nsec_max = g_trppu_exp ? 2 : 3;
	if (e->runs < g_pub_runs || e->vb_used < g_pub_vtx)
		die("echo counts went back: runs %u < %u or vb_used %u < %u", e->runs, g_pub_runs, e->vb_used, g_pub_vtx);
	if (e->runs > g_ref->sec_end[nsec_max - 1] || e->vb_used > g_ref->vtx_end[nsec_max - 1])
		die("echo past the end: runs %u vb_used %u", e->runs, e->vb_used);
	check_runs(g_pub_runs, e->runs, "published");
	check_vtx(g_pub_vtx, e->vb_used, "published");
	for (u32 k = 0; k < 3; k++)
	{
		const u32 f = k == 0 ? SPEVB_OP_DONE : k == 1 ? SPEVB_PT_DONE : SPEVB_TR_DONE;
		if (!(e->flags & f))
			continue;
		if (k == 2 && g_trppu_exp)
			die("TR done where the PPU had to sort");
		if (e->sec_end[k] != g_ref->sec_end[k] || e->strips[k] != g_ref->strips[k] || e->runs < e->sec_end[k])
			die("section %u: sec_end %u strips %u runs %u, ref %u %u", k, e->sec_end[k], e->strips[k], e->runs,
			    g_ref->sec_end[k], g_ref->strips[k]);
		if (e->runs == e->sec_end[k] && e->vb_used != g_ref->vtx_end[k])
			die("section %u done: vb_used %u, ref %u", k, e->vb_used, g_ref->vtx_end[k]);
	}
	if (e->flags & SPEVB_TR_PPU)
	{
		if (!g_trppu_exp)
			die("TR left to the PPU without cause");
		if (!(e->flags & SPEVB_PT_DONE) || e->vb_used != g_ref->vtx_end[1] || e->runs != g_ref->sec_end[1])
			die("TR_PPU before PT is out");
	}
	g_pub_runs = e->runs;
	g_pub_vtx = e->vb_used;
}

static void spu_check(const Scene& s, const RefOut& ref)
{
	g_ref = &ref;
	g_trppu_exp = tr_ppu_expected(s);
	g_pub_runs = g_pub_vtx = g_pubs = 0;
	memset(g_vb, 0xCD, (size_t)(s.cap + 4) * SPEVB_VTX_STRIDE);
	memset(g_runs, 0xCD, (size_t)SPEVB_MAX_RUNS * sizeof(SpeRun));
	SpeVbBell q;
	memset(&q, 0, sizeof(q));
	q.seq = ++g_seq;
	q.ctx = s.ctx;
	q.nvtx = s.nvtx;
	q.nstrip = s.nstrip;
	q.nparam = s.nparam;
	q.vb_ea = (u32)spu_ea(g_vb);
	q.cap = s.cap;
	q.flags = (s.autosort ? SPEVB_AUTOSORT : 0) | (s.spr ? SPEVB_SPRITE_CULL : 0) | (s.clip ? SPEVB_CLIP_KEY : 0) |
	          (s.notr ? SPEVB_NO_TR : 0);
	if (spu_build(&q))
		die("DMA left in the queue");
	const SpeVbEcho* e = g_echo;
	const u32 nsec = g_trppu_exp ? 2 : 3;
	const u32 want = SPEVB_OP_DONE | SPEVB_PT_DONE | (g_trppu_exp ? SPEVB_TR_PPU : SPEVB_TR_DONE);
	if (e->seq != g_seq || e->flags != want)
		die("final echo seq %u flags %X, want %u %X", e->seq, e->flags, g_seq, want);
	if (e->runs != ref.sec_end[nsec - 1] || e->vb_used != ref.vtx_end[nsec - 1])
		die("final runs %u vb_used %u, ref %u %u", e->runs, e->vb_used, ref.sec_end[nsec - 1], ref.vtx_end[nsec - 1]);
	check_runs(0, e->runs, "final");
	check_vtx(0, e->vb_used, "final");
	// Nothing past the last vertex: the RSX block ends at the cap.
	const u8* tail = (const u8*)(g_vb + (size_t)e->vb_used * SLOT_WORDS);
	for (u32 k = 0; k < SPEVB_VTX_STRIDE * 4; k++)
		if (tail[k] != 0xCD)
			die("write past the last vertex (byte %u)", k);
	if (g_pubs < 3)
		die("only %u echoes", g_pubs);
}

// ---- 3j: bump runs carry each vertex's offset colour in u1 ---------------

// The offset colour of a source vertex, from its u and v: an emitted copy names its source by the same two words.
static u32 offs_of(u32 u, u32 v) { return u ^ (v << 7 | v >> 25) ^ 0x5A5A5A5Au; }
static u32 rgba(u32 argb) { return argb << 8 | argb >> 24; }
static bool bump_param(const SpeParam& p) { return (p.pcw & 8) && ((p.tcw >> 27) & 7) == 4; }

static u64 g_bump_vtx, g_bump_imm;

// With Bump Maps on, the runs and every word but u1 stay as in the plain pass; u1 is the source's offset colour as
// RGBA in a bump run, else 0. Then the glBegin path (Strip Merge off): each vertex gets its own strip's offs.
static void bump_check(const Scene& s, const RefOut& plain)
{
	const SpeOut* o = g_out[s.ctx];
	for (u32 i = 0; i < s.nvtx; i++)
		g_offs[i] = offs_of(o->vtx[i].u, o->vtx[i].v);
	// Bump Maps off over the PPU replay's lists (they have offs): the plain pass, word for word.
	RefOut off;
	ref_run(s, false, off, false, true);
	if (off.runs.size() != plain.runs.size() ||
	    memcmp(off.runs.data(), plain.runs.data(), off.runs.size() * sizeof(SpeRun)) || off.vb != plain.vb)
		die("bump off with offs: the output differs from the plain pass");
	RefOut b;
	ref_run(s, false, b, true);
	for (int k = 0; k < 3; k++)
		if (b.sec_end[k] != plain.sec_end[k] || b.strips[k] != plain.strips[k] || b.vtx_end[k] != plain.vtx_end[k])
			die("bump pass: section %d differs", k);
	if (b.runs.size() != plain.runs.size() ||
	    memcmp(b.runs.data(), plain.runs.data(), b.runs.size() * sizeof(SpeRun)) || b.vb.size() != plain.vb.size())
		die("bump pass: the runs differ");
	std::vector<u8> in_bump(b.vb.size() / 9, 0);
	for (const SpeRun& r : b.runs)
		if (bump_param(o->param[r.param]))
			for (u32 i = r.first; i < r.first + r.count; i++)
				in_bump[i] = 1;
	for (u32 i = 0; i < in_bump.size(); i++)
	{
		const u32* a = &b.vb[(size_t)i * 9];
		const u32* q = &plain.vb[(size_t)i * 9];
		for (int k = 0; k < 9; k++)
			if (k != 6 && a[k] != q[k])
				die("bump pass: vertex %u word %d %08X, plain %08X", i, k, a[k], q[k]);
		const u32 want = in_bump[i] ? rgba(offs_of(a[4], a[5])) : 0;
		if (a[6] != want)
			die("bump pass: vertex %u u1 %08X, want %08X (%s run)", i, a[6], want, in_bump[i] ? "bump" : "plain");
		g_bump_vtx += in_bump[i];
	}

	std::vector<ImmVtx> imm;
	g_imm = &imm;
	s_vb = 0;
	s_merge = false;
	g_trace.clear();
	static const u32 lists[3] = { ListType_Opaque, ListType_Punch_Through, ListType_Translucent };
	for (u32 k = 0; k < 3; k++)
		draw_list(lists[k]);
	g_imm = 0;
	size_t n = 0;
	for (u32 k = 0; k < 3; k++)
		for (u32 i = 0; i < s.nstrip; i++)
		{
			const SpeStrip& st = o->strip[i];
			if (st.list != lists[k])
				continue;
			const bool bump = bump_param(o->param[st.param]);
			for (u32 j = 0; j < st.count; j++, n++)
			{
				if (n >= imm.size() || imm[n].v != (const DcVertex*)&o->vtx[st.first + j])
					die("glBegin pass: vertex %zu is not strip %u's vertex %u", n, i, j);
				if (imm[n].offs != (bump ? g_offs[st.first + j] : 0))
					die("glBegin pass: strip %u vertex %u offs %08X, want %08X", i, j, imm[n].offs,
					    bump ? g_offs[st.first + j] : 0);
				g_bump_imm += bump;
			}
		}
	if (n != imm.size())
		die("glBegin pass: %zu vertices, the strips hold %zu", imm.size(), n);
}

// ---- float steps ---------------------------------------------------------

static u64 g_float_checks;

// recip2 in both lanes: x in lane 0, the value before it in lane 2.
static u32 t_recip(u32 x)
{
	static u32 prev = 0x3F800000u;
	u32 qa, qb;
	t_recip2(x, prev, &qa, &qb);
	if (qb != fb(1.0f / bf(prev)))
		die("recip2 lane 2 (%08X) = %08X, fdivs %08X", prev, qb, fb(1.0f / bf(prev)));
	prev = x;
	return qa;
}

static void float_checks(u32 n)
{
	// fmul_rn: any bits, against fmuls.
	for (u32 i = 0; i < n; i++)
	{
		u32 a = rnd32(), b = rnd32();
		const u32 r = rnd32() % 8;
		if (r == 0) a &= 0x807FFFFFu;
		if (r == 1) b &= 0x807FFFFFu;
		if (r == 2) a = (a & 0x80FFFFFFu) | 0x3F000000u, b = (b & 0x80FFFFFFu) | 0x00000000u;
		if (r == 3) a = (a & 0x807FFFFFu) | 0x7F000000u, b = (b & 0x807FFFFFu) | 0x40000000u;
		if (t_fmul(a, b) != ppc_mul(a, b))
			die("fmul_rn(%08X, %08X) = %08X, fmuls %08X", a, b, t_fmul(a, b), ppc_mul(a, b));
		g_float_checks++;
	}
	// mul_xy: the fast path and its exits.
	for (u32 i = 0; i < n; i++)
	{
		const u32 x = gen_xy(), y = gen_xy(), w = (i & 3) ? gen_w(false) : rnd32();
		u32 ox, oy;
		t_mulxy(x, y, w, &ox, &oy);
		if (ox != ppc_mul(x, w) || oy != ppc_mul(y, w))
			die("mul_xy(%08X, %08X, %08X) = %08X %08X, fmuls %08X %08X", x, y, w, ox, oy, ppc_mul(x, w), ppc_mul(y, w));
		g_float_checks += 2;
	}
	// recip over [2^-126, 1e9]: two whole binades, the edges, and random values.
	static bool s_binades;
	if (!s_binades)
	{
		s_binades = true;
		for (u32 b = 0x3F800000u; b < 0x40000000u; b++)
			if (t_recip(b) != fb(1.0f / bf(b)))
				die("recip(%08X)", b);
		for (u32 b = 0x00800000u; b < 0x01000000u; b += 3)
			if (t_recip(b) != fb(1.0f / bf(b)))
				die("recip(%08X)", b);
		g_float_checks += 0x800000u + 0x800000u / 3;
	}
	for (u32 i = 0; i < n; i++)
	{
		const u32 b = 0x00800000u + rnd32() % (0x4E6E6B29u - 0x00800000u);
		if (t_recip(b) != fb(1.0f / bf(b)))
			die("recip(%08X) = %08X, fdivs %08X", b, t_recip(b), fb(1.0f / bf(b)));
		g_float_checks++;
	}
	// add_pos on positive normal terms, near the overflow too; FLT_MAX + 2^103 is the tie that rounds to Inf.
	static const u32 edge[][2] = { { 0x7F7FFFFFu, 0x73000000u }, { 0x7F7FFFFFu, 0x72FFFFFFu }, { 0x7F7FFFFEu, 0x73C00000u },
	                               { 0x7F7FFFFEu, 0x73BFFFFFu }, { 0x7F000000u, 0x7F000000u }, { 0x00800000u, 0x00800000u } };
	for (const auto& e : edge)
		if (t_add(e[0], e[1]) != fb(bf(e[0]) + bf(e[1])) || t_add(e[1], e[0]) != fb(bf(e[0]) + bf(e[1])))
			die("add_pos(%08X, %08X) = %08X, fadds %08X", e[0], e[1], t_add(e[0], e[1]), fb(bf(e[0]) + bf(e[1])));
	for (u32 i = 0; i < n; i++)
	{
		u32 a = 0x00800000u + rnd32() % 0x7F000000u, b = 0x00800000u + rnd32() % 0x7F000000u;
		const u32 r = rnd32() % 8;
		if (r == 0) a = 0x7F7FFFFFu - rnd32() % 64, b = 0x7F7FFFFFu - rnd32() % 0x01000000u;
		if (r == 1) a = 0x7F7FFFFFu - rnd32() % 4096, b = 0x73000000u + rnd32() % 0x01000000u;
		if (r == 2) a = 0x00800000u + rnd32() % 1000;
		if (r == 3) b = a + rnd32() % 64;
		if (t_add(a, b) != fb(bf(a) + bf(b)))
			die("add_pos(%08X, %08X) = %08X, fadds %08X", a, b, t_add(a, b), fb(bf(a) + bf(b)));
		g_float_checks++;
	}
	// Quotients next to a rounding midpoint, where recip and div_cnt need their one-ulp corrections.
	for (u32 i = 0; i < n; i++)
	{
		const u32 q = 0x30000000u + rnd32() % 0x40000000u;
		const double m = ((double)bf(q) + (double)bf(q + 1)) / 2;
		const u32 x = fb((float)(1.0 / m));
		if (t_recip(x) != fb(1.0f / bf(x)))
			die("recip(%08X) = %08X, fdivs %08X", x, t_recip(x), fb(1.0f / bf(x)));
		const u32 cnt = 3 + rnd32() % 200;
		const u32 a = fb((float)(m * cnt));
		if (t_div(a, cnt) != fb(bf(a) / (float)cnt))
			die("div_cnt(%08X, %u) = %08X, fdivs %08X", a, cnt, t_div(a, cnt), fb(bf(a) / (float)cnt));
		g_float_checks += 2;
	}
	// div_cnt: a strip's 1/W sum by its vertex count.
	for (u32 i = 0; i < n; i++)
	{
		const u32 cnt = (i & 1) ? 3 + rnd32() % 62 : 1 + rnd32() % 65535;
		u32 a = 0x30800000u + rnd32() % (0x7F7FFFFFu - 0x30800000u);
		if (rnd32() % 16 == 0)
			a = 0x7F800000u;
		const u32 want = fb(bf(a) / (float)cnt);
		if (t_div(a, cnt) != want)
			die("div_cnt(%08X, %u) = %08X, fdivs %08X", a, cnt, t_div(a, cnt), want);
		g_float_checks++;
	}
}

int main(int argc, char** argv)
{
	const u32 cases = argc > 1 ? (u32)atoi(argv[1]) : 3000;
	spu_arena(ARENA);
	u8* p = g_arena;
	u32 out_ea[SPE_CTX];
	for (u32 i = 0; i < SPE_CTX; i++)
	{
		g_out[i] = (SpeOut*)p;
		out_ea[i] = (u32)spu_ea(p);
		p += sizeof(SpeOut);
	}
	g_runs = (SpeRun*)p;
	p += sizeof(SpeRun) * SPEVB_MAX_RUNS;
	g_echo = (SpeVbEcho*)p;
	p += sizeof(SpeVbEcho);
	g_vb = (u32*)p;
	g_vb_max = (u32)((g_arena + ARENA - p) / SPEVB_VTX_STRIDE) - 8;
	g_echo_ea = spu_ea(g_echo);
	spu_setup(out_ea, (u32)spu_ea(g_runs), g_echo_ea);
	g_land_hook = land_hook;

	shim_re_err = 1.0 / 4096;
	float_checks(200000);
	shim_re_err = 1.0 / 1024;
	float_checks(50000);
	// A 2^-7 estimate still lands within one ulp after two Newton steps, and often needs the corrections.
	shim_re_err = 1.0 / 128;
	float_checks(50000);
	shim_re_err = 1.0 / 4096;

	u32 trppu = 0, capped = 0;
	u64 runs = 0, vtx = 0;
	for (u32 c = 0; c < cases; c++)
	{
		Scene s;
		const u32 kind = c % 8;
		g_seed = c * 2654435761u + 1;
		gen_scene(s, kind);
		RefOut a, b;
		ref_run(s, true, a);
		ref_run(s, false, b);
		same_ref(a, b);
		spu_check(s, a);
		bump_check(s, b);
		trppu += g_trppu_exp;
		capped += a.vtx_end[2] >= s.cap && s.cap < 3 * s.nvtx + 4 * s.nstrip + 16;
		runs += a.runs.size();
		vtx += a.vtx_end[2];
	}
	printf("PASS: %u scenes (%u with TR left to the PPU, %u at the cap), %llu runs, %llu vertices; %llu float checks; "
	       "3j bump vertices %llu (vertex arrays), %llu (glBegin)\n",
	       cases, trppu, capped, (unsigned long long)runs, (unsigned long long)vtx, (unsigned long long)g_float_checks,
	       (unsigned long long)g_bump_vtx, (unsigned long long)g_bump_imm);
	return 0;
}
