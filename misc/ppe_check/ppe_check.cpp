// Host-only check for plan 5.24: ps3_jit_ppe.h against ppu-as words (ppe_words.inc), then its new forms against
// the old ones on a PPC64 model with bit-exact lfs / stfs / fctiwz. Not part of the PS3 build.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[512];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_ppe.h"
#include "ppe_words.inc"

static int g_fail, g_checks;

static void check(bool ok, const char* what, const char* detail)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 12)
			printf("FAIL %s: %s\n", what, detail);
		g_fail++;
	}
}

static u64 g_seed = 88172645463325252ull;
static u64 rnd64()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return g_seed;
}

// As the driver: context in r30 (jit_scratch at 0x80, fr[n] at 0x40 + 4n, fpul at 0x10C), FR0-FR15 in f14-f29.
static const u32 CTX = 30, SCRATCH = 0x80, FR0_OFS = 0x40, FPUL = 0x10C, CTX_SIZE = 0x160;
static const u64 CTX_BASE = 0x20000000ull;
static const u32 BIAS_AT[3] = { 0x10B23450u, 0x10B2F008u, 0x10B27FF8u };

static double dval(u64 b) { double d; memcpy(&d, &b, 8); return d; }
static u64 dbits(double d) { u64 b; memcpy(&b, &d, 8); return b; }

// Power ISA "Load Floating-Point Single": the word to the double format, SNaN payloads kept.
static u64 lfs_conv(u32 w)
{
	const u32 e = (w >> 23) & 0xFF;
	const u64 w01 = (u64)(w >> 30) << 62, low = (u64)(w & 0x3FFFFFFF) << 29, w1 = (w >> 30) & 1;
	if (e > 0 && e < 255)
		return w01 | ((w1 ? 0ull : 7ull) << 59) | low;
	if (e == 255 || (w & 0x7FFFFFFF) == 0)
		return w01 | ((w1 ? 7ull : 0ull) << 59) | low;
	s32 exp = -126;
	u64 frac = (u64)(w & 0x7FFFFF) << 29;
	while (!(frac & (1ull << 52)))
	{
		frac <<= 1;
		exp--;
	}
	return ((u64)(w >> 31) << 63) | ((u64)(exp + 1023) << 52) | (frac & ((1ull << 52) - 1));
}

// Power ISA "Store Floating-Point Single"; undef for a double no lfs or single op can leave in an FPR.
static u32 stfs_conv(u64 f, bool& undef)
{
	const u32 e = (f >> 52) & 0x7FF;
	if (e > 896 || (f << 1) == 0)
		return (u32)((f >> 62) << 30) | (u32)((f >> 29) & 0x3FFFFFFF);
	if (e >= 874)
	{
		s32 exp = (s32)e - 1023;
		u64 frac = (1ull << 52) | (f & ((1ull << 52) - 1));
		while (exp < -126)
		{
			frac >>= 1;
			exp++;
		}
		return (u32)((f >> 63) << 31) | (u32)((frac >> 29) & 0x7FFFFF);
	}
	undef = true;
	return 0;
}

// fctiwz: truncate toward zero, saturate, NaN to 0x80000000.
static u32 fctiwz_ref(u64 f)
{
	const double v = dval(f);
	if (std::isnan(v))
		return 0x80000000u;
	const double t = trunc(v);
	if (t > 2147483647.0)
		return 0x7FFFFFFFu;
	if (t < -2147483648.0)
		return 0x80000000u;
	return (u32)(s32)t;
}

static u64 mask64(u32 mb, u32 me)
{
	if (mb <= me)
		return (~0ull >> mb) & (~0ull << (63 - me));
	return ~((~0ull >> (me + 1)) & (~0ull << (64 - mb)));
}

// PPC64 model of what the header may emit. Undefined result bits (mfocrf's other fields, fctiwz's high word)
// are random, so a reader of them fails.
struct Cpu
{
	u64 r[32];
	u64 f[32];
	u32 cr;
	std::map<u64, u32> mem;
	int bad;
	const char* why;

	void fault(const char* w) { if (!bad) { bad = 1; why = w; } }
	u32 ld32(u64 ea)
	{
		if ((ea & 3) || !mem.count(ea)) { fault("load from an unexpected word"); return 0; }
		return mem[ea];
	}
	void st32(u64 ea, u32 v)
	{
		if ((ea & 3) || !mem.count(ea)) { fault("store to an unexpected word"); return; }
		mem[ea] = v;
	}

