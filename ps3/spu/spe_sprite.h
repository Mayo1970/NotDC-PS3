#pragma once
// Plan section 7, bundle 17 (P80, Flycast CaclulateSpritePlane): a sprite's corner D on the plane through A, B and C.
// One text for the PPU replay (ps3_rend.cpp) and the SPU decoder (spe_dec.c), so both give the same bits.
#include <stdint.h>
#include <string.h>

static inline uint32_t spr_bits(float f)
{
	uint32_t b;
	memcpy(&b, &f, 4);
	return b;
}

// A float the plane math takes: +0, or a normal value from 2^-40 to 2^40. Sums and products then stay normal doubles.
static inline int spr_plain(uint32_t b)
{
	return !b || ((b >> 23) & 0xFF) - 87u <= 80u;
}

static inline double spr_d(uint32_t b)
{
	float f;
	memcpy(&f, &b, 4);
	return (double)f;
}

// The product goes through memory: no compiler can then fuse it with the next add (fmadd on the PPE, dfma on the SPU).
static inline double spr_mul(double a, double b)
{
	volatile double p = a * b;
	return p;
}

// a / b rounded to nearest even, for normal values. The SPU has no double divide (its libgcc call is not exact), so
// it takes the quotient bit by bit; misc/ta4_check runs this form against the PPU's.
static inline double spr_div(double a, double b)
{
	if (a == 0.0)
		return 0.0;
#if defined(__SPU__) || defined(SPR_SOFT_DIV)
	uint64_t x, y;
	memcpy(&x, &a, 8);
	memcpy(&y, &b, 8);
	const uint64_t mant = ((uint64_t)1 << 52) - 1;
	uint64_t ma = (x & mant) | ((uint64_t)1 << 52);
	const uint64_t mb = (y & mant) | ((uint64_t)1 << 52);
	int32_t e = (int32_t)((x >> 52) & 0x7FF) - (int32_t)((y >> 52) & 0x7FF) + 1023;
	if (ma < mb)
	{
		ma <<= 1;
		e--;
	}
	// 53 quotient bits and a round bit; what is left of ma is the sticky bit.
	uint64_t q = 0;
	for (int i = 0; i < 54; i++)
	{
		q <<= 1;
		if (ma >= mb)
		{
			ma -= mb;
			q |= 1;
		}
		ma <<= 1;
	}
	uint64_t m = q >> 1;
	if ((q & 1) && (ma || (m & 1)))
		m++;
	if (m >> 53)
	{
		m >>= 1;
		e++;
	}
	x = ((x ^ y) & ((uint64_t)1 << 63)) | ((uint64_t)e << 52) | (m & mant);
	double r;
	memcpy(&r, &x, 8);
	return r;
#else
	return a / b;
#endif
}

// r as float bits: +0 for a zero, 0 (no result) outside 2^-100 to 2^101, where a float is not normal or near it.
static inline int spr_f(double r, uint32_t* out)
{
	uint64_t b;
	memcpy(&b, &r, 8);
	if (!(b << 1))
	{
		*out = 0;
		return 1;
	}
	if (((uint32_t)(b >> 52) & 0x7FF) - 923u > 200u)
		return 0;
	const float f = (float)r;
	memcpy(out, &f, 4);
	return 1;
}

// x, y of A, B, C, D and z (1/W), u, v of A, B, C, all float bits. 1: out is D's z, u, v. 0: the old corner stays
// (a parallelogram, where it is the plane's corner; a sprite with no area; a value that is not plain).
static inline int spr_plane(const uint32_t* x, const uint32_t* y, const uint32_t* z, const uint32_t* u, const uint32_t* v,
                            uint32_t* out)
{
	for (int k = 0; k < 4; k++)
		if (!spr_plain(x[k]) || !spr_plain(y[k]))
			return 0;
	for (int k = 0; k < 3; k++)
		if (!spr_plain(z[k]) || !spr_plain(u[k]) || !spr_plain(v[k]))
			return 0;
	const double ax = spr_d(x[0]), ay = spr_d(y[0]);
	const double abx = spr_d(x[1]) - ax, aby = spr_d(y[1]) - ay;
	const double acx = spr_d(x[2]) - ax, acy = spr_d(y[2]) - ay;
	const double apx = spr_d(x[3]) - ax, apy = spr_d(y[3]) - ay;
	if (apx == acx - abx && apy == acy - aby)
		return 0;
	const double det = spr_mul(abx, acy) - spr_mul(aby, acx);
	if (det == 0.0)
		return 0;
	// AP = k1 AB + k2 AC, by Cramer's rule.
	const double k1 = spr_div(spr_mul(apx, acy) - spr_mul(apy, acx), det);
	const double k2 = spr_div(spr_mul(abx, apy) - spr_mul(aby, apx), det);
	const uint32_t* const w[3] = { z, u, v };
	for (int k = 0; k < 3; k++)
	{
		const double a = spr_d(w[k][0]);
		const double r = a + spr_mul(k1, spr_d(w[k][1]) - a) + spr_mul(k2, spr_d(w[k][2]) - a);
		if (!spr_f(r, &out[k]))
			return 0;
	}
	return 1;
}
