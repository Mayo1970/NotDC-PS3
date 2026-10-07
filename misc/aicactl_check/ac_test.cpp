// OPTIMIZATION_PLAN 5.15 B1 test, run in misc/spu_sim: ps3/spu/aica_ctl.h against nullAICA's own aica.cpp, aica_mem.cpp,
// sgc_if.cpp, dsp.cpp and vbaARM's arm_mem.cpp, all built for the SPU, on random SH4 / ARM7 register traffic and samples.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include "types.h"
#include "aica_ctl.h"

// ---- the reference ----
u32 FASTCALL aica_ReadMem_reg(u32 addr, u32 size);
void FASTCALL aica_WriteMem_reg(u32 addr, u32 data, u32 size);
void libAICA_TimeStep();
void AICA_Init_test();   // appended to aica.cpp's copy by build_spu.sh
void arm_init_mem();
template<int sz, typename T> T ReadMemArm(u32 addr);
template<int sz, typename T> void WriteMemArm(u32 addr, T data);
void ref_aica_params(u8* ram, u32* istext, HollyRaiseInterruptFP* raise, HollyCancelInterruptFP* cancel);
extern arm_init_params arm_params;
extern u8* aica_reg;
extern u8* aica_ram_l;
extern bool aica_interr, e68k_out;
extern u32 aica_reg_L, e68k_reg_L, e68k_reg_M;
struct TimerShadow { void* data; s32 c_step; u32 m_step; u32 id; };   // aica.cpp's AicaTimer
extern TimerShadow timers[3];

// ---- what the reference links against ----
int g_verify_fail;
bool g_aica_spe_on = true, g_aica_spe_snd = true;
bool g_snd_thread;
bool g_arm_spe;
volatile bool g_snd_on_worker;
void snd_irq_set(bool) {}
void snd_cdda_pop(s16*) {}
int g_aica_fast_preset, g_aica_kyonb_preset;
static int g_mute;
extern "C" int get_mute_pcm16_preset() { return g_mute; }
extern "C" int get_cdda_preset() { return 0; }
void FASTCALL gdrom_get_cdda(s16*) {}
u64 AicaVoxTicks;
u32 AicaVoiceSum, AicaSampleCount;
void aica_WriteSample(s16, s16) {}
extern "C" void ps3_audio_aica_ready() {}
bool g_arm7_in_run;
u32 g_arm7s_runs, g_arm7s_fiqs;
u64 g_arm7s_ticks;
// The Looney Tunes fixes (plan 6.5, P31): seed bits 3 and 4
static int g_race, g_clamp;
extern "C" int get_aica_race_fix_preset() { return g_race; }
extern "C" int get_aica_ca_clamp_preset() { return g_clamp; }
extern "C" int get_arm7_reset_fix_preset() { return 0; }
u32 g_arm_races, g_arm_resets, g_arm_stale;
void g2dma_take(u32 out[7]) { for (int i = 0; i < 7; i++) out[i] = 0; }
void arm_spe_fix_take(u32 out[6]) { for (int i = 0; i < 6; i++) out[i] = 0; }
bool arm_reset_latch();   // arm_mem.cpp
// The reference's diagnostic prints go nowhere: these replace newlib's, which would stop the SPU for the PPU.
extern "C" int printf(const char*, ...) { return 0; }
extern "C" int puts(const char*) { return 0; }
extern "C" int snprintf(char* b, size_t n, const char*, ...)
{
	if (n)
		b[0] = 0;
	return 0;
}

static u32 g_istext;
static void FASTCALL ref_raise(HollyInterruptID i) { g_istext |= 1u << (u8)i; }
static void FASTCALL ref_cancel(HollyInterruptID i) { g_istext &= ~(1u << (u8)i); }

// ---- twin and CD-DA calls, side 0 the reference, side 1 the port ----
struct Hook { u32 kind, a, b, c, d, steps; };
struct Side { Hook log[64]; u32 n, steps, mon_n; };
static Side g_hs[2];

static void hook(int s, u32 kind, u32 a, u32 b, u32 c, u32 d)
{
	Side& h = g_hs[s];
	Hook& e = h.log[h.n++ & 63];
	e.kind = kind;
	e.a = a;
	e.b = b;
	e.c = c;
	e.d = d;
	e.steps = h.steps;
}

