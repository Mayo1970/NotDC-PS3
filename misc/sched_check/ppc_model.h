// PPC64 model of the words the JIT emits (64-bit mode, big-endian), for misc/sched_check and misc/fmem_check.
// Undefined result bits get a fixed value that depends only on the inputs. Not part of the PS3 build.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace pm {

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

struct Cpu
{
	u64 r[32];
	u64 f[32];          // raw double bits
	u32 cr;             // IBM bit 0 = MSB
	u32 ca, so, ov;
	u64 ctr, lr;
	std::map<u64, u8> mem;     // bytes written; others read as a hash of their address
	u64 last_ea;               // the last access: address, size, load/store
	u32 last_size;
	int last_kind;             // 0 none, 1 load, 2 store
	int bad;                   // 1: a word the model does not run
	u64 mem_seed;
};

static inline u8 mem_init(const Cpu& c, u64 a)
{
	u64 x = (a + c.mem_seed) * 0x9E3779B97F4A7C15ull;
	x ^= x >> 29;
	x *= 0xBF58476D1CE4E5B9ull;
	return (u8)(x >> 56);
}
static inline u8 rd8(Cpu& c, u64 a) { auto it = c.mem.find(a); return it == c.mem.end() ? mem_init(c, a) : it->second; }
static inline u64 rdn(Cpu& c, u64 a, u32 n)
{
	u64 v = 0;
	for (u32 i = 0; i < n; i++)
		v = (v << 8) | rd8(c, a + i);
	c.last_ea = a; c.last_size = n; c.last_kind = 1;
	return v;
}
static inline void wrn(Cpu& c, u64 a, u64 v, u32 n)
{
	for (u32 i = 0; i < n; i++)
		c.mem[a + i] = (u8)(v >> (8 * (n - 1 - i)));
	c.last_ea = a; c.last_size = n; c.last_kind = 2;
}

static inline u64 rotl64(u64 x, u32 n) { n &= 63; return n ? (x << n) | (x >> (64 - n)) : x; }
static inline u64 mask64(u32 mb, u32 me)
{
	const u64 a = ~0ull >> mb, b = ~0ull << (63 - me);
	return mb <= me ? (a & b) : (a | b);
}
static inline u64 exts(u64 v, u32 bits) { return (u64)((s64)(v << (64 - bits)) >> (64 - bits)); }
static inline double dval(u64 b) { double d; memcpy(&d, &b, 8); return d; }
static inline u64 dbits(double d) { u64 b; memcpy(&b, &d, 8); return b; }
static inline float fval(u32 b) { float f; memcpy(&f, &b, 4); return f; }
static inline u32 fbits(float f) { u32 b; memcpy(&b, &f, 4); return b; }
static inline u64 single_round(double d) { return dbits((double)(float)d); }

static inline void cr_set(Cpu& c, u32 field, u32 v4) { c.cr = (c.cr & ~(0xF0000000u >> (4 * field))) | ((v4 & 15) << (28 - 4 * field)); }
static inline u32 cr_bit(const Cpu& c, u32 b) { return (c.cr >> (31 - b)) & 1; }
static inline void cr_setbit(Cpu& c, u32 b, u32 v) { c.cr = (c.cr & ~(1u << (31 - b))) | ((v & 1) << (31 - b)); }
static inline void rec(Cpu& c, u64 v) { s64 s = (s64)v; cr_set(c, 0, (s < 0 ? 8 : s > 0 ? 4 : 2) | c.so); }
static inline void cmp_s(Cpu& c, u32 bf, s64 a, s64 b) { cr_set(c, bf, (a < b ? 8 : a > b ? 4 : 2) | c.so); }
static inline void cmp_u(Cpu& c, u32 bf, u64 a, u64 b) { cr_set(c, bf, (a < b ? 8 : a > b ? 4 : 2) | c.so); }

static inline u64 lfs_conv(u32 w) { return dbits((double)fval(w)); }
static inline u32 stfs_conv(u64 f) { return fbits((float)dval(f)); }

static void fcmp(Cpu& c, u32 bf, double a, double b)
{
	const u32 v = (std::isnan(a) || std::isnan(b)) ? 1 : a < b ? 8 : a > b ? 4 : 2;
	cr_set(c, bf, v);
}

