#pragma once
// OPTIMIZATION_PLAN 5.12 #2: draw_list, draw_tr_sorted and draw_strip of ps3_rend.cpp on the SPU. The same runs and
// ps3gl_vertex_t words, bit for bit. Include after echo, echo_ea, setup and diag; misc/spevb_check runs it on the PC.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <string.h>
#include <stddef.h>
#include "spe_vb.h"

#define F_ONE   0x3F800000u
#define F_INF   0x7F800000u
#define F_1E25  0x69045951u  // 1e25f, vertex_inf's |x| and |y| limit
#define F_INF_W 0x01202205u  // 1.0f / 3.4e37f, vertex_inf's W limit
#define F_1E9   0x4E6E6B28u  // 1 / INV_W_FLOOR, the largest W the decoder writes

#define TAG_ECHO 1
#define TAG_IN   2   // long-strip chunks, setup
#define TAG_SW   3   // two per input window: strip records 3, 4; vertices 5, 6; params 20, 21
#define TAG_VW   5
#define TAG_PW   20
#define TAG_SLOT 8   // one per ring slot: 8..15
#define TAG_VB   16  // two per output stream: 16..19
#define TAG_RUN  18
#define OUT_MASK (0xFu << TAG_VB)

enum { VB_STAGE_RUN, VB_STAGE_SLOT, VB_STAGE_OUT, VB_STAGE_IN, VB_STAGE_EVENT, VB_STAGE_PUB, VB_STAGE_SLIDE };

#define RING      8
#define SLOT_VTX  64    // vertices a slot or a vertex window holds per strip; the rest comes through longbuf
#define LONG_VTX  680   // vertices per long-strip chunk, under the 16 KB DMA limit
#define PUB_VTX   256   // vertices between two publishes

enum { SEC_OP, SEC_PT, SEC_TR };
static const uint32_t sec_list[3] = { 0, 4, 2 };  // ListType_Opaque, _Punch_Through, _Translucent
static const uint32_t sec_flag[3] = { SPEVB_OP_DONE, SPEVB_PT_DONE, SPEVB_TR_DONE };

// The diagnostic split counts the ticks of each wait by its stage, and the DMA commands; diag gets them after
// each build (its fields are volatile, so per-strip updates would be loads and stores).
static uint32_t t_wait[8], n_cmds;

static void vb_wait(uint32_t mask, uint32_t stage)
{
	echo.stage = stage;
	const uint32_t t0 = spu_read_decrementer();
	mfc_write_tag_mask(mask);
	mfc_read_tag_status_all();
	t_wait[stage] += t0 - spu_read_decrementer();
	echo.stage = VB_STAGE_RUN;
}

// The MFC queue holds 16 commands, and a full one stalls the next command; the split counts that stall.
static inline void q_room(void)
{
	n_cmds++;
	if (mfc_stat_cmd_queue())
		return;
	const uint32_t t0 = spu_read_decrementer();
	while (!mfc_stat_cmd_queue())
		;
	diag.t_qfull += t0 - spu_read_decrementer();
}

static void diag_flush(void)
{
	diag.t_slot += t_wait[VB_STAGE_SLOT];
	diag.t_pub += t_wait[VB_STAGE_PUB];
	diag.t_slide += t_wait[VB_STAGE_SLIDE];
	diag.t_in += t_wait[VB_STAGE_IN];
	diag.n_cmd += n_cmds;
	memset(t_wait, 0, sizeof(t_wait));
	n_cmds = 0;
}

// ---- float steps, exact as the PPU rounds them ---------------------------

static inline vec_double2 to_d(uint32_t b) { return spu_extend((vec_float4)spu_splats(b)); }
static inline uint32_t to_f(vec_double2 v) { return spu_extract((vec_uint4)spu_roundtf(v), 0); }

// Words of double i. The SPU is big-endian, so the high word comes first; the PC check sets VB_HI 1 for x86.
#ifndef VB_HI
#define VB_HI 0
#endif
static inline uint32_t d_hi(vec_double2 v, int i) { return spu_extract((vec_uint4)v, 2 * i + VB_HI); }
static inline uint32_t d_lo(vec_double2 v, int i) { return spu_extract((vec_uint4)v, 2 * i + 1 - VB_HI); }

// a * b as fmuls: round to nearest even, denormal results kept, PPC NaN rules. mul_xy's rare path.
static uint32_t fmul_rn(uint32_t a, uint32_t b)
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
			return 0x7FC00000u;  // Inf * 0
		return s | F_INF;
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
	const uint64_t p = (uint64_t)ma * mb;  // in [2^46, 2^48)
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
		return s;  // below half the smallest denormal
	uint64_t m = p >> sh;
	const uint64_t rem = p & ((1ull << sh) - 1), half = 1ull << (sh - 1);
	if (rem > half || (rem == half && (m & 1)))
		m++;
	if (!e)
		return s | (uint32_t)m;  // m == 2^23 packs as the smallest normal
	if (m >> 24)
	{
		m >>= 1;
		e++;
	}
	if (e >= 255)
		return s | F_INF;
	return s | ((uint32_t)e << 23) | ((uint32_t)m & 0x7FFFFFu);
}

static inline uint32_t fplain(uint32_t f)
{
	return (f & 0x7F800000u) - 0x00800000u < 0x7F000000u;
}

// x*w and y*w: the exact double product, rounded once to single as fmuls does.
static inline void mul_xy(uint32_t x, uint32_t y, uint32_t w, uint32_t* ox, uint32_t* oy)
{
	if (fplain(x) && fplain(y) && fplain(w))
	{
		const vec_float4 a = (vec_float4)spu_insert(y, spu_promote(x, 0), 2);
		const vec_double2 p = spu_mul(spu_extend(a), spu_extend((vec_float4)spu_splats(w)));
		const uint32_t hx = d_hi(p, 0) & 0x7FFFFFFFu, hy = d_hi(p, 1) & 0x7FFFFFFFu;
		// Products in [2^-126, 2^127): frds keeps them; below it flushes the denormals the PPU keeps.
		if (hx - 0x38100000u < 0x0FD00000u && hy - 0x38100000u < 0x0FD00000u)
		{
			const vec_uint4 r = (vec_uint4)spu_roundtf(p);
			*ox = spu_extract(r, 0);
			*oy = spu_extract(r, 2);
			return;
		}
	}
	diag.n_slowmul++;
	*ox = fmul_rn(x, w);
	*oy = fmul_rn(y, w);
}