static u32 mon_value(Side& h, u32 reg, u32 keep) { return ((h.mon_n++ * 0x9E3779B1u) >> 16) ^ (reg * 7 + keep); }

void ps3aica_write(u32 off, u32 data, u32 size, u16 old16) { hook(0, 'W', off, data, size, old16); }
u16 ps3aica_read_mon(u32 reg, bool keep_lp)
{
	hook(0, 'M', reg, keep_lp, 0, 0);
	return (u16)mon_value(g_hs[0], reg, keep_lp);
}
void ps3aica_step() { g_hs[0].steps++; }
void ps3aica_cdda(const s16*, bool silent) { hook(0, 'C', silent, 0, 0, 0); }
void arm_avoid_race() { hook(0, 'X', 0, 0, 0, 0); }

static void ac_race(void) { hook(1, 'X', 0, 0, 0, 0); }
static void ac_twin_write(u32 off, u32 data, u32 size, u32 old16) { hook(1, 'W', off, data, size, old16); }
static u32 ac_twin_mon(u32 reg, int keep_lp)
{
	hook(1, 'M', reg, keep_lp != 0, 0, 0);
	return mon_value(g_hs[1], reg, keep_lp != 0);
}
static void ac_twin_step(void) { g_hs[1].steps++; }
static void ac_cdda_refill(void) { hook(1, 'C', 1, 0, 0, 0); }   // CD Audio off: the sector is silent

// ---- random traffic ----
static u32 g_rng;
static u32 rnd(u32 n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng % n;
}

static u32 pick_addr()
{
	static const u8 offs[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x1D, 0x1E, 0x1F,
	                           0x20, 0x24, 0x28, 0x29, 0x40, 0x44, 0x7C, 0x7D, 0x7E, 0x7F };
	if (rnd(25) == 0)
		return 0x2814 + rnd(2);   // CA: CA Clamp (P31) wants many
	if (rnd(25) == 0)
		return (ac.reg[0x280E] & 0x3F) * 0x80 + (rnd(2) ? 0x08 : 0x0C);   // the monitored slot's LSA or LEA
	const u32 r = rnd(100);
	if (r < 28) return rnd(64) * 0x80 + offs[rnd(sizeof(offs))];
	if (r < 44) return 0x2800 + rnd(0x18);
	if (r < 48) return 0x2818 + rnd(0x78);
	if (r < 76) return 0x2890 + rnd(0x30);
	if (r < 80) return 0x2C00 + rnd(4);
	if (r < 86) return 0x2D00 + rnd(8);
	if (r < 93) return 0x3000 + rnd(0x5000);
	return rnd(0x8000);
}

static u32 pick_data(u32 a)
{
	const u32 w = a & ~3u;
	if (w >= 0x2890 && w < 0x289C && rnd(4))
		return (0xF0 + rnd(16)) | (rnd(8) << 8);   // a timer close to its overflow
	if ((w == 0x289C || w == 0x28B4) && rnd(2))
		return rnd(0x800);
	if ((w == 0x28A8 || w == 0x28AC || w == 0x28B0) && rnd(2))
		return rnd(0x100);
	if (a < 0x2000 && (a & 0x7F) <= 1 && rnd(2))
		return 0xC000 | rnd(0x10000);   // KYONEX, KYONB
	if (rnd(10) == 0)
		return rnd(0x10000) | (rnd(0x10000) << 16);
	return rnd(0x10000);
}

// ---- report, put to main memory at the end ----
enum { COV_RD, COV_WR, COV_ARM_RD, COV_ARM_WR, COV_STEPS, COV_E68K_UP, COV_E68K_DOWN, COV_IRQ_UP, COV_IRQ_DOWN,
       COV_OVF, COV_KYONEX, COV_LFORE, COV_MON, COV_CDDA, COV_ACCEPT, COV_RACE, COV_CLAMP, COV_LATCH, COV_N };
struct OpRec { u32 kind, addr, size, data; };
struct Report
{
	u32 status, op, what, a, b, c, d, verify_fail;
	u32 cov[COV_N];
	u32 pad[32 - 8 - COV_N];
	OpRec last[8];   // the ops before the failure, oldest first
} __attribute__((aligned(128)));
static OpRec g_ops[8];
static u32 g_opn;

