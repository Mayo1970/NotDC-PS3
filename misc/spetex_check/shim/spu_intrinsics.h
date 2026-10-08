#pragma once
// 5.12 #3 PC check: the SPU intrinsics spe_tex_hash.h uses. Word vectors hold host words: SPU byte k is byte k & 3 of
// word k >> 2, most significant first, and a quadword shift works on the 128-bit number w0:w1:w2:w3.
#include <stdint.h>
#include <string.h>

typedef uint32_t vec_uint4 __attribute__((vector_size(16)));
typedef uint8_t vec_uchar16 __attribute__((vector_size(16)));

#define spu_extract(v, i) ((v)[i])

static inline vec_uint4 spu_add(vec_uint4 a, vec_uint4 b) { return a + b; }
static inline vec_uint4 spu_and(vec_uint4 a, vec_uint4 b) { return a & b; }
static inline vec_uint4 spu_xor(vec_uint4 a, vec_uint4 b) { return a ^ b; }
static inline vec_uint4 spu_sel(vec_uint4 a, vec_uint4 b, vec_uint4 m) { return (a & ~m) | (b & m); }

// cg: the carry out of each word sum.
static inline vec_uint4 spu_genc(vec_uint4 a, vec_uint4 b)
{
	vec_uint4 r;
	for (int i = 0; i < 4; i++)
		r[i] = (uint32_t)(((uint64_t)a[i] + b[i]) >> 32);
	return r;
}

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

static inline unsigned __int128 shim_q(vec_uint4 v)
{
	return (unsigned __int128)v[0] << 96 | (unsigned __int128)v[1] << 64 | (unsigned __int128)v[2] << 32 | v[3];
}
static inline vec_uint4 shim_v(unsigned __int128 q)
{
	return (vec_uint4){ (uint32_t)(q >> 96), (uint32_t)(q >> 64), (uint32_t)(q >> 32), (uint32_t)q };
}
// shlqbyi: bytes, count & 31 (16 and up give zero); shlqbii: bits, count & 7; rotqmbii: right by -count & 7.
static inline vec_uint4 spu_slqwbyte(vec_uint4 a, unsigned n)
{
	n &= 31;
	return n >= 16 ? (vec_uint4){ 0, 0, 0, 0 } : shim_v(shim_q(a) << (8 * n));
}
static inline vec_uint4 spu_slqw(vec_uint4 a, unsigned n) { return shim_v(shim_q(a) << (n & 7)); }
static inline vec_uint4 spu_rlmaskqw(vec_uint4 a, int n) { return shim_v(shim_q(a) >> ((unsigned)-n & 7)); }
