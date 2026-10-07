#pragma once
// 5.12 #2 PC check: the SPU intrinsics spe_vb_build.h uses, with the SPU's float rules. Inputs the SPU would
// read differently (denormals, exponent 255) and frds results it would flush abort the run.
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef float vec_float4 __attribute__((vector_size(16)));
typedef uint32_t vec_uint4 __attribute__((vector_size(16)));
typedef int32_t vec_int4 __attribute__((vector_size(16)));
typedef uint8_t vec_uchar16 __attribute__((vector_size(16)));
typedef double vec_double2 __attribute__((vector_size(16)));

void shim_die(const char* why, uint32_t a, uint32_t b);
// Relative error of the frest + fi estimate: the check runs with up to 2^-12 (shim_re_err sets the scale).
extern double shim_re_err;

static inline uint32_t shim_fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float shim_bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static inline vec_uint4 shim_splats_u(uint32_t x) { return (vec_uint4){ x, x, x, x }; }
static inline vec_double2 shim_splats_d(double x) { return (vec_double2){ x, x }; }
#define spu_splats(x) _Generic((x), double: shim_splats_d, default: shim_splats_u)(x)
#define spu_extract(v, i) ((v)[i])
#define spu_promote(x, i) ((vec_uint4){ (x), 0, 0, 0 })
#define spu_insert(x, v, i) ({ vec_uint4 _t = (v); _t[i] = (x); _t; })

// fesd: elements 0 and 2. The SPU reads a denormal as 0 and exponent 255 as a number.
static inline double shim_fesd(uint32_t b)
{
	const uint32_t e = (b >> 23) & 0xFF;
	if ((e == 0 && (b << 1)) || e == 255)
		shim_die("spu_extend of a denormal or Inf/NaN", b, 0);
	return (double)shim_bitsf(b);
}
static inline vec_double2 spu_extend(vec_float4 a)
{
	const vec_uint4 u = (vec_uint4)a;
	return (vec_double2){ shim_fesd(u[0]), shim_fesd(u[2]) };
}

// frds, round to nearest: the SPU flushes results below FLT_MIN and has no Inf, so the code must not need them.
// 0x1.ffffffp127 = 2^128 - 2^103, where IEEE rounding turns to Inf.
static inline uint32_t shim_frds(double d)
{
	const double a = fabs(d);
	if (d != 0.0 && (a < 0x1p-126 || a >= 0x1.ffffffp127))
		shim_die("spu_roundtf outside the normal range", (uint32_t)(a < 1 ? 0 : 1), 0);
	return shim_fbits((float)d);
}
static inline vec_float4 spu_roundtf(vec_double2 a)
{
	return (vec_float4)(vec_uint4){ shim_frds(a[0]), 0, shim_frds(a[1]), 0 };
}

static inline vec_double2 spu_mul(vec_double2 a, vec_double2 b) { return a * b; }
static inline vec_double2 shim_add_d(vec_double2 a, vec_double2 b) { return a + b; }
static inline vec_uint4 shim_add_u(vec_uint4 a, vec_uint4 b) { return a + b; }
#define spu_add(a, b) _Generic((a), vec_double2: shim_add_d, default: shim_add_u)(a, b)

