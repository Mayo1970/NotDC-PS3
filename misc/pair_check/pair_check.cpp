// P105 JIT PAIR CALLS, host only: the cold paths' 64-bit C calls (cut from ps3/ps3_driver.cpp) on a PPC64 model, into
// _vmem's handler path (cut from dc/mem/_vmem.cpp, big endian): device memory must get the words the RAM path stores.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;
typedef uintptr_t unat;

#define CODE_WORDS 256
static u32 g_code[CODE_WORDS];
static u32 g_n;
static int g_call[CODE_WORDS];   // the fake C function a `bl .` at that word calls, -1 none
void ppc_emit(u32 insn) { g_call[g_n] = -1; g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"

// ---- dc/mem/_vmem.cpp's dispatch as the PS3 builds it: one MMIO handler that records its word accesses ----
#define INLINE inline
#define fastcall
#define ENDIAN_LITTLE 1
#define ENDIAN_BIG 2
#define HOST_ENDIAN ENDIAN_BIG
#define HANDLER_MAX 0x1F
[[noreturn]] static void die(const char* why) { printf("FAIL: die(%s)\n", why); exit(1); }
typedef u8 Rf8(u32); typedef u16 Rf16(u32); typedef u32 Rf32(u32);
typedef void Wf8(u32, u8); typedef void Wf16(u32, u16); typedef void Wf32(u32, u32);
static Rf8* _vmem_RF8[HANDLER_MAX + 1];
static Rf16* _vmem_RF16[HANDLER_MAX + 1];
static Rf32* _vmem_RF32[HANDLER_MAX + 1];
static Wf8* _vmem_WF8[HANDLER_MAX + 1];
static Wf16* _vmem_WF16[HANDLER_MAX + 1];
static Wf32* _vmem_WF32[HANDLER_MAX + 1];
static void* _vmem_MemInfo_ptr[0x100];
#include "vmem.inc"

static const char* g_bad;
static u32 g_rd_addr, g_rd_hi, g_rd_lo;          // the two words a read finds
static u32 g_wr_n, g_wr_addr[4], g_wr_val[4];    // the words a write stored, in order
static u32 mmio_rd32(u32 a)
{
	if (a == g_rd_addr)
		return g_rd_hi;
	if (a == g_rd_addr + 4)
		return g_rd_lo;
	g_bad = "a read of another address";
	return 0;
}
static void mmio_wr32(u32 a, u32 v)
{
	if (g_wr_n < 4)
	{
		g_wr_addr[g_wr_n] = a;
		g_wr_val[g_wr_n] = v;
	}
	g_wr_n++;
}
static u8 mmio_rd8(u32) { g_bad = "a byte read"; return 0; }
static u16 mmio_rd16(u32) { g_bad = "a halfword read"; return 0; }
static void mmio_wr8(u32, u8) { g_bad = "a byte write"; }
static void mmio_wr16(u32, u16) { g_bad = "a halfword write"; }

// The C functions the cold paths call, as _vmem.cpp's.
static u64 ReadMem64(u32 a) { return _vmem_readt<u64>(a); }
static void WriteMem64(u32 a, u64 v) { _vmem_writet<u64>(a, v); }

enum { FN_RD64, FN_WR64, FN_COUNT };
void ppc_call_c(void* fn)
{
	const u32 at = g_n;
	ppc_emit(0x48000001);   // bl . : the model calls the fake
	g_call[at] = fn == (void*)ReadMem64 ? FN_RD64 : fn == (void*)WriteMem64 ? FN_WR64 : 99;
}
template<typename T> void ppc_call_c(T* fn) { ppc_call_c((void*)fn); }

void ppc_bx(void* dst, u32 LK)
{
	const snat offs = (u8*)dst - (u8*)emit_GetCCPtr();
	ppc_bx((u32)(offs >> 2), 0, LK);
}

#include "ps3/ps3_jit_mmu.h"
#ifndef PAIR_HEADER
#define PAIR_HEADER "ps3/ps3_jit_pair.h"
#endif
#include PAIR_HEADER

// ---- FlushCold's pair branches, as ps3_driver.cpp has them ----
ppc_ireg ppc_contex = ppc_r30;
static struct { bool pair; } s_jo;
struct ColdFrag
{
	u8 kind;
	void* fuct;
	u8 fdreg;
	u16 fofs;
};
#include "clrldi.inc"

static void emit_cold(const ColdFrag& c)
{
	if (c.kind == 0)
	{
	}
#include "read.inc"
	else if (c.kind == 2)
	{
	}
#include "write.inc"
}

static int g_fail, g_checks;
static void check(bool ok, const char* what, const char* detail)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 16)
			printf("FAIL %s: %s\n", what, detail);
		g_fail++;
	}
}

static u64 g_seed = 0x9E3779B97F4A7C15ull;
static u64 rnd()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return g_seed;
}

// ---- the PPC64 model: the context's slots at CTX ----
static const u64 CTX = 0x10300000, CTX_SIZE = 0x1000;
static u8 g_ctx[CTX_SIZE];

