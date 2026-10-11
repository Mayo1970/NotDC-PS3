// OPTIMIZATION_PLAN 6.5 PC check: the real dc/aica/aica_if.cpp (AICA G2 DMA) against a model of the rules, with AICA
// Race Fix off (the old rules) and on (Flycast a920ae590 + a44619931), DMA Fix and SCHED off and on, on random traffic.
// Section 7, bundle 6: also G2 DMA Small (P24) and G2 DMA Read (P9) off and on, so 32 configurations.
#include "types.h"
#include "dc/mem/sb.h"
#include "dc/asic/asic.h"
#include "dc/sh4/sh4_sched.h"
#include <stdlib.h>
#include <unordered_map>

// ---- what aica_if.cpp links against ----
RegisterStruct sb_regs[0x540];
u32 SB_ISTNRM;
u32 SB_ADSTAG, SB_ADSTAR, SB_ADLEN, SB_ADDIR, SB_ADEN, SB_ADST, SB_ADSUSP;
u32 SB_E1STAG, SB_E1STAR, SB_E1LEN, SB_E1DIR, SB_E1EN, SB_E1ST;
u32 SB_E2STAG, SB_E2STAR, SB_E2LEN, SB_E2DIR, SB_E2EN, SB_E2ST;
__settings settings;
void SaveSettings() {}
s32 rtc_cycles;
static int g_race, g_dmafix, g_sched, g_small, g_read;
extern "C" int get_sh4_clock_preset() { return 200; }
extern "C" int get_dma_fix_preset() { return g_dmafix; }
extern "C" int get_sched_preset() { return g_sched; }
extern "C" int get_aica_race_fix_preset() { return g_race; }
extern "C" int get_g2_dma_small_preset() { return g_small; }
extern "C" int get_g2_dma_read_preset() { return g_read; }
u32 g_aram_hz_dma;   // plan 6.6: set around the copy; must be 0 again after every start
static u32 g_msgbox;
int msgboxf(const char*, unsigned int, ...) { g_msgbox++; return 0; }
u32 libAICA_ReadMem_aica_reg(u32, u32) { return 0; }
void libAICA_WriteMem_aica_reg(u32, u32, u32) {}
void asic_RaiseInterrupt(HollyInterruptID i) { SB_ISTNRM |= 1u << (u32)i; }
static std::unordered_map<u32, u32> g_mem;   // the real code's memory
u32 ReadMem32_nommu(u32 a) { auto it = g_mem.find(a); return it == g_mem.end() ? a * 2654435761u : it->second; }
void WriteMem32_nommu(u32 a, u32 v) { g_mem[a] = v; }
static sh4_sched_callback* g_cb;
static int g_cb_pend = -1;   // the deadline scheduler: cycles to the callback, -1 none
int sh4_sched_register(int, sh4_sched_callback* f) { g_cb = f; return 0; }
void sh4_sched_request(int, int cycles) { g_cb_pend = cycles; }

void aica_sb_Init();
void UpdateAicaDma(u32 cycles);
void g2dma_take(u32 out[9]);

// ---- the model ----
struct Model
{
	u32 star, stag, len, dir, en, st, susp, ist, run_len, msgbox;
	int pend;   // cycles to the completion, -1 none
	std::unordered_map<u32, u32> mem;
};
static Model M;
static u32 g_m_small;   // transfers the model ended in the start call (P24)

static u32 m_read32(u32 a) { auto it = M.mem.find(a); return it == M.mem.end() ? a * 2654435761u : it->second; }

static void m_complete()
{
	if (g_race)
	{
		M.en = (M.len & 0x80000000u) ? 0 : 1;
		M.star += M.run_len;
		M.stag += M.run_len;
		M.susp |= 0x10;
	}
	else
		M.en = (M.len & 0x80000000u) ? 1 : 0;
	M.st = 0;
	M.len = 0;
	M.ist |= 1u << 15;
}

