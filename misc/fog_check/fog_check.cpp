// Plan 3a (WSL): ps3_rend.cpp's fog texture and scale (cut out by run.sh), read as fog_vp.vcg and fog_fp.fcg read
// them, against Flycast's fog_mode2 (d3d_shaders.cpp: a 128 x 2 table, floor and pow). Args: tables.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "types.h"
#include "ps3/ps3_rend_regs.h"

typedef unsigned GLuint;
RenderRegs g_rr;

#include "region.inc"

[[noreturn]] static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

static u32 rs = 0x2545F491;
static u32 rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static double rndf() { return rnd() / 4294967296.0; }

// FOG_DENSITY as Flycast FOG_DENSITY_type::get().
static double ref_density(u32 d)
{
	return ((d >> 8) & 0xFF) / 128.0 * pow(2.0, (double)(s8)(d & 0xFF));
}

// Flycast fog_mode2 on z = FOG_DENSITY / W, with the table's two texture rows as bits 15:8 and bits 7:0.
static double ref_fog(double z)
{
	if (!(z > 1.0))
		z = 1.0;
	if (z > 255.9999)
		z = 255.9999;
	const double e = floor(log2(z));
	const double m = z * 16.0 / pow(2.0, e) - 16.0;
	const int idx = (int)(floor(m) + e * 16.0);
	const double frac = m - floor(m);
	return ((g_rr.fog_table[idx] >> 8) * (1.0 - frac) + (g_rr.fog_table[idx] & 0xFF) * frac) / 255.0;
}

// The two programs in floats: texcoord0.z = W x scale, s = saturate(log2 x -0.125), then the RSX's bilinear read of
// row 0 (rows 0 and 1 are equal) with clamp to edge.
static double prog_fog(float w, float scale)
{
	const float fogc = w * scale;
	float s = log2f(fogc) * -0.125f;
	s = s > 0.0f ? (s < 1.0f ? s : 1.0f) : 0.0f;
	const float u = s * FOG_TEX_W - 0.5f;
	int i0 = (int)floorf(u);
	const float f = u - floorf(u);
	int i1 = i0 + 1;
	i0 = i0 < 0 ? 0 : i0 > FOG_TEX_W - 1 ? FOG_TEX_W - 1 : i0;
	i1 = i1 < 0 ? 0 : i1 > FOG_TEX_W - 1 ? FOG_TEX_W - 1 : i1;
	return (s_fog_pix[i0 * 4 + 3] * (1.0 - f) + s_fog_pix[i1 * 4 + 3] * f) / 255.0;
}

// The reference's lowest and highest value for z in [z0, z1]: it is linear between the table's 128 knots.
static void ref_range(double z0, double z1, double* lo, double* hi)
{
	*lo = 1e9;
	*hi = -1e9;
	auto take = [&](double z) { const double r = ref_fog(z); *lo = r < *lo ? r : *lo; *hi = r > *hi ? r : *hi; };
	take(z0);
	take(z1);
	for (int i = 0; i <= 128; i++)
	{
		const double knot = pow(2.0, i / 16) * (1.0 + (i % 16) / 16.0);
		if (knot > z0 && knot < z1)
		{
			take(knot * (1.0 - 1e-12));
			take(knot * (1.0 + 1e-12));
		}
	}
}

// A table as games make it: a fog curve over two octaves of z or more, each entry's low byte the next entry's value.
static void table_curve()
{
	const double lo = pow(2.0, rndf() * 6.0), hi = lo * pow(2.0, 2.0 + rndf() * 4.0), gamma = 1.0 + rndf() * 1.5;
	u32 val[129];
	for (int i = 0; i <= 128; i++)
	{
		const double z = pow(2.0, i / 16) * (1.0 + (i % 16) / 16.0);
		double t = (hi - z) / (hi - lo);
		t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
		val[i] = (u32)(pow(t, gamma) * 255.0 + 0.5);
	}
	for (int i = 0; i < 128; i++)
		g_rr.fog_table[i] = (u16)(val[i] << 8 | val[i + 1]);
}

static void table_random()
{
	for (int i = 0; i < 128; i++)
		g_rr.fog_table[i] = (u16)rnd();
}