struct Cpu
{
	u64 r[32];
	u32 f[32];     // single-precision bits: lfs and stfs move them unchanged here
	const char* why;
	void bad(const char* w) { if (!why) why = w; }
};

static u8* at(Cpu& c, u64 a, u32 n)
{
	if (a >= CTX && a + n <= CTX + CTX_SIZE)
		return &g_ctx[a - CTX];
	c.bad("an access outside the context");
	return 0;
}
static u64 ldm(Cpu& c, u64 a, u32 n)
{
	u8* p = at(c, a, n);
	u64 v = 0;
	for (u32 i = 0; p && i < n; i++)
		v = (v << 8) | p[i];
	return v;
}
static void stm(Cpu& c, u64 a, u32 n, u64 v)
{
	u8* p = at(c, a, n);
	for (u32 i = 0; p && i < n; i++)
		p[i] = (u8)(v >> (8 * (n - 1 - i)));
}

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

// The call as ELFv1 makes it: the address zero-extended in r3, a write's u64 whole in r4, a read's back in r3.
static void fake_call(Cpu& c, int fn)
{
	if (c.r[3] >> 32)
		c.bad("r3 is not the address, zero-extended");
	else if (fn == FN_WR64)
		WriteMem64((u32)c.r[3], c.r[4]);
	else if (fn == FN_RD64)
		c.r[3] = ReadMem64((u32)c.r[3]);
	else
		c.bad("bl to no fake");
	// The call may change every volatile register but a read's r3.
	if (fn != FN_RD64)
		c.r[3] = rnd();
	c.r[0] = rnd();
	for (int k = 4; k <= 12; k++)
		c.r[k] = rnd();
	for (int k = 0; k <= 13; k++)
		c.f[k] = (u32)rnd();
}

// Runs from word 0 to word end.
static void run(Cpu& c, u32 end)
{
	u32 pc = 0;
	for (int steps = 0; steps < 1000 && pc != end; steps++)
	{
		if (pc >= g_n)
		{
			c.bad("pc outside the code");
			return;
		}
		const u32 x = g_code[pc], op = x >> 26, rd = (x >> 21) & 31, ra = (x >> 16) & 31, rb = (x >> 11) & 31;
		const s64 simm = (s16)(x & 0xFFFF);
		const u64 base = ra ? c.r[ra] : 0;
		const u32 cur = pc++;
		switch (op)
		{
		case 18:	// bl
			if ((x & 3) != 1 || g_call[cur] < 0) { c.bad("a branch the cold path does not have"); return; }
			fake_call(c, g_call[cur]);
			break;
		case 24: c.r[ra] = c.r[rd] | (x & 0xFFFF); break;			// ori
		case 30:	// rldicl, rldicr
		{
			if (x & 1) { c.bad("rldic."); return; }
			const u32 sh = rb | (((x >> 1) & 1) << 5), m6 = (x >> 5) & 63, mb = (m6 >> 1) | ((m6 & 1) << 5);
			const u32 xo = (x >> 2) & 7;
			if (xo == 0) c.r[ra] = rotl64(c.r[rd], sh) & mask64(mb, 63);
			else if (xo == 1) c.r[ra] = rotl64(c.r[rd], sh) & mask64(0, mb);
			else { c.bad("op 30 form"); return; }
			break;
		}
		case 31:
			if (((x >> 1) & 0x3FF) != 444 || (x & 1)) { c.bad("op 31 xo"); return; }
			c.r[ra] = c.r[rd] | c.r[rb];						// or
			break;
		case 32: c.r[rd] = ldm(c, base + (u64)simm, 4); break;			// lwz
		case 36: stm(c, base + (u64)simm, 4, c.r[rd]); break;			// stw
		case 48: c.f[rd] = (u32)ldm(c, base + (u64)simm, 4); break;		// lfs
		case 52: stm(c, base + (u64)simm, 4, c.f[rd]); break;			// stfs
		default:
			c.bad("an opcode the model lacks");
			return;
		}
		if (c.why)
			return;
	}
	if (pc != end)
		c.bad("no end");
}

static void cpu_init(Cpu& c, u32 addr)
{
	memset(&c, 0, sizeof(c));
	for (int k = 0; k < 32; k++)
	{
		c.r[k] = rnd();
		c.f[k] = (u32)rnd();
	}
	c.r[30] = CTX;
	c.r[3] = (rnd() << 32) | addr;   // the address register's upper half is junk (clrldi32 drops it)
	c.r[4] = 0;                      // the inline lookup leaves the MMIO entry's ptr there
}

// An MMIO address in any of the handler pages (area 0's palette, sound RAM, a P2 mirror).
static u32 mmio_addr()
{
	static const u32 base[4] = { 0xA05F9000u, 0x005F9800u, 0xA0800000u, 0x80700000u };
	const u32 a = base[rnd() & 3] + (u32)(rnd() & 0x7F8);
	_vmem_MemInfo_ptr[a >> 24] = (void*)(unat)4;   // handler 1
	return a;
}

