// Host-only check for rows JIT SHORT FORMS (ON, 8C PREFIX) and SCHEDULE FORMS (ps3/ps3_jit_fmem.h): each form against
// the old one on a PPC64 model, the cold path run as its slow access; the interleaved SQ form against the old one.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t snat;

static u32 g_code[256];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }

#include "emitter/PPCEmit/ppc_emitter.h"
#include "ps3/ps3_jit_sq.h"
void ppc_bx(void* dst, u32 LK) { ppc_bx((u32)(((u8*)dst - (u8*)emit_GetCCPtr()) >> 2), 0, LK); }
#ifndef FMEM_HEADER
#define FMEM_HEADER "ps3/ps3_jit_fmem.h"
#endif
#include FMEM_HEADER
#include "misc/sched_check/ppc_model.h"

static const u32 RAM_HI = 0x3051, SQ_BASE = 0x11E4D300;
static const u64 RAM = (u64)RAM_HI << 16;
static u32 bias_hi(u32 prefix) { return (u32)(((s64)RAM - ((s64)prefix << 24)) >> 16) & 0xFFFF; }

static int g_fail, g_checks;
static u32 g_expected_diff, g_fast, g_cold, g_8c_fast, g_0c_fast, g_8c_mirror, g_8c_junk;
static void fail(const char* what, const char* detail)
{
	if (g_fail < 20)
		printf("FAIL %s: %s\n", what, detail);
	g_fail++;
}

static u64 g_seed = 0x9E3779B97F4A7C15ull;
static u64 rnd() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17; return g_seed; }
static u32 rn(u32 n) { return (u32)(rnd() % n); }

// The old form, as ps3_driver.cpp emits it with the rows off: guard, branch, host address, access, extension.
static ppc_label* old_emit(const FmAcc& a, void** done)
{
	const bool pair = a.kind & 1;
	const u32 sz = pair ? 4 : a.sz;
	ppc_rlwinmx(ppc_r0, a.areg, 3, 29, 2, 0);
	ppc_xoris(ppc_r0, ppc_r0, 0x6000);
	ppc_cmpli(ppc_cr0, ppc_r0, 7, 0);
	ppc_label* cold = ppc_CreateLabel();
	ppc_bcx(BO_FALSE, BI_CR0_LT, 0, 0, 0);
	ppc_rlwinmx(a.rt, a.areg, 0, 8, 31, 0);
	ppc_addis(a.rt, a.rt, RAM_HI);
	if (sz < 4)
		ppc_xori(a.rt, a.rt, 4 - sz);
	if (a.kind == 0)
	{
		if (a.fpr) ppc_lfs(a.d0, a.rt, 0);
		else if (sz == 1) { ppc_lbz(a.d0, a.rt, 0); ppc_extsbx(a.d0, a.d0, 0); }
		else if (sz == 2) { ppc_lhz(a.d0, a.rt, 0); ppc_extshx(a.d0, a.d0, 0); }
		else ppc_lwz(a.d0, a.rt, 0);
	}
	else if (a.kind == 1)
	{
		if (a.fpr) { ppc_lfs(a.d0, a.rt, 0); ppc_lfs(a.d1, a.rt, 4); }
		else { ppc_lwz(a.d1, a.rt, 4); ppc_lwz(a.d0, a.rt, 0); }
	}
	else if (a.kind == 2)
	{
		if (a.fpr) ppc_stfs(a.d0, a.rt, 0);
		else if (sz == 1) ppc_stb(a.d0, a.rt, 0);
		else if (sz == 2) ppc_sth(a.d0, a.rt, 0);
		else ppc_stw(a.d0, a.rt, 0);
	}
	else
	{
		if (a.fpr) { ppc_stfs(a.d0, a.rt, 0); ppc_stfs(a.d1, a.rt, 4); }
		else { ppc_stw(a.d0, a.rt, 0); ppc_stw(a.d1, a.rt, 4); }
	}
	*done = emit_GetCCPtr();
	return cold;
}

// Guest memory: RAM is the host block (big-endian words, sub-words swizzled), the rest a map by guest address.
struct Mach
{
	pm::Cpu c;
	std::map<u64, u8> io;
};