	void run(const u32* code, u32 n)
	{
		for (u32 pc = 0; pc < n && !bad; pc++)
		{
			const u32 x = code[pc], op = x >> 26, d = (x >> 21) & 31, a = (x >> 16) & 31, b = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 ea = (a ? r[a] : 0) + (u64)simm;
			switch (op)
			{
			case 14: r[d] = (a ? r[a] : 0) + (u64)simm; break;
			case 15: r[d] = (a ? r[a] : 0) + ((u64)simm << 16); break;
			case 27: r[a] = r[d] ^ ((u64)(x & 0xFFFF) << 16); break;
			case 21:
			{
				if (x & 1) { fault("rlwinm. (record form)"); return; }
				const u32 sh = b, mb = (x >> 6) & 31, me = (x >> 1) & 31;
				const u64 lo = (u32)r[d], dup = (lo << 32) | lo;
				const u64 rot = sh ? (dup << sh) | (dup >> (64 - sh)) : dup;
				r[a] = rot & mask64(mb + 32, me + 32);
				break;
			}
			case 32: r[d] = ld32(ea); break;
			case 36: st32(ea, (u32)r[d]); break;
			case 48: f[d] = lfs_conv(ld32(ea)); break;
			case 50: f[d] = ((u64)ld32(ea) << 32) | ld32(ea + 4); break;
			case 52:
			{
				bool undef = false;
				const u32 w = stfs_conv(f[d], undef);
				if (undef) { fault("stfs of a double outside the single range"); return; }
				st32(ea, w);
				break;
			}
			case 54: st32(ea, (u32)(f[d] >> 32)); st32(ea + 4, (u32)f[d]); break;
			case 31:
			{
				const u32 xo = (x >> 1) & 0x3FF;
				if (x & 1) { fault("record form"); return; }
				if (xo == 19)
				{
					const u32 fxm = (x >> 12) & 0xFF;
					if (x & 0x00100000)
					{
						if (__builtin_popcount(fxm) != 1) { fault("mfocrf of more than one field"); return; }
						const u32 sh = 28 - 4 * (__builtin_clz(fxm) - 24);
						r[d] = (rnd64() & ~(0xFull << sh)) | ((u64)((cr >> sh) & 0xF) << sh);
					}
					else
					{
						if (fxm) { fault("mfcr with FXM bits"); return; }
						r[d] = cr;
					}
				}
				else if (xo == 983)
					st32((a ? r[a] : 0) + r[b], (u32)f[d]);
				else { fault("unexpected X-form"); return; }
				break;
			}
			case 63:
			{
				if (x & 1) { fault("record form"); return; }
				if (((x >> 1) & 0x3FF) == 15 && a == 0)
					f[d] = (rnd64() << 32) | fctiwz_ref(f[b]);
				else if (((x >> 1) & 31) == 20 && ((x >> 6) & 31) == 0)
					f[d] = dbits(dval(f[a]) - dval(f[b]));
				else { fault("unexpected FP op"); return; }
				break;
			}
			default:
				fault("unexpected instruction");
				return;
			}
		}
	}
};

// A single-precision pattern, specials often: zeros, denormals, infinities, quiet and signalling NaNs, and the
// edges of the s32 range.
static u32 rnd_single()
{
	static const u32 edge[] = { 0x4F000000u, 0xCF000000u, 0x4EFFFFFFu, 0xCEFFFFFFu, 0xCF000001u, 0x3F7FFFFFu,
	                            0xBF7FFFFFu, 0x3F800000u, 0xBF800000u, 0x4B000001u };
	const u32 s = (u32)(rnd64() & 0x80000000u);
	switch (rnd64() % 12)
	{
	case 0: return s;
	case 1: return s | (u32)(1 + rnd64() % 0x7FFFFF);
	case 2: return s | 0x7F800000u;
	case 3: return s | 0x7FC00000u | (u32)(rnd64() & 0x3FFFFF);
	case 4: return s | 0x7F800000u | (u32)(1 + rnd64() % 0x3FFFFF);
	case 5: return edge[rnd64() % (sizeof(edge) / sizeof(edge[0]))];
	default: return (u32)rnd64();
	}
}

static void fresh(Cpu& c)
{
	c.bad = 0;
	c.why = "";
	c.mem.clear();
	for (u32 k = 0; k < 32; k++)
	{
		c.r[k] = rnd64();
		c.f[k] = lfs_conv(rnd_single());
	}
	c.r[CTX] = CTX_BASE;
	c.cr = (u32)rnd64();
	for (u32 w = 0; w < CTX_SIZE; w += 4)
		c.mem[CTX_BASE + w] = (u32)rnd64();
	for (u32 k = 0; k < 3; k++)
	{
		c.mem[BIAS_AT[k]] = 0x43300000u;
		c.mem[BIAS_AT[k] + 4] = 0x80000000u;
	}
}