static ColdFrag frag(u8 kind)
{
	ColdFrag f;
	f.kind = kind;
	f.fuct = 0;
	f.fdreg = (rnd() & 1) ? (u8)(14 + rnd() % 17) : (u8)0xFF;
	f.fofs = (u16)(0x40 + 8 * (rnd() % 64));
	return f;
}

// fmov DRm,@Rn to device memory: word[a] = the pair's first word, word[a+4] = its second, as the RAM path stores them.
static u32 test_write(int cases, bool pair)
{
	u32 lost = 0;
	for (int i = 0; i < cases; i++)
	{
		const u32 a = mmio_addr(), hi = (u32)rnd(), lo = (u32)rnd();
		const ColdFrag f = frag(3);
		g_n = 0;
		s_jo.pair = pair;
		emit_cold(f);
		const u32 end = g_n;
		Cpu c;
		cpu_init(c, a);
		if (f.fdreg != 0xFF)
		{
			c.f[f.fdreg] = hi;
			c.f[f.fdreg + 1] = lo;
		}
		else
		{
			c.r[5] = (rnd() << 32) | hi;
			c.r[6] = (rnd() << 32) | lo;
		}
		g_wr_n = 0;
		g_bad = 0;
		run(c, end);
		const bool ok = !c.why && !g_bad && g_wr_n == 2 && g_wr_addr[0] == a && g_wr_val[0] == hi &&
		                g_wr_addr[1] == a + 4 && g_wr_val[1] == lo;
		lost += !ok;
		if (pair)
		{
			char d[200];
			snprintf(d, sizeof(d), "%08X <- %08X %08X (%s): %u writes, %08X=%08X %08X=%08X%s%s", a, hi, lo,
			         f.fdreg != 0xFF ? "pinned FRs" : "r5:r6", g_wr_n, g_wr_addr[0], g_wr_val[0], g_wr_addr[1],
			         g_wr_val[1], c.why ? ", model: " : "", c.why ? c.why : (g_bad ? g_bad : ""));
			check(ok, "pair write", d);
		}
	}
	return lost;
}

// fmov @Rm,DRn from device memory: the pair's first word = word[a], its second = word[a+4], as the RAM path loads them.
static u32 test_read(int cases, bool pair)
{
	u32 lost = 0;
	for (int i = 0; i < cases; i++)
	{
		const u32 a = mmio_addr();
		g_rd_addr = a;
		g_rd_hi = (u32)rnd();
		g_rd_lo = (u32)rnd();
		const ColdFrag f = frag(1);
		g_n = 0;
		s_jo.pair = pair;
		emit_cold(f);
		const u32 end = g_n;
		Cpu c;
		cpu_init(c, a);
		g_bad = 0;
		run(c, end);
		bool ok = !c.why && !g_bad;
		if (f.fdreg != 0xFF)
			ok = ok && c.f[f.fdreg] == g_rd_hi && c.f[f.fdreg + 1] == g_rd_lo;
		else
			ok = ok && c.r[3] == g_rd_hi && c.r[4] == g_rd_lo;   // zero-extended, as emit_pair_ret says
		lost += !ok;
		if (pair)
		{
			char d[200];
			snprintf(d, sizeof(d), "%08X -> %08X %08X (%s): %016llX %016llX%s%s", a, g_rd_hi, g_rd_lo,
			         f.fdreg != 0xFF ? "pinned FRs" : "r3:r4",
			         (unsigned long long)(f.fdreg != 0xFF ? c.f[f.fdreg] : c.r[3]),
			         (unsigned long long)(f.fdreg != 0xFF ? c.f[f.fdreg + 1] : c.r[4]), c.why ? ", model: " : "",
			         c.why ? c.why : (g_bad ? g_bad : ""));
			check(ok, "pair read", d);
		}
	}
	return lost;
}

int main(int argc, char** argv)
{
	const int cases = argc > 1 ? atoi(argv[1]) : 20000;
	_vmem_RF8[1] = mmio_rd8;
	_vmem_RF16[1] = mmio_rd16;
	_vmem_RF32[1] = mmio_rd32;
	_vmem_WF8[1] = mmio_wr8;
	_vmem_WF16[1] = mmio_wr16;
	_vmem_WF32[1] = mmio_wr32;
	test_write(cases, true);
	test_read(cases, true);
	// OLD, the model of the fault: every pair write stores 0 (Alone in the Dark's palette), every pair read is wrong.
	const u32 old_w = test_write(cases, false), old_r = test_read(cases, false);
	char d[96];
	snprintf(d, sizeof(d), "%u of %d writes and %u of %d reads lose their data", old_w, cases, old_r, cases);
	check(old_w == (u32)cases && old_r == (u32)cases, "OLD", d);
	printf("[pair] OLD: %s\n", d);
	if (g_fail)
	{
		printf("FAIL: %d of %d checks\n", g_fail, g_checks);
		return 1;
	}
	printf("PASS: %d checks (%d pair writes, %d pair reads, FIXED and OLD)\n", g_checks, cases, cases);
	return 0;
}
