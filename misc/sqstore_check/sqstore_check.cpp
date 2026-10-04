// Host-only check for plan 5.17 Step 3: runs the code emit_sq_store emits on a small PPC64 model and
// compares it with the canonical store-queue write. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[64];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_sq.h"

// Below 2 GB and 64-byte aligned, like sq_both on the PS3.
static const u32 SQ_BASE = 0x11E4D300;

struct Cpu
{
	u64 gpr[32];
	u32 fpr[32];   // stfs stores the single-precision bits; FPU PIN keeps them exact
	u32 cr0;       // LT, GT, EQ as 8, 4, 2
	u32 sq[16];    // the 64 bytes at SQ_BASE, as words
	int bad;
};

static u64 rnd_state = 88172645463325252ull;
static u64 rnd() { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 7; rnd_state ^= rnd_state << 17; return rnd_state; }

static u64 rotl64(u64 x, u32 n) { n &= 63; return n ? (x << n) | (x >> (64 - n)) : x; }
static u64 mask64(u32 mb, u32 me)
{
	u64 m = 0;
	for (u32 b = mb;; b = (b + 1) & 63)
	{
		m |= 1ull << (63 - b);
		if (b == me)
			break;
	}
	return m;
}

static void store32(Cpu& c, u64 ea, u32 v)
{
	if (ea < SQ_BASE || ea + 4 > (u64)SQ_BASE + 64 || (ea & 3))
	{
		c.bad = 1;
		return;
	}
	c.sq[(ea - SQ_BASE) >> 2] = v;
}

// Runs g_code from 0 until pc reaches done or other (instruction indexes). Returns the end pc.
static u32 run(Cpu& c, u32 done, u32 other)
{
	u32 pc = 0;
	for (int steps = 0; steps < 64 && pc != done && pc != other; steps++)
	{
		const u32 i = g_code[pc];
		const u32 op = i >> 26, rS = (i >> 21) & 31, rA = (i >> 16) & 31;
		const s64 d = (s16)(i & 0xFFFF);
		switch (op)
		{
		case 21: // rlwinm
		{
			const u32 sh = (i >> 11) & 31, mb = (i >> 6) & 31, me = (i >> 1) & 31;
			if (i & 1) { c.bad = 2; return pc; }
			const u64 x = c.gpr[rS] & 0xFFFFFFFFull;
			c.gpr[rA] = rotl64(x | (x << 32), sh) & mask64(mb + 32, me + 32);
			break;
		}
		case 10: // cmpli
		{
			if (((i >> 23) & 7) != 0 || ((i >> 21) & 1)) { c.bad = 3; return pc; }
			const u32 a = (u32)c.gpr[rA], b = i & 0xFFFF;
			c.cr0 = a < b ? 8 : a > b ? 4 : 2;
			break;
		}
		case 16: // bc
		{
			const u32 bo = rS, bi = rA;
			if ((i & 3) || !(bo & 4) || bi > 3) { c.bad = 4; return pc; }
			const u32 bit = (c.cr0 >> (3 - bi)) & 1;
			if ((bo & 0x10) || bit == ((bo >> 3) & 1))
			{
				pc = (u32)((s64)pc + ((s16)(i & 0xFFFC) >> 2));
				continue;
			}
			break;
		}
		case 15: // addis
			c.gpr[rS] = (rA ? c.gpr[rA] : 0) + ((u64)d << 16);
			break;
		case 36: // stw
			store32(c, (rA ? c.gpr[rA] : 0) + d, (u32)c.gpr[rS]);
			break;
		case 52: // stfs
			store32(c, (rA ? c.gpr[rA] : 0) + d, c.fpr[rS]);
			break;
		default:
			c.bad = 5;
			return pc;
		}
		pc++;
	}
	return pc;
}