// Everything but the listed GPRs, FPRs and context words must be as before.
static bool only_changed(const Cpu& a, const Cpu& b, u32 gprs, u32 fprs, const u32* ofs, u32 n_ofs)
{
	for (u32 k = 0; k < 32; k++)
	{
		if (!((gprs >> k) & 1) && a.r[k] != b.r[k]) return false;
		if (!((fprs >> k) & 1) && a.f[k] != b.f[k]) return false;
	}
	if (a.cr != b.cr || a.mem.size() != b.mem.size())
		return false;
	for (const auto& m : a.mem)
	{
		bool listed = false;
		for (u32 k = 0; k < n_ofs; k++)
			listed |= m.first == CTX_BASE + ofs[k];
		if (!listed && b.mem.at(m.first) != m.second)
			return false;
	}
	return true;
}

static void test_words()
{
	g_n = 0;
	for (u32 b = 0; b < 4; b++)
		emit_cr0_bit(3, b, false);
	for (u32 b = 0; b < 4; b++)
		emit_cr0_bit(3, b, true);
	emit_cr0_bit(7, 2, true);
	emit_cr0_bit(12, 1, true);
	emit_flds_direct(14, CTX, FPUL);
	emit_flds_direct(29, CTX, FPUL);
	emit_fsts_direct(14, CTX, FPUL);
	emit_fsts_direct(29, CTX, FPUL);
	emit_ftrc(25, CTX, SCRATCH, FPUL, 3, false);
	emit_ftrc(25, CTX, SCRATCH, FPUL, 3, true);
	emit_ftrc(1, CTX, SCRATCH, FPUL, 3, true);
	emit_i2f(3, 4, 2, CTX, SCRATCH, 0);
	for (u32 k = 0; k < 3; k++)
		emit_i2f(3, 4, 2, CTX, SCRATCH, BIAS_AT[k]);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// CR0 bit b as 0/1 in rD by both forms, whatever the other CR fields hold; only rD changes.
static void test_cr0(u32 iters)
{
	static const u32 regs[] = { 3, 7, 12 };
	for (u32 t = 0; t < iters; t++)
	{
		const bool focrf = t & 1;
		const u32 D = regs[(t >> 1) % 3], b = (t >> 3) & 3;
		Cpu c;
		fresh(c);
		c.cr = (c.cr & 0x0FFFFFFFu) | ((t >> 5) & 15) << 28;
		g_n = 0;
		emit_cr0_bit(D, b, focrf);
		Cpu before = c;
		c.run(g_code, g_n);
		const u64 want = (c.cr >> (31 - b)) & 1;
		char d[96];
		snprintf(d, sizeof(d), "focrf=%d r%u b=%u cr=%08X: got %016llX want %llu", focrf, D, b, c.cr,
		         (unsigned long long)c.r[D], (unsigned long long)want);
		check(!c.bad && c.r[D] == want && only_changed(before, c, 1u << D, 0, 0, 0), "cr0 bit", c.bad ? c.why : d);
	}
}

static u32 fr_slot(u32 fpr) { return FR0_OFS + 4 * (fpr - 14); }

// flds: the FPUL word equals the old bounce's and the original single; the direct form writes nothing else.
static void test_flds(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const u32 fs = 14 + (u32)(rnd64() % 16), w = rnd_single();
		Cpu c;
		fresh(c);
		c.f[fs] = lfs_conv(w);
		Cpu old = c, nu = c;
		g_n = 0;
		ppc_stfs(fs, CTX, fr_slot(fs));
		ppc_lwz(3, CTX, fr_slot(fs));
		ppc_stw(3, CTX, FPUL);
		old.run(g_code, g_n);
		g_n = 0;
		emit_flds_direct(fs, CTX, FPUL);
		nu.run(g_code, g_n);
		const u32 ofs[1] = { FPUL };
		char d[96];
		snprintf(d, sizeof(d), "f%u w=%08X: new %08X old %08X", fs, w, nu.mem[CTX_BASE + FPUL], old.mem[CTX_BASE + FPUL]);
		check(!old.bad && !nu.bad && nu.mem[CTX_BASE + FPUL] == old.mem[CTX_BASE + FPUL] &&
		      nu.mem[CTX_BASE + FPUL] == w && only_changed(c, nu, 0, 0, ofs, 1), "flds", nu.bad ? nu.why : d);
	}
}

