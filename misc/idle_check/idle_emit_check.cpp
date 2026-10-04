// Host-only check for plan 5.25: ps3_jit_idle.h against ppu-as words (idle_words.inc) and on a small PPC64 model.
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
#include "ps3/ps3_jit_idle.h"
#include "idle_words.inc"

static int g_fail, g_checks;

static void check(bool ok, const char* what, const char* detail)
{
	g_checks++;
	if (!ok)
	{
		g_fail++;
		printf("FAIL %s: %s\n", what, detail);
	}
}

// As in the driver: ctx r30, cycles r29, temps r4-r6, SH4 GPRs pinned in r14..r28.
static const IdleRegs k_regs = { 30, 29, 4, 5, 6, 14, 15 };
static const u32 R11_OFS = 0xB4, WORK_OFS = 0x154;

static const IdleEnt* ent_at(u32 a) { return (const IdleEnt*)(uintptr_t)a; }

static void test_words()
{
	g_n = 0;
	emit_idle_mark(30, WORK_OFS);
	emit_idle_check(k_regs, R11_OFS, WORK_OFS, ent_at(0x10B23450u));
	emit_idle_check(k_regs, R11_OFS, WORK_OFS, ent_at(0x10B2FFF0u));
	emit_idle_check(k_regs, R11_OFS, WORK_OFS, ent_at(0x10B27FF0u));
	const u32 want = (u32)(sizeof(k_ref) / sizeof(k_ref[0]));
	check(g_n == want, "words", "count differs from ppu-as");
	for (u32 i = 0; i < g_n && i < want; i++)
	{
		char d[64];
		snprintf(d, sizeof(d), "word %u: %08X, ppu-as %08X", i, g_code[i], k_ref[i]);
		check(g_code[i] == k_ref[i], "words", d);
	}
}

static u32 rotl32(u32 x, u32 n) { return n ? (x << n) | (x >> (32 - n)) : x; }

// PPC64 model of what the emitters may use: addis, lwz, stw, addi, xor, or, rotlwi, cmplwi, bc on CR0.
struct Model
{
	u64 r[32];
	u32 cr0;	// LT GT EQ SO as bits 3..0
	std::map<u64, u32> mem;
	bool ok = true;
	const char* what;

	void bad(const char* d) { check(false, what, d); ok = false; }

	void run(const u32* code, u32 n)
	{
		for (u32 pc = 0; pc < n && ok;)
		{
			const u32 x = code[pc], op = x >> 26, rd = (x >> 21) & 31, ra = (x >> 16) & 31, rb = (x >> 11) & 31;
			const s64 simm = (s16)(x & 0xFFFF);
			const u64 ea = (ra ? r[ra] : 0) + (u64)simm;
			switch (op)
			{
			case 15: r[rd] = (ra ? r[ra] : 0) + ((u64)simm << 16); break;
			case 14: r[rd] = (ra ? r[ra] : 0) + (u64)simm; break;
			case 32:
				if (!mem.count(ea)) { bad("load from an unexpected word"); return; }
				r[rd] = mem[ea];
				break;
			case 36:
				if (!mem.count(ea)) { bad("store to an unexpected word"); return; }
				mem[ea] = (u32)r[rd];
				break;
			case 21:
			{
				const u32 sh = rb, mb = (x >> 6) & 31, me = (x >> 1) & 31;
				if ((x & 1) || mb != 0 || me != 31) { bad("rlwinm other than rotlwi"); return; }
				r[ra] = rotl32((u32)r[rd], sh);
				break;
			}
			case 31:
			{
				const u32 xo = (x >> 1) & 0x3FF;
				if (x & 1) { bad("record form"); return; }
				if (xo == 316) r[ra] = r[rd] ^ r[rb];
				else if (xo == 444) r[ra] = r[rd] | r[rb];
				else { bad("unexpected X-form"); return; }
				break;
			}
			case 10:
			{
				if ((x >> 21) & 1) { bad("cmpli with L=1"); return; }
				if ((x >> 23) & 7) { bad("compare into a field other than cr0"); return; }
				const u32 a = (u32)r[ra], b = x & 0xFFFF;
				cr0 = (a < b ? 8 : a > b ? 4 : 2) | (cr0 & 1);
				break;
			}
			case 16:
			{
				const u32 bo = (x >> 21) & 31, bi = (x >> 16) & 31;
				if ((x & 3) || bi > 3 || (bo != 12 && bo != 4)) { bad("unexpected branch form"); return; }
				const bool bit = (cr0 >> (3 - bi)) & 1;
				if (bit == (bo == 12))
				{
					pc += (u32)((s32)(s16)(x & 0xFFFC) >> 2);
					continue;
				}
				break;
			}
			default:
				bad("unexpected instruction");
				return;
			}
			pc++;
		}
	}
};

static u64 g_seed = 88172645463325252ull;
static u64 rnd64()
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return g_seed;
}

// The hash the check must store: r11, then each pinned GPR's low word, rotated by 5 between them.
static u32 want_hash(const u64* r, u32 r11)
{
	u32 h = r11;
	for (u32 k = 0; k < 15; k++)
		h = (k ? rotl32(h, 5) : h) ^ (u32)r[14 + k];
	return h;
}

struct Case
{
	u64 ctx;
	u32 ent;
	u64 regs[32];
	u32 r11, work, checks, skips, hash;
	u64 cyc;
};