static void trace(u32 kind, u32 addr, u32 size, u32 data)
{
	OpRec& r = g_ops[g_opn++ & 7];
	r.kind = kind;
	r.addr = addr;
	r.size = size;
	r.data = data;
}
static Report g_rep;

static int fail(u32 op, u32 what, u32 a, u32 b, u32 c, u32 d)
{
	for (u32 i = 0; i < 8; i++)
		g_rep.last[i] = g_ops[(g_opn + i) & 7];
	g_rep.status = 2;
	g_rep.op = op;
	g_rep.what = what;
	g_rep.a = a;
	g_rep.b = b;
	g_rep.c = c;
	g_rep.d = d;
	return 1;
}

// 0 = same; else fills the report
static int compare(u32 op, int full)
{
	if (g_hs[0].n != g_hs[1].n)
		return fail(op, 'h', g_hs[0].n, g_hs[1].n, g_hs[0].log[(g_hs[0].n - 1) & 63].kind, g_hs[1].log[(g_hs[1].n - 1) & 63].kind);
	if (g_hs[0].steps != g_hs[1].steps)
		return fail(op, 's', g_hs[0].steps, g_hs[1].steps, 0, 0);
	if (e68k_out != (ac.e68k_out != 0) || e68k_reg_L != ac.e68k_l || e68k_reg_M != ac.e68k_m)
		return fail(op, 'e', e68k_out, ac.e68k_out, e68k_reg_L, ac.e68k_l);
	if (aica_interr != (ac.interr != 0) || (aica_interr && aica_reg_L != ac.reg_l))
		return fail(op, 'i', aica_interr, ac.interr, aica_reg_L, ac.reg_l);
	if (((g_istext >> 1) & 1) != ac.sh4_irq)
		return fail(op, 'q', g_istext, ac.sh4_irq, 0, 0);
	for (u32 i = 0; i < 3; i++)
		if (timers[i].c_step != ac.c_step[i] || timers[i].m_step != ac.m_step[i])
			return fail(op, 't', i, timers[i].c_step, ac.c_step[i], timers[i].m_step);
	if (full)
		for (u32 a = 0; a < 0x8000; a += 16)
			if (memcmp(aica_reg + a, ac.reg + a, 16))
				for (u32 b = a; b < a + 16; b++)
					if (aica_reg[b] != ac.reg[b])
						return fail(op, 'r', b, aica_reg[b], ac.reg[b], 0);
	return 0;
}

static u32 g_seen_hooks;

// The new hook records of this op must match one for one.
static int compare_hooks(u32 op)
{
	for (; g_seen_hooks != g_hs[0].n; g_seen_hooks++)
	{
		const Hook& x = g_hs[0].log[g_seen_hooks & 63];
		const Hook& y = g_hs[1].log[g_seen_hooks & 63];
		if (x.kind != y.kind || x.a != y.a || x.b != y.b || x.c != y.c || x.d != y.d || x.steps != y.steps)
			return fail(op, 'w', (x.kind << 16) | y.kind, x.a, y.a, (x.d << 16) | y.d);
		if (x.kind == 'M')
			g_rep.cov[COV_MON]++;
		else if (x.kind == 'C')
			g_rep.cov[COV_CDDA]++;
		else if (x.kind == 'X')
			g_rep.cov[COV_RACE]++;
	}
	return 0;
}

static u8 s_reg_ref[0x8000 + 16] __attribute__((aligned(16)));
static u8 s_aram[4096] __attribute__((aligned(16)));