// fsts: FRn equals the old bounce's and lfs of the word; the direct form writes nothing else.
static void test_fsts(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const u32 fd = 14 + (u32)(rnd64() % 16), w = rnd_single();
		Cpu c;
		fresh(c);
		c.mem[CTX_BASE + FPUL] = w;
		Cpu old = c, nu = c;
		g_n = 0;
		ppc_lwz(3, CTX, FPUL);
		ppc_stw(3, CTX, fr_slot(fd));
		ppc_lfs(fd, CTX, fr_slot(fd));
		old.run(g_code, g_n);
		g_n = 0;
		emit_fsts_direct(fd, CTX, FPUL);
		nu.run(g_code, g_n);
		char d[96];
		snprintf(d, sizeof(d), "f%u w=%08X: new %016llX old %016llX", fd, w, (unsigned long long)nu.f[fd],
		         (unsigned long long)old.f[fd]);
		check(!old.bad && !nu.bad && nu.f[fd] == old.f[fd] && nu.f[fd] == lfs_conv(w) &&
		      only_changed(c, nu, 0, 1u << fd, 0, 0), "fsts", nu.bad ? nu.why : d);
	}
}

// ftrc: both forms leave fctiwz's word in FPUL (the old one after the caller's stw); the direct form changes
// only FPUL, its index register and f0.
static void test_ftrc(u32 iters)
{
	for (u32 t = 0; t < iters; t++)
	{
		const u32 fa = (t % 3) == 0 ? 1 : (t % 3) == 1 ? 25 : 14 + (u32)(rnd64() % 16);
		Cpu c;
		fresh(c);
		c.f[fa] = lfs_conv(rnd_single());
		const u32 want = fctiwz_ref(c.f[fa]);
		Cpu old = c, nu = c;
		g_n = 0;
		emit_ftrc(fa, CTX, SCRATCH, FPUL, 3, false);
		ppc_stw(3, CTX, FPUL);
		old.run(g_code, g_n);
		g_n = 0;
		emit_ftrc(fa, CTX, SCRATCH, FPUL, 3, true);
		nu.run(g_code, g_n);
		const u32 ofs[1] = { FPUL };
		char d[96];
		snprintf(d, sizeof(d), "f%u=%016llX: new %08X old %08X want %08X", fa, (unsigned long long)c.f[fa],
		         nu.mem[CTX_BASE + FPUL], old.mem[CTX_BASE + FPUL], want);
		check(!old.bad && !nu.bad && old.mem[CTX_BASE + FPUL] == want && nu.mem[CTX_BASE + FPUL] == want &&
		      only_changed(c, nu, 1u << 3, 1u << 0, ofs, 1), "ftrc", old.bad ? old.why : nu.bad ? nu.why : d);
	}
}

// float: f0 = (double)(s32) of x's low word in both forms, junk above it; only r3, r4, f0, f2 and the scratch
// slot change.
static void test_float(u32 iters)
{
	static const u32 edge[] = { 0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x80000001u, 0x7FFFFFFEu, 0x01000001u };
	for (u32 t = 0; t < iters; t++)
	{
		const u32 xw = (t % 4) == 0 ? edge[(t / 4) % 8] : (u32)rnd64();
		const u32 bias = BIAS_AT[t % 3];
		Cpu c;
		fresh(c);
		c.r[3] = (rnd64() & 0xFFFFFFFF00000000ull) | xw;
		const u64 want = dbits((double)(s32)xw);
		Cpu old = c, nu = c;
		g_n = 0;
		emit_i2f(3, 4, 2, CTX, SCRATCH, 0);
		old.run(g_code, g_n);
		g_n = 0;
		emit_i2f(3, 4, 2, CTX, SCRATCH, bias);
		nu.run(g_code, g_n);
		const u32 ofs[2] = { SCRATCH, SCRATCH + 4 };
		char d[112];
		snprintf(d, sizeof(d), "x=%08X bias@%08X: new %016llX old %016llX want %016llX", xw, bias,
		         (unsigned long long)nu.f[0], (unsigned long long)old.f[0], (unsigned long long)want);
		check(!old.bad && !nu.bad && old.f[0] == want && nu.f[0] == want &&
		      only_changed(c, nu, (1u << 3) | (1u << 4), (1u << 0) | (1u << 2), ofs, 2) &&
		      nu.mem.at(bias) == 0x43300000u && nu.mem.at(bias + 4) == 0x80000000u,
		      "float", old.bad ? old.why : nu.bad ? nu.why : d);
	}
}

int main(int argc, char** argv)
{
	const u32 iters = argc > 1 ? (u32)atoi(argv[1]) : 200000;
	// A second argument "model" skips the words, so mutate.py can tell what the model alone catches.
	if (argc <= 2 || strcmp(argv[2], "model") != 0)
		test_words();
	test_cr0(iters);
	test_flds(iters);
	test_fsts(iters);
	test_ftrc(iters);
	test_float(iters);
	printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