static bool is_ram(u32 a) { return ((a >> 26) & 7) == 3 && (a >> 29) != 7; }

// The slow access the cold path makes (ReadMem/WriteMem through _vmem): RAM or the other map. Pairs and words by
// word, as the sub-word swizzle only touches bytes and halves.
static u64 guest_read(Mach& m, u32 a, u32 sz)
{
	if (is_ram(a))
	{
		const u64 h = RAM + (a & 0xFFFFFF);
		const u64 hs = sz < 4 ? h ^ (4 - sz) : h;
		return pm::rdn(m.c, hs, sz);
	}
	u64 v = 0;
	for (u32 i = 0; i < sz; i++)
	{
		auto it = m.io.find(a + i);
		v = (v << 8) | (it == m.io.end() ? (u8)((a + i) * 37 + 11) : it->second);
	}
	return v;
}
static void guest_write(Mach& m, u32 a, u64 v, u32 sz)
{
	if (is_ram(a))
	{
		const u64 h = RAM + (a & 0xFFFFFF);
		pm::wrn(m.c, sz < 4 ? h ^ (4 - sz) : h, v, sz);
		return;
	}
	for (u32 i = 0; i < sz; i++)
		m.io[a + i] = (u8)(v >> (8 * (sz - 1 - i)));
}

// The cold fragment: the address (base + disp), the slow access into d0/d1 or from them, C-call clobbers.
static void cold_path(Mach& m, const FmAcc& a)
{
	const u32 addr = (u32)m.c.r[a.areg] + a.disp;
	const bool pair = a.kind & 1;
	u64 v0 = 0, v1 = 0;
	if (a.kind == 2 || a.kind == 3)
	{
		v0 = a.fpr ? pm::stfs_conv(m.c.f[a.d0]) : m.c.r[a.d0];
		v1 = !pair ? 0 : a.fpr ? pm::stfs_conv(m.c.f[a.d1]) : m.c.r[a.d1];
	}
	for (u32 k = 0; k < 32; k++)
		if (k == 0 || (k >= 3 && k <= 12))
			m.c.r[k] = rnd();
	for (u32 k = 0; k < 14; k++)
		m.c.f[k] = rnd();
	m.c.cr = (m.c.cr & 0x00FFF000u) | ((u32)rnd() & 0xFF000FFFu);
	m.c.ca = rn(2);
	if (a.kind == 0)
	{
		const u64 v = guest_read(m, addr, a.sz);
		if (a.fpr) m.c.f[a.d0] = pm::lfs_conv((u32)v);
		else m.c.r[a.d0] = a.sz == 1 ? pm::exts(v, 8) : a.sz == 2 ? pm::exts(v, 16) : (u64)(u32)v;
	}
	else if (a.kind == 1)
	{
		const u64 w0 = guest_read(m, addr, 4), w1 = guest_read(m, addr + 4, 4);
		if (a.fpr) { m.c.f[a.d0] = pm::lfs_conv((u32)w0); m.c.f[a.d1] = pm::lfs_conv((u32)w1); }
		else { m.c.r[a.d0] = w0; m.c.r[a.d1] = w1; }
	}
	else if (a.kind == 2)
		guest_write(m, addr, v0, a.sz);
	else
	{
		guest_write(m, addr, v0, 4);
		guest_write(m, addr + 4, v1, 4);
	}
}

// Runs g_code[0..g_n): a taken branch to `cold` runs the cold path, then goes on at `done`. Returns whether it
// went cold, or -1 on a fault.
static int run(Mach& m, const FmAcc& a, u32 cold, u32 done)
{
	u32 pc = 0;
	int went = 0;
	for (int steps = 0; pc < g_n && steps < 64; steps++)
	{
		const u32 w = g_code[pc];
		if ((w >> 26) == 16)
		{
			const u32 bo = (w >> 21) & 31, bi = (w >> 16) & 31;
			const u32 bit = pm::cr_bit(m.c, bi);
			const bool taken = (bo & 0x10) || bit == ((bo >> 3) & 1);
			if (taken)
			{
				if (pc != cold)
					return -1;
				cold_path(m, a);
				went = 1;
				pc = done;
				continue;
			}
			pc++;
			continue;
		}
		if (!pm::step(m.c, w))
			return -1;
		if (m.c.last_kind && !went)
		{
			// A fast-path access must land in RAM or its pad.
			if (m.c.last_ea < RAM || m.c.last_ea + m.c.last_size > RAM + (16u << 20) + 4096)
				return -1;
		}
		pc++;
	}
	return went;
}

