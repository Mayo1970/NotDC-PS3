// P18 (WSL): ps3/ps3_depth.h on a model of the RSX depth path (dp4, divide, viewport, cull, store), in 8 model
// variants. FLOAT must keep every DC compare at 2^-14 apart; FIXED must stay as it was. DEPTH_SOURCE: a mutant.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DEPTH_SOURCE
#include DEPTH_SOURCE
#else
#include "ps3/ps3_depth.h"
#endif
// P91: the decoders' WIDE near-plane cap (INVW_CAP_WIDE). BELL_SOURCE: a mutant of spe_bell.h.
#ifdef BELL_SOURCE
#include BELL_SOURCE
#else
#include "ps3/spu/spe_bell.h"
#endif

// ps3_rend.cpp: set_pos's 1/W floor and update_fz's cap on the frame's largest 1/W.
static const float INV_W_FLOOR = 1e-9f;
static const float MAX_INV_W = 1048576.0f;

// Hardware unknowns: rcp + multiply or a divide, a fused viewport z, rounding or truncation on store.
struct Model { bool rcp, fused, round; };

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd32()
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 7;
	s_rng ^= s_rng << 17;
	return (uint32_t)(s_rng >> 32);
}
static double urand() { return (rnd32() >> 8) / 16777216.0; }
static double lrand(double lo, double hi) { return exp(log(lo) + (log(hi) - log(lo)) * urand()); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// What draw_scene and ps3gl_apply_states make of depth_proj: clip z row, depth range, viewport z scale and offset.
struct Pipe { bool fz; float m10, m14, zn, zf, scale, offset; };

static Pipe make_pipe(bool fz, float n)
{
	Pipe p;
	p.fz = fz;
	depth_proj(fz, n, &p.m10, &p.m14, &p.zn);
	p.zf = 1.0f;
	p.scale = (p.zf - p.zn) * 0.5f;
	p.offset = (p.zf + p.zn) * 0.5f;
	return p;
}

// The plan's first sketch: z = n/W through GL's remap (m[10] = -1, m[14] = 2n, depth range 0..1).
static Pipe plan_pipe(float n)
{
	Pipe p;
	p.fz = true;
	p.m10 = -1.0f;
	p.m14 = 2.0f * n;
	p.zn = 0.0f;
	p.zf = 1.0f;
	p.scale = 0.5f;
	p.offset = 0.5f;
	return p;
}

// Window z of a vertex with this 1/W, as set_pos stores W and the RSX transforms it.
static float window_z(const Pipe& p, const Model& md, float inv)
{
	volatile float w = 1.0f / inv;
	volatile float xy = 0.0f;   // m[2] * X + m[6] * Y, both 0 for finite x, y
	volatile float t = p.m10 * w;
	volatile float cz = (xy + t) + p.m14;
	float ndc;
	if (md.rcp)
	{
		// A deterministic rcp error of up to 2 ulp, so equal inputs keep equal depth.
		volatile float r = 1.0f / w;
		int k = (int)((fbits(w) * 2654435761u) >> 29) % 5 - 2;
		float rr = r;
		for (; k > 0; k--) rr = nextafterf(rr, INFINITY);
		for (; k < 0; k++) rr = nextafterf(rr, 0.0f);
		ndc = cz * rr;
	}
	else
		ndc = cz / w;
	volatile float prod = ndc * p.scale;
	return md.fused ? fmaf(ndc, p.scale, p.offset) : prod + p.offset;
}

// FLOAT's z must not be culled by the depth range nor need a clamp to [0, 1]. FIXED (the OFF path, unchanged) can
// pass 1 by an ulp at W = 1e9 in this model, so its store clamps instead.
static bool inside(const Pipe& p, float win)
{
	return win >= fminf(p.zn, p.zf) && win <= fmaxf(p.zn, p.zf) && win >= 0.0f && win <= 1.0f;
}

// FIXED: Z24 of the window z (computed in double, a best case). FLOAT: f32 bits >> 7.
static uint32_t store(bool fz, const Model& md, float win)
{
	if (!fz)
	{
		double s = (win < 0.0f ? 0.0 : win > 1.0f ? 1.0 : (double)win) * 16777215.0;
		return md.round ? (uint32_t)llrint(s) : (uint32_t)s;
	}
	uint32_t b = fbits(win);
	if (md.round)
		b += 0x40;
	return (b >> 7) & 0xFFFFFF;
}

static bool gl_cmp(unsigned f, uint32_t in, uint32_t st)
{
	switch (f)
	{
	case 0: return false;
	case 1: return in < st;
	case 2: return in == st;
	case 3: return in <= st;
	case 4: return in > st;
	case 5: return in != st;
	case 6: return in >= st;
	default: return true;
	}
}

// The PVR's test on 1/W, new against stored, in DepthMode order.
static bool dc_cmp(unsigned m, float in, float st)
{
	switch (m)
	{
	case 0: return false;
	case 1: return in < st;
	case 2: return in == st;
	case 3: return in <= st;
	case 4: return in > st;
	case 5: return in != st;
	case 6: return in >= st;
	default: return true;
	}
}

static int s_fail;
static void check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		s_fail++;
}