static inline u64 add_ca(Cpu& c, u64 a, u64 b, u32 cin)
{
	const u64 s = a + b + cin;
	c.ca = (s < a || (cin && s == a)) ? 1 : 0;
	return s;
}

// fctiw/fctiwz with saturation; the high word is fixed (0xFFF80000), as PPC64 leaves it undefined.
static u64 fcti(double v, bool trunc)
{
	s64 i;
	if (std::isnan(v)) i = (s64)(s32)0x80000000;
	else
	{
		double t = trunc ? std::trunc(v) : std::nearbyint(v);
		if (t > 2147483647.0) i = 0x7FFFFFFF;
		else if (t < -2147483648.0) i = (s64)(s32)0x80000000;
		else i = (s64)t;
	}
	return 0xFFF8000000000000ull | (u32)i;
}
static u64 fctid(double v, bool trunc)
{
	if (std::isnan(v)) return 0x8000000000000000ull;
	double t = trunc ? std::trunc(v) : std::nearbyint(v);
	if (t >= 9223372036854775807.0) return 0x7FFFFFFFFFFFFFFFull;
	if (t < -9223372036854775808.0) return 0x8000000000000000ull;
	return (u64)(s64)t;
}

// Runs one word. Returns false for a branch or a word the model does not run (bad set for the latter).
static bool step(Cpu& c, u32 w)
{
	const u32 op = w >> 26, rD = (w >> 21) & 31, rA = (w >> 16) & 31, rB = (w >> 11) & 31, rC = (w >> 6) & 31;
	const u64 simm = (u64)(s64)(s16)(w & 0xFFFF), uimm = w & 0xFFFF;
	const u64 A0 = rA ? c.r[rA] : 0;
	c.last_kind = 0;
	switch (op)
	{
	case 7: c.r[rD] = (u64)((s64)c.r[rA] * (s64)simm); return true;
	case 8: c.r[rD] = add_ca(c, ~c.r[rA], simm, 1); return true;
	case 10:
		if ((w >> 21) & 1) cmp_u(c, rD >> 2, c.r[rA], uimm);
		else cmp_u(c, rD >> 2, (u32)c.r[rA], uimm);
		return true;
	case 11:
		if ((w >> 21) & 1) cmp_s(c, rD >> 2, (s64)c.r[rA], (s64)simm);
		else cmp_s(c, rD >> 2, (s32)c.r[rA], (s64)simm);
		return true;
	case 12: c.r[rD] = add_ca(c, c.r[rA], simm, 0); return true;
	case 13: c.r[rD] = add_ca(c, c.r[rA], simm, 0); rec(c, c.r[rD]); return true;
	case 14: c.r[rD] = A0 + simm; return true;
	case 15: c.r[rD] = A0 + (simm << 16); return true;
	case 19:
	{
		const u32 xo = (w >> 1) & 0x3FF;
		if (xo == 0) { cr_set(c, rD >> 2, (c.cr >> (28 - 4 * (rA >> 2))) & 15); return true; }
		const u32 a = cr_bit(c, rA), b = cr_bit(c, rB);
		u32 v;
		switch (xo)
		{
		case 257: v = a & b; break;
		case 449: v = a | b; break;
		case 193: v = a ^ b; break;
		case 225: v = !(a & b); break;
		case 33: v = !(a | b); break;
		case 289: v = !(a ^ b); break;
		case 129: v = a & !b; break;
		case 417: v = a | !b; break;
		default: c.bad = 1; return false;
		}
		cr_setbit(c, rD, v);
		return true;
	}
	case 20: case 21: case 23:
	{
		const u32 sh = op == 23 ? (u32)(c.r[rB] & 31) : rB;
		const u32 mb = (w >> 6) & 31, me = (w >> 1) & 31;
		const u64 x = (u32)c.r[rD];
		const u64 rot = rotl64(x | (x << 32), sh), m = mask64(mb + 32, me + 32);
		c.r[rA] = op == 20 ? (rot & m) | (c.r[rA] & ~m) : rot & m;
		if (w & 1) rec(c, c.r[rA]);
		return true;
	}
	case 24: c.r[rA] = c.r[rD] | uimm; return true;
	case 25: c.r[rA] = c.r[rD] | (uimm << 16); return true;
	case 26: c.r[rA] = c.r[rD] ^ uimm; return true;
	case 27: c.r[rA] = c.r[rD] ^ (uimm << 16); return true;
	case 28: c.r[rA] = c.r[rD] & uimm; rec(c, c.r[rA]); return true;
	case 29: c.r[rA] = c.r[rD] & (uimm << 16); rec(c, c.r[rA]); return true;
	case 30:
	{
		const u32 sh = ((w >> 11) & 31) | (((w >> 1) & 1) << 5), mb = ((w >> 6) & 31) | (((w >> 5) & 1) << 5);
		const u64 rot = rotl64(c.r[rD], sh);
		switch ((w >> 2) & 7)
		{
		case 0: c.r[rA] = rot & mask64(mb, 63); break;
		case 1: c.r[rA] = rot & mask64(0, mb); break;
		case 2: c.r[rA] = rot & mask64(mb, 63 - sh); break;
		case 3: { const u64 m = mask64(mb, 63 - sh); c.r[rA] = (rot & m) | (c.r[rA] & ~m); break; }
		default: c.bad = 1; return false;
		}
		if (w & 1) rec(c, c.r[rA]);
		return true;
	}
	case 32: c.r[rD] = rdn(c, A0 + simm, 4); return true;
	case 34: c.r[rD] = rdn(c, A0 + simm, 1); return true;
	case 40: c.r[rD] = rdn(c, A0 + simm, 2); return true;
	case 42: c.r[rD] = exts(rdn(c, A0 + simm, 2), 16); return true;
	case 36: wrn(c, A0 + simm, c.r[rD], 4); return true;
	case 38: wrn(c, A0 + simm, c.r[rD], 1); return true;
	case 44: wrn(c, A0 + simm, c.r[rD], 2); return true;
	case 48: c.f[rD] = lfs_conv((u32)rdn(c, A0 + simm, 4)); return true;
	case 50: c.f[rD] = rdn(c, A0 + simm, 8); return true;
	case 52: wrn(c, A0 + simm, stfs_conv(c.f[rD]), 4); return true;
	case 54: wrn(c, A0 + simm, c.f[rD], 8); return true;
	case 58:
	{
		const u64 ea = A0 + (u64)(s64)(s16)(w & 0xFFFC);
		if ((w & 3) == 0) c.r[rD] = rdn(c, ea, 8);
		else if ((w & 3) == 2) c.r[rD] = exts(rdn(c, ea, 4), 32);
		else { c.bad = 1; return false; }
		return true;
	}
	case 62:
		if (w & 3) { c.bad = 1; return false; }
		wrn(c, A0 + (u64)(s64)(s16)(w & 0xFFFC), c.r[rD], 8);
		return true;
	case 59: case 63:
	{
		const double a = dval(c.f[rA]), b = dval(c.f[rB]), cc = dval(c.f[rC]);
		const u32 xo5 = (w >> 1) & 0x1F;
		if (w & 1) { c.bad = 1; return false; }
		if (op == 59 || xo5 >= 18)
		{
			double v;
			switch (xo5)
			{
			case 18: v = a / b; break;
			case 20: v = a - b; break;
			case 21: v = a + b; break;
			case 22: v = std::sqrt(b); break;
			case 23: if (op == 59) { c.bad = 1; return false; } c.f[rD] = (a >= 0.0) ? c.f[rC] : c.f[rB]; return true;
			case 24: if (op == 63) { c.bad = 1; return false; } v = 1.0 / b; break;
			case 25: v = a * cc; break;
			case 26: if (op == 59) { c.bad = 1; return false; } v = 1.0 / std::sqrt(b); break;
			case 28: v = std::fma(a, cc, -b); break;
			case 29: v = std::fma(a, cc, b); break;
			case 30: v = -std::fma(a, cc, -b); break;
			case 31: v = -std::fma(a, cc, b); break;
			default: c.bad = 1; return false;
			}
			c.f[rD] = op == 59 ? single_round(v) : dbits(v);
			return true;
		}
		switch ((w >> 1) & 0x3FF)
		{
		case 0: case 32: fcmp(c, rD >> 2, a, b); return true;
		case 12: c.f[rD] = single_round(b); return true;
		case 14: c.f[rD] = fcti(b, false); return true;
		case 15: c.f[rD] = fcti(b, true); return true;
		case 40: c.f[rD] = c.f[rB] ^ 0x8000000000000000ull; return true;
		case 72: c.f[rD] = c.f[rB]; return true;
		case 136: c.f[rD] = c.f[rB] | 0x8000000000000000ull; return true;
		case 264: c.f[rD] = c.f[rB] & 0x7FFFFFFFFFFFFFFFull; return true;
		case 814: c.f[rD] = fctid(b, false); return true;
		case 815: c.f[rD] = fctid(b, true); return true;
		case 846: c.f[rD] = dbits((double)(s64)c.f[rB]); return true;
		}
		c.bad = 1;
		return false;
	}
	case 31:
	{
		const u32 xo = (w >> 1) & 0x3FF, rS = rD;
		const u64 a = c.r[rA], b = c.r[rB], s = c.r[rS];
		switch (xo & 0x1FF)
		{
		case 8: case 10: case 40: case 136: case 138: case 266: case 104: case 200: case 202: case 232: case 234:
		case 11: case 75: case 235: case 9: case 73: case 233: case 457: case 459: case 489: case 491:
		{
			if (xo & 0x200) { c.bad = 1; return false; }
			u64 v = 0;
			switch (xo)
			{
			case 8: v = add_ca(c, ~a, b, 1); break;
			case 10: v = add_ca(c, a, b, 0); break;
			case 40: v = b - a; break;
			case 136: v = add_ca(c, ~a, b, c.ca); break;
			case 138: v = add_ca(c, a, b, c.ca); break;
			case 266: v = a + b; break;
			case 104: v = 0 - a; break;
			case 200: v = add_ca(c, ~a, 0, c.ca); break;
			case 202: v = add_ca(c, a, 0, c.ca); break;
			case 232: v = add_ca(c, ~a, ~0ull, c.ca); break;
			case 234: v = add_ca(c, a, ~0ull, c.ca); break;
			case 11: { const u64 p = (u64)(u32)a * (u64)(u32)b; v = (p >> 32) | (p & 0xFFFFFFFF00000000ull); break; }
			case 75: { const s64 p = (s64)(s32)a * (s64)(s32)b; v = ((u64)p >> 32) | ((u64)p & 0xFFFFFFFF00000000ull); break; }
			case 235: v = (u64)((s64)(s32)a * (s64)(s32)b); break;
			case 9: v = (u64)(((unsigned __int128)a * b) >> 64); break;
			case 73: v = (u64)(((__int128)(s64)a * (s64)b) >> 64); break;
			case 233: v = a * b; break;
			case 457: v = b ? a / b : 0; break;
			case 459: v = (u32)b ? (u64)((u32)a / (u32)b) : 0; break;
			case 489: v = ((s64)b == 0 || ((s64)a == INT64_MIN && (s64)b == -1)) ? 0 : (u64)((s64)a / (s64)b); break;
			case 491: v = ((s32)b == 0 || ((s32)a == INT32_MIN && (s32)b == -1)) ? 0 : (u64)(u32)((s32)a / (s32)b); break;
			}
			c.r[rD] = v;
			if (w & 1) rec(c, v);
			return true;
		}
		}
		u64 v;
		switch (xo)
		{
		case 0:
			if ((w >> 21) & 1) cmp_s(c, rD >> 2, (s64)a, (s64)b); else cmp_s(c, rD >> 2, (s32)a, (s32)b);
			return true;
		case 32:
			if ((w >> 21) & 1) cmp_u(c, rD >> 2, a, b); else cmp_u(c, rD >> 2, (u32)a, (u32)b);
			return true;
		case 28: v = s & b; break;
		case 60: v = s & ~b; break;
		case 124: v = ~(s | b); break;
		case 284: v = ~(s ^ b); break;
		case 316: v = s ^ b; break;
		case 412: v = s | ~b; break;
		case 444: v = s | b; break;
		case 476: v = ~(s & b); break;
		case 24: v = (b & 0x20) ? 0 : (u64)(u32)((u32)s << (b & 31)); break;
		case 536: v = (b & 0x20) ? 0 : (u64)((u32)s >> (b & 31)); break;
		case 27: v = (b & 0x40) ? 0 : s << (b & 63); break;
		case 539: v = (b & 0x40) ? 0 : s >> (b & 63); break;
		case 792:
		{
			const u32 n = (u32)(b & 0x3F);
			const s32 x = (s32)s;
			if (n >= 32) { v = (u64)(s64)(x < 0 ? -1 : 0); c.ca = x < 0; }
			else { v = (u64)(s64)(x >> n); c.ca = n && x < 0 && ((u32)x << (32 - n)); }
			break;
		}
		case 794:
		{
			const u32 n = (u32)(b & 0x7F);
			const s64 x = (s64)s;
			if (n >= 64) { v = (u64)(x < 0 ? -1 : 0); c.ca = x < 0; }
			else { v = (u64)(x >> n); c.ca = x < 0 && n && ((u64)x << (64 - n)); }
			break;
		}
		case 824:
		{
			const u32 n = rB;
			const s32 x = (s32)s;
			v = (u64)(s64)(x >> n);
			c.ca = x < 0 && n && ((u32)x << (32 - n));
			break;
		}
		case 26: v = (u32)s ? (u64)__builtin_clz((u32)s) : 32; break;
		case 58: v = s ? (u64)__builtin_clzll(s) : 64; break;
		case 922: v = exts(s, 16); break;
		case 954: v = exts(s, 8); break;
		case 986: v = exts(s, 32); break;
		case 19:
			if ((w >> 20) & 1)
			{
				u32 m = 0;
				for (u32 f = 0; f < 8; f++)
					if (((w >> 12) & 0xFF) & (0x80 >> f)) m |= 0xF0000000u >> (4 * f);
				c.r[rD] = c.cr & m;
			}
			else
				c.r[rD] = c.cr;
			return true;
		case 144:
		{
			u32 m = 0;
			for (u32 f = 0; f < 8; f++)
				if (((w >> 12) & 0xFF) & (0x80 >> f)) m |= 0xF0000000u >> (4 * f);
			c.cr = (c.cr & ~m) | ((u32)s & m);
			return true;
		}
		case 339: case 467:
		{
			const u32 spr = rA | (rB << 5);
			if (xo == 339)
			{
				if (spr == 8) c.r[rD] = c.lr;
				else if (spr == 9) c.r[rD] = c.ctr;
				else if (spr == 1) c.r[rD] = ((u64)c.so << 31) | ((u64)c.ov << 30) | ((u64)c.ca << 29);
				else { c.bad = 1; return false; }
			}
			else
			{
				if (spr == 8) c.lr = s;
				else if (spr == 9) c.ctr = s;
				else if (spr == 1) { c.so = (s >> 31) & 1; c.ov = (s >> 30) & 1; c.ca = (s >> 29) & 1; }
				else { c.bad = 1; return false; }
			}
			return true;
		}
		case 23: c.r[rD] = rdn(c, A0 + b, 4); return true;
		case 87: c.r[rD] = rdn(c, A0 + b, 1); return true;
		case 279: c.r[rD] = rdn(c, A0 + b, 2); return true;
		case 343: c.r[rD] = exts(rdn(c, A0 + b, 2), 16); return true;
		case 21: c.r[rD] = rdn(c, A0 + b, 8); return true;
		case 341: c.r[rD] = exts(rdn(c, A0 + b, 4), 32); return true;
		case 534: { const u32 x = (u32)rdn(c, A0 + b, 4); c.r[rD] = __builtin_bswap32(x); return true; }
		case 790: { const u16 x = (u16)rdn(c, A0 + b, 2); c.r[rD] = __builtin_bswap16(x); return true; }
		case 535: c.f[rD] = lfs_conv((u32)rdn(c, A0 + b, 4)); return true;
		case 599: c.f[rD] = rdn(c, A0 + b, 8); return true;
		case 151: wrn(c, A0 + b, s, 4); return true;
		case 215: wrn(c, A0 + b, s, 1); return true;
		case 407: wrn(c, A0 + b, s, 2); return true;
		case 149: wrn(c, A0 + b, s, 8); return true;
		case 662: wrn(c, A0 + b, __builtin_bswap32((u32)s), 4); return true;
		case 918: wrn(c, A0 + b, __builtin_bswap16((u16)s), 2); return true;
		case 663: wrn(c, A0 + b, stfs_conv(c.f[rS]), 4); return true;
		case 727: wrn(c, A0 + b, c.f[rS], 8); return true;
		case 983: wrn(c, A0 + b, (u32)c.f[rS], 4); return true;
		default: c.bad = 1; return false;
		}
		c.r[rA] = v;
		if (w & 1) rec(c, v);
		return true;
	}
	}
	c.bad = 1;
	return false;
}

} // namespace pm