// Double i of a pair from high word h and low word l (bytes of the H and L quadwords).
#if VB_HI
#define DW(h, l) l, l + 1, l + 2, l + 3, h, h + 1, h + 2, h + 3
#else
#define DW(h, l) h, h + 1, h + 2, h + 3, l, l + 1, l + 2, l + 3
#endif
#define HW(i) 8 * (i) + 4 * VB_HI, 8 * (i) + 4 * VB_HI + 1, 8 * (i) + 4 * VB_HI + 2, 8 * (i) + 4 * VB_HI + 3
#define LW(i) 8 * (i) + 4 - 4 * VB_HI, 8 * (i) + 5 - 4 * VB_HI, 8 * (i) + 6 - 4 * VB_HI, 8 * (i) + 7 - 4 * VB_HI
#define Z4 0x80, 0x80, 0x80, 0x80
static const vec_uchar16 m_xy = { DW(0, 16), DW(4, 20) };
static const vec_uchar16 m_zw = { DW(8, 24), DW(12, 28) };
static const vec_uchar16 m_d02 = { DW(0, 16), DW(8, 24) };
static const vec_uchar16 m_h02 = { HW(0), Z4, HW(1), Z4 };
static const vec_uchar16 m_l02 = { LW(0), Z4, LW(1), Z4 };
static const vec_uchar16 m_dup02 = { 0, 1, 2, 3, 0, 1, 2, 3, 8, 9, 10, 11, 8, 9, 10, 11 };
static const vec_uchar16 m_d0d0 = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 };
static const vec_uchar16 m_d1d1 = { 8, 9, 10, 11, 12, 13, 14, 15, 8, 9, 10, 11, 12, 13, 14, 15 };
static const vec_uchar16 m_his = { HW(0), HW(1), HW(2), HW(3) };
static const vec_uchar16 m_odd = { 4, 5, 6, 7, Z4, 12, 13, 14, 15, Z4 };

// Positive normal floats (lanes 0 and 2) as doubles: to_d with integer steps, not fesd's DP stall.
static inline vec_uint4 f2d_hi(vec_uint4 f) { return spu_add(spu_rlmask(f, -3), spu_splats(0x38000000u)); }
static inline vec_uint4 f2d_lo(vec_uint4 f) { return spu_sl(f, 29); }
static inline vec_double2 f2d(vec_uint4 f) { return (vec_double2)spu_shuffle(f2d_hi(f), f2d_lo(f), m_d02); }

// Positive doubles in the normal float range, rounded to nearest even as floats (lanes 0 and 2): frds with
// integer steps. A double from 2^128 - 2^103 up gives 0x7F800000 (+Inf) or more.
static inline vec_uint4 d2f(vec_double2 d)
{
	const vec_uint4 H = spu_shuffle((vec_uint4)d, (vec_uint4)d, m_h02), L = spu_shuffle((vec_uint4)d, (vec_uint4)d, m_l02);
	const vec_uint4 m = spu_or(spu_rlmask(spu_sl(H, 12), -9), spu_rlmask(L, -29));
	const vec_uint4 e = spu_sl(spu_and(spu_sub(H, spu_splats(0x38000000u)), spu_splats(0x7FF00000u)), 3);
	const vec_uint4 up = spu_rlmask(spu_add(spu_add(spu_and(L, spu_splats(0x1FFFFFFFu)), spu_splats(0x0FFFFFFFu)),
	                                        spu_and(m, spu_splats(1u))), -29);
	return spu_add(spu_add(e, m), up);
}

// 1/x as fdivs gives it for x normal in lanes 0 and 2. q starts within one ulp; the fused residual at each
// neighbouring midpoint has an exact sign and picks the side (1/x is never a midpoint). As recip() in spe_dec.c,
// two at once and without branches: the midpoints are q and q - 1 as doubles plus half their ulp (bit 28).
static inline __attribute__((always_inline)) vec_uint4 recip2(vec_uint4 X)
{
	const vec_double2 one = spu_splats(1.0);
	const vec_double2 xd = f2d(X);
	vec_double2 y = f2d((vec_uint4)spu_re((vec_float4)X));
	y = spu_madd(y, spu_nmsub(xd, y, one), y);
	y = spu_madd(y, spu_nmsub(xd, y, one), y);
	const vec_uint4 q = d2f(y);
	const vec_uint4 Q = spu_sub(spu_shuffle(q, q, m_dup02), ((vec_uint4){ 0, 1, 0, 1 }));
	const vec_uint4 H = f2d_hi(Q), L = spu_or(f2d_lo(Q), spu_splats(0x10000000u));
	const vec_uint4 xu = (vec_uint4)xd;
	const vec_double2 r0 = spu_nmsub((vec_double2)spu_shuffle(xu, xu, m_d0d0), (vec_double2)spu_shuffle(H, L, m_xy), one);
	const vec_double2 r1 = spu_nmsub((vec_double2)spu_shuffle(xu, xu, m_d1d1), (vec_double2)spu_shuffle(H, L, m_zw), one);
	// Signs of r_up0, r_dn0, r_up1, r_dn1: q + 1 when r_up >= 0, q - 1 when r_dn < 0.
	const vec_uint4 S = spu_rlmask(spu_shuffle((vec_uint4)r0, (vec_uint4)r1, m_his), -31);
	return spu_sub(spu_add(q, spu_splats(1u)), spu_add(S, spu_shuffle(S, S, m_odd)));
}

// fadds of positive normal floats in lane 0, through doubles: double rounding is exact for + (53 >= 2*24+2).
// 0x7F800000 or more: the sum rounds to +Inf.
static inline __attribute__((always_inline)) vec_uint4 add_pos(vec_uint4 a, vec_uint4 b)
{
	return d2f(spu_add(f2d(a), f2d(b)));
}

// fdivs of a positive normal sum (or +Inf) by a strip's vertex count; a / n is never a midpoint.
static uint32_t div_cnt(uint32_t a, uint32_t n)
{
	if (a >= F_INF)
		return F_INF;
	const vec_float4 nf = spu_convtf(spu_splats(n), 0);
	const vec_double2 nd = spu_extend(nf), ad = to_d(a);
	const vec_double2 one = spu_splats(1.0), half = spu_splats(0.5);
	vec_double2 y = spu_extend(spu_re(nf));
	y = spu_madd(y, spu_nmsub(nd, y, one), y);
	y = spu_madd(y, spu_nmsub(nd, y, one), y);
	const uint32_t q = to_f(spu_mul(ad, y));
	const vec_double2 qd = to_d(q);
	const vec_double2 ru = spu_nmsub(nd, spu_mul(spu_add(qd, to_d(q + 1)), half), ad);
	if (!(d_hi(ru, 0) >> 31))
		return q + 1;
	const vec_double2 rd = spu_nmsub(nd, spu_mul(spu_add(qd, to_d(q - 1)), half), ad);
	if (d_hi(rd, 0) >> 31)
		return q - 1;
	return q;
}

// ---- output streams ------------------------------------------------------

// An LS window pair over a stream in main or RSX memory. Records are 16 or 48 bytes, so every put is whole
// quadwords: the bytes since the last put, all final. Only the window being reused waits for its DMA.
typedef struct
{
	uint8_t* ls[2];
	uint32_t win, cur, base, put, tag, ea;
} OStream;

#define VB_WIN 16384
static uint8_t ls_vb[2][VB_WIN] __attribute__((aligned(128)));
static uint8_t ls_run[2][2048] __attribute__((aligned(128)));
static OStream os_vb = { { ls_vb[0], ls_vb[1] }, VB_WIN, 0, 0, 0, TAG_VB, 0 };
static OStream os_run = { { ls_run[0], ls_run[1] }, sizeof(ls_run[0]), 0, 0, 0, TAG_RUN, 0 };

static uint32_t out_bytes;  // the diagnostic split's put volume