// draw_scene: n = 1 / (largest 1/W x 1.001).
static float near_of(float max_inv) { return 1.0f / (max_inv * 1.001f); }

struct Range
{
	const char* name;
	double inv_lo, inv_hi;   // the frame's largest 1/W
	double w_lo, w_hi;       // the stored polygon's W
	double r_lo, r_hi;       // relative 1/W gap of the new polygon
	bool equal;              // the new polygon has the stored one's 1/W
};

struct Tally { long n, bad, cull; double bad_gap_max; };

static Tally run_range(const Range& rg, const Model& md, bool plan, bool fz, long samples)
{
	Tally t = { 0, 0, 0, 0.0 };
	for (long i = 0; i < samples; i++)
	{
		float max_inv = (float)lrand(rg.inv_lo, rg.inv_hi);
		float n = near_of(max_inv);
		Pipe p = plan ? plan_pipe(n) : make_pipe(fz, n);
		float inv_st = (float)(1.0 / lrand(rg.w_lo, rg.w_hi));
		if (inv_st > max_inv) inv_st = max_inv;
		if (inv_st < INV_W_FLOOR) inv_st = INV_W_FLOOR;
		float inv_in = inv_st;
		if (!rg.equal)
		{
			double r = lrand(rg.r_lo, rg.r_hi);
			inv_in = (float)(inv_st * ((rnd32() & 1) ? 1.0 + r : 1.0 / (1.0 + r)));
			if (inv_in > max_inv) inv_in = max_inv;
			if (inv_in < INV_W_FLOOR) inv_in = INV_W_FLOOR;
		}
		float z_st = window_z(p, md, inv_st), z_in = window_z(p, md, inv_in);
		if (p.fz && (!inside(p, z_st) || !inside(p, z_in)))
		{
			t.cull++;
			continue;
		}
		uint32_t q_st = store(p.fz, md, z_st), q_in = store(p.fz, md, z_in);
		double gap = fabs((double)inv_in / inv_st - 1.0);
		t.n++;
		for (unsigned m = 0; m < 8; m++)
			if (gl_cmp(depth_cmp(p.fz, m), q_in, q_st) != dc_cmp(m, inv_in, inv_st))
			{
				t.bad++;
				if (gap > t.bad_gap_max) t.bad_gap_max = gap;
				break;
			}
	}
	return t;
}

static const Model s_models[8] = {
	{ false, false, false }, { false, false, true }, { false, true, false }, { false, true, true },
	{ true, false, false }, { true, false, true }, { true, true, false }, { true, true, true },
};

