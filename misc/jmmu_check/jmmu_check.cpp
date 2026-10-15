// Bundle 20 (P38), host only: ps3/ps3_jit_mmu.h against ppu-as words, then on a PPC64 model: the JTC access against
// _vmem's lane rules, each cold path around a C call that may leave an exception. Not part of the PS3 build.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

#define CODE_WORDS 2048
static u32 g_code[CODE_WORDS];
static u32 g_n;
static int g_call[CODE_WORDS];   // the fake C function a `bl .` at that word calls, -1 none
void ppc_emit(u32 insn) { g_call[g_n] = -1; g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"

// The fake C functions, by the pointer the emitters get.
enum { FN_RD8, FN_RD16, FN_RD32, FN_RD64, FN_WR8, FN_WR16, FN_WR32, FN_WR64, FN_PREF, FN_COUNT };
static char g_fn_mark[FN_COUNT];
static void* fnp(int id) { return &g_fn_mark[id]; }

void ppc_call_c(void* fn)
{
	const int id = (int)((char*)fn - g_fn_mark);
	const u32 at = g_n;
	ppc_emit(0x48000001);   // bl . : the model calls the fake
	g_call[at] = id >= 0 && id < FN_COUNT ? id : 99;
}

void ppc_bx(void* dst, u32 LK)
{
	const snat offs = (u8*)dst - (u8*)emit_GetCCPtr();
	ppc_bx((u32)(offs >> 2), 0, LK);
}

#ifndef JMMU_HEADER
#define JMMU_HEADER "ps3/ps3_jit_mmu.h"
#endif
#include JMMU_HEADER
#include "jmmu_words.inc"

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

// ---- the words ----
static void test_words()
{
	g_n = 0;
	emit_jtc_lookup(20, 0x10234560, 1)->MarkLabel();
	emit_jtc_lookup(3, 0x1023C560, 2)->MarkLabel();
	emit_jtc_lookup(27, 0x10234560, 4)->MarkLabel();
	emit_jtc_lookup(3, 0x10234560, 8)->MarkLabel();
	emit_jtc_load(1, 20, 0xFF);
	emit_jtc_load(2, 21, 0xFF);
	emit_jtc_load(4, 3, 0xFF);
	emit_jtc_load(4, 0, 15);
	emit_jtc_load_pair(3, 4, 0xFF, 0xFF);
	emit_jtc_load_pair(3, 4, 14, 15);
	emit_jtc_store(1, 20, 0xFF);
	emit_jtc_store(2, 4, 0xFF);
	emit_jtc_store(4, 26, 0xFF);
	emit_jtc_store(4, 0, 16);
	emit_jtc_store_pair(5, 6, 0xFF, 0xFF);
	emit_jtc_store_pair(0, 0, 14, 15);
	jm_rldicl(3, 3, 0, 32);
	jm_rldicl(4, 3, 0, 32);
	jm_rldicl(3, 3, 32, 32);
	jm_rldicl(7, 9, 63, 1);
	jm_rldicr(4, 5, 32, 31);
	jm_rldicr(8, 10, 1, 62);
	jm_std(3, 0x48, 30);
	jm_ld(4, 0x50, 30);
	jm_std(31, (u32)-8, 1);
	JmmuCold c = { 30, 0x48, 0x1023AABC, 0x8C012345, 0x9C, &g_code[0], &g_code[0] };
	emit_jmmu_cold_read(c, 1, 20, 21, 0xFF, fnp(FN_RD8));
	c.cyc_ofs = 0;
	emit_jmmu_cold_read(c, 4, 3, 3, 17, fnp(FN_RD32));
	emit_jmmu_cold_read_pair(c, 0xFF, 0xFF, fnp(FN_RD64));
	emit_jmmu_cold_read_pair(c, 14, 15, fnp(FN_RD64));
	emit_jmmu_cold_write(c, 2, 27, 22, 0xFF, fnp(FN_WR16));
	emit_jmmu_cold_write(c, 1, 3, 4, 0xFF, fnp(FN_WR8));
	emit_jmmu_cold_write(c, 4, 3, 0, 16, fnp(FN_WR32));
	emit_jmmu_cold_write_pair(c, 0xFF, 0xFF, fnp(FN_WR64));
	emit_jmmu_cold_write_pair(c, 14, 15, fnp(FN_WR64));
	emit_jmmu_fd_test(30, 0xF0)->MarkLabel();
	emit_jmmu_fd_exit(c);
	emit_jmmu_dispatch(0x10298000, 0x102B8AB0, 0x102BCDE0, 30, 0x114, 0x11C);
	const bool sq_md[4] = { false, true, true, false }, sq_pair[4] = { false, true, false, true };
	const bool sq_fpr[4] = { false, true, true, false };
	const u32 sq_a[4] = { 20, 3, 3, 3 }, sq_d0[4] = { 22, 14, 16, 5 }, sq_d1[4] = { 0, 15, 0, 6 };
	for (int k = 0; k < 4; k++)
	{
		const JmmuSqOther o = emit_jmmu_sq_store(sq_a[k], sq_pair[k], sq_fpr[k], sq_d0[k], sq_d1[k], 0x102C8040, sq_md[k],
		                                         0x102D9F10, &g_code[0]);
		o.area->MarkLabel();
		if (o.sqmd)
			o.sqmd->MarkLabel();
	}
	JmmuCold pc_c = { 30, 0x48, 0x1023AABC, 0x8C012345, 0x9C, &g_code[0], &g_code[0] };
	emit_jmmu_pref(pc_c, 20, fnp(FN_PREF))->MarkLabel();
	pc_c.cyc_ofs = 0;
	emit_jmmu_pref(pc_c, 3, fnp(FN_PREF))->MarkLabel();
	emit_jmmu_raise(pc_c);
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	char d[96];
	snprintf(d, sizeof(d), "%u words, ppu-as %u", g_n, want);
	check(g_n == want, "words", d);
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

// ---- the PPC64 model: JTC tables, fault word and context at 0x10200000 (2 MB), host pages at 0x20000000 (64 KB) ----
// The code a bctr reaches has 32-bit addresses, as the PS3's code cache: word i of g_code is at CODE32 + 4 * i.
static const u64 LO_BASE = 0x10200000, LO_SIZE = 0x200000, HOST_BASE = 0x20000000, HOST_SIZE = 0x10000;
static const u64 CODE32 = 0x00800000;
static u8 g_lo[LO_SIZE], g_host[HOST_SIZE];

struct Cpu
{
	u64 r[32];
	u32 f[32];     // single-precision bits: lfs and stfs move them unchanged here
	u32 cr0;       // LT 8, GT 4, EQ 2
	u64 ctr;
	const char* why;
	void bad(const char* w) { if (!why) why = w; }
};

static u8* at(Cpu& c, u64 a, u32 n)
{
	if (a >= LO_BASE && a + n <= LO_BASE + LO_SIZE)
		return &g_lo[a - LO_BASE];
	if (a >= HOST_BASE && a + n <= HOST_BASE + HOST_SIZE)
		return &g_host[a - HOST_BASE];
	c.bad("an access outside the model's memory (a poisoned address?)");
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
static u64 host_ld(u64 a, u32 n) { Cpu c = {}; return ldm(c, a, n); }
static void host_st(u64 a, u32 n, u64 v) { Cpu c = {}; stm(c, a, n, v); }
static const u8* at_host(u64 a) { Cpu c = {}; return at(c, a, 64); }

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
static void set_cr(Cpu& c, s64 a, s64 b) { c.cr0 = a < b ? 8 : a > b ? 4 : 2; }
static void set_cru(Cpu& c, u64 a, u64 b) { c.cr0 = a < b ? 8 : a > b ? 4 : 2; }

// A fake call: what it must get and what it gives back.
struct Call
{
	int fn;
	u32 va;            // r3 in, which must be zero-extended
	u64 r4;            // r4 in for a write, exactly (a sub-word value masked, a pair joined)
	u64 ret;           // r3 back (a 32-bit value gets junk above it)
	u32 fault;         // the code it leaves in the fault word
	u32 fault_addr;
	int calls;
	const char* why;
};
static Call g_callp;

static void fake_call(Cpu& c, int fn)
{
	Call& p = g_callp;
	p.calls++;
	if (fn != p.fn)
		p.why = "the wrong C function";
	else if (c.r[3] != p.va)
		p.why = "r3 is not the address, zero-extended";
	else if (fn >= FN_WR8 && fn <= FN_WR64 && c.r[4] != p.r4)
		p.why = "r4 is not the value as the C function takes it";
	c.r[3] = fn == FN_RD64 ? p.ret : (p.ret & 0xFFFFFFFFull) | (rnd() << 32);
	stm(c, p.fault_addr, 4, p.fault);
	// The C call may change every volatile register but r3 (its result) and CR0.
	c.r[0] = rnd();
	for (int k = 4; k <= 12; k++)
		c.r[k] = rnd();
	c.cr0 = (u32)(rnd() & 14);
}

// Runs from word 0 until the pc reaches one of the stop words; returns it, or -1 with why set.
static int run(Cpu& c, u32 stop_a, u32 stop_b, u32 stop_c = 0xFFFFFFFF)
{
	u32 pc = 0;
	for (int steps = 0; steps < 4000; steps++)
	{
		if (pc == stop_a || pc == stop_b || pc == stop_c)
			return (int)pc;
		if (pc >= CODE_WORDS)
		{
			c.bad("pc outside the code");
			return -1;
		}
		const u32 x = g_code[pc], op = x >> 26, rd = (x >> 21) & 31, ra = (x >> 16) & 31, rb = (x >> 11) & 31;
		const s64 simm = (s16)(x & 0xFFFF);
		const u64 uimm = x & 0xFFFF;
		const u64 base = ra ? c.r[ra] : 0;
		const u32 cur = pc++;
		switch (op)
		{
		case 10:	// cmpli
			if ((x >> 23) & 7) { c.bad("cmpli crf"); return -1; }
			if ((x >> 21) & 1) set_cru(c, c.r[ra], uimm); else set_cru(c, (u32)c.r[ra], uimm);
			break;
		case 11:	// cmpi
			if ((x >> 23) & 7) { c.bad("cmpi crf"); return -1; }
			if ((x >> 21) & 1) set_cr(c, (s64)c.r[ra], simm); else set_cr(c, (s32)c.r[ra], simm);
			break;
		case 14: c.r[rd] = base + (u64)simm; break;					// addi
		case 15: c.r[rd] = base + ((u64)simm << 16); break;				// addis
		case 16:	// bc
		{
			const u32 bo = rd, bi = ra;
			if ((x & 3) || bi > 3 || !(bo & 4)) { c.bad("bc form"); return -1; }
			const u32 bit = (c.cr0 >> (3 - bi)) & 1;
			if ((bo & 0x10) || bit == ((bo >> 3) & 1))
				pc = (u32)((s64)cur + ((s16)(x & 0xFFFC) >> 2));
			break;
		}
		case 18:	// b, bl
			if (x & 2) { c.bad("b AA"); return -1; }
			if (x & 1)
			{
				if (g_call[cur] < 0 || g_call[cur] >= FN_COUNT) { c.bad("bl to no fake"); return -1; }
				fake_call(c, g_call[cur]);
				break;
			}
			pc = (u32)((s64)cur + (((s32)(x << 6) >> 6) >> 2));
			break;
		case 19:	// bctr, to a word of g_code at the 32-bit address CODE32 + 4 * index
			if ((x & 0xFFE0FFFFu) != 0x4E800420u) { c.bad("op 19"); return -1; }	// BO 20: any BI
			if (c.ctr < CODE32 || c.ctr >= CODE32 + 4 * CODE_WORDS || (c.ctr & 3)) { c.bad("bctr outside the code"); return -1; }
			pc = (u32)((c.ctr - CODE32) >> 2);
			break;
		case 21:	// rlwinm
		{
			if (x & 1) { c.bad("rlwinm."); return -1; }
			const u32 sh = rb, mb = (x >> 6) & 31, me = (x >> 1) & 31;
			const u64 w = c.r[rd] & 0xFFFFFFFFull;
			c.r[ra] = rotl64(w | (w << 32), sh) & mask64(mb + 32, me + 32);
			break;
		}
		case 24: c.r[ra] = c.r[rd] | uimm; break;					// ori
		case 26: c.r[ra] = c.r[rd] ^ uimm; break;					// xori
		case 28: c.r[ra] = c.r[rd] & uimm; set_cr(c, (s64)c.r[ra], 0); break;	// andi.
		case 30:	// rldicl, rldicr
		{
			if (x & 1) { c.bad("rldic."); return -1; }
			const u32 sh = rb | (((x >> 1) & 1) << 5), m6 = (x >> 5) & 63, mb = (m6 >> 1) | ((m6 & 1) << 5);
			const u32 xo = (x >> 2) & 7;
			if (xo == 0) c.r[ra] = rotl64(c.r[rd], sh) & mask64(mb, 63);
			else if (xo == 1) c.r[ra] = rotl64(c.r[rd], sh) & mask64(0, mb);
			else { c.bad("op 30 form"); return -1; }
			break;
		}
		case 31:
		{
			const u32 xo = (x >> 1) & 0x3FF;
			if ((x & 1) && xo != 0) { c.bad("op 31 record form"); return -1; }
			const u64 idx = base + c.r[rb];
			switch (xo)
			{
			case 0:   if ((x >> 21) & 1) set_cr(c, (s64)c.r[ra], (s64)c.r[rb]); else set_cr(c, (s32)c.r[ra], (s32)c.r[rb]); break;
			case 32:  if ((x >> 21) & 1) set_cru(c, c.r[ra], c.r[rb]); else set_cru(c, (u32)c.r[ra], (u32)c.r[rb]); break;
			case 266: c.r[rd] = c.r[ra] + c.r[rb]; break;			// add
			case 444: c.r[ra] = c.r[rd] | c.r[rb]; break;			// or
			case 316: c.r[ra] = c.r[rd] ^ c.r[rb]; break;			// xor
			case 26:	// cntlzw: the low word's leading zeros
			{
				const u32 w = (u32)c.r[rd];
				c.r[ra] = w ? (u64)__builtin_clz(w) : 32;
				break;
			}
			case 87:  c.r[rd] = ldm(c, idx, 1); break;			// lbzx
			case 343: c.r[rd] = (u64)(s64)(s16)ldm(c, idx, 2); break;	// lhax
			case 23:  c.r[rd] = ldm(c, idx, 4); break;			// lwzx
			case 535: c.f[rd] = (u32)ldm(c, idx, 4); break;			// lfsx
			case 215: stm(c, idx, 1, c.r[rd]); break;			// stbx
			case 407: stm(c, idx, 2, c.r[rd]); break;			// sthx
			case 151: stm(c, idx, 4, c.r[rd]); break;			// stwx
			case 663: stm(c, idx, 4, c.f[rd]); break;			// stfsx
			case 954: c.r[ra] = (u64)(s64)(s8)c.r[rd]; break;		// extsb
			case 922: c.r[ra] = (u64)(s64)(s16)c.r[rd]; break;		// extsh
			case 467:	// mtctr
				if (((x >> 16) & 31) != 9 || ((x >> 11) & 31) != 0) { c.bad("mtspr"); return -1; }
				c.ctr = c.r[rd];
				break;
			default: c.bad("op 31 xo"); return -1;
			}
			break;
		}
		case 32: c.r[rd] = ldm(c, base + (u64)simm, 4); break;			// lwz
		case 36: stm(c, base + (u64)simm, 4, c.r[rd]); break;			// stw
		case 48: c.f[rd] = (u32)ldm(c, base + (u64)simm, 4); break;		// lfs
		case 52: stm(c, base + (u64)simm, 4, c.f[rd]); break;			// stfs
		case 58:	// ld
			if (x & 3) { c.bad("ld form"); return -1; }
			c.r[rd] = ldm(c, base + (u64)(s64)(s16)(x & 0xFFFC), 8);
			break;
		case 62:	// std
			if (x & 3) { c.bad("std form"); return -1; }
			stm(c, base + (u64)(s64)(s16)(x & 0xFFFC), 8, c.r[rd]);
			break;
		default:
			c.bad("an opcode the model lacks");
			return -1;
		}
		if (c.why)
			return -1;
	}
	c.bad("no end");
	return -1;
}

// ---- the fast path against _vmem's rules ----
static const u32 TBL[2] = { 0x10234560, 0x1023C560 };   // a positive and a negative low half
static const u32 DONE = 1900, MISS = 1950;

// _vmem_readt's direct path on the host page: a sub-word access at its lane, addr ^ (4 - sz).
static u64 ref_read(u32 host, u32 va, u32 sz)
{
	u32 off = va & 0x3FF;
	if (sz < 4)
		off ^= 4 - sz;
	return host_ld((u64)host + off, sz);
}
static void ref_write(u32 host, u32 va, u32 sz, u64 v)
{
	u32 off = va & 0x3FF;
	if (sz < 4)
		off ^= 4 - sz;
	host_st((u64)host + off, sz, v);
}

static void fill_host() { for (u32 i = 0; i < HOST_SIZE; i++) g_host[i] = (u8)rnd(); }

static void test_fast(int cases)
{
	static const u32 sizes[4] = { 1, 2, 4, 8 };
	char d[160];
	int hits = 0, misses = 0;
	for (int n = 0; n < cases; n++)
	{
		const u32 sz = sizes[rnd() & 3], tbl = TBL[rnd() & 1];
		const bool write = rnd() & 1, fpr = (sz == 4 || sz == 8) && (rnd() % 3 == 0);
		// Pairs keep the address in r3 (rarg0); scalars use a pinned register or r3.
		const u32 areg = sz == 8 ? 3 : (rnd() & 1) ? 20 : 3;
		const u32 dreg = sz == 8 ? 3 : (rnd() & 1) ? 21 : (rnd() & 1) ? areg : 3;
		u32 va = (u32)rnd();
		const u32 kind = (u32)(rnd() % 4);
		if (kind == 0)
			va &= ~(sz - 1);		// aligned
		const u32 idx = (va >> 10) & 1023;
		const u32 host = (u32)(HOST_BASE + ((rnd() % (HOST_SIZE / 1024)) << 10));
		u32 tag = va & ~0x3FFu;
		if (kind == 2)
			tag ^= 0x400u << (rnd() % 22);	// another page
		else if (kind == 3)
			tag = 0xFFFFFFFF;
		host_st(tbl + idx * 8, 4, tag);
		host_st(tbl + idx * 8 + 4, 4, host);
		fill_host();
		const bool aligned = (va & (sz - 1)) == 0;
		const bool hit = aligned && tag == (va & ~0x3FFu);

		g_n = 0;
		ppc_label* miss = emit_jtc_lookup(areg, tbl, sz);
		const u32 f0 = fpr ? 14 : 0xFF, f1 = fpr ? 15 : 0xFF;
		if (sz == 8 && write)
			emit_jtc_store_pair(5, 6, f0, f1);
		else if (sz == 8)
			emit_jtc_load_pair(3, 4, f0, f1);
		else if (write)
			emit_jtc_store(sz, 22, f0);
		else
			emit_jtc_load(sz, dreg, f0);
		ppc_bx(&g_code[DONE], 0);
		miss->MarkLabel();
		ppc_bx(&g_code[MISS], 0);

		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		c.r[areg] = (rnd() << 32) | va;	// a poisoned upper half, as pinned registers have
		for (int k = 0; k < 32; k++)
			c.f[k] = (u32)rnd();
		const Cpu before = c;
		static u8 host_before[HOST_SIZE], want[HOST_SIZE];
		memcpy(host_before, g_host, HOST_SIZE);
		if (write && hit)
		{
			if (sz == 8)
			{
				ref_write(host, va, 4, fpr ? before.f[14] : (u32)before.r[5]);
				ref_write(host, va + 4, 4, fpr ? before.f[15] : (u32)before.r[6]);
			}
			else
				ref_write(host, va, sz, fpr ? before.f[14] : before.r[22]);
			memcpy(want, g_host, HOST_SIZE);
			memcpy(g_host, host_before, HOST_SIZE);
		}
		const int end = run(c, DONE, MISS);
		snprintf(d, sizeof(d), "sz %u %s%s va %08X tag %08X areg r%u d r%u: %s", sz, write ? "write" : "read",
		         fpr ? " fpr" : "", va, tag, areg, dreg, c.why ? c.why : "");
		if (end < 0)
		{
			check(false, "fast path run", d);
			continue;
		}
		check(end == (int)(hit ? DONE : MISS), hit ? "a hit took the miss" : "a miss took the hit", d);
		if (!hit)
		{
			misses++;
			check(!memcmp(host_before, g_host, HOST_SIZE), "a miss wrote memory", d);
			continue;
		}
		hits++;
		if (write)
		{
			check(!memcmp(want, g_host, HOST_SIZE), "the store's bytes", d);
			continue;
		}
		if (sz == 8)
		{
			const u32 w0 = (u32)ref_read(host, va, 4), w1 = (u32)ref_read(host, va + 4, 4);
			if (fpr)
				check(c.f[14] == w0 && c.f[15] == w1, "pair into the FPRs", d);
			else
				check((u32)c.r[3] == w0 && (u32)c.r[4] == w1, "pair: word[a] in r3, word[a+4] in r4", d);
		}
		else
		{
			const u64 v = ref_read(host, va, sz);
			const u32 want = sz == 1 ? (u32)(s32)(s8)v : sz == 2 ? (u32)(s32)(s16)v : (u32)v;
			if (fpr)
				check(c.f[14] == want, "into the FPR", d);
			else
				check((u32)c.r[dreg] == want, "the loaded value, sign-extended", d);
		}
		// Only the destination and the scratch registers change.
		bool kept = true;
		for (int k = 13; k < 32; k++)
			if (k != (int)dreg && c.r[k] != before.r[k])
				kept = false;
		check(kept, "a register other than the destination changed", d);
	}
	char line[64];
	snprintf(line, sizeof(line), "%d hits, %d misses", hits, misses);
	check(hits > cases / 10 && misses > cases / 10, "too few cases of a kind", line);
}

// ---- the cold paths ----
// The fault word's low half is negative, so its addis needs the carry.
static const u32 FAULT = 0x1026AABC, CTX = 0x10300000, FOFS = 0x48, CYC = 0x9C, EPC = 0x8C012345;

static void test_cold(int cases)
{
	static const u32 scalar[3] = { 1, 2, 4 };
	char d[160];
	int faults = 0, oks = 0;
	for (int n = 0; n < cases; n++)
	{
		const int kind = (int)(rnd() % 4);	// read, read pair, write, write pair
		const u32 sz = kind == 1 || kind == 3 ? 8 : scalar[rnd() % 3];
		const bool fpr = (sz == 4 || sz == 8) && (rnd() & 1);
		const u32 areg = sz == 8 ? 3 : (rnd() & 1) ? 20 : 3;
		const u32 dreg = (rnd() & 1) ? 21 : 3, sreg = (rnd() & 1) ? 22 : 4;
		const bool fault = rnd() % 3 == 0;
		const u32 va = (u32)rnd();
		JmmuCold jc = { 30, FOFS, FAULT, EPC, (rnd() & 1) ? CYC : 0, &g_code[MISS], &g_code[DONE] };

		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		for (int k = 0; k < 32; k++)
			c.f[k] = (u32)rnd();
		c.r[30] = CTX;
		c.r[areg] = (rnd() << 32) | va;
		host_st(CTX + CYC, 4, 0);
		g_callp = Call();
		g_callp.va = va;
		g_callp.fault = fault ? 0x040 : 0;
		g_callp.fault_addr = FAULT;
		g_callp.ret = rnd();

		g_n = 0;
		if (kind == 0)
		{
			g_callp.fn = sz == 1 ? FN_RD8 : sz == 2 ? FN_RD16 : FN_RD32;
			emit_jmmu_cold_read(jc, sz, areg, dreg, fpr ? 17 : 0xFF, fnp(g_callp.fn));
		}
		else if (kind == 1)
		{
			g_callp.fn = FN_RD64;
			emit_jmmu_cold_read_pair(jc, fpr ? 14 : 0xFF, fpr ? 15 : 0xFF, fnp(FN_RD64));
		}
		else if (kind == 2)
		{
			g_callp.fn = sz == 1 ? FN_WR8 : sz == 2 ? FN_WR16 : FN_WR32;
			const u64 v = fpr ? c.f[16] : c.r[sreg];
			g_callp.r4 = sz == 1 ? (v & 0xFF) : sz == 2 ? (v & 0xFFFF) : (v & 0xFFFFFFFFull);
			emit_jmmu_cold_write(jc, sz, areg, sreg, fpr ? 16 : 0xFF, fnp(g_callp.fn));
		}
		else
		{
			g_callp.fn = FN_WR64;
			g_callp.r4 = fpr ? ((u64)c.f[14] << 32) | c.f[15] : (c.r[5] << 32) | (c.r[6] & 0xFFFFFFFFull);
			emit_jmmu_cold_write_pair(jc, fpr ? 14 : 0xFF, fpr ? 15 : 0xFF, fnp(FN_WR64));
		}
		const Cpu before = c;
		const int end = run(c, DONE, MISS);
		snprintf(d, sizeof(d), "kind %d sz %u%s areg r%u d r%u s r%u fault %d cyc %u: %s %s", kind, sz, fpr ? " fpr" : "",
		         areg, dreg, sreg, (int)fault, jc.cyc_ofs, c.why ? c.why : "", g_callp.why ? g_callp.why : "");
		if (end < 0 || g_callp.why || g_callp.calls != 1)
		{
			check(false, "cold path run", d);
			continue;
		}
		if (jc.cyc_ofs)
			check(host_ld(CTX + CYC, 4) == (u32)before.r[29], "the cycle count stored before the call", d);
		if (fault)
		{
			faults++;
			check(end == (int)MISS && (u32)c.r[3] == EPC, "a fault exits with r3 = the SPC", d);
			if (kind == 0)
				check(fpr ? c.f[17] == before.f[17] : (dreg == 3 || c.r[dreg] == before.r[dreg]),
				      "a fault wrote the destination", d);
			continue;
		}
		oks++;
		check(end == (int)DONE, "no fault: back to done", d);
		const u64 ret = g_callp.ret;
		if (kind == 0)
		{
			const u32 want = sz == 1 ? (u32)(s32)(s8)ret : sz == 2 ? (u32)(s32)(s16)ret : (u32)ret;
			if (fpr)
				check(c.f[17] == (u32)ret, "the value into the FPR", d);
			else
				check((u32)c.r[dreg] == want, "the value, sign-extended", d);
		}
		else if (kind == 1)
		{
			if (fpr)
				check(c.f[14] == (u32)(ret >> 32) && c.f[15] == (u32)ret, "the u64 into the FPR pair", d);
			else
				check((u32)c.r[3] == (u32)(ret >> 32) && (u32)c.r[4] == (u32)ret, "the u64 into r3:r4", d);
		}
		bool kept = true;
		for (int k = 13; k < 32; k++)
			if (!(kind == 0 && !fpr && k == (int)dreg) && c.r[k] != before.r[k])
				kept = false;
		check(kept, "a non-volatile register changed", d);
	}
	char line[64];
	snprintf(line, sizeof(line), "%d faults, %d calls back", faults, oks);
	check(faults > cases / 10 && oks > cases / 10, "too few cases of a kind", line);
}

// The FPU-disabled test and exit: FD set leaves with 0x800 in the fault word and r3 = the block's start.
static void test_fd()
{
	char d[96];
	for (int fd = 0; fd < 2; fd++)
	{
		JmmuCold jc = { 30, FOFS, FAULT, EPC, 0, &g_code[MISS], &g_code[DONE] };
		g_n = 0;
		ppc_label* off = emit_jmmu_fd_test(30, 0xF0);
		ppc_bx(&g_code[DONE], 0);
		off->MarkLabel();
		emit_jmmu_fd_exit(jc);
		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		c.r[30] = CTX;
		memset(g_lo, 0, LO_SIZE);
		const u32 sr = (u32)rnd() & ~0x8000u;
		host_st(CTX + 0xF0, 4, sr | (fd ? 0x8000u : 0));
		const int end = run(c, DONE, MISS);
		snprintf(d, sizeof(d), "FD %d SR %08X: %s", fd, sr, c.why ? c.why : "");
		check(end == (int)(fd ? MISS : DONE), "the FD branch", d);
		if (fd)
			check(host_ld(FAULT, 4) == 0x800 && (u32)c.r[3] == EPC, "0x800 and the SPC", d);
	}
}

// ---- row MMU Block Cache: the dispatch against the index and key of sh4_mmu_jit.cpp, written apart here ----
static const u32 DTBL = 0x10298000, DGEN = 0x102B8AB0, DPTEH = 0x102BCDE0, SR_OFS = 0x114, FPSCR_OFS = 0x11C;

static u32 ref_index(u32 pc) { return ((pc ^ (pc >> 12)) >> 1) & 4095; }
static u32 ref_key(u32 sr, u32 fpscr, u32 pteh)
{
	return ((sr >> 30) & 1) | (((fpscr >> 19) & 1) << 2) | (((fpscr >> 20) & 1) << 3) | ((fpscr & 3) == 1 ? 16 : 0) |
	       (pteh & 0xFF) << 8;
}

// The way the dispatch takes: the first whose pc matches, a hit if its key and generation match too.
static int ref_way(u32 set, u32 pc, u32 key, u32 gen)
{
	for (u32 w = 0; w < 2; w++)
		if ((u32)host_ld(set + w * 16, 4) == pc)
			return (u32)host_ld(set + w * 16 + 4, 4) == key && (u32)host_ld(set + w * 16 + 8, 4) == gen ? (int)w : -1;
	return -1;
}

static const u32 DONE1 = 1925;

static void test_dispatch(int cases)
{
	char d[160];
	int kinds[7] = {};
	for (int n = 0; n < cases; n++)
	{
		const u32 pc = (u32)rnd(), sr = (u32)rnd(), pteh = (u32)rnd(), gen = (u32)rnd();
		// FPSCR's RM 1 in a quarter of the cases, so the key's bit 4 is seen set.
		const u32 fpscr = (rnd() & 3) ? (u32)rnd() : (((u32)rnd() & ~3u) | 1);
		const u32 key = ref_key(sr, fpscr, pteh);
		// 0, 1: a hit in way 0, 1; 2: the pc in no way, both with its key and generation; 3, 4: its way with a wrong key,
		// generation; 5: a hit in way 1, way 0 stale; 6: the pc in both ways, way 0's key wrong (way 0 taken: a miss).
		const int kind = (int)(rnd() % 7);
		const u32 set = DTBL + ref_index(pc) * 32;
		const u32 way = (kind == 1 || kind == 5 || (kind >= 3 && kind <= 4 && (rnd() & 1))) ? 1 : 0;
		for (u32 w = 0; w < 2; w++)
		{
			u32 other = (u32)rnd();
			if (other == pc)
				other ^= 2;
			host_st(set + w * 16, 4, other);
			host_st(set + w * 16 + 4, 4, kind == 2 ? key : (u32)rnd());
			host_st(set + w * 16 + 8, 4, kind == 2 ? gen : (u32)rnd());
			host_st(set + w * 16 + 12, 4, CODE32 + 4 * (w ? DONE1 : DONE));
		}
		if (kind != 2)
		{
			const u32 e = set + way * 16;
			host_st(e, 4, pc);
			host_st(e + 4, 4, kind == 3 ? key ^ (1u << (rnd() % 32)) : key);
			host_st(e + 8, 4, kind == 4 ? gen ^ (1u << (rnd() % 32)) : gen);
			if (kind == 5)
				host_st(set + 8, 4, gen ^ 1);
			if (kind == 6)
			{
				host_st(set, 4, pc);
				host_st(set + 4, 4, key ^ (1u << (rnd() % 32)));
				host_st(set + 16, 4, pc);
				host_st(set + 20, 4, key);
				host_st(set + 24, 4, gen);
			}
		}
		const int want = ref_way(set, pc, key, gen);
		host_st(DGEN, 4, gen);
		host_st(DPTEH, 4, pteh);
		host_st(CTX + SR_OFS, 4, sr);
		host_st(CTX + FPSCR_OFS, 4, fpscr);
		g_n = 0;
		emit_jmmu_dispatch(DTBL, DGEN, DPTEH, 30, SR_OFS, FPSCR_OFS);
		ppc_bx(&g_code[MISS], 0);
		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		c.r[30] = CTX;
		c.r[3] = (rnd() << 32) | pc;	// a poisoned upper half, as `li` of a P1 pc leaves
		const Cpu before = c;
		const int end = run(c, DONE, MISS, DONE1);
		snprintf(d, sizeof(d), "kind %d pc %08X sr %08X fpscr %08X pteh %08X: %s", kind, pc, sr, fpscr, pteh,
		         c.why ? c.why : "");
		if (end < 0)
		{
			check(false, "dispatch run", d);
			continue;
		}
		kinds[kind]++;
		check(end == (int)(want < 0 ? MISS : want ? DONE1 : DONE), want < 0 ? "a wrong entry was entered" :
		      "the hit's way not taken", d);
		check((kind == 0 || kind == 1 || kind == 5) == (want >= 0), "the case's own reference", d);
		bool kept = true;
		for (int k = 1; k < 32; k++)
			if (k != 10 && k != 11 && k != 12 && c.r[k] != before.r[k])
				kept = false;
		check(kept, "a register other than r0 and r10-r12 changed", d);
	}
	char line[64];
	snprintf(line, sizeof(line), "%d %d %d %d %d %d %d", kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5],
	         kinds[6]);
	bool enough = true;
	for (int k = 0; k < 7; k++)
		enough = enough && kinds[k] > cases / 10;
	check(enough, "too few cases", line);
}

// ---- row MMU Pref JIT: the store queue test, the call and its fault exit ----
static void test_pref(int cases)
{
	char d[160];
	int skips = 0, calls = 0, faults = 0;
	for (int n = 0; n < cases; n++)
	{
		const bool sq = rnd() % 3 != 0, fault = sq && rnd() % 3 == 0;
		u32 va = (u32)rnd();
		if (sq)
			va = 0xE0000000 | (va & 0x03FFFFFF);
		else if ((va >> 26) == 0x38)
			va ^= 0x04000000;
		const u32 areg = (rnd() & 1) ? 20 : 3;
		JmmuCold jc = { 30, FOFS, FAULT, EPC, (rnd() & 1) ? CYC : 0, &g_code[MISS], &g_code[DONE] };
		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		c.r[30] = CTX;
		c.r[areg] = (rnd() << 32) | va;
		host_st(CTX + CYC, 4, 0);
		g_callp = Call();
		g_callp.fn = FN_PREF;
		g_callp.va = va;
		g_callp.ret = fault ? 0x040 : 0;
		g_callp.fault_addr = FAULT + 0x100;
		g_n = 0;
		ppc_label* exc = emit_jmmu_pref(jc, areg, fnp(FN_PREF));
		ppc_bx(&g_code[DONE], 0);
		exc->MarkLabel();
		emit_jmmu_raise(jc);
		const Cpu before = c;
		const int end = run(c, DONE, MISS);
		snprintf(d, sizeof(d), "va %08X areg r%u fault %d cyc %u: %s %s", va, areg, (int)fault, jc.cyc_ofs,
		         c.why ? c.why : "", g_callp.why ? g_callp.why : "");
		if (end < 0 || g_callp.why)
		{
			check(false, "pref run", d);
			continue;
		}
		check(g_callp.calls == (sq ? 1 : 0), sq ? "a store queue address not flushed" : "another address called the flush",
		      d);
		check(end == (int)(fault ? MISS : DONE), fault ? "a fault not taken" : "no fault, but the exit", d);
		if (fault)
			check((u32)c.r[3] == EPC, "a fault exits with r3 = the SPC", d);
		if (sq && jc.cyc_ofs)
			check(host_ld(CTX + CYC, 4) == (u32)before.r[29], "the cycle count stored before the call", d);
		bool kept = true;
		for (int k = 13; k < 32; k++)
			if (c.r[k] != before.r[k])
				kept = false;
		check(kept, "a non-volatile register changed", d);
		skips += !sq;
		calls += sq && !fault;
		faults += fault;
	}
	char line[64];
	snprintf(line, sizeof(line), "%d skips, %d calls, %d faults", skips, calls, faults);
	check(skips > cases / 10 && calls > cases / 10 && faults > cases / 10, "too few cases of a kind", line);
}

// ---- row MMU SQ Stores: the store against the rule of xlat_data (sh4_mmu_int.cpp) for the store queue area ----
static const u32 SQB = 0x102C8040, MMUCR_A = 0x102D9F10;

static void test_sq(int cases)
{
	char d[160];
	int stored = 0, passed = 0;
	for (int n = 0; n < cases; n++)
	{
		const bool pair = rnd() & 1, fpr = rnd() & 1, md = rnd() & 1, sqmd = rnd() & 1;
		const u32 areg = pair ? 3 : (rnd() & 1) ? 20 : 3;
		const u32 d0 = fpr ? (pair ? 14 : 16) : pair ? 5 : (rnd() & 1) ? 22 : 4, d1 = fpr ? 15 : 6;
		u32 va = (u32)rnd();
		const u32 kind = (u32)(rnd() % 4);
		if (kind < 3)
			va = 0xE0000000 | (va & 0x03FFFFFF);	// the store queue area
		if (kind == 0)
			va &= pair ? ~7u : ~3u;
		const bool aligned = (va & (pair ? 7 : 3)) == 0;
		const bool want = (va >> 26) == 0x38 && aligned && (md || !sqmd);
		host_st(MMUCR_A, 4, ((u32)rnd() & ~0x200u) | (sqmd ? 0x200u : 0));
		for (u32 i = 0; i < 64; i += 4)
			host_st(SQB + i, 4, (u32)rnd());
		u8 before_sq[64];
		memcpy(before_sq, at_host(SQB), 64);
		g_n = 0;
		const JmmuSqOther o = emit_jmmu_sq_store(areg, pair, fpr, d0, d1, SQB, md, MMUCR_A, &g_code[DONE]);
		o.area->MarkLabel();
		if (o.sqmd)
			o.sqmd->MarkLabel();
		ppc_bx(&g_code[MISS], 0);
		Cpu c = {};
		for (int k = 0; k < 32; k++)
			c.r[k] = rnd();
		for (int k = 0; k < 32; k++)
			c.f[k] = (u32)rnd();
		c.r[areg] = (rnd() << 32) | va;
		const Cpu before = c;
		const int end = run(c, DONE, MISS);
		snprintf(d, sizeof(d), "va %08X %s%s md %d sqmd %d: %s", va, pair ? "pair" : "scalar", fpr ? " fpr" : "", (int)md,
		         (int)sqmd, c.why ? c.why : "");
		if (end < 0)
		{
			check(false, "sq run", d);
			continue;
		}
		check(end == (int)(want ? DONE : MISS), want ? "a store queue store went to C" : "a store took the store queue", d);
		u8 want_sq[64];
		memcpy(want_sq, before_sq, 64);
		if (want)
		{
			stored++;
			const u32 off = va & (pair ? 0x38 : 0x3C);
			const u32 w0 = fpr ? before.f[d0] : (u32)before.r[d0], w1 = fpr ? before.f[d1] : (u32)before.r[d1];
			for (u32 i = 0; i < 4; i++)
				want_sq[off + i] = (u8)(w0 >> (24 - 8 * i));
			if (pair)
				for (u32 i = 0; i < 4; i++)
					want_sq[off + 4 + i] = (u8)(w1 >> (24 - 8 * i));
		}
		else
			passed++;
		check(!memcmp(want_sq, at_host(SQB), 64), "the store queue bytes", d);
		bool kept = true;
		for (int k = 1; k < 32; k++)
			if (k != 11 && k != 12 && c.r[k] != before.r[k])
				kept = false;
		check(kept, "a register other than r0, r11 and r12 changed", d);
	}
	char line[64];
	snprintf(line, sizeof(line), "%d stored, %d to C", stored, passed);
	check(stored > cases / 8 && passed > cases / 8, "too few cases of a kind", line);
}

int main(int argc, char** argv)
{
	const int cases = argc > 1 ? atoi(argv[1]) : 20000;
	// JMMU_NOWORDS: the model alone, so mutate.py can show it catches a bug without the ppu-as words.
	if (!getenv("JMMU_NOWORDS"))
		test_words();
	test_fast(cases);
	test_cold(cases);
	test_fd();
	test_dispatch(cases);
	test_sq(cases);
	test_pref(cases);
	printf("%s: %d checks, %d failed (%d fast and cold cases each)\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail, cases);
	return g_fail ? 1 : 0;
}
