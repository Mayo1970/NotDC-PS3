// Host-only check for plan 5.20: runs what emit_ftrv / emit_fipr emit on a small PPC float model and compares
// it bit for bit with the old operation order. Not part of the PS3 build.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[256];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_fv.h"

static const u32 CTX = 30;            // ppc_contex
static const u32 FR0 = 0x100;         // fr[0] slot in the fake context, as in Sh4Context
static const u32 XF0 = 0x140;         // xf[0]

struct Cpu
{
	float f[32];
	u8 mem[0x200];
	int bad;
};

static u32 bits(float x) { u32 u; memcpy(&u, &x, 4); return u; }
static float flt(u32 u) { float x; memcpy(&x, &u, 4); return x; }

// PPC operand precedence for a NaN result (frA, then frB, then frC), so a swapped role shows up as a different NaN.
static float qnan(float x) { return flt(bits(x) | 0x00400000u); }
static float ppc_mul(float a, float c)
{
	if (std::isnan(a)) return qnan(a);
	if (std::isnan(c)) return qnan(c);
	return a * c;
}
static float ppc_madd(float a, float c, float b)
{
	if (std::isnan(a)) return qnan(a);
	if (std::isnan(b)) return qnan(b);
	if (std::isnan(c)) return qnan(c);
	return fmaf(a, c, b);
}

static float ld(Cpu& c, u32 ofs)
{
	if (ofs + 4 > sizeof(c.mem)) { c.bad = 1; return 0; }
	u32 u; memcpy(&u, &c.mem[ofs], 4); return flt(u);
}
static void st(Cpu& c, u32 ofs, float x)
{
	if (ofs + 4 > sizeof(c.mem)) { c.bad = 1; return; }
	u32 u = bits(x); memcpy(&c.mem[ofs], &u, 4);
}

static void run(Cpu& c)
{
	for (u32 pc = 0; pc < g_n && !c.bad; pc++)
	{
		const u32 i = g_code[pc], op = i >> 26;
		const u32 d = (i >> 21) & 31, a = (i >> 16) & 31, b = (i >> 11) & 31, cc = (i >> 6) & 31;
		const s32 disp = (s16)(i & 0xFFFF);
		if (op == 48 || op == 52)
		{
			if (a != CTX) { c.bad = 2; return; }
			if (op == 48) c.f[d] = ld(c, (u32)disp); else st(c, (u32)disp, c.f[d]);
		}
		else if (op == 59 && ((i >> 1) & 31) == 25 && !(i & 1))
			c.f[d] = ppc_mul(c.f[a], c.f[cc]);           // fmuls: one rounding to single
		else if (op == 59 && ((i >> 1) & 31) == 29 && !(i & 1))
			c.f[d] = ppc_madd(c.f[a], c.f[cc], c.f[b]);  // fmadds: fused, one rounding
		else if (op == 63 && ((i >> 1) & 1023) == 72 && !(i & 1))
			c.f[d] = c.f[b];
		else
			c.bad = 3;
	}
}

static u64 rnd_state = 0x9E3779B97F4A7C15ull;
static u64 rnd() { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 7; rnd_state ^= rnd_state << 17; return rnd_state; }
static float rfl()
{
	switch (rnd() % 16)
	{
	case 0: return 0.0f;
	case 1: return -0.0f;
	case 2: return flt(0x00000001u + (u32)(rnd() % 0x7FFFFF));   // denormal
	case 3: return (rnd() & 1) ? INFINITY : -INFINITY;
	case 4: return flt(0x7FC00000u | (u32)(rnd() & 0x3FFFFF));   // NaN with a payload
	case 5: return 1.0f;
	default: return flt((u32)rnd() & 0xBFFFFFFFu);              // finite, any sign
	}
}

// The old shop_ftrv / shop_fipr order: column 0 by a multiply, then fused multiply-adds.
static void ref_ftrv(const float m[16], const float v[4], float out[4])
{
	for (u32 i = 0; i < 4; i++)
	{
		float s = ppc_mul(m[i], v[0]);
		for (u32 j = 1; j < 4; j++)
			s = ppc_madd(m[4 * j + i], v[j], s);
		out[i] = s;
	}
}
static float ref_fipr(const float a[4], const float b[4])
{
	float s = ppc_mul(a[0], b[0]);
	for (u32 i = 1; i < 4; i++)
		s = ppc_madd(a[i], b[i], s);
	return s;
}

static bool same(float x, float y) { return bits(x) == bits(y); }