// prefix: an 8C PREFIX case, mostly that prefix and its neighbours; 0: any address.
static u32 rnd_addr(u32 prefix)
{
	if (prefix && rn(3))
	{
		static const u32 top[] = { 0x8C, 0x0C, 0x8D, 0x8B, 0x0D, 0x0B, 0xAC, 0xCC, 0x9C, 0x84 };
		const u32 off = rn(4) == 0 ? 0x00FFFFFF - rn(64) : rn(4) == 0 ? rn(64) : (u32)rnd() & 0x00FFFFFF;
		return (rn(2) ? prefix : top[rn(10)]) << 24 | off;
	}
	const u32 region = rn(8), area = rn(5) ? 3 : rn(8);
	u32 off = (u32)rnd() & 0x03FFFFFF & ~0x03000000u;
	switch (rn(6))
	{
	case 0: off = (u32)rnd() & 0x00FFFFFF; break;
	case 1: off = 0x00FFFFFF - rn(64); break;                  // the 16 MB mirror's end
	case 2: off = 0x03FFFFFF - rn(64); break;                  // the area's end
	case 3: off = rn(64); break;
	}
	return (region << 29) | (area << 26) | (off & 0x03FFFFFF);
}

static void rnd_state(pm::Cpu& c)
{
	c = pm::Cpu();
	for (u32 i = 0; i < 32; i++)
	{
		c.r[i] = rnd();
		c.f[i] = pm::dbits((double)pm::fval((u32)rnd()));
	}
	c.cr = (u32)rnd();
	c.mem_seed = 77;
}

// Live after the access: the pinned GPRs and FPRs, the destinations, guest memory.
static bool same(const Mach& x, const Mach& y, const FmAcc& a)
{
	for (u32 k = 14; k <= 28; k++)
		if ((u32)x.c.r[k] != (u32)y.c.r[k])
			return false;
	for (u32 k = 14; k <= 29; k++)
		if (x.c.f[k] != y.c.f[k])
			return false;
	if (a.kind < 2 && !a.fpr)
	{
		if (x.c.r[a.d0] != y.c.r[a.d0] || (a.kind == 1 && (u32)x.c.r[a.d1] != (u32)y.c.r[a.d1]))
			return false;
	}
	if (a.kind < 2 && a.fpr && (x.c.f[a.d0] != y.c.f[a.d0] || (a.kind == 1 && x.c.f[a.d1] != y.c.f[a.d1])))
		return false;
	return x.c.mem == y.c.mem && x.io == y.io && ((x.c.cr ^ y.c.cr) & 0x00FFF000u) == 0;
}

// Registers as ngen_Compile picks them for each kind (r3-r6 = rarg0-3 / rrv0-1, r14-r28 pinned, f14-f29 pinned).
static FmAcc pick(bool fold, bool sched)
{
	FmAcc a = {};
	a.kind = (u8)rn(4);
	a.fpr = rn(3) == 0;
	a.sz = (a.kind & 1) || a.fpr ? 4 : (u8)(1u << rn(3));
	const bool pinned = fold || rn(2);
	a.areg = (u8)(pinned ? 14 + rn(15) : 3);
	if (a.kind == 1 && !fold)
		a.areg = 3;
	if (a.kind == 3 && !fold)
		a.areg = 3;
	a.disp = (u16)(fold && (a.sz == 4) ? rn(5) == 0 ? FM_FOLD_MAX : rn(64) * 4 : 0);
	switch (a.kind)
	{
	case 0:
		a.d0 = (u8)(a.fpr ? 14 + rn(16) : rn(2) ? 14 + rn(15) : 3);
		a.rt = (u8)(sched && a.areg == 3 ? 4 : 3);
		break;
	case 1:
		a.d0 = (u8)(a.fpr ? 14 + rn(15) : 3);
		a.d1 = (u8)(a.fpr ? a.d0 + 1 : 4);
		a.rt = 5;
		break;
	case 2:
		a.d0 = (u8)(a.fpr ? 14 + rn(16) : rn(2) ? 14 + rn(15) : 4);
		a.rt = 5;
		break;
	default:
		a.d0 = (u8)(a.fpr ? 14 + rn(15) : 5);
		a.d1 = (u8)(a.fpr ? a.d0 + 1 : 6);
		a.rt = 4;
		break;
	}
	return a;
}