static u32 pick_addr(bool pair)
{
	static const u32 edges[] = { 0xE0000000, 0xE3FFFFF8, 0xE0000020, 0xDFFFFFF8, 0xE4000000, 0x00000000,
	                             0xFFFFFFF8, 0x8C000000, 0x0C000000, 0xA5000000, 0x7FFFFFF8, 0xE2000038 };
	u32 a;
	const u32 k = (u32)(rnd() % 16);
	if (k < 6)
		a = 0xE0000000 | (u32)(rnd() & 0x03FFFFFF);
	else if (k < 10)
		a = edges[rnd() % (sizeof(edges) / sizeof(edges[0]))];
	else
		a = (u32)rnd();
	return a & (pair ? ~7u : ~3u);
}

int main()
{
	const u32 pinned[] = { 14, 15, 20, 28, 31 };
	u32 fails = 0, cases = 0, sq_hits = 0;
	for (int shape = 0; shape < 4; shape++)
	{
		const bool pair = shape & 1, fpr = (shape >> 1) & 1;
		for (u32 pi = 0; pi < 6; pi++)
		{
			// As ps3_driver.cpp: a pair keeps its address in rarg0 and data in rarg2:rarg3 or two FPRs,
			// EA in rarg1; a scalar has areg (pinned or rarg0), data in a pinned reg or rarg1, EA in rarg2.
			const u32 areg = pair ? ppc_rarg0 : (pi < 5 ? pinned[pi] : (u32)ppc_rarg0);
			const u32 ea = pair ? ppc_rarg1 : ppc_rarg2;
			const u32 d0 = fpr ? 14 + pi : (pair ? ppc_rarg2 : (pi < 5 ? pinned[(pi + 1) % 5] : (u32)ppc_rarg1));
			const u32 d1 = fpr ? 15 + pi : ppc_rarg3;
			g_n = 0;
			ppc_label* other = emit_sq_store(areg, ea, pair, fpr, d0, d1, SQ_BASE);
			// The store-queue path falls through to done; the bne is patched to land one word later.
			const u32 done = g_n;
			ppc_emit(0x7FE00008);
			other->MarkLabel();
			const u32 other_pc = done + 1;
			for (u32 t = 0; t < 200000; t++, cases++)
			{
				Cpu c;
				for (u32 r = 0; r < 32; r++) { c.gpr[r] = rnd(); c.fpr[r] = (u32)rnd(); }
				memset(c.sq, 0xA5, sizeof(c.sq));
				c.cr0 = 0; c.bad = 0;
				const u32 addr = pick_addr(pair);
				// Pinned registers carry a garbage upper half (jit-64bit-poison); rarg0 is clean or not.
				c.gpr[areg] = (rnd() & 1 ? rnd() << 32 : 0) | addr;
				Cpu before = c;
				const u32 end = run(c, done, other_pc);
				Cpu want = before;
				const bool is_sq = (addr >> 26) == 0x38;
				if (is_sq)
				{
					const u32 w = (addr & (pair ? 0x38 : 0x3C)) >> 2;
					want.sq[w] = fpr ? before.fpr[d0] : (u32)before.gpr[d0];
					if (pair)
						want.sq[w + 1] = fpr ? before.fpr[d1] : (u32)before.gpr[d1];
					sq_hits++;
				}
				bool ok = !c.bad && end == (is_sq ? done : other_pc) && !memcmp(c.sq, want.sq, sizeof(c.sq));
				for (u32 r = 1; r < 32 && ok; r++)
					if (r != ea && c.gpr[r] != before.gpr[r])
						ok = false;
				for (u32 r = 0; r < 32 && ok; r++)
					if (c.fpr[r] != before.fpr[r])
						ok = false;
				if (!ok)
				{
					if (fails < 5)
						printf("FAIL shape pair=%d fpr=%d areg=r%u addr=%08X bad=%d end=%u (done %u)\n",
						       pair, fpr, areg, addr, c.bad, end, done);
					fails++;
				}
			}
		}
	}
	printf("%s: %u cases, %u store-queue addresses, %u failures\n", fails ? "FAIL" : "PASS", cases, sq_hits, fails);
	return fails != 0;
}