// Runs one check; the model's registers and words then go back into c, as the next turn would see them.
static void run_case(Case& c, bool want_skip, const char* what)
{
	g_n = 0;
	emit_idle_check(k_regs, R11_OFS, WORK_OFS, ent_at(c.ent));
	check(g_n == 47, what, "not 47 instructions");

	Model m;
	m.what = what;
	memcpy(m.r, c.regs, sizeof(m.r));
	m.r[30] = c.ctx;
	m.r[29] = c.cyc;
	m.cr0 = (u32)rnd64() & 15;
	m.mem[c.ctx + R11_OFS] = c.r11;
	m.mem[c.ctx + WORK_OFS] = c.work;
	m.mem[c.ent] = c.checks;
	m.mem[c.ent + 4] = c.skips;
	m.mem[c.ent + 8] = c.hash;
	u64 r0[32];
	memcpy(r0, m.r, sizeof(r0));
	m.run(g_code, g_n);
	if (!m.ok)
		return;

	const u32 h = want_hash(m.r, c.r11);
	check(m.mem[c.ent + 8] == h, what, "stored hash differs from the model's");
	check(m.mem[c.ctx + WORK_OFS] == 0, what, "work mark not cleared");
	check(m.mem[c.ctx + R11_OFS] == c.r11, what, "r11 written");
	check(m.mem[c.ent] == c.checks + 1, what, "check not counted once");
	check(m.mem[c.ent + 4] == c.skips + (want_skip ? 1 : 0), what, want_skip ? "skip not counted" : "skip counted");
	check(m.r[29] == (want_skip ? 0 : c.cyc), what, want_skip ? "cycles not zeroed" : "cycles changed");
	for (int i = 0; i < 32; i++)
		if (i != 4 && i != 5 && i != 6 && i != 29 && m.r[i] != r0[i])
		{
			char d[32];
			snprintf(d, sizeof(d), "r%d changed", i);
			check(false, what, d);
		}
	check(m.mem.size() == 5, what, "touched other words");

	c.work = 0;
	c.checks = m.mem[c.ent];
	c.skips = m.mem[c.ent + 4];
	c.hash = m.mem[c.ent + 8];
	c.cyc = m.r[29];
	memcpy(c.regs, m.r, sizeof(c.regs));
}

static void test_model()
{
	const u64 ctxs[] = { 0x10012340ull, 0x00A1B2C0ull, 0x7FFE0000ull };
	const u32 ents[] = { 0x10B23450u, 0x10B2FFF0u, 0x10B27FF0u, 0x00010000u, 0x7FFEFFF0u };
	const u32 works[] = { 1, 0x10012340u, 0x80000000u, 0xFFFFFFFFu };
	const u64 cycs[] = { 0, 1, 1792, 0x7FFFFFFFull, 0xFFFFFFFF00000010ull };
	char what[160];
	for (u64 cx : ctxs)
		for (u32 e : ents)
			for (u32 w : works)
				for (u64 y : cycs)
				{
					Case c;
					c.ctx = cx;
					c.ent = e;
					for (int i = 0; i < 32; i++)
						c.regs[i] = rnd64();	// junk high words on every register
					c.r11 = (u32)rnd64();
					c.checks = (u32)rnd64();
					c.skips = (u32)rnd64();
					c.cyc = y;
					c.work = 0;
					c.hash = want_hash(c.regs, c.r11) ^ 0x00010000u;
					snprintf(what, sizeof(what), "ctx %llX ent %08X work %08X cyc %llX", (unsigned long long)cx, e, w,
					         (unsigned long long)y);

					// A stale hash: run on. The same registers again: skip. A work mark: run on.
					run_case(c, false, what);
					run_case(c, true, what);
					c.cyc = y;
					c.work = w;
					run_case(c, false, what);
					run_case(c, true, what);

					// New high words alone change nothing.
					for (int i = 14; i <= 28; i++)
						c.regs[i] = (c.regs[i] & 0xFFFFFFFFull) | (rnd64() << 32);
					c.cyc = y;
					run_case(c, true, what);

					// One changed low bit of any pinned GPR or of r11: run on, then skip.
					for (int i = 13; i <= 28; i++)
					{
						const u32 bit = 1u << (rnd64() & 31);
						if (i == 13)
							c.r11 ^= bit;
						else
							c.regs[i] ^= bit;
						c.cyc = y;
						run_case(c, false, what);
						run_case(c, true, what);
					}

					// A counter and a pointer moving together (i++, j--): run on.
					c.regs[16] += 1;
					c.regs[17] -= 1;
					c.cyc = y;
					run_case(c, false, what);
				}

	// The mark stores the context base's low word, which is never 0.
	for (u64 cx : ctxs)
	{
		g_n = 0;
		emit_idle_mark(30, WORK_OFS);
		Model m;
		m.what = "mark";
		for (int i = 0; i < 32; i++)
			m.r[i] = rnd64();
		m.r[30] = cx;
		m.cr0 = 0;
		m.mem[cx + WORK_OFS] = 0;
		m.run(g_code, g_n);
		check(m.ok && g_n == 1 && m.mem[cx + WORK_OFS] == (u32)cx && (u32)cx != 0, "mark", "wrong word or value");
	}
}

int main()
{
	test_words();
	test_model();
	printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