static void check_forms(u32 cases)
{
	for (u32 k = 0; k < cases; k++)
	{
		const u8 guard = (u8)rn(3);
		const bool sched = rn(2), fold = guard && rn(2);
		FmAcc a = pick(fold, sched);
		if (!guard && !sched)
			continue;
		const u8 prefix = rn(2) ? 0x8C : 0x0C;
		const FmForm f = { guard, prefix, sched, RAM_HI, bias_hi(prefix) };
		Mach m0;
		rnd_state(m0.c);
		const u32 base = rnd_addr(guard == 2 ? prefix : 0);
		const u32 ea = base + a.disp;
		// A pinned GPR's upper half: junk; for 8C PREFIX mostly clear (lwz, ppc_li), else sign bits, a carry, a top byte.
		u64 hi = rnd() & 0xFFFFFFFF00000000ull;
		if (guard == 2)
		{
			const u32 h = rn(8);
			hi = h < 5 ? 0 : h == 5 ? 0xFFFFFFFF00000000ull : h == 6 ? 0x100000000ull : (rnd() & 0xFF) << 56 | 1ull << 56;
		}
		m0.c.r[a.areg] = hi | base;

		// Old: the effective address in rarg0 (r3) when folded, as the addi did.
		FmAcc o = a;
		o.disp = 0;
		Mach mo = m0;
		if (a.disp || ((a.kind & 1) && a.areg != 3))
		{
			o.areg = 3;
			if (o.rt == 3) o.rt = 4;
			mo.c.r[3] = (u32)ea;
		}
		if (o.kind == 0 && o.areg == 3 && o.rt == 4)
			o.rt = 3;
		g_n = 0;
		void* done;
		ppc_label* cold = old_emit(o, &done);
		const u32 oc = (u32)((u32*)cold - g_code), od = (u32)((u32*)done - g_code);
		const int ro = run(mo, o, oc, od);

		g_n = 0;
		cold = fm_emit(f, a, &done);
		const u32 nc = (u32)((u32*)cold - g_code), nd = (u32)((u32*)done - g_code);
		Mach mn = m0;
		const int rnw = run(mn, a, nc, nd);
		g_checks++;
		char why[160];
		snprintf(why, sizeof why, "kind %u sz %u fpr %d areg r%u rt r%u d0 %u disp %u addr %08X:%08X guard %u sched %d",
		         a.kind, a.sz, a.fpr, a.areg, a.rt, a.d0, a.disp, (u32)(hi >> 32), ea, guard, sched);
		if (ro < 0 || rnw < 0)
		{
			fail("a run faulted (a branch elsewhere, or a fast access outside RAM)", why);
			continue;
		}
		// Expected differences: the short guard takes P4's window 0xEC-0xEF; a fold checks the base, not the address.
		const bool p4 = guard == 1 && ((base >> 26) & 7) == 3 && (base >> 29) == 7;
		const bool split = a.disp && (((base >> 26) != (ea >> 26)) || (base & 0xFFFFFF) + a.disp > 0xFFFFFF);
		// A misaligned pair over a mirror's end (an SH4 address error): the old form reads the pad, the cold path wraps.
		const bool cross = guard == 2 && (a.kind & 1) && (ea & 0xFFFFFF) > 0xFFFFF8;
		if (p4 || split || cross)
		{
			g_expected_diff++;
			if (p4 && rnw != 0)
				fail("the short guard did not take P4's area 3 as RAM", why);
			continue;
		}
		// 8C PREFIX: fast only for 0x8Cxxxxxx with a clear upper half; any other address gets the old form's result cold.
		const bool want_cold = !is_ram(ea), new_cold = guard == 2 ? (m0.c.r[a.areg] >> 24) != prefix : want_cold;
		if (rnw != (int)new_cold || ro != (int)want_cold)
		{
			fail("fast or cold path chosen wrongly", why);
			continue;
		}
		g_fast += !new_cold;
		g_cold += new_cold;
		g_8c_fast += guard == 2 && !new_cold && prefix == 0x8C;
		g_0c_fast += guard == 2 && !new_cold && prefix == 0x0C;
		g_8c_mirror += guard == 2 && new_cold && !want_cold && !hi;
		g_8c_junk += guard == 2 && new_cold && (base >> 24) == prefix && hi;
		if (!same(mo, mn, a))
			fail("the new form's result differs from the old form's", why);
	}
}