int main(int argc, char** argv)
{
	long samples = argc > 1 ? atol(argv[1]) : 200000;
	char buf[256];

	// Every frame range the renderer allows, gaps from 2^-14 (FLOAT keeps 16 mantissa bits).
	const Range all = { "all ranges, gap >= 2^-14", 1.0, MAX_INV_W, 1e-6, 1e9, 1.0 / 16384, 1.0, false };
	const Range eq = { "equal 1/W", 1.0, MAX_INV_W, 1e-6, 1e9, 0, 0, true };
	// FIXED where its 2^-24 step is fine: near 0.05-1, W 1-100, gaps from 1%.
	const Range safe = { "near 0.05-1, W 1-100, gap >= 1%", 1.0, 20.0, 1.0, 100.0, 0.01, 1.0, false };
	// Logs (ps3log_p16_run1): Soul Calibur near median 0.000473, stage W to 2083-10000; Crazy Taxi near 0.05, W to 8491.
	const Range sc = { "Soul Calibur stage (near 0.000473, W 500-10000, gap 0.1-10%)", 2112.0, 2112.0, 500.0, 1e4,
	                   1e-3, 0.1, false };
	const Range ct = { "Crazy Taxi far (near 0.05, W 1000-8500, gap 0.1-10%)", 19.98, 19.98, 1000.0, 8500.0, 1e-3,
	                   0.1, false };

	long bad_float = 0, cull_any = 0;
	double worst_gap = 0;
	long fixed_sc_bad = 0, fixed_sc_n = 0, fixed_ct_bad = 0, fixed_ct_n = 0, plan_bad = 0, plan_n = 0;
	long safe_bad = 0, eq_bad = 0, sc_float_bad = 0, ct_float_bad = 0;
	for (int v = 0; v < 8; v++)
	{
		const Model& md = s_models[v];
		Tally a = run_range(all, md, false, true, samples);
		bad_float += a.bad;
		cull_any += a.cull;
		if (a.bad_gap_max > worst_gap) worst_gap = a.bad_gap_max;
		for (int fz = 0; fz < 2; fz++)
		{
			Tally e = run_range(eq, md, false, fz != 0, samples / 4);
			eq_bad += e.bad;
			cull_any += e.cull;
		}
		Tally s = run_range(safe, md, false, false, samples / 4);
		safe_bad += s.bad;
		cull_any += s.cull;
		Tally scf = run_range(sc, md, false, true, samples / 4), scx = run_range(sc, md, false, false, samples / 4);
		Tally ctf = run_range(ct, md, false, true, samples / 4), ctx = run_range(ct, md, false, false, samples / 4);
		Tally pl = run_range(sc, md, true, true, samples / 4);
		sc_float_bad += scf.bad;
		ct_float_bad += ctf.bad;
		fixed_sc_bad += scx.bad;
		fixed_sc_n += scx.n;
		fixed_ct_bad += ctx.bad;
		fixed_ct_n += ctx.n;
		plan_bad += pl.bad;
		plan_n += pl.n;
		cull_any += scf.cull + scx.cull + ctf.cull + ctx.cull;
	}
	snprintf(buf, sizeof(buf), "FLOAT, %s: %ld wrong compares (largest gap wrong %.3g)", all.name, bad_float, worst_gap);
	check(bad_float == 0, buf);
	snprintf(buf, sizeof(buf), "FIXED and FLOAT, %s: %ld wrong compares", eq.name, eq_bad);
	check(eq_bad == 0, buf);
	snprintf(buf, sizeof(buf), "FIXED (OFF path), %s: %ld wrong compares", safe.name, safe_bad);
	check(safe_bad == 0, buf);
	snprintf(buf, sizeof(buf), "FLOAT, %s: %ld wrong; FIXED %.1f%% of pairs wrong", sc.name, sc_float_bad,
	         100.0 * fixed_sc_bad / (fixed_sc_n ? fixed_sc_n : 1));
	check(sc_float_bad == 0 && fixed_sc_bad > 0, buf);
	snprintf(buf, sizeof(buf), "FLOAT, %s: %ld wrong; FIXED %.2f%% of pairs wrong", ct.name, ct_float_bad,
	         100.0 * fixed_ct_bad / (fixed_ct_n ? fixed_ct_n : 1));
	check(ct_float_bad == 0, buf);
	snprintf(buf, sizeof(buf), "FLOAT: no vertex culled at 1/W in [1e-9, largest 1/W]: %ld culled", cull_any);
	check(cull_any == 0, buf);
	printf("INFO: the plan's sketch (m[10] = -1, m[14] = 2n, GL remap) on FLOAT, Soul Calibur stage: %.1f%% of pairs "
	       "wrong\n", 100.0 * plan_bad / (plan_n ? plan_n : 1));

	// The mapping itself, against z = n/W and z = 1 - n/W in double from the same float n and W.
	long map_bad[2] = { 0, 0 };
	for (int v = 0; v < 8; v++)
		for (long i = 0; i < samples / 8; i++)
		{
			float max_inv = (float)lrand(1.0, MAX_INV_W);
			float n = near_of(max_inv);
			float inv = (float)lrand(INV_W_FLOOR, max_inv);
			for (int fz = 0; fz < 2; fz++)
			{
				Pipe p = make_pipe(fz != 0, n);
				float z = window_z(p, s_models[v], inv);
				if (fz && !inside(p, z))
				{
					map_bad[fz]++;
					continue;
				}
				double w = (double)(1.0f / inv);
				double want = fz ? n / w : 1.0 - n / w;
				bool ok = fz ? fabs(z / want - 1.0) <= ldexp(1.0, -20) : fabs(z - want) <= ldexp(1.0, -21);
				if (!ok)
					map_bad[fz]++;
			}
		}
	snprintf(buf, sizeof(buf), "window z = n/W (FLOAT, rel 2^-20): %ld off; = 1 - n/W (FIXED, abs 2^-21): %ld off",
	         map_bad[1], map_bad[0]);
	check(map_bad[0] == 0 && map_bad[1] == 0, buf);

	// Both ends of the frame's range land inside FLOAT's depth range, not culled.
	long ends_bad = 0;
	const float maxes[] = { 1.0f, 2.0f, 1000.0f, 2112.0f, 10240.0f, MAX_INV_W * 0.999f };
	for (int v = 0; v < 8; v++)
		for (float mi : maxes)
		{
			Pipe p = make_pipe(true, near_of(mi));
			ends_bad += !inside(p, window_z(p, s_models[v], mi));
			ends_bad += !inside(p, window_z(p, s_models[v], INV_W_FLOOR));
		}
	snprintf(buf, sizeof(buf), "FLOAT: 1/W = largest and 1/W = 1e-9 inside the depth range: %ld culled", ends_bad);
	check(ends_bad == 0, buf);

	// P91 Near Plane Cap WIDE: FLOAT with the largest 1/W up to the decoders' cap; Re-Volt's menu frame (HW 2026-10-15:
	// menu 1/W 1.7M-2.5M over a 3D scene at 2.1-467). FIXED keeps the old cap.
	const uint32_t cap_bits = INVW_CAP_WIDE;
	float wide_max;
	memcpy(&wide_max, &cap_bits, 4);
	const Range wide = { "largest 1/W 2^20 to the WIDE cap, gap >= 2^-14", MAX_INV_W, wide_max, 1e-14, 1e9, 1.0 / 16384,
	                     1.0, false };
	const Range rv = { "Re-Volt menu (largest 1/W 2.5M, W 4e-7 to 0.47, gap 0.1-10%)", 2.5e6, 2.5e6, 4e-7, 0.47, 1e-3, 0.1,
	                   false };
	long wide_bad = 0, rv_bad = 0, wide_cull = 0, wide_map = 0, wide_clear = 0;
	double wide_gap = 0;
	for (int v = 0; v < 8; v++)
	{
		const Model& md = s_models[v];
		Tally a = run_range(wide, md, false, true, samples / 2), b = run_range(rv, md, false, true, samples / 4);
		wide_bad += a.bad;
		rv_bad += b.bad;
		wide_cull += a.cull + b.cull;
		if (a.bad_gap_max > wide_gap) wide_gap = a.bad_gap_max;
		for (long i = 0; i < samples / 8; i++)
		{
			float max_inv = (float)lrand(MAX_INV_W, wide_max);
			float n = near_of(max_inv);
			float inv = (float)lrand(INV_W_FLOOR, max_inv);
			Pipe p = make_pipe(true, n);
			float z = window_z(p, md, inv);
			double want = n / (double)(1.0f / inv);
			if (!inside(p, z) || fabs(z / want - 1.0) > ldexp(1.0, -20))
				wide_map++;
			if (!gl_cmp(depth_cmp(true, 4), store(true, md, z), 0))
				wide_clear++;
		}
		const float wide_ends[2] = { wide_max * 0.999f, 2.5e6f };
		for (float mi : wide_ends)
		{
			Pipe p = make_pipe(true, near_of(mi));
			wide_cull += !inside(p, window_z(p, md, mi)) + !inside(p, window_z(p, md, INV_W_FLOOR));
		}
	}
	snprintf(buf, sizeof(buf), "P91 FLOAT, %s: %ld wrong compares (largest gap wrong %.3g)", wide.name, wide_bad, wide_gap);
	check(wide_bad == 0, buf);
	snprintf(buf, sizeof(buf), "P91 FLOAT, %s: %ld wrong compares", rv.name, rv_bad);
	check(rv_bad == 0, buf);
	snprintf(buf, sizeof(buf), "P91 FLOAT to the WIDE cap: z = n/W (rel 2^-20) off %ld, not over the clear %ld, culled %ld",
	         wide_map, wide_clear, wide_cull);
	check(wide_map == 0 && wide_clear == 0 && wide_cull == 0, buf);

	// The clear is depth at 1/W = 0: ps3gl writes (u32)(d * 0xFFFFFF) << 8, which must be FLOAT's 0.0 as well.
	const Model& m0 = s_models[0];
	uint32_t ps3gl_float = (uint32_t)(depth_clear(true) * 0xFFFFFF), ps3gl_fixed = (uint32_t)(depth_clear(false) * 0xFFFFFF);
	snprintf(buf, sizeof(buf), "clear: FLOAT raw %06X (float encoding %06X), FIXED raw %06X", (unsigned)ps3gl_float,
	         (unsigned)store(true, m0, depth_clear(true)), (unsigned)ps3gl_fixed);
	check(ps3gl_float == 0 && store(true, m0, depth_clear(true)) == 0 && ps3gl_fixed == 0xFFFFFF, buf);
	long clear_bad = 0, fixed_ties = 0, clear_n = 0;
	for (int v = 0; v < 8; v++)
		for (long i = 0; i < samples / 8; i++)
		{
			float max_inv = (float)lrand(1.0, MAX_INV_W);
			float inv = (float)lrand(INV_W_FLOOR, max_inv);
			Pipe pf = make_pipe(true, near_of(max_inv)), px = make_pipe(false, near_of(max_inv));
			float zf = window_z(pf, s_models[v], inv), zx = window_z(px, s_models[v], inv);
			clear_n++;
			if (!inside(pf, zf) || !gl_cmp(depth_cmp(true, 4), store(true, s_models[v], zf), ps3gl_float))
				clear_bad++;
			if (store(false, s_models[v], zx) == ps3gl_fixed)
				fixed_ties++;
		}
	snprintf(buf, sizeof(buf), "FLOAT: a polygon passes \"greater\" over the clear: %ld of %ld fail (FIXED ties the "
	         "clear in %.1f%%)", clear_bad, clear_n, 100.0 * fixed_ties / clear_n);
	check(clear_bad == 0, buf);

	// The tables: FLOAT keeps each DC test, FIXED flips less/greater, and NEVER/EQUAL/NOTEQUAL/ALWAYS stay.
	static const unsigned flip[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
	bool tab_ok = true;
	for (unsigned m = 0; m < 16; m++)
		tab_ok = tab_ok && depth_cmp(true, m) == (m & 7) && depth_cmp(false, m) == flip[m & 7];
	check(tab_ok, "depth_cmp: FLOAT = DepthMode, FIXED = DepthMode with less/greater swapped");

	printf(s_fail ? "%d FAILED\n" : "ALL PASS\n", s_fail);
	return s_fail ? 1 : 0;
}