// Word vectors hold host words: SPU byte k is byte k & 3 of word k >> 2, most significant first, so a shuffle
// sees what the SPU sees for any value built from words. Patterns are byte arrays.
static inline uint32_t shim_byte(vec_uint4 v, int k) { return (v[k >> 2] >> (24 - 8 * (k & 3))) & 0xFF; }
static inline vec_uint4 spu_shuffle(vec_uint4 a, vec_uint4 b, vec_uchar16 m)
{
	vec_uint4 r = { 0, 0, 0, 0 };
	for (int k = 0; k < 16; k++)
	{
		const uint32_t s = m[k];
		const uint32_t v = s >= 0xE0 ? 0x80 : s >= 0xC0 ? 0xFF : s >= 0x80 ? 0 : shim_byte((s & 16) ? b : a, s & 15);
		r[k >> 2] |= v << (24 - 8 * (k & 3));
	}
	return r;
}
static inline vec_uint4 spu_rlqwbyte(vec_uint4 a, int n)
{
	vec_uint4 r = { 0, 0, 0, 0 };
	for (int k = 0; k < 16; k++)
		r[k >> 2] |= shim_byte(a, (k + n) & 15) << (24 - 8 * (k & 3));
	return r;
}
static inline vec_uint4 spu_sub(vec_uint4 a, vec_uint4 b) { return a - b; }
static inline vec_uint4 spu_sel(vec_uint4 a, vec_uint4 b, vec_uint4 m) { return (a & ~m) | (b & m); }
static inline vec_uint4 spu_and(vec_uint4 a, vec_uint4 b) { return a & b; }
static inline vec_uint4 spu_andc(vec_uint4 a, vec_uint4 b) { return a & ~b; }
static inline vec_uint4 spu_or(vec_uint4 a, vec_uint4 b) { return a | b; }
static inline vec_uint4 spu_xor(vec_uint4 a, vec_uint4 b) { return a ^ b; }
static inline vec_uint4 spu_cmpeq(vec_uint4 a, vec_uint4 b) { return (vec_uint4)(a == b); }
static inline vec_uint4 shim_cmpgt_u(vec_uint4 a, vec_uint4 b) { return (vec_uint4)(a > b); }
static inline vec_uint4 shim_cmpgt_s(vec_int4 a, vec_int4 b) { return (vec_uint4)(a > b); }
#define spu_cmpgt(a, b) _Generic((a), vec_int4: shim_cmpgt_s, default: shim_cmpgt_u)(a, b)
// rotmi: a negative count shifts right, logical.
static inline vec_uint4 spu_rlmask(vec_uint4 a, int n)
{
	if (n > 0 || n < -31)
		shim_die("spu_rlmask count", (uint32_t)n, 0);
	return a >> (uint32_t)-n;
}
static inline vec_uint4 spu_sl(vec_uint4 a, uint32_t n) { return a << n; }
static inline vec_uint4 spu_orx(vec_uint4 a) { return (vec_uint4){ a[0] | a[1] | a[2] | a[3], 0, 0, 0 }; }
static inline vec_double2 spu_madd(vec_double2 a, vec_double2 b, vec_double2 c)
{
	return (vec_double2){ fma(a[0], b[0], c[0]), fma(a[1], b[1], c[1]) };
}
static inline vec_double2 spu_nmsub(vec_double2 a, vec_double2 b, vec_double2 c)
{
	return (vec_double2){ fma(-a[0], b[0], c[0]), fma(-a[1], b[1], c[1]) };
}

static inline uint32_t shim_re1(uint32_t b)
{
	const uint32_t e = (b >> 23) & 0xFF;
	if (e == 0 || e == 255)
		shim_die("spu_re of 0, a denormal or Inf/NaN", b, 0);
	// A fixed pseudo-random error per input, so runs repeat.
	uint32_t h = b * 0x9E3779B1u;
	h ^= h >> 15;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	const double r = ((double)h / 4294967295.0) * 2.0 - 1.0;
	return shim_fbits((float)((1.0 / shim_bitsf(b)) * (1.0 + r * shim_re_err)));
}
static inline vec_float4 spu_re(vec_float4 a)
{
	const vec_uint4 u = (vec_uint4)a;
	return (vec_float4)(vec_uint4){ shim_re1(u[0]), shim_re1(u[1]), shim_re1(u[2]), shim_re1(u[3]) };
}

// cuflt with scale 0; the code only converts vertex counts (< 2^24, exact).
static inline vec_float4 spu_convtf(vec_uint4 a, int scale)
{
	(void)scale;
	return (vec_float4){ (float)a[0], (float)a[1], (float)a[2], (float)a[3] };
}