static void os_put(OStream* s, uint32_t end)
{
	if (end == s->put)
		return;
	q_room();
	mfc_put(s->ls[s->cur] + (s->put - s->base), s->ea + s->put, end - s->put, s->tag + s->cur, 0, 0);
	out_bytes += end - s->put;
	s->put = end;
}

// The write index b needs the other window: put this one, then map the other from b's 128-byte line on.
static void os_slide(OStream* s, uint32_t b)
{
	diag.n_slide++;
	os_put(s, b);
	const uint32_t o = s->cur ^ 1;
	vb_wait(1u << (s->tag + o), VB_STAGE_SLIDE);
	s->cur = o;
	s->base = b & ~127u;
}

// The ea must sit on 128 bytes; the previous build's puts are done (its last publish waited for them).
static void os_open(OStream* s, uint32_t ea)
{
	s->ea = ea;
	s->cur = 0;
	s->base = 0;
	s->put = 0;
}

// ---- build state ---------------------------------------------------------

typedef struct { uint32_t first, count, param; } Job;

// EAs fit in 32 bits on the PS3: 64-bit math costs __muldi3 calls on the SPU.
static uint32_t vtx_ea, strip_ea, par_ea;
static uint32_t vb_used, vb_cap, nruns, pub_vtx, sec_strips;

// s_run of ps3_rend.cpp. last holds the words of the last vertex emitted: run_emit(s_run.last) writes them again.
// cmask makes the color word from a vertex's x, y, w, col quadword (alpha_or as 0xFF in the shuffle).
static struct
{
	uint32_t open, reversed, cull, param, list, count, first, alpha_or;
	SpeParam p __attribute__((aligned(16)));
	vec_uint4 last[3];
	vec_uchar16 cmask, cmask1;
} run;

// Vertex vb_used's slot in LS (stream byte 48 * vb_used), and the end of its window.
static vec_uint4* vb_p;
static vec_uint4* vb_end;

static void vb_slide(void)
{
	const uint32_t b = vb_used * SPEVB_VTX_STRIDE;
	os_slide(&os_vb, b);
	vb_p = (vec_uint4*)(os_vb.ls[os_vb.cur] + (b - os_vb.base));
	vb_end = (vec_uint4*)(os_vb.ls[os_vb.cur] + VB_WIN);
}

static inline SpeRun* run_at(uint32_t idx)
{
	const uint32_t b = idx * (uint32_t)sizeof(SpeRun);
	if (b + sizeof(SpeRun) > os_run.base + os_run.win)
		os_slide(&os_run, b);
	return (SpeRun*)(os_run.ls[os_run.cur] + (b - os_run.base));
}

static void publish(uint32_t flags, int sec)
{
	diag.n_pub++;
	os_put(&os_vb, vb_used * SPEVB_VTX_STRIDE);
	os_put(&os_run, nruns * (uint32_t)sizeof(SpeRun));
	vb_wait(OUT_MASK | (1u << TAG_ECHO), VB_STAGE_PUB);
	echo.runs = nruns;
	echo.vb_used = vb_used;
	if (sec >= 0)
	{
		echo.sec_end[sec] = nruns;
		echo.strips[sec] = sec_strips;
	}
	if (flags & SPEVB_TR_PPU)
		echo.trppu++;
	echo.flags |= flags;
	q_room();
	mfc_putf((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	pub_vtx = vb_used;
}

// vb_emit's words of v in its 48-byte slot: x*W, y*W, W, 1 | u, v, 0, 0 | RGBA, 0, 0, 0. A strip's vertex gets them
// once: every copy run_emit makes of it is the same.
static void vtx_words(const SpeVtx* v, vec_uint4* o)
{
	uint32_t xw, yw;
	mul_xy(v->x, v->y, v->w, &xw, &yw);
	const uint32_t c = v->col | run.alpha_or;
	o[0] = (vec_uint4){ xw, yw, v->w, F_ONE };
	o[1] = (vec_uint4){ v->u, v->v, 0, 0 };
	o[2] = (vec_uint4){ (c << 8) | (c >> 24), 0, 0, 0 };
}

// run_emit: at the cap the vertex is dropped and run.last stays, as on the PPU.
static void emit(const vec_uint4* o)
{
	if (vb_used >= vb_cap)
		return;
	if (vb_end - vb_p < 3)
		vb_slide();
	vb_p[0] = o[0];
	vb_p[1] = o[1];
	vb_p[2] = o[2];
	vb_p += 3;
	if (o != run.last)
	{
		run.last[0] = o[0];
		run.last[1] = o[1];
		run.last[2] = o[2];
	}
	vb_used++;
	run.count++;
}

static void run_close(void)
{
	if (!run.open)
		return;
	*(vec_uint4*)run_at(nruns++) = (vec_uint4){ run.first, run.count, run.param, 0 };
	run.open = 0;
	// The first run goes out at once, so the render thread starts early; then every PUB_VTX vertices.
	if (nruns == 1 || vb_used - pub_vtx >= PUB_VTX)
		publish(0, -1);
}

// vertex_inf on the bits: NaN or |x|, |y| > 1e25, or W below 1/3.4e37 (a NaN W is not).
static inline int vtx_inf(const SpeVtx* v)
{
	if ((v->x & 0x7FFFFFFFu) > F_1E25 || (v->y & 0x7FFFFFFFu) > F_1E25)
		return 1;
	const uint32_t w = v->w;
	if ((w & 0x7FFFFFFFu) > F_INF)
		return 0;
	return (w >> 31) || w < F_INF_W;
}

// strip_equivalent on the 4 words at once; both params sit on 16 bytes.
static inline int equivalent(const SpeParam* a, const SpeParam* b)
{
	const vec_uint4 d = spu_xor(*(const vec_uint4*)a, *(const vec_uint4*)b);
	return spu_extract(spu_orx(spu_and(d, ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, ~0u }))), 0) == 0;
}

// apply_param's alpha force: UseAlpha off, or a texture (it always binds) with ShadInstr 1.
static inline uint32_t alpha_rule(const SpeParam* p)
{
	const int force = !((p->tsp >> 20) & 1) || (((p->pcw >> 3) & 1) && ((p->tsp >> 6) & 3) == 1);
	return force ? 0xFF000000u : 0;
}

// ---- input windows -------------------------------------------------------

// The in-order passes read strip records, vertices and params through two LS buffers each: the next window loads
// while one is in use. A jump (another block of the list, a param index going back) loads its window at once.
typedef struct
{
	const uint8_t* cbuf;  // the current buffer and its first element
	uint32_t clo;
	uint8_t* buf[2];
	uint32_t lo[2];
	uint32_t cur, n, step, align, esize, tag, lim, ea;
} IWin;

#define IW_NONE 0x80000000u  // an empty buffer's lo: i - IW_NONE is past any window
#define SW_N    340          // strip records per window (4080 bytes), from a multiple of 4
#define VW_N    340          // vertices per window (8160 bytes), from an even vertex
#define VW_STEP 276          // vertex windows overlap by SLOT_VTX, so a strip of up to SLOT_VTX sits in one
#define PW_N    128          // params per window (2 KB), from a multiple of 8