static void m_write_adst(u32 d)
{
	if (!(d & 1))
		return;
	if (g_race && (M.st & 1))
		return;
	if (!(M.en & 1))
		return;
	const bool rd = M.dir == 1;
	if (rd && !g_read)
	{
		M.msgbox++;
		return;
	}
	u32 l = M.len & 0x7FFFFFFFu;
	if (l > 8u * 1024 * 1024)
		l = 8u * 1024 * 1024;
	l &= ~3u;
	// P9: the copy runs from the G2 address to the system address; the registers still move forward
	const u32 src = rd ? M.stag : M.star, dst = rd ? M.star : M.stag;
	for (u32 i = 0; i < l; i += 4)
		M.mem[dst + i] = m_read32(src + i);
	const bool overlap = (M.st & 1) != 0;
	if (g_race)
	{
		M.run_len = l;
		M.susp &= ~0x10u;
	}
	else
	{
		M.star += l;
		M.stag += l;
	}
	M.st = 1;
	if (g_dmafix)
	{
		int c = (int)(l * 4);
		// P24: 512 cycles or less end here, but not over a transfer that still runs (its end is due)
		if (g_small && c <= 512 && !overlap)
		{
			g_m_small++;
			m_complete();
			return;
		}
		M.pend = c < 64 ? 64 : c;
	}
	else
		m_complete();
}

static u32 m_read_adst()
{
	if (!g_race)
		return M.st;
	if ((M.ist & (1u << 15)) && !(M.st & 2))
	{
		M.st |= 2;
		return 1;
	}
	M.st &= ~2u;
	return M.st;
}

static void m_advance(u32 c)
{
	if (M.pend < 0)
		return;
	M.pend -= (int)c;
	if (M.pend <= 0)
	{
		M.pend = -1;
		m_complete();
	}
}

// ---- the real code's side ----
static const u32 ADST = (SB_ADST_addr - SB_BASE) >> 2;
static u32 r_read_adst()   // sb.cpp sb_ReadMem's choice
{
	return (sb_regs[ADST].flags & REG_READ_DATA) ? *sb_regs[ADST].data32 : sb_regs[ADST].readFunction();
}
static void r_advance(u32 c)
{
	if (!g_sched)
	{
		UpdateAicaDma(c);
		return;
	}
	if (g_cb_pend < 0)
		return;
	g_cb_pend -= (int)c;
	if (g_cb_pend <= 0)
	{
		g_cb_pend = -1;
		g_cb(0, 0, 0);
	}
}

// ---- random traffic ----
static u64 g_rng;
static u32 rnd(u32 n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return (u32)(g_rng % n);
}

static const char* g_fail;
static u32 g_fa, g_fb;
static bool check(const char* what, u32 a, u32 b)
{
	if (a == b || g_fail)
		return a == b;
	g_fail = what;
	g_fa = a;
	g_fb = b;
	return false;
}

static bool compare()
{
	check("ADSTAR", SB_ADSTAR, M.star);
	check("ADSTAG", SB_ADSTAG, M.stag);
	check("ADLEN", SB_ADLEN, M.len);
	check("ADEN", SB_ADEN, M.en);
	check("ADST", SB_ADST, M.st);
	check("ADSUSP", SB_ADSUSP, M.susp);
	check("ISTNRM", SB_ISTNRM, M.ist);
	check("msgbox", g_msgbox, M.msgbox);
	check("memory size", (u32)g_mem.size(), (u32)M.mem.size());
	for (auto& kv : M.mem)
		if (!check("memory word", ReadMem32_nommu(kv.first), kv.second))
			break;
	return !g_fail;
}

// starts, busy refusals, guard reads (1 while idle), completions, bit 31 ends, ADDIR 1 refused, no ADEN, small ends,
// ADDIR 1 transfers
static u32 g_cov[9];