int main()
{
	u32 fails = 0, cases = 0;
	// ftrv on FV0/4/8/12: all pinned (f14+n, FPU PIN), all in memory, or a mixed set.
	for (u32 t = 0; t < 400000; t++, cases++)
	{
		const u32 n = 4 * (u32)(rnd() % 4), mode = (u32)(rnd() % 3);
		FvElem v[4];
		for (u32 k = 0; k < 4; k++)
		{
			const bool pin = mode == 0 || (mode == 2 && (rnd() & 1));
			v[k].reg = pin ? 14 + n + k : FV_MEM;
			v[k].ofs = FR0 + 4 * (n + k);
		}
		g_n = 0;
		emit_ftrv(v, XF0, CTX);
		Cpu c; memset(&c, 0, sizeof(c));
		for (u32 r = 0; r < 32; r++) c.f[r] = rfl();
		for (u32 k = 0; k < 0x200; k += 4) st(c, k, rfl());
		float m[16], in[4], want[4];
		for (u32 k = 0; k < 16; k++) m[k] = ld(c, XF0 + 4 * k);
		for (u32 k = 0; k < 4; k++) in[k] = v[k].reg != FV_MEM ? c.f[v[k].reg] : ld(c, v[k].ofs);
		ref_ftrv(m, in, want);
		Cpu before = c;
		run(c);
		bool ok = !c.bad;
		for (u32 k = 0; k < 4 && ok; k++)
			ok = same(v[k].reg != FV_MEM ? c.f[v[k].reg] : ld(c, v[k].ofs), want[k]);
		// Only f0-f11 are scratch; every other FPR and every context word not written must be unchanged.
		for (u32 r = 12; r < 32 && ok; r++)
		{
			bool out = false;
			for (u32 k = 0; k < 4; k++) out |= v[k].reg == r;
			if (!out) ok = same(c.f[r], before.f[r]);
		}
		for (u32 w = 0; w < 0x200 && ok; w += 4)
		{
			bool out = false;
			for (u32 k = 0; k < 4; k++) out |= v[k].reg == FV_MEM && v[k].ofs == w;
			if (!out) ok = !memcmp(&c.mem[w], &before.mem[w], 4);
		}
		if (!ok)
		{
			if (fails < 3)
			{
				printf("FAIL ftrv n=%u mode=%u bad=%d\n", n, mode, c.bad);
				for (u32 k = 0; k < 4; k++)
					printf("  v%u in %08X got %08X want %08X\n", k, bits(in[k]),
					       bits(v[k].reg != FV_MEM ? c.f[v[k].reg] : ld(c, v[k].ofs)), bits(want[k]));
				for (u32 k = 0; k < 16; k++)
					printf(" m%u=%08X", k, bits(m[k]));
				printf("\n");
			}
			fails++;
		}
	}
	// fipr FVm,FVn: rd = fr[n+3]; m may equal n.
	for (u32 t = 0; t < 400000; t++, cases++)
	{
		const u32 n = 4 * (u32)(rnd() % 4), m = 4 * (u32)(rnd() % 4), mode = (u32)(rnd() % 3);
		FvElem a[4], b[4], rd;
		for (u32 k = 0; k < 4; k++)
		{
			const bool pa = mode == 0 || (mode == 2 && (rnd() & 1));
			a[k].reg = pa ? 14 + n + k : FV_MEM; a[k].ofs = FR0 + 4 * (n + k);
		}
		for (u32 k = 0; k < 4; k++)
		{
			b[k].ofs = FR0 + 4 * (m + k);
			// One register file: an element shared with a keeps a's allocation.
			b[k].reg = (m == n) ? a[k].reg : ((mode == 0 || (mode == 2 && (rnd() & 1))) ? 14 + m + k : FV_MEM);
		}
		rd = a[3];
		g_n = 0;
		emit_fipr(a, b, rd, CTX);
		Cpu c; memset(&c, 0, sizeof(c));
		for (u32 r = 0; r < 32; r++) c.f[r] = rfl();
		for (u32 k = 0; k < 0x200; k += 4) st(c, k, rfl());
		float x[4], y[4];
		for (u32 k = 0; k < 4; k++)
		{
			x[k] = a[k].reg != FV_MEM ? c.f[a[k].reg] : ld(c, a[k].ofs);
			y[k] = b[k].reg != FV_MEM ? c.f[b[k].reg] : ld(c, b[k].ofs);
		}
		const float want = ref_fipr(x, y);
		Cpu before = c;
		run(c);
		bool ok = !c.bad && same(rd.reg != FV_MEM ? c.f[rd.reg] : ld(c, rd.ofs), want);
		for (u32 r = 3; r < 32 && ok; r++)
			if (r != rd.reg) ok = same(c.f[r], before.f[r]);
		for (u32 w = 0; w < 0x200 && ok; w += 4)
			if (!(rd.reg == FV_MEM && rd.ofs == w)) ok = !memcmp(&c.mem[w], &before.mem[w], 4);
		if (!ok)
		{
			if (fails < 10) printf("FAIL fipr n=%u m=%u mode=%u bad=%d\n", n, m, mode, c.bad);
			fails++;
		}
	}
	printf("%s: %u cases, %u failures\n", fails ? "FAIL" : "PASS", cases, fails);
	return fails != 0;
}