int main(int argc, char** argv)
{
	const u32 tables = argc > 1 ? (u32)atoi(argv[1]) : 3000;
	fog_tex_init();
	double worst_curve = 0.0, worst_rand = 0.0;
	u64 samples = 0, clamped = 0;
	const double texel = 8.0 / FOG_TEX_W;
	for (u32 t = 0; t < tables; t++)
	{
		const bool curve = (t & 1) == 0;
		if (curve)
			table_curve();
		else
			table_random();
		g_rr.fog_col_ram = rnd();
		g_rr.fog_col_vert = rnd();
		fog_tex_pixels();

		// The colours: rows 0-1 FOG_COL_RAM with the table, rows 2-3 FOG_COL_VERT, each pair of rows equal.
		const u32 row = FOG_TEX_W * 4;
		if (memcmp(s_fog_pix, s_fog_pix + row, row) != 0 || memcmp(s_fog_pix + 2 * row, s_fog_pix + 3 * row, row) != 0)
			die("table %u: the rows of a pair differ", t);
		for (u32 k = 0; k < FOG_TEX_W; k++)
		{
			const u8* p = &s_fog_pix[k * 4];
			const u8* q = p + 2 * row;
			const u32 ram = g_rr.fog_col_ram, vert = g_rr.fog_col_vert;
			if (p[0] != (u8)(ram >> 16) || p[1] != (u8)(ram >> 8) || p[2] != (u8)ram)
				die("table %u texel %u: FOG_COL_RAM %06X gives %02X %02X %02X", t, k, ram & 0xFFFFFF, p[0], p[1], p[2]);
			if (q[0] != (u8)(vert >> 16) || q[1] != (u8)(vert >> 8) || q[2] != (u8)vert || q[3] != 255)
				die("table %u texel %u: FOG_COL_VERT %06X gives %02X %02X %02X %02X", t, k, vert & 0xFFFFFF, q[0], q[1], q[2], q[3]);
		}

		for (u32 n = 0; n < 400; n++)
		{
			// Densities from 2^-6 to 2^13, and a W that puts z between 1/4 and 1024: in the table and past both ends.
			const u32 d = (1 + rnd() % 255) << 8 | ((u32)((int)(rnd() % 20) - 6) & 0xFF);
			const float scale = fog_scale(d);
			const double den = ref_density(d);
			if (fabs(scale * den - 1.0) > 1e-5)
				die("density %04X: scale %g, reference 1 / %g", d, scale, den);
			const float w = (float)(den / pow(2.0, -2.0 + rndf() * 12.0));
			const double z = den / w;
			const double got = prog_fog(w, scale);
			samples++;
			if (z <= 1.0 || z >= 255.9999)
				clamped++;
			// The read mixes two texels at most one texel away: the value must lie between the reference's values there.
			double lo, hi;
			ref_range(z * pow(2.0, -1.02 * texel), z * pow(2.0, 1.02 * texel), &lo, &hi);
			const double out = got < lo ? lo - got : got > hi ? got - hi : 0.0;
			if (out > 1.0 / 255.0)
				die("table %u (%s) density %04X W %g: z %g gives %g, the reference within a texel is [%g, %g]",
				    t, curve ? "curve" : "random", d, w, z, got * 255.0, lo * 255.0, hi * 255.0);
			const double err = fabs(got - ref_fog(z)) * 255.0;
			if (curve)
				worst_curve = err > worst_curve ? err : worst_curve;
			else
				worst_rand = err > worst_rand ? err : worst_rand;
		}
		// A fog curve has no jump, so the texture must follow it to 2 of 255.
		if (curve && worst_curve > 2.0)
			die("table %u: a fog curve is off by %g of 255", t, worst_curve);
	}

	// The ends: no density, the largest one, and W at the decoder's limits.
	table_random();
	fog_tex_pixels();
	const double far_end = (g_rr.fog_table[0] >> 8) / 255.0, near_end = ref_fog(255.9999);
	const struct { u32 d; float w; double want; const char* name; } ends[] = {
		{ 0x0000, 1.0f, far_end, "density 0" }, { 0x0005, 1.0f, far_end, "mantissa 0" },
		{ 0xFF7F, 1.0f, near_end, "the largest density" }, { 0x8080, 1.0f, far_end, "the smallest exponent" },
		{ 0xFF00, 1e9f, far_end, "W at the 1/W floor" }, { 0x8007, 1e-30f, near_end, "a W near 0" },
	};
	for (const auto& e : ends)
	{
		const float scale = fog_scale(e.d);
		if (!(scale >= 1e-20f && scale <= 1e20f))
			die("%s: scale %g is not kept finite", e.name, scale);
		const double got = prog_fog(e.w, scale);
		if (fabs(got - e.want) > 1.5 / 255.0)
			die("%s: %g, the table's end is %g", e.name, got * 255.0, e.want * 255.0);
	}
	printf("PASS: %u tables, %llu lookups (%llu past the table's ends); worst difference from fog_mode2: %.2f of 255 "
	       "on fog curves, %.2f on random tables (jumps blur over one texel)\n",
	       tables, (unsigned long long)samples, (unsigned long long)clamped, worst_curve, worst_rand);
	return 0;
}