// One run: ops of random G2 DMA traffic on a fresh state. Returns false on the first difference.
static bool run(u32 ops)
{
	extern s32 aica_dma_delay_cycles;
	aica_dma_delay_cycles = 0;
	g_mem.clear();
	M = Model();
	M.pend = -1;
	g_cb_pend = -1;
	SB_ISTNRM = SB_ADSTAG = SB_ADSTAR = SB_ADLEN = SB_ADDIR = SB_ADEN = SB_ADST = SB_ADSUSP = 0;
	g_msgbox = 0;
	memset(sb_regs, 0, sizeof(sb_regs));
	sb_regs[ADST].data32 = &SB_ADST;
	sb_regs[ADST].flags = REG_32BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;   // sb_Init's default
	aica_sb_Init();
	for (u32 op = 0; op < ops; op++)
	{
		const u32 k = rnd(100);
		if (k < 8)
		{
			const u32 v = 0x0C000000 + rnd(0x4000) * 4;
			SB_ADSTAR = M.star = v;
		}
		else if (k < 16)
		{
			const u32 v = 0x00800000 + rnd(0x4000) * 4;
			SB_ADSTAG = M.stag = v;
		}
		else if (k < 26)
		{
			// one length in five sits at the 128-byte limit of P24
			u32 v = rnd(5) == 0 ? 96 + rnd(18) * 4 : rnd(4) ? rnd(0x200) * 4 : rnd(0x1000);
			if (rnd(2))
				v |= 0x80000000u;
			SB_ADLEN = M.len = v;
		}
		else if (k < 28)
			SB_ADDIR = M.dir = rnd(20) == 0;
		else if (k < 36)
			SB_ADEN = M.en = rnd(5) ? 1 : 0;
		else if (k < 38)
			SB_ADSUSP = M.susp = rnd(0x40);
		else if (k < 58)   // start
		{
			const u32 d = rnd(8) ? 1 : rnd(4);
			const u32 st0 = M.st, ms0 = M.msgbox, sm0 = g_m_small;
			const bool running = (M.st & 1) != 0, en = (M.en & 1) != 0;
			extern void Write_SB_ADST(u32 data);
			Write_SB_ADST(d);
			m_write_adst(d);
			if (d & 1)
			{
				if (g_race && running)
					g_cov[1]++;
				else if (!en)
					g_cov[6]++;
				else if (M.msgbox != ms0)
					g_cov[5]++;
				else
				{
					g_cov[0]++;
					g_cov[7] += g_m_small - sm0;
					g_cov[8] += M.dir == 1;
				}
			}
			(void)st0;
		}
		else if (k < 78)   // the game reads SB_ADST
		{
			const u32 x = r_read_adst(), y = m_read_adst();
			if (g_race && y == 1 && !(M.st & 1))
				g_cov[2]++;
			if (!check("SB_ADST read", x, y))
				return false;
		}
		else if (k < 84)   // its interrupt handler acknowledges the AICA DMA end
		{
			SB_ISTNRM &= ~(1u << 15);
			M.ist &= ~(1u << 15);
		}
		else   // time
		{
			const u32 c = 16 + rnd(rnd(4) ? 400 : 6000);
			const bool p = M.pend >= 0;
			const u32 len0 = M.len;
			r_advance(c);
			m_advance(c);
			if (p && M.pend < 0)
			{
				g_cov[3]++;
				g_cov[4] += (len0 & 0x80000000u) != 0;
			}
		}
		if (!compare())
			return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	const u32 runs = argc > 1 ? (u32)strtoul(argv[1], 0, 10) : 200;
	const u32 ops = argc > 2 ? (u32)strtoul(argv[2], 0, 10) : 3000;
	g_rng = 0x9E3779B97F4A7C15ull * (argc > 3 ? strtoull(argv[3], 0, 10) + 1 : 1);
	u32 bad = 0, total = 0;
	u32 c[9];
	g2dma_take(c);
	for (int cfg = 0; cfg < 32; cfg++)
	{
		g_race = cfg & 1;
		g_dmafix = (cfg >> 1) & 1;
		g_sched = (cfg >> 2) & 1;
		g_small = (cfg >> 3) & 1;
		g_read = (cfg >> 4) & 1;
		memset(g_cov, 0, sizeof(g_cov));
		u32 ok = 0;
		for (u32 r = 0; r < runs; r++)
		{
			g_fail = 0;
			if (run(ops))
				ok++;
			else if (bad++ < 6)
				printf("MISMATCH race %d dmafix %d sched %d small %d read %d run %u: %s real %08X model %08X\n", g_race,
				       g_dmafix, g_sched, g_small, g_read, r, g_fail, g_fa, g_fb);
			total++;
		}
		printf("race %d dmafix %d sched %d small %d read %d: %u / %u runs | starts %u busy %u guard reads %u ends %u"
		       " (bit 31 %u) addir1 refused %u no aden %u small ends %u reads %u\n", g_race, g_dmafix, g_sched, g_small,
		       g_read, ok, runs, g_cov[0], g_cov[1], g_cov[2], g_cov[3], g_cov[4], g_cov[5], g_cov[6], g_cov[7], g_cov[8]);
		// The real code's own [aicafix] counts for the two rows
		g2dma_take(c);
		if (c[7] != g_cov[7] || c[8] != g_cov[8])
		{
			printf("MISMATCH counters: small %u model %u, rd %u model %u\n", c[7], g_cov[7], c[8], g_cov[8]);
			bad++;
		}
	}
	printf("%s (%u of %u runs)\n", bad ? "FAIL" : "PASS", total - bad, total);
	return bad ? 1 : 0;
}