static uint8_t ls_sw[2][SW_N * 12] __attribute__((aligned(128)));
// pair_check and w4 read up to 112 bytes past a strip.
static uint8_t ls_vw[2][VW_N * 24 + 112] __attribute__((aligned(128)));
static uint8_t ls_pw[2][PW_N * 16] __attribute__((aligned(128)));
static IWin sw = { 0, 0, { ls_sw[0], ls_sw[1] }, { 0, 0 }, 0, SW_N, SW_N, 4, 12, TAG_SW, 0, 0 };
static IWin vw = { 0, 0, { ls_vw[0], ls_vw[1] }, { 0, 0 }, 0, VW_N, VW_STEP, 2, 24, TAG_VW, 0, 0 };
static IWin pw = { 0, 0, { ls_pw[0], ls_pw[1] }, { 0, 0 }, 0, PW_N, PW_N, 8, 16, TAG_PW, 0, 0 };

// A prefetch of an earlier pass may still be landing in the buffer: its tag goes first.
static void iw_load(IWin* w, uint32_t k, uint32_t lo)
{
	vb_wait(1u << (w->tag + k), VB_STAGE_IN);
	q_room();
	mfc_get(w->buf[k], w->ea + lo * w->esize, w->n * w->esize, w->tag + k, 0, 0);
	w->lo[k] = lo;
}

// lim: elements in use; no prefetch starts past it.
static void iw_open(IWin* w, uint32_t ea, uint32_t lim)
{
	w->ea = ea;
	w->lim = lim;
	w->lo[0] = w->lo[1] = w->clo = IW_NONE;
}

// [i, i + c) is not in the current window: the other one has it (the prefetch), or it loads now.
static void iw_move(IWin* w, uint32_t i, uint32_t c)
{
	const uint32_t k = w->cur ^ 1;
	if (i - w->lo[k] + c > w->n)
		iw_load(w, k, i & ~(w->align - 1));
	vb_wait(1u << (w->tag + k), VB_STAGE_IN);
	w->cur = k;
	w->cbuf = w->buf[k];
	w->clo = w->lo[k];
	if (w->lo[k] + w->step < w->lim)
		iw_load(w, k ^ 1, w->lo[k] + w->step);
}

// Elements [i, i + c) in LS; n and esize are the window's, as constants. A vertex window holds c up to SLOT_VTX.
static inline __attribute__((always_inline)) const uint8_t* iw_get(IWin* w, uint32_t i, uint32_t c, uint32_t n,
                                                                   uint32_t esize)
{
	if (__builtin_expect(i - w->clo + c > n, 0))
		iw_move(w, i, c);
	return w->cbuf + (i - w->clo) * esize;
}

// A strip's vertices [lo, hi) in LS. Reads only move forward, so a long strip reloads at the first miss.
typedef struct
{
	const uint8_t* base;
	uint32_t lo, hi, first, count;
} VWin;

static uint8_t longbuf[16384] __attribute__((aligned(128)));

static const SpeVtx* vwin_load(VWin* w, uint32_t i)
{
	const uint32_t n = (w->count - i < LONG_VTX) ? w->count - i : LONG_VTX;
	const uint32_t ea = vtx_ea + (w->first + i) * (uint32_t)sizeof(SpeVtx);
	const uint32_t a0 = ea & ~15u, a1 = (ea + n * (uint32_t)sizeof(SpeVtx) + 15) & ~15u;
	q_room();
	mfc_get(longbuf, a0, a1 - a0, TAG_IN, 0, 0);
	vb_wait(1u << TAG_IN, VB_STAGE_IN);
	w->base = longbuf + (ea - a0);
	w->lo = i;
	w->hi = i + n;
	return (const SpeVtx*)w->base;
}

static inline const SpeVtx* vat(VWin* w, uint32_t i)
{
	if (i - w->lo < w->hi - w->lo)
		return (const SpeVtx*)(w->base + (i - w->lo) * sizeof(SpeVtx));
	return vwin_load(w, i);
}

// The strip's vertices from the vertex window; an empty or long strip starts with none in LS.
static inline __attribute__((always_inline)) void vw_strip(const Job* j, VWin* w)
{
	w->first = j->first;
	w->count = j->count;
	w->lo = 0;
	w->hi = 0;
	w->base = 0;
	if (__builtin_expect(j->count - 1u < SLOT_VTX, 1))
	{
		w->base = iw_get(&vw, j->first, j->count, VW_N, sizeof(SpeVtx));
		w->hi = j->count;
	}
}

// ---- the prefetch ring (the sorted TR pass) ------------------------------

static uint8_t slot_vtx[RING][SLOT_VTX * 24 + 112] __attribute__((aligned(128)));  // pair_check reads past
static SpeParam slot_par[RING] __attribute__((aligned(16)));
static Job slot_job[RING];
static uint32_t slot_off[RING];

static void slot_issue(uint32_t k, const Job* j)
{
	const uint32_t n = j->count < SLOT_VTX ? j->count : SLOT_VTX;
	const uint32_t ea = vtx_ea + j->first * (uint32_t)sizeof(SpeVtx);
	const uint32_t a0 = ea & ~15u, a1 = (ea + n * (uint32_t)sizeof(SpeVtx) + 15) & ~15u;
	q_room();
	mfc_get(slot_vtx[k], a0, a1 - a0, TAG_SLOT + k, 0, 0);
	q_room();
	mfc_get(&slot_par[k], par_ea + j->param * (uint32_t)sizeof(SpeParam), sizeof(SpeParam), TAG_SLOT + k, 0, 0);
	slot_off[k] = ea - a0;
	slot_job[k] = *j;
}

static inline void slot_win(uint32_t k, VWin* w)
{
	const Job* j = &slot_job[k];
	w->base = slot_vtx[k] + slot_off[k];
	w->lo = 0;
	w->hi = j->count < SLOT_VTX ? j->count : SLOT_VTX;
	w->first = j->first;
	w->count = j->count;
}

// Jobs from next() in order, RING strips in flight ahead of proc(). Inlined, so next and proc are direct calls.
static inline __attribute__((always_inline)) void ring_pass(int (*next)(Job*), void (*proc)(uint32_t))
{
	uint32_t head = 0, tail = 0;
	Job j;
	while (head - tail < RING && next(&j))
		slot_issue(head++ % RING, &j);
	while (tail != head)
	{
		const uint32_t k = tail % RING;
		vb_wait(1u << (TAG_SLOT + k), VB_STAGE_SLOT);
		proc(k);
		tail++;
		if (next(&j))
			slot_issue(head++ % RING, &j);
	}
}

// ---- draw_strip ----------------------------------------------------------

static uint32_t cur_list, vin;

// draw_strip's loop state; last_good as its words, as a long strip's window may move past the vertex.
static struct
{
	vec_uint4 good[3];
	uint32_t have_good, dupe_next;
} st;

