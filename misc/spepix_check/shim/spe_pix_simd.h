#pragma once
// 5.12 #4/#5 PC check: spe_pix_simd.h on x86. A q_t holds the SPU's words as numbers (LS holds host words): byte k is
// byte k & 3 of word k >> 2, most significant first; halfword k the high (k even) or low half of word k >> 1.
#include <stdint.h>
#include <string.h>

void shim_die(const char* why, uint32_t a, uint32_t b);

typedef struct { uint32_t w[4]; } q_t;

#define QW(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define QI(b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15) \
	{ { QW(b0, b1, b2, b3), QW(b4, b5, b6, b7), QW(b8, b9, b10, b11), QW(b12, b13, b14, b15) } }

static inline uint32_t sh_b(q_t v, int k) { return (v.w[k >> 2] >> (24 - 8 * (k & 3))) & 0xFF; }
static inline uint32_t sh_h(q_t v, int k) { return (v.w[k >> 1] >> ((k & 1) ? 0 : 16)) & 0xFFFF; }
static inline void sh_seth(q_t* v, int k, uint32_t x)
{
	const int s = (k & 1) ? 0 : 16;
	v->w[k >> 1] = (v->w[k >> 1] & ~(0xFFFFu << s)) | ((x & 0xFFFFu) << s);
}

static inline q_t q_words(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	q_t r = { { a, b, c, d } };
	return r;
}
// lqd/stqd drop the low 4 address bits; code that relies on that is a bug here.
static inline q_t q_ld(const void* p)
{
	if ((uintptr_t)p & 15)
		shim_die("unaligned quadword load", (uint32_t)(uintptr_t)p, 0);
	q_t r;
	memcpy(&r, p, 16);
	return r;
}
static inline void q_st(void* p, q_t v)
{
	if ((uintptr_t)p & 15)
		shim_die("unaligned quadword store", (uint32_t)(uintptr_t)p, 0);
	memcpy(p, &v, 16);
}

// shufb: pattern bytes 10xxxxxx give 0x00, 110xxxxx 0xFF, 111xxxxx 0x80; others pick byte & 31 of a:b.
static inline q_t q_shuf(q_t a, q_t b, q_t m)
{
	q_t r = { { 0, 0, 0, 0 } };
	for (int k = 0; k < 16; k++)
	{
		const uint32_t s = sh_b(m, k);
		const uint32_t v = s >= 0xE0 ? 0x80 : s >= 0xC0 ? 0xFF : s >= 0x80 ? 0 : sh_b((s & 16) ? b : a, s & 15);
		r.w[k >> 2] |= v << (24 - 8 * (k & 3));
	}
	return r;
}

static inline q_t q_and(q_t a, q_t b)
{
	for (int i = 0; i < 4; i++)
		a.w[i] &= b.w[i];
	return a;
}
static inline q_t q_or(q_t a, q_t b)
{
	for (int i = 0; i < 4; i++)
		a.w[i] |= b.w[i];
	return a;
}
static inline q_t q_andc(q_t a, q_t b)
{
	for (int i = 0; i < 4; i++)
		a.w[i] &= ~b.w[i];
	return a;
}
// selb: bits of b where m is 1.
static inline q_t q_sel(q_t a, q_t b, q_t m)
{
	for (int i = 0; i < 4; i++)
		a.w[i] = (a.w[i] & ~m.w[i]) | (b.w[i] & m.w[i]);
	return a;
}
static inline q_t w_splat(uint32_t v) { return q_words(v, v, v, v); }
static inline q_t h_splat(uint16_t v) { return w_splat((uint32_t)v << 16 | v); }
static inline uint32_t w_extract(q_t v, int i) { return v.w[i & 3]; }

static inline q_t w_add(q_t a, q_t b)
{
	for (int i = 0; i < 4; i++)
		a.w[i] += b.w[i];
	return a;
}

// shli / shlhi / rothmi / rotmahi with a constant count, as the code uses them (count below the lane width).
static inline q_t w_sll(q_t v, int n)
{
	for (int i = 0; i < 4; i++)
		v.w[i] <<= n;
	return v;
}
#define H_MAP(expr)                                   \
	q_t r = v;                                        \
	for (int k = 0; k < 8; k++)                       \
	{                                                 \
		const uint32_t x = sh_h(v, k);                \
		sh_seth(&r, k, (expr));                       \
	}                                                 \
	return r;
static inline q_t h_sll(q_t v, int n) { H_MAP(x << n) }
static inline q_t h_srl(q_t v, int n) { H_MAP(x >> n) }
static inline q_t h_sra(q_t v, int n) { H_MAP((uint32_t)((int32_t)(int16_t)x >> n)) }
static inline q_t h_and(q_t v, uint16_t m) { H_MAP(x & m) }
static inline q_t b_and(q_t v, uint8_t m) { return q_and(v, w_splat(0x01010101u * m)); }
static inline q_t h_add(q_t a, q_t b)
{
	q_t r = a;
	for (int k = 0; k < 8; k++)
		sh_seth(&r, k, sh_h(a, k) + sh_h(b, k));
	return r;
}
static inline q_t h_sub(q_t a, q_t b)
{
	q_t r = a;
	for (int k = 0; k < 8; k++)
		sh_seth(&r, k, sh_h(a, k) - sh_h(b, k));
	return r;
}
static inline q_t h_cmpgt(q_t a, q_t b)
{
	q_t r = a;
	for (int k = 0; k < 8; k++)
		sh_seth(&r, k, (int16_t)sh_h(a, k) > (int16_t)sh_h(b, k) ? 0xFFFF : 0);
	return r;
}