// The interleaved store-queue form against the old test + body: the same store, or the same branch.
static void check_sq(u32 cases)
{
	for (u32 k = 0; k < cases; k++)
	{
		const bool pair = rn(2), fpr = rn(3) == 0;
		const u32 areg = rn(2) ? 14 + rn(15) : 3, ea = pair ? 4 : 5;
		const u32 d0 = fpr ? 14 + rn(15) : pair ? 5 : (rn(2) ? 14 + rn(15) : 4), d1 = fpr ? d0 + 1 : 6;
		if (!fpr && pair && (d0 == areg || d1 == areg))
			continue;
		pm::Cpu c0;
		rnd_state(c0);
		const u32 a = rn(2) ? 0xE0000000u | (rn(64) & (pair ? 0x38 : 0x3C)) : rnd_addr(0);
		c0.r[areg] = (rnd() & 0xFFFFFFFF00000000ull) | a;
		g_n = 0;
		ppc_label* oo = emit_sq_check(areg);
		emit_sq_body(areg, ea, pair, fpr, d0, d1, SQ_BASE);
		const u32 old_end = g_n, old_other = (u32)((u32*)oo - g_code);
		u32 oldw[32];
		memcpy(oldw, g_code, sizeof oldw);
		g_n = 0;
		ppc_label* no = fm_emit_sq_sched(areg, ea, pair, fpr, d0, d1, SQ_BASE);
		const u32 new_end = g_n, new_other = (u32)((u32*)no - g_code);
		pm::Cpu x = c0, y = c0;
		int bx = 0, by = 0;
		for (u32 pc = 0; pc < old_end; pc++)
		{
			const u32 w = oldw[pc];
			if ((w >> 26) == 16) { if (pc == old_other && !pm::cr_bit(x, 2)) { bx = 1; break; } continue; }
			pm::step(x, w);
		}
		for (u32 pc = 0; pc < new_end; pc++)
		{
			const u32 w = g_code[pc];
			if ((w >> 26) == 16) { if (pc == new_other && !pm::cr_bit(y, 2)) { by = 1; break; } continue; }
			pm::step(y, w);
		}
		g_checks++;
		if (bx != by || x.mem != y.mem)
			fail("the interleaved store-queue form differs", pair ? "pair" : "word");
		for (u32 r = 14; r <= 28; r++)
			if (x.r[r] != y.r[r])
				fail("the interleaved store-queue form changed a pinned register", "");
	}
}

int main(int argc, char** argv)
{
	const u32 cases = argc > 1 ? (u32)atoi(argv[1]) : 400000;
	if (argc > 2)
		g_seed ^= (u64)atoi(argv[2]) * 0x2545F4914F6CDD1Dull;
	check_forms(cases);
	check_sq(cases / 4);
	printf("forms: %d checks, %u fast, %u cold, %u expected differences (P4 window, split folds, crossing pairs)\n",
	       g_checks, g_fast, g_cold, g_expected_diff);
	printf("8C PREFIX: %u fast at 0x8C, %u fast at 0x0C, %u other RAM mirrors cold, %u junk upper halves cold\n",
	       g_8c_fast, g_0c_fast, g_8c_mirror, g_8c_junk);
	if (g_fast < cases / 20 || g_cold < cases / 20)
		fail("too few fast or cold runs", "");
	if (g_8c_fast < cases / 80 || g_0c_fast < cases / 80 || g_8c_mirror < cases / 200 || g_8c_junk < cases / 200)
		fail("too few 8C PREFIX runs of a kind", "");
	if (g_fail)
	{
		printf("FAIL: %d\n", g_fail);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