// For a vertex at byte 0 or 8 of its quadword: x, y, w, col, then u, v, 0, 0, from the two quadwords it spans.
static const vec_uchar16 vmask[2][2] = {
	{ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	  { 16, 17, 18, 19, 20, 21, 22, 23, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } },
	{ { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 },
	  { 24, 25, 26, 27, 28, 29, 30, 31, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } },
};
static const vec_uchar16 m_ww = { DW(8, 24), DW(8, 24) };
static const vec_uchar16 m_ew = { 8, 9, 10, 11, 8, 9, 10, 11, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
// x*W and y*W (frds lanes 0 and 2), then W; the 1.0 is ORed in.
static const vec_uchar16 m_q0 = { 0, 1, 2, 3, 8, 9, 10, 11, 24, 25, 26, 27, 0x80, 0x80, 0x80, 0x80 };
// col (ARGB, lane 3) as RGBA; 0xC0 gives the 0xFF of alpha_or.
static const vec_uchar16 m_col = { 13, 14, 15, 12, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
static const vec_uchar16 m_col_ff = { 13, 14, 15, 0xC0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
// The same from lane 1, for the second vertex of a pair.
static const vec_uchar16 m_col1 = { 5, 6, 7, 4, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
static const vec_uchar16 m_col1_ff = { 5, 6, 7, 0xC0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
// A pair A, B as n0 = xA yA wA cA, n1 = uA vA xB yB, n2 = wB cB uB vB: x y of both, W of each twice, u v of each.
static const vec_uchar16 m_pxy = { 0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31 };
static const vec_uchar16 m_pww = { 8, 9, 10, 11, 8, 9, 10, 11, 16, 17, 18, 19, 16, 17, 18, 19 };
static const vec_uchar16 m_uv0 = { 0, 1, 2, 3, 4, 5, 6, 7, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
static const vec_uchar16 m_uv1 = { 8, 9, 10, 11, 12, 13, 14, 15, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
static const vec_uchar16 m_wA = { DW(0, 16), DW(0, 16) };
static const vec_uchar16 m_q0A = { 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 0x80, 0x80, 0x80, 0x80 };

// A vertex for vtx_check and vtx_out: its two quadwords, x y w col, |x y w col|, the lanes that are 0.
typedef struct
{
	vec_uint4 a0, a1, X, A, Z;
	const vec_uchar16* m;
} VtxIn;

// vtx_inf and mul_xy's fast case on the vertex at vp: nonzero lanes leave it to strip_slow.
static inline __attribute__((always_inline)) vec_uint4 vtx_check(const uint8_t* vp, VtxIn* v)
{
	const uint32_t off = (uint32_t)(uintptr_t)vp & 8;
	const vec_uint4* a = (const vec_uint4*)(vp - off);
	v->m = vmask[off >> 3];
	v->a0 = a[0];
	v->a1 = a[1];
	const vec_uint4 X = v->X = spu_shuffle(v->a0, v->a1, v->m[0]);
	const vec_uint4 A = v->A = spu_and(X, spu_splats(0x7FFFFFFFu));
	// vtx_inf: |x|, |y| > 1e25 or NaN; W below 1/3.4e37 as a signed compare (negative too), unless NaN.
	const vec_uint4 big = spu_cmpgt(A, ((vec_uint4){ F_1E25, F_1E25, F_INF, ~0u }));
	const vec_uint4 sml = spu_cmpgt(((vec_int4){ INT32_MIN, INT32_MIN, (int32_t)F_INF_W, INT32_MIN }), (vec_int4)X);
	const vec_uint4 inf = spu_or(spu_and(big, ((vec_uint4){ ~0u, ~0u, 0, 0 })), spu_andc(sml, big));
	// mul_xy's fast case: W normal; x, y zero, or normal with ex + ew in [128, 379] (products in [2^-126, 2^127)).
	const vec_uint4 Z = v->Z = spu_cmpeq(A, spu_splats(0u));
	const vec_uint4 E = spu_rlmask(A, -23);
	const vec_uint4 S = spu_add(E, spu_shuffle(E, E, m_ew));
	const vec_uint4 b1 = spu_cmpgt(spu_add(E, spu_splats(~0u)), ((vec_uint4){ 253, 253, 253, ~0u }));
	const vec_uint4 b2 = spu_cmpgt(spu_add(S, spu_splats(-128u)), ((vec_uint4){ 251, 251, ~0u, ~0u }));
	return spu_or(inf, spu_andc(spu_or(b1, b2), Z));
}

// The slot words of a vertex vtx_check passed. x, y and W as exact doubles from their bits (exponent + 896),
// one dfm, one frds: what fmuls gives.
static inline __attribute__((always_inline)) void vtx_out(const VtxIn* v, vec_uchar16 cm, vec_uint4* q)
{
	const vec_uint4 X = v->X;
	const vec_uint4 H = spu_or(spu_add(spu_rlmask(v->A, -3), spu_andc(spu_splats(0x38000000u), v->Z)),
	                           spu_andc(X, spu_splats(0x7FFFFFFFu)));
	const vec_uint4 L = spu_sl(X, 29);
	const vec_double2 p = spu_mul((vec_double2)spu_shuffle(H, L, m_xy), (vec_double2)spu_shuffle(H, L, m_ww));
	q[0] = spu_or(spu_shuffle((vec_uint4)spu_roundtf(p), X, m_q0), ((vec_uint4){ 0, 0, 0, F_ONE }));
	q[1] = spu_shuffle(v->a0, v->a1, v->m[1]);
	q[2] = spu_shuffle(X, X, cm);
}

// 1 leaves the vertex at vp to strip_slow; 0 gives its slot words.
static inline __attribute__((always_inline)) uint32_t vtx_fast(const uint8_t* vp, vec_uchar16 cm, vec_uint4* q)
{
	VtxIn v;
	if (__builtin_expect(spu_extract(spu_orx(vtx_check(vp, &v)), 0) != 0, 0))
		return 1;
	vtx_out(&v, cm, q);
	return 0;
}

// Two vertices at once: x y of both in one vector, W of each twice in another, and the vtx_check tests on those.
typedef struct
{
	vec_uint4 n0, n1, n2, XY, WW;
} PairIn;

// The pair at vp; nm (vmask[][0] for the offset of vp) lines its 48 bytes up as if vp sat on 16 bytes.
// It reads up to 112 bytes past vp, which the vertex buffers leave room for.
static inline __attribute__((always_inline)) vec_uint4 pair_check(const uint8_t* vp, vec_uchar16 nm, PairIn* v)
{
	const vec_uint4* a = (const vec_uint4*)((uintptr_t)vp & ~(uintptr_t)15);
	const vec_uint4 a1 = a[1], a2 = a[2];
	v->n0 = spu_shuffle(a[0], a1, nm);
	v->n1 = spu_shuffle(a1, a2, nm);
	v->n2 = spu_shuffle(a2, a[3], nm);
	const vec_uint4 XY = v->XY = spu_shuffle(v->n0, v->n1, m_pxy);
	const vec_uint4 WW = v->WW = spu_shuffle(v->n0, v->n2, m_pww);
	const vec_uint4 AXY = spu_and(XY, spu_splats(0x7FFFFFFFu)), AW = spu_and(WW, spu_splats(0x7FFFFFFFu));
	const vec_uint4 inf = spu_or(spu_cmpgt(AXY, spu_splats(F_1E25)),
	                             spu_andc(spu_cmpgt((vec_int4)spu_splats(F_INF_W), (vec_int4)WW), spu_cmpgt(AW, spu_splats(F_INF))));
	const vec_uint4 EXY = spu_rlmask(AXY, -23), EW = spu_rlmask(AW, -23);
	const vec_uint4 b1 = spu_cmpgt(spu_add(EXY, spu_splats(~0u)), spu_splats(253u));
	const vec_uint4 b2 = spu_cmpgt(spu_add(spu_add(EXY, EW), spu_splats(-128u)), spu_splats(251u));
	const vec_uint4 bw = spu_cmpgt(spu_add(EW, spu_splats(~0u)), spu_splats(253u));
	return spu_or(inf, spu_or(spu_andc(spu_or(b1, b2), spu_cmpeq(AXY, spu_splats(0u))), bw));
}

// The 6 slot words of a pair pair_check passed, as vtx_out for each vertex.
static inline __attribute__((always_inline)) void pair_out(const PairIn* v, vec_uchar16 cm, vec_uchar16 cm1, vec_uint4* o)
{
	const vec_uint4 XY = v->XY, WW = v->WW;
	const vec_uint4 AXY = spu_and(XY, spu_splats(0x7FFFFFFFu));
	const vec_uint4 HXY = spu_or(spu_add(spu_rlmask(AXY, -3), spu_andc(spu_splats(0x38000000u), spu_cmpeq(AXY, spu_splats(0u)))),
	                             spu_andc(XY, spu_splats(0x7FFFFFFFu)));
	const vec_uint4 LXY = spu_sl(XY, 29);
	const vec_uint4 HW = f2d_hi(WW), LW = f2d_lo(WW);
	const vec_double2 pa = spu_mul((vec_double2)spu_shuffle(HXY, LXY, m_xy), (vec_double2)spu_shuffle(HW, LW, m_wA));
	const vec_double2 pb = spu_mul((vec_double2)spu_shuffle(HXY, LXY, m_zw), (vec_double2)spu_shuffle(HW, LW, m_ww));
	o[0] = spu_or(spu_shuffle((vec_uint4)spu_roundtf(pa), WW, m_q0A), ((vec_uint4){ 0, 0, 0, F_ONE }));
	o[1] = spu_shuffle(v->n1, v->n1, m_uv0);
	o[2] = spu_shuffle(v->n0, v->n0, cm);
	o[3] = spu_or(spu_shuffle((vec_uint4)spu_roundtf(pb), WW, m_q0), ((vec_uint4){ 0, 0, 0, F_ONE }));
	o[4] = spu_shuffle(v->n2, v->n2, m_uv1);
	o[5] = spu_shuffle(v->n2, v->n2, cm1);
}

#define FAST_MAX 64  // vertices per strip_fast call; their slots fit in a window after a slide

// draw_strip's loop from vertex i on: the first vertex goes out 1 to 3 times (dupe_next, the parity copy), each
// later one once. Stops at the first vertex for strip_slow: inf, floats mul_xy's fast case misses, the window, the cap.
static inline __attribute__((always_inline)) uint32_t strip_fast(const VWin* w, uint32_t i, uint32_t n)
{
	if (__builtin_expect(i - w->lo >= w->hi - w->lo, 0))
		return i;
	uint32_t k = (w->hi < n ? w->hi : n) - i;
	if (k > FAST_MAX)
		k = FAST_MAX;
	if (__builtin_expect(vb_cap - vb_used < k + 2, 0))
	{
		if (vb_cap - vb_used < 3)
			return i;
		k = vb_cap - vb_used - 2;
	}
	if (__builtin_expect(vb_end - vb_p < 3 * (FAST_MAX + 2), 0))
		vb_slide();
	const vec_uchar16 cm = run.cmask;
	const uint8_t* vp = w->base + (i - w->lo) * sizeof(SpeVtx);
	vec_uint4 q[3];
	if (__builtin_expect(vtx_fast(vp, cm, q), 0))
		return i;
	const uint32_t dupe = st.dupe_next;
	const uint32_t r0 = 1 + dupe + ((((i ^ (run.count + dupe)) & 1)) ^ run.reversed);
	vec_uint4* o = vb_p;
	o[0] = o[3] = o[6] = q[0];
	o[1] = o[4] = o[7] = q[1];
	o[2] = o[5] = o[8] = q[2];
	o += 3 * r0;
	uint32_t j = 1;
	vp += sizeof(SpeVtx);
	// Pairs, each checked beside the DP chain of the one before; the single loop takes the last vertex, or the
	// pair that stops the run.
	if (j + 2 <= k)
	{
		const vec_uchar16 nm = vmask[((uintptr_t)vp & 8) >> 3][0];
		const vec_uchar16 cm1 = run.cmask1;
		PairIn P;
		if (__builtin_expect(spu_extract(spu_orx(pair_check(vp, nm, &P)), 0) == 0, 1))
			for (;;)
			{
				PairIn N;
				const vec_uint4 u = pair_check(vp + 2 * sizeof(SpeVtx), nm, &N);
				pair_out(&P, cm, cm1, o);
				j += 2;
				vp += 2 * sizeof(SpeVtx);
				o += 6;
				if (__builtin_expect((j + 2 > k) | (spu_extract(spu_orx(u), 0) != 0), 0))
					break;
				P = N;
			}
	}
	for (; j < k; j++, vp += sizeof(SpeVtx), o += 3)
		if (vtx_fast(vp, cm, o))
			break;
	run.last[0] = st.good[0] = o[-3];
	run.last[1] = st.good[1] = o[-2];
	run.last[2] = st.good[2] = o[-1];
	st.have_good = 1;
	st.dupe_next = 0;
	vb_used += r0 + j - 1;
	run.count += r0 + j - 1;
	vb_p = o;
	return i + j;
}

// draw_strip's loop body, exact, at vertex i: an inf run up to the next good vertex, or one vertex with its copies.
static uint32_t strip_slow(VWin* w, uint32_t i, uint32_t n)
{
	const SpeVtx* v = vat(w, i);
	if (vtx_inf(v))
	{
		// Skip the inf run; the next good vertex restarts the strip after a degenerate join.
		while (i < n - 1u)
		{
			if (!vtx_inf(vat(w, i + 1)))
			{
				if (st.have_good)
				{
					emit(st.good);
					st.dupe_next = 1;
				}
				break;
			}
			i++;
		}
		return i + 1;
	}
	vtx_words(v, st.good);
	st.have_good = 1;
	if (st.dupe_next)
	{
		emit(st.good);
		st.dupe_next = 0;
	}
	// Vertex i must sit at the same parity as in its own strip, or its triangles flip winding.
	if (((i ^ run.count) & 1) ^ run.reversed)
		emit(st.good);
	emit(st.good);
	return i + 1;
}

// The rest of a strip after strip_fast stopped at vertex i.
static void __attribute__((noinline)) strip_rest(VWin* w, uint32_t i, uint32_t n)
{
	while (i < n)
	{
		const uint32_t f = strip_fast(w, i, n);
		i = f != i ? f : strip_slow(w, i, n);
	}
}

// The join's c (1 or 2) copies of run.last: two slots written, c counted. Near the cap or a window's end, exact.
static inline __attribute__((always_inline)) void emit_last(uint32_t c)
{
	if (__builtin_expect(vb_cap - vb_used < 2 || vb_end - vb_p < 6, 0))
	{
		while (c--)
			emit(run.last);
		return;
	}
	const vec_uint4 l0 = run.last[0], l1 = run.last[1], l2 = run.last[2];
	vb_p[0] = vb_p[3] = l0;
	vb_p[1] = vb_p[4] = l1;
	vb_p[2] = vb_p[5] = l2;
	vb_p += 3 * c;
	vb_used += c;
	run.count += c;
}

// draw_strip's new run: the open one closes (and may publish).
static inline __attribute__((always_inline)) void run_open(const Job* s, const SpeParam* p, uint32_t list)
{
	run_close();
	run.alpha_or = alpha_rule(p);
	run.cmask = run.alpha_or ? m_col_ff : m_col;
	run.cmask1 = run.alpha_or ? m_col1_ff : m_col1;
	run.first = vb_used;
	run.open = 1;
	run.reversed = 0;
	run.cull = (p->isp >> 27) & 3;
	run.param = s->param;
	run.p = *p;
	run.list = list;
	run.count = 0;
	st.dupe_next = 0;
}

// Inlined into each pass, so its registers are saved once per pass, not once per strip.
static inline __attribute__((always_inline)) void draw_strip(const Job* s, const SpeParam* p, VWin* w, uint32_t list)
{
	const uint32_t n = s->count;
	sec_strips++;
	vin += n;
	st.have_good = 0;
	// The merge test without branches: most strips join the open run.
	const uint32_t join = run.open & (run.count != 0) & (run.list == list) &
	                      (run.count + n * 2u + 4u <= SPEVB_RUN_MAX_VTX) & equivalent(p, &run.p);
	if (__builtin_expect(join, 1))
	{
		// Repeat the last vertex; one more copy flips the parity when the cull direction differs.
		const uint32_t cull = (p->isp >> 27) & 3;
		const uint32_t flip = (cull >= 2) & (cull != run.cull);
		emit_last(1 + (run.reversed ^ flip));
		run.reversed = flip;
		st.dupe_next = 1;
	}
	else
		run_open(s, p, list);
	const uint32_t i = strip_fast(w, 0, n);
	if (__builtin_expect(i < n, 0))
		strip_rest(w, i, n);
}

static inline __attribute__((always_inline)) void proc_draw(uint32_t k)
{
	VWin w;
	slot_win(k, &w);
	draw_strip(&slot_job[k], &slot_par[k], &w, cur_list);
}

// ---- the TR keys ---------------------------------------------------------

// TR autosort (draw_tr_sorted): key = sum of 1/W over the strip / count. Keys are positive or +Inf for the
// W the decoder writes ({0} or [2^-126, 1e9]); any other W leaves the TR list to the PPU.
typedef struct { uint32_t key, idx; } KeyRec;
typedef struct { uint32_t first; uint16_t count, param; } TrRec;
static KeyRec keys[SPEVB_TR_MAX], keys_tmp[SPEVB_TR_MAX];
static TrRec tr_rec[SPEVB_TR_MAX];
static uint32_t key_n, key_seen, key_over, key_bad, sort_pos;

#define KEY_CHUNK 64
static uint32_t key_w[KEY_CHUNK] __attribute__((aligned(16)));
static vec_uint4 key_q[KEY_CHUNK / 2];
static const vec_uchar16 m_w01 = { 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7 };
static const vec_uchar16 m_w23 = { 8, 9, 10, 11, 8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15 };

// W of vertices k..k+3 of the strip at vp, read in place; from vertex n on, past the strip, 1.0.
static inline vec_uint4 w4(const uint8_t* vp, uint32_t k, uint32_t n)
{
	const uint32_t* wp = (const uint32_t*)(vp + 8) + 6 * k;
	const vec_uint4 W = { wp[0], wp[6], wp[12], wp[18] };
	const vec_uint4 v = spu_add(spu_splats(k), ((vec_uint4){ 0, 1, 2, 3 }));
	return spu_sel(W, spu_splats(F_ONE), spu_cmpgt(v, spu_splats(n - 1)));
}

// The strip's key, n >= 1: the fadds sum of fdivs(1, W) from the first vertex, by n. A W of 0 makes it +Inf, so
// does a sum past the float range; a W the decoder never writes sets key_bad.
static uint32_t key_of(VWin* w, uint32_t n)
{
	vec_uint4 flags = spu_splats(0u);  // 1: a W of 0, 2: a W out of [2^-126, 1e9]
	vec_uint4 inf = spu_splats(0u);
	vec_uint4 s = spu_splats(0u);
	// A strip of up to SLOT_VTX vertices sits in the vertex window: its W are read in place, 4 at a time.
	const uint32_t in_ls = w->lo == 0 && w->hi == n;
	for (uint32_t c = 0; c < n; c += KEY_CHUNK)
	{
		const uint32_t m = n - c < KEY_CHUNK ? n - c : KEY_CHUNK;
		if (!__builtin_expect(in_ls, 1))
		{
			uint32_t k = 0;
			for (; k < m; k++)
				key_w[k] = vat(w, c + k)->w;
			for (; k & 3; k++)
				key_w[k] = F_ONE;
		}
		for (uint32_t p = 0; p < m; p += 4)
		{
			const vec_uint4 W = __builtin_expect(in_ls, 1) ? w4(w->base, p, n) : *(const vec_uint4*)&key_w[p];
			const vec_uint4 zero = spu_cmpeq(W, spu_splats(0u));
			const vec_uint4 bad = spu_andc(spu_cmpgt(spu_sub(W, spu_splats(0x00800000u)),
			                                         spu_splats(F_1E9 - 0x00800000u)), zero);
			flags = spu_or(flags, spu_or(spu_and(zero, spu_splats(1u)), spu_and(bad, spu_splats(2u))));
			const vec_uint4 Ws = spu_sel(W, spu_splats(F_ONE), spu_or(zero, bad));
			key_q[p >> 1] = recip2(spu_shuffle(Ws, Ws, m_w01));
			key_q[(p >> 1) + 1] = recip2(spu_shuffle(Ws, Ws, m_w23));
		}
		if (spu_extract(spu_orx(flags), 0))
			continue;
		uint32_t k = 0;
		if (!c)
		{
			s = key_q[0];
			k = 1;
		}
		for (; k < m; k++)
		{
			s = add_pos(s, spu_rlqwbyte(key_q[k >> 1], (k & 1) << 3));
			inf = spu_or(inf, spu_cmpgt(s, spu_splats(0x7F7FFFFFu)));
		}
	}
	const uint32_t f = spu_extract(spu_orx(flags), 0);
	if (f & 2)
	{
		key_bad = 1;  // the PPU sorts this TR list; the key goes unused
		return 0;
	}
	return (f & 1) || spu_extract(inf, 0) ? F_INF : div_cnt(spu_extract(s, 0), n);
}

// 0 past SPEVB_TR_MAX strips: the PPU sorts. draw_tr_sorted skips empty strips.
static int key_strip(const Job* j, VWin* w)
{
	if (key_seen++ >= SPEVB_TR_MAX)
	{
		key_over = 1;
		return 0;
	}
	if (!j->count)
		return 1;
	keys[key_n].key = key_of(w, j->count);
	keys[key_n].idx = key_n;
	tr_rec[key_n].first = j->first;
	tr_rec[key_n].count = (uint16_t)j->count;
	tr_rec[key_n].param = (uint16_t)j->param;
	key_n++;
	return 1;
}

// std::stable_sort on a.key < b.key: positive floats and +Inf order as their bits, and an LSD radix sort is stable.
static void sort_keys(uint32_t n)
{
	static uint32_t hist[256];
	KeyRec* src = keys;
	KeyRec* dst = keys_tmp;
	for (uint32_t sh = 0; sh < 32; sh += 8)
	{
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
		KeyRec* t = src;
		src = dst;
		dst = t;
	}
}

static int sorted_next(Job* j)
{
	if (sort_pos >= key_n)
		return 0;
	const TrRec* r = &tr_rec[keys[sort_pos++].idx];
	j->first = r->first;
	j->count = r->count;
	j->param = r->param;
	return 1;
}

// ---- the in-order passes -------------------------------------------------

static uint32_t n_strip, n_vtx, n_param;

// Each section's strips as blocks of consecutive strip records, from one scan: the passes then skip the other
// lists' records. Past SEG_MAX blocks, seg_all makes every pass scan every record.
#define SEG_MAX 256
typedef struct { uint16_t lo, hi; } Seg;
static Seg segs[3][SEG_MAX];
static uint32_t nseg[3], seg_all;

static void seg_add(uint32_t list, uint32_t lo, uint32_t hi)
{
	const int sec = list == sec_list[SEC_OP] ? SEC_OP : list == sec_list[SEC_PT] ? SEC_PT :
	                list == sec_list[SEC_TR] ? SEC_TR : -1;
	if (sec < 0 || lo == hi)
		return;
	if (nseg[sec] == SEG_MAX)
	{
		seg_all = 1;
		return;
	}
	segs[sec][nseg[sec]].lo = (uint16_t)lo;
	segs[sec][nseg[sec]].hi = (uint16_t)hi;
	nseg[sec]++;
}

static void scan_lists(void)
{
	nseg[0] = nseg[1] = nseg[2] = seg_all = 0;
	iw_open(&sw, strip_ea, n_strip);
	uint32_t cur = ~0u, lo = 0;
	for (uint32_t i = 0; i < n_strip;)
	{
		const uint8_t* sp = iw_get(&sw, i, 1, SW_N, sizeof(SpeStrip));
		const uint32_t e = sw.clo + SW_N < n_strip ? sw.clo + SW_N : n_strip;
		for (; i < e; i++, sp += sizeof(SpeStrip))
		{
			const uint32_t l = ((const SpeStrip*)sp)->list;
			if (__builtin_expect(l != cur, 0))
			{
				seg_add(cur, lo, i);
				cur = l;
				lo = i;
			}
		}
	}
	seg_add(cur, lo, n_strip);
}

// draw_list, or with key the TR key pass: the strips of the section in submission order, through the input windows.
static inline __attribute__((always_inline)) void pass_list(uint32_t sec, int key)
{
	const uint32_t list = sec_list[sec];
	iw_open(&sw, strip_ea, n_strip);
	iw_open(&vw, vtx_ea, n_vtx);
	if (!key)
		iw_open(&pw, par_ea, n_param);
	const uint32_t ns = seg_all ? 1 : nseg[sec];
	for (uint32_t g = 0; g < ns; g++)
	{
		uint32_t i = seg_all ? 0 : segs[sec][g].lo;
		const uint32_t hi = seg_all ? n_strip : segs[sec][g].hi;
		while (i < hi)
		{
			const uint8_t* sp = iw_get(&sw, i, 1, SW_N, sizeof(SpeStrip));
			const uint32_t e = sw.clo + SW_N < hi ? sw.clo + SW_N : hi;
			for (; i < e; i++, sp += sizeof(SpeStrip))
			{
				const SpeStrip* s = (const SpeStrip*)sp;
				if (__builtin_expect(seg_all, 0) && s->list != list)
					continue;
				const Job j = { s->first, s->count, s->param };
				VWin w;
				vw_strip(&j, &w);
				if (key)
				{
					if (!key_strip(&j, &w))
						return;
				}
				else
					draw_strip(&j, (const SpeParam*)iw_get(&pw, j.param, 1, PW_N, sizeof(SpeParam)), &w, list);
			}
		}
	}
}

// ---- one build -----------------------------------------------------------

// draw_scene's list order: opaque, punch-through, then translucent sorted (autosort) or in order.
static void build(const SpeVbBell* q)
{
	const uint32_t out = setup.out_ea[q->ctx & (SPE_CTX - 1)];
	vtx_ea = out + offsetof(SpeOut, vtx);
	strip_ea = out + offsetof(SpeOut, strip);
	par_ea = out + offsetof(SpeOut, param);
	n_strip = q->nstrip;
	n_vtx = q->nvtx;
	n_param = q->nparam;
	vb_used = nruns = pub_vtx = vin = 0;
	vb_cap = q->cap;
	run.open = 0;
	os_open(&os_vb, q->vb_ea);
	os_open(&os_run, setup.runs_ea);
	vb_p = (vec_uint4*)ls_vb[0];
	vb_end = (vec_uint4*)(ls_vb[0] + VB_WIN);

	vb_wait(1u << TAG_ECHO, VB_STAGE_OUT);
	echo.seq = q->seq;
	echo.runs = 0;
	echo.flags = 0;
	echo.vb_used = 0;
	for (int k = 0; k < 3; k++)
		echo.sec_end[k] = echo.strips[k] = 0;
	q_room();
	mfc_putf((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);

	scan_lists();
	for (int sec = SEC_OP; sec <= SEC_PT; sec++)
	{
		sec_strips = 0;
		pass_list(sec, 0);
		run_close();
		publish(sec_flag[sec], sec);
		diag.n_strip += sec_strips;
	}

	cur_list = sec_list[SEC_TR];
	sec_strips = 0;
	uint32_t flag = SPEVB_TR_DONE;
	if (q->flags & SPEVB_AUTOSORT)
	{
		key_n = key_seen = key_over = key_bad = 0;
		uint32_t t0 = spu_read_decrementer();
		pass_list(SEC_TR, 1);
		diag.t_key += t0 - spu_read_decrementer();
		if (key_over || key_bad)
			flag = SPEVB_TR_PPU;
		else
		{
			t0 = spu_read_decrementer();
			sort_keys(key_n);
			diag.t_sort += t0 - spu_read_decrementer();
			t0 = spu_read_decrementer();
			sort_pos = 0;
			ring_pass(sorted_next, proc_draw);
			diag.t_tr += t0 - spu_read_decrementer();
		}
	}
	else
		pass_list(SEC_TR, 0);
	if (flag == SPEVB_TR_DONE)
		run_close();
	publish(flag, flag == SPEVB_TR_DONE ? SEC_TR : -1);
	diag.n_strip += sec_strips;
	diag.n_vin += vin;
	diag.n_vout += vb_used;
	diag.kb_out += out_bytes >> 10;
	out_bytes &= 1023;
	diag_flush();
}