// arg1 = seed, arg2 = ops, arg3 = report EA; the seed's low bits pick aica_fast, KYONB mode and MutePcm16.
int main(uint64_t seed, uint64_t nops, uint64_t rep_ea)
{
	g_rng = (u32)seed * 2654435761u + 1;
	g_aica_fast_preset = (int)(seed & 1);
	g_aica_kyonb_preset = (int)((seed >> 1) & 1);
	g_mute = (int)((seed >> 2) & 1);
	g_race = (int)((seed >> 3) & 1);
	g_clamp = (int)((seed >> 4) & 1);
	aica_reg = s_reg_ref;
	aica_ram_l = s_aram;
	ref_aica_params(s_aram, &g_istext, ref_raise, ref_cancel);
	arm_params.aica_ram = s_aram;
	arm_params.ReadMem_aica_reg = aica_ReadMem_reg;
	arm_params.WriteMem_aica_reg = aica_WriteMem_reg;
	arm_init_mem();
	AICA_Init_test();
	ac_init();
	ac.race_fix = (u32)g_race;
	ac.ca_clamp = (u32)g_clamp;
	g_rep.status = 1;
	int bad = compare(0, 1);

	for (u32 op = 1; op <= (u32)nops && !bad; op++)
	{
		const u32 clamped0 = ac.ca_clamped;
		const u32 kind = rnd(100);
		if (kind < 30)   // SH4 write
		{
			const u32 sz = rnd(10) < 3 ? 1 : rnd(4) ? 2 : 4;
			const u32 a = pick_addr() & ~(sz - 1), d0 = pick_data(a);
			const u32 d = sz == 1 ? d0 & 0xFF : sz == 2 ? d0 & 0xFFFF : d0, o = a & 0x7F;
			trace('w', a, sz, d);
			aica_WriteMem_reg(0x00700000 | a, d, sz);
			ac_write(0x00700000 | a, d, sz);
			g_rep.cov[COV_WR]++;
			if (a < 0x2000)
			{
				g_rep.cov[COV_KYONEX] += (o == 0 && sz > 1 && (d & 0x8000)) || (o == 1 && sz == 1 && (d & 0x80));
				g_rep.cov[COV_LFORE] += (o == 0x1C && sz > 1 && (d & 0x8000)) || (o == 0x1D && sz == 1 && (d & 0x80));
			}
		}
		else if (kind < 50)   // SH4 read
		{
			const u32 sz = rnd(10) < 3 ? 1 : rnd(4) ? 2 : 4;
			const u32 a = pick_addr() & ~(sz - 1);
			trace('r', a, sz, 0);
			const u32 x = aica_ReadMem_reg(0x00700000 | a, sz), y = ac_read(0x00700000 | a, sz);
			g_rep.cov[COV_RD]++;
			if (x != y)
				bad = fail(op, 'R', a, sz, x, y);
		}
		else if (kind < 60)   // ARM7 write: bytes and words, some words unaligned
		{
			const u32 sz = rnd(10) < 4 ? 1 : 4, hi = 0x00800000 | (rnd(0x100) << 15) | (rnd(4) ? 0 : rnd(0x100) << 24);
			u32 a = pick_addr();
			if (sz == 4)   // odd 16-bit accesses: the SPU build of the reference aligns them down, the PPU does not
				a = rnd(10) ? a & ~3u : (a & ~3u) | 2;
			const u32 d = pick_data(a & 0x7FFF), e0 = ac.e68k_out;
			trace('W', hi | a, sz, d);
			if (sz == 1)
			{
				WriteMemArm<1, u8>(hi | a, (u8)d);
				ac_arm_write(hi | a, d & 0xFF, 1);
			}
			else
			{
				WriteMemArm<4, u32>(hi | a, d);
				ac_arm_write(hi | a, d, 4);
			}
			g_rep.cov[COV_ARM_WR]++;
			g_rep.cov[COV_ACCEPT] += e0 && !ac.e68k_out;
		}
		else if (kind < 70 && rnd(10) == 0)   // ARM7 Reset Fix: the latch clear of an ARM7 reset
		{
			const u32 stale0 = ac.stale;
			trace('Z', 0, 0, 0);
			const bool x = arm_reset_latch();
			ac_reset_latch();
			if (x != (ac.stale != stale0))
				bad = fail(op, 'Z', x, ac.stale - stale0, 0, 0);
			g_rep.cov[COV_LATCH] += x;
		}
		else if (kind < 70)   // interrupt traffic: FIQ accepts, pending clears, enables and levels
		{
			const u32 r = rnd(8), e0 = ac.e68k_out;
			u32 a, d, arm = 1, sz = 4;
			switch (r)
			{
			case 0: case 1: a = 0x2D04; d = rnd(4) ? 1 : rnd(0x10000); sz = rnd(2) ? 4 : 1; break;
			case 2: a = 0x28A4; d = rnd(3) == 0 ? 0x7FF : rnd(2) ? 1u << rnd(11) : rnd(0x800); break;   // SCIRE
			case 3: a = 0x28BC; d = rnd(2) ? 1u << rnd(11) : rnd(0x800); arm = rnd(2); break;   // MCIRE
			case 4: a = rnd(2) ? 0x289C : 0x28B4; d = rnd(0x800); arm = rnd(2); break;   // SCIEB, MCIEB
			case 5: a = 0x28A8 + 4 * rnd(3); d = rnd(0x100); arm = rnd(2); break;   // SCILV0-2
			case 6: a = rnd(2) ? 0x28A0 : 0x28B8; d = 0x20; arm = rnd(2); break;   // SCPU
			default: a = 0x2D00; d = 0; arm = 2; break;   // REG_L read
			}
			trace(arm == 2 ? 'L' : arm ? 'W' : 'w', a, sz, d);
			if (arm == 2)
			{
				const u32 x = ReadMemArm<4, u32>(0x00800000 | a), y = ac_arm_read(0x00800000 | a, 4);
				if (x != y)
					bad = fail(op, 'L', a, 4, x, y);
			}
			else if (arm)
			{
				if (sz == 1)
				{
					WriteMemArm<1, u8>(0x00800000 | a, (u8)d);
					ac_arm_write(0x00800000 | a, d & 0xFF, 1);
				}
				else
				{
					WriteMemArm<4, u32>(0x00800000 | a, d);
					ac_arm_write(0x00800000 | a, d, 4);
				}
			}
			else
			{
				aica_WriteMem_reg(0x00700000 | a, d, 2);
				ac_write(0x00700000 | a, d, 2);
			}
			g_rep.cov[COV_ACCEPT] += e0 && a == 0x2D04 && arm == 1 && (d & 1);
		}
		else if (kind < 82)   // ARM7 read
		{
			const u32 sz = rnd(10) < 4 ? 1 : 4, hi = 0x00800000 | (rnd(0x100) << 15);
			u32 a = pick_addr();
			if (sz == 4)   // odd 16-bit accesses: the SPU build of the reference aligns them down, the PPU does not
				a = rnd(10) ? a & ~3u : (a & ~3u) | 2;
			trace('R', hi | a, sz, 0);
			const u32 x = sz == 1 ? ReadMemArm<1, u8>(hi | a) : ReadMemArm<4, u32>(hi | a);
			const u32 y = ac_arm_read(hi | a, sz);
			g_rep.cov[COV_ARM_RD]++;
			if (x != y)
				bad = fail(op, 'A', a, sz, x, y);
		}
		else   // samples, sometimes past a CD-DA refill
		{
			const u32 n = rnd(20) ? 1 + rnd(4) : 300 + rnd(600);
			trace('S', 0, n, 0);
			for (u32 i = 0; i < n; i++)
			{
				const u32 e0 = ac.e68k_out, q0 = ac.sh4_irq, t0 = ac.reg[0x28A3] & 0xC0, t1 = ac.reg[0x28A2] & 1;
				libAICA_TimeStep();
				ac_timestep();
				g_rep.cov[COV_OVF] += ((ac.reg[0x28A3] & 0xC0) != t0) || ((ac.reg[0x28A2] & 1) != t1);
				g_rep.cov[COV_E68K_UP] += !e0 && ac.e68k_out;
				g_rep.cov[COV_IRQ_UP] += !q0 && ac.sh4_irq;
				g_rep.cov[COV_IRQ_DOWN] += q0 && !ac.sh4_irq;
				if ((bad = compare(op, 0)) || (bad = compare_hooks(op)))
					break;
			}
			g_rep.cov[COV_STEPS] += n;
		}
		if (!bad)
			bad = compare(op, 1);
		if (!bad)
			bad = compare_hooks(op);
		g_rep.cov[COV_CLAMP] += ac.ca_clamped - clamped0;
	}
	if (!bad)
		bad = compare((u32)nops, 1);
	g_rep.verify_fail = g_verify_fail;
	mfc_put(&g_rep, (u32)rep_ea, sizeof(g_rep), 1, 0, 0);
	mfc_write_tag_mask(2);
	mfc_read_tag_status_all();
	if (bad)
		spu_stop(2);
	spu_stop(1);
	return 0;
}
