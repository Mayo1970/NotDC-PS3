// OPTIMIZATION_PLAN 5.15 B2: the spe_arm7 SPU image in misc/spu_sim against an x86 reference made of the same ARM7
// core, aica_ctl.h and caique model on flat memory, fed the same SH4 commands. Usage: harness elf syms seed batches
#include "spusim.h"
#include "aica_model.h"
#include "spe_aica.h"
#include "spe_arm7.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <vector>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
static void put32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void put16(u8* p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

static const u32 ARAM = 2u << 20;

// ---- main memory as the SPU sees it (big-endian data) ----
enum : u64 { EA_BELL = 0x10000000, EA_ECHO = 0x10000100, EA_SETUP = 0x10000200, EA_ABELL = 0x10000300,
             EA_AECHO = 0x10000400, EA_RING = 0x10010000, EA_LOG = 0x10100000, EA_ARAM = 0x20000000 };
static u8 g_bell[128], g_echo[128], g_setup[128], g_abell[128], g_aecho[256];
static u8 g_ring[ARM_CMDS * 16];
static u8* g_log;
static u8* g_aram;
struct Region { u64 ea; u32 size; u8* p; };
static Region g_rg[8];

static u8* ea_fn(void*, u64 ea, u32 size)
{
	for (const Region& r : g_rg)
		if (r.p && ea >= r.ea && ea + size <= r.ea + r.size)
			return r.p + (ea - r.ea);
	return 0;
}

// ---- reference: arm_mem.cpp on a flat ARAM in PS3 order, aica_ctl.h, ps3_aica_spe.cpp's twin and log ----
static u8* g_raram;
#include "aica_ctl.h"

static inline uint32_t a7_fetch(uint32_t a) { return be32(&g_raram[a & 0x1FFFFC]); }
static uint32_t a7_rd32(uint32_t addr)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
		return ac_arm_read(addr, 4);
	const uint32_t w = be32(&g_raram[(addr & ~3u) & 0x1FFFFF]);
	if (!(addr & 3))
		return w;
	const uint32_t rot = (addr & 3) * 8;
	return (w >> rot) | (w << (32 - rot));
}
static uint32_t a7_rd8(uint32_t addr)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
		return ac_arm_read(addr, 1);
	return g_raram[(addr ^ 3) & 0x1FFFFF];
}
static void a7_wr32(uint32_t addr, uint32_t v)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
	{
		ac_arm_write(addr, v, 4);
		return;
	}
	if (addr & 3)
	{
		for (u32 i = 0; i < 4; i++)
			g_raram[((addr + i) ^ 3) & 0x1FFFFF] = (u8)(v >> (8 * i));
		return;
	}
	put32(&g_raram[addr & 0x1FFFFF], v);
}
static void a7_wr8(uint32_t addr, uint32_t v)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
	{
		ac_arm_write(addr, v & 0xFF, 1);
		return;
	}
	g_raram[(addr ^ 3) & 0x1FFFFF] = (u8)v;
}
#define A7_MAP_N 8192u
#define A7_E68K_OUT ac.e68k_out
#include "arm7_spu.h"

static A7Cpu g_cpu;

uint8_t caique_rd8(uint32_t a) { return g_raram[(a ^ 3) & 0x1FFFFF]; }
uint16_t caique_rd16(uint32_t a) { return (uint16_t)(g_raram[(a ^ 3) & 0x1FFFFF] | (g_raram[((a + 1) ^ 3) & 0x1FFFFF] << 8)); }
const uint8_t* caique_line(uint32_t a) { return g_raram + (a & 0x1FFF80); }
uint16_t caique_dsp_rd16(uint32_t a, uint32_t, uint32_t) { return caique_rd16(a); }
void caique_dsp_wr16(uint32_t, uint32_t, uint32_t, uint16_t) {}

static caique::AicaModel* g_m;
static u32 r_now, r_checks;
struct Rec { u32 sample; u8 op; u16 off; u32 val, hash; };
static std::vector<Rec> g_rlog;
static u32 g_fifo[16], g_fw, g_fr;

// snap_hash of the big-endian CtlSnap image, as the SPU computes it (misc/spu_sim/model_wrap.cpp)
static u32 snap_hash_be()
{
	static caique::CtlSnap s;
	g_m->snap(s);
	u32 w[sizeof(s) / 4], n = 0;
	w[n++] = s.sample;
	w[n++] = s.lfsr;
	w[n++] = s.mdec;
	w[n++] = s.eg_cnt;
	for (int c = 0; c < 64; c++)
	{
		const caique::SlotSnap& p = s.s[c];
		w[n++] = p.CA;
		w[n++] = p.step;
		w[n++] = (u32)p.a << 16 | p.fv;
		w[n++] = (u32)p.aeg << 24 | (u32)p.feg << 16 | (u32)p.flags << 8 | p.lfo;
		w[n++] = p.lfo_cnt;
	}
	u32 x = 2166136261u;
	for (u32 i = 0; i < n; i++)
		x = (x ^ w[i]) * 16777619u;
	return x;
}

static void r_push(u8 op, u16 off, u32 val, u32 hash) { g_rlog.push_back({ r_now, op, off, val, hash }); }
static void r_catch_up() { g_m->advance(r_now - (u32)g_m->samples, false, 0); }

static void ac_twin_write(uint32_t off, uint32_t data, uint32_t size, uint32_t old16)
{
	if (off & 2)
		return;
	u32 val;
	if (size == 1)
		val = (off & 1) ? ((old16 & 0x00FF) | ((data & 0xFF) << 8)) : ((old16 & 0xFF00) | (data & 0xFF));
	else
		val = data & 0xFFFF;
	off &= 0x7FFC;
	r_catch_up();
	g_m->write(off, val);
	r_push(AICA_OP_WRITE, (u16)off, val, 0);
}

static uint32_t ac_twin_mon(uint32_t reg, int keep_lp)
{
	r_catch_up();
	caique::Slot& c = g_m->slot[(g_m->r(0x280C) >> 8) & 0x3F];
	const bool lp = c.looped;
	const u16 v = (u16)g_m->read(reg);
	if (keep_lp)
		c.looped = lp;
	return v;
}

static void ac_twin_step(void)
{
	if ((++r_now & (AICA_BLOCK - 1)) != 0)
		return;
	r_catch_up();
	r_push(AICA_OP_CHECK, 0, r_checks, snap_hash_be());
	r_checks++;
}

static void ac_cdda_refill(void)
{
	u32 v = AICA_CDDA_SILENT;
	if (g_fr != g_fw)
		v = g_fifo[g_fr++ & 15];
	r_push(AICA_OP_CDDA, 0, v, 0);
}

// arm_aica.cpp arm_aica_run
static u32 r_acc, r_calls;
static void ref_mu(u32 cycles, u32 samples, u32 flags)
{
	const u32 stage = ARM_MU_STAGE(flags);
	u32 batch = ARM_MU_BATCH(flags), ticks;
	if (batch <= 1)
	{
		ticks = (cycles / 20) >> stage;
		r_acc = 0;
		r_calls = 0;
	}
	else
	{
		if (batch > 16)
			batch = 16;
		r_acc += cycles;
		if (++r_calls < batch)
			goto deferred;
		ticks = (r_acc / 20) >> stage;
		r_acc = 0;
		r_calls = 0;
	}
	g_cpu.idle_pref = (u8)ARM_MU_SAFE(flags);   // arm7_cached reads the preset when it rebuilds its table
	a7_run(&g_cpu, ticks);
deferred:
	while (samples--)
		ac_timestep();
}

// ---- random ARM7 programs (misc/arm7spu_check), with AICA register bases ----
static u64 g_rng;
static u32 rnd()
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return (u32)g_rng;
}
static u32 pick(u32 n) { return rnd() % n; }
static u32 cond_field()
{
	const u32 r = pick(100);
	return r < 60 ? 0xE : r < 98 ? pick(15) : 0xF;
}
static u32 reg_field() { return pick(100) < 6 ? 15 : pick(15); }
static const u32 k_modes[7] = { 0x10, 0x11, 0x12, 0x13, 0x17, 0x1b, 0x1f };

static u32 gen_insn(bool nocontrol)
{
	const u32 c = cond_field() << 28;
	for (;;)
	{
		const u32 k = pick(100);
		if (k < 30)
		{
			u32 op = c | (pick(16) << 21) | (pick(2) << 20) | (reg_field() << 16) | (reg_field() << 12);
			if (pick(2))
				op |= 0x02000000 | (pick(16) << 8) | pick(256);
			else
			{
				op |= reg_field();
				const u32 f = pick(4);
				if (f == 1 || f == 2)
					op |= (pick(32) << 7) | (pick(4) << 5);
				else if (f == 3)
					op |= (reg_field() << 8) | (pick(4) << 5) | 0x10;
			}
			if (nocontrol && ((op >> 12) & 0xF) == 15)
				continue;
			return op;
		}
		if (k < 52)   // LDR / STR, often off r8-r11 (AICA register bases)
		{
			const u32 base = pick(3) == 0 ? 8 + pick(4) : reg_field();
			u32 op = c | 0x04000000 | (pick(32) << 20) | (base << 16) | (reg_field() << 12);
			if (pick(2))
				op |= 0x02000000 | (pick(32) << 7) | (pick(4) << 5) | reg_field();
			else
				op |= base >= 8 && base <= 11 ? pick(64) * 4 : pick(4096);
			if (nocontrol && (op & 0x00100000) && ((op >> 12) & 0xF) == 15)
				continue;
			return op;
		}
		if (k < 58)
		{
			u32 list = rnd() & 0xFFFF;
			if (!list)
				list = 1;
			u32 op = c | 0x08000000 | (pick(32) << 20) | (reg_field() << 16) | list;
			if (nocontrol && (op & 0x00100000) && (op & 0x8000))
				continue;
			return op;
		}
		if (k < 63)
			return c | 0x00000090 | (pick(4) << 20) | (reg_field() << 16) | (reg_field() << 12) | (reg_field() << 8) | reg_field();
		if (k < 65)
			return c | 0x01000090 | (pick(2) << 22) | (reg_field() << 16) | (reg_field() << 12) | reg_field();
		if (k < 70)
		{
			const u32 spsr = pick(2) << 22;
			if (pick(3) == 0)
				return c | 0x010F0000 | spsr | (reg_field() << 12);
			const u32 fields = pick(16) << 16;
			if (pick(2))
				return c | 0x0320F000 | spsr | fields | (rnd() & 0xC0) | k_modes[pick(7)];
			return c | 0x0120F000 | spsr | fields | reg_field();
		}
		if (nocontrol)
			continue;
		if (k < 90)
		{
			int32_t off = pick(100) < 20 ? -2 : (int32_t)pick(40) - 24;
			if (pick(10) == 0)
				off = (int32_t)(rnd() & 0xFFFF) - 0x8000;
			return c | 0x0A000000 | (pick(4) == 0 ? 0x01000000 : 0) | ((u32)off & 0x00FFFFFF);
		}
		if (k < 93)
			return c | 0x0F000000 | (rnd() & 0xFFFFFF);
		if (k < 95)
			return c | 0x0C000000 | (rnd() & 0x03FFFFFF);
		return rnd();
	}
}

// The program: code in the first 64 KB, a reset handler that points r8-r11 at AICA registers, data above.
static void gen_program(u8* mem)
{
	u32 w[16384];
	u32 i = 0;
	while (i < 16384)
	{
		const u32 r = pick(1000);
		if (r < 3 && i + 160 < 16384)
			for (u32 n = 140 + pick(20); n; n--)
				w[i++] = gen_insn(true);
		else if (r < 30 && i + 8 < 16384)
		{
			w[i++] = 0xE5810000 | (pick(8) << 12) | (pick(8) << 16) | pick(64);
			const u32 head = i;
			w[i++] = 0xE5900000 | (pick(4) << 12) | (pick(8) << 16) | (pick(16) << 2);
			if (pick(2))
				w[i++] = 0xE2800001 | (pick(4) << 12) | (pick(4) << 16);
			w[i++] = 0xE3500000 | (pick(4) << 16) | pick(4);
			w[i] = 0x1A000000 | ((u32)((int32_t)head - (int32_t)i - 2) & 0x00FFFFFF);
			i++;
		}
		else
			w[i++] = gen_insn(false);
	}
	// Reset vector: B to a stub at 0x100 that points r8-r11 (and FIQ's r10) at AICA registers, then B 0x200.
	w[0] = 0xEA00003E;   // B 0x100
	u32 s = 0x40;
	w[s++] = 0xE3A08502;   // MOV r8, #0x00800000
	w[s++] = 0xE2888B0A;   // ADD r8, r8, #0x2800
	w[s++] = 0xE2889080;   // ADD r9, r8, #0x80
	w[s++] = 0xE288AC05;   // ADD r10, r8, #0x500 (REG_L)
	w[s++] = 0xE3A0B502;   // MOV r11, #0x00800000
	w[s++] = 0xE28BBD00 | pick(64) * 2;   // ADD r11, r11, #channel * 0x80
	w[s++] = 0xE3A0D801;   // MOV sp, #0x10000
	w[s++] = 0xE3A000D1;   // MOV r0, #0xD1
	w[s++] = 0xE121F000;   // MSR CPSR_c, r0 (FIQ mode)
	w[s++] = 0xE3A0A502;   // MOV r10, #0x00800000 (r10_fiq)
	w[s++] = 0xE28AAC2D;   // ADD r10, r10, #0x2D00
	w[s++] = 0xE3A0001F;   // MOV r0, #0x1F
	w[s++] = 0xE121F000;   // MSR CPSR_c, r0 (system mode, IRQ and FIQ on)
	w[s] = 0xEA000000 | ((u32)((int32_t)(0x200 / 4) - (int32_t)s - 2) & 0x00FFFFFF);   // B 0x200
	if (pick(4))
		w[1] = 0xE1B0F00E;
	if (pick(4))
		w[2] = 0xE1B0F00E;
	// FIQ: read REG_L, accept at REG_M, return
	w[7] = 0xEA000000 | ((u32)((int32_t)(0x180 / 4) - 7 - 2) & 0x00FFFFFF);
	u32 f = 0x180 / 4;
	w[f++] = 0xE59A0000;   // LDR r0, [r10]      REG_L
	w[f++] = 0xE3A01001;   // MOV r1, #1
	w[f++] = 0xE58A1004;   // STR r1, [r10, #4]  REG_M accept
	w[f++] = 0xE25EF004;   // SUBS pc, lr, #4
	for (u32 j = 0; j < 16384; j++)
		put32(mem + 4 * j, w[j]);
	for (u32 j = 16384; j < ARAM / 4; j++)
	{
		const u32 r = pick(4);
		put32(mem + 4 * j, r == 0 ? gen_insn(false) : r == 1 ? pick(0x10000) : r == 2 ? (pick(0x4000) << 2) : rnd());
	}
}

// ---- SH4 side: register traffic like misc/aicactl_check ----
static u32 pick_reg()
{
	static const u8 offs[] = { 0x00, 0x01, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x1D, 0x20, 0x24, 0x28, 0x40, 0x44 };
	const u32 r = pick(100);
	if (r < 35) return pick(64) * 0x80 + offs[pick(sizeof(offs))];
	if (r < 50) return 0x2800 + pick(0x18);
	if (r < 85) return 0x2890 + pick(0x30);
	if (r < 92) return 0x3000 + pick(0x5000);
	return pick(0x8000);
}
static u32 pick_data(u32 a)
{
	const u32 w = a & ~3u;
	if (w >= 0x2890 && w < 0x289C && pick(4)) return (0xF0 + pick(16)) | (pick(4) << 8);
	if ((w == 0x289C || w == 0x28B4) && pick(2)) return pick(0x800);
	if ((w == 0x28A8 || w == 0x28AC || w == 0x28B0) && pick(2)) return pick(0x100);
	if (a < 0x2000 && (a & 0x7F) <= 1 && pick(2)) return 0xC000 | pick(0x10000);
	return pick(0x10000);
}

// ---- the SPU ----
static SpuSim* g_sim;
static u32 g_head, g_consumed, g_cpu_ea, g_ac_ea;
static u32 g_dlo = 0xFFFF, g_dhi;
static bool g_lag;   // the sound SPU stops taking records for a while: the ARM7 SPU waits in log_room
static bool g_storm; // SH4 register writes by the dozen (lagging seeds): the log fills and wraps
static u64 g_cmds, g_mismatch, g_limit_runs;

struct Cmd { u8 op, flags; u16 a; u32 b, c; };

static void push_cmd(const Cmd& c)
{
	u8* p = g_ring + (g_head & (ARM_CMDS - 1)) * 16;
	p[0] = c.op;
	p[1] = c.flags;
	put16(p + 2, c.a);
	put32(p + 4, c.b);
	put16(p + 8, g_dlo);
	put16(p + 10, g_dhi);
	put32(p + 12, c.c);
	g_dlo = 0xFFFF;
	g_dhi = 0;
	g_head++;
	g_cmds++;
}

static void aram_write(u32 h, u32 v, u32 n)   // an SH4 store to both ARAM copies (host offset h)
{
	if (n == 4)
	{
		put32(g_aram + h, v);
		put32(g_raram + h, v);
	}
	else
		g_aram[h] = g_raram[h] = (u8)v;
	const u32 l = h >> 7;
	if (l < g_dlo) g_dlo = l;
	if (l > g_dhi) g_dhi = l;
}

static bool fail(const char* what, u32 a, u32 b, u32 c)
{
	if (g_mismatch++ < 6)
		printf("  MISMATCH after %llu commands: %s %08X %08X %08X\n", (unsigned long long)g_cmds, what, a, b, c);
	return false;
}

// The sound SPU's side: compare every published record, then take them (unless it is lagging).
static bool take_log()
{
	const u32 head = be32(g_abell);
	for (; g_consumed != head; g_consumed++)
	{
		if (g_consumed >= g_rlog.size())
			return fail("log: record past the reference", g_consumed, head, (u32)g_rlog.size());
		const u8* r = g_log + (g_consumed & (AICA_LOG - 1)) * 16;
		const Rec& x = g_rlog[g_consumed];
		if (be32(r) != x.sample || r[4] != x.op || be16(r + 6) != x.off || be32(r + 8) != x.val || be32(r + 12) != x.hash)
			return fail("log record", g_consumed, (u32)r[4] << 16 | x.op, be32(r + 8) ^ x.val);
	}
	if (!g_lag)
		put32(g_aecho + offsetof(AicaEcho, tail), head);
	return true;
}

static bool run_spu()
{
	put32(g_bell + offsetof(ArmBell, head), g_head);
	spusim_llr_lost(g_sim);
	for (;;)
	{
		const int rc = spusim_run(g_sim, 4000000);
		if (rc == SPU_RUN_BLOCKED)
			return true;
		if (rc != SPU_RUN_LIMIT)
		{
			printf("  SPU stopped: rc %d %s\n", rc, g_sim->msg);
			return false;
		}
		// Busy in log_room (or just long): let the sound SPU catch up.
		g_limit_runs++;
		if (!take_log())
			return false;
		put32(g_aecho + offsetof(AicaEcho, tail), be32(g_abell));
	}
}

static bool compare(bool rd, u32 rd_seq, u32 rd_val)
{
	const u8* e = g_echo;
	if (be32(e + offsetof(ArmEcho, tail)) != g_head)
		return fail("echo tail", be32(e + offsetof(ArmEcho, tail)), g_head, 0);
	if (be32(e + offsetof(ArmEcho, irq)) != ac.sh4_irq)
		return fail("echo irq", be32(e + offsetof(ArmEcho, irq)), ac.sh4_irq, 0);
	if (be32(e + offsetof(ArmEcho, samples)) != ac.smp)
		return fail("echo samples", be32(e + offsetof(ArmEcho, samples)), ac.smp, 0);
	if (rd && (be32(e + offsetof(ArmEcho, rd_seq)) != rd_seq || be32(e + offsetof(ArmEcho, rd_val)) != rd_val))
		return fail("register read", be32(e + offsetof(ArmEcho, rd_seq)), be32(e + offsetof(ArmEcho, rd_val)), rd_val);
	if (be32(e + offsetof(ArmEcho, log_drops)) || be32(e + offsetof(ArmEcho, cdda_under)))
		return fail("log drops / cdda under", be32(e + offsetof(ArmEcho, log_drops)), be32(e + offsetof(ArmEcho, cdda_under)), 0);
	// The SPU's ARM7 and AICA state, read from its LS
	const u8* c = g_sim->ls + g_cpu_ea;
	for (u32 i = 0; i < 47; i++)
		if (be32(c + 4 * i) != g_cpu.r[i])
			return fail("arm reg", i, be32(c + 4 * i), g_cpu.r[i]);
	if (be32(c + offsetof(A7Cpu, next_pc)) != g_cpu.next_pc)
		return fail("arm next_pc", be32(c + offsetof(A7Cpu, next_pc)), g_cpu.next_pc, 0);
	if (memcmp(c + offsetof(A7Cpu, n), &g_cpu.n, offsetof(A7Cpu, mode) - offsetof(A7Cpu, n)))
		return fail("arm flags/enables", be32(c + offsetof(A7Cpu, n)), be32(c + offsetof(A7Cpu, n) + 4), g_cpu.enabled);
	for (u32 f = offsetof(A7Cpu, mode); f < offsetof(A7Cpu, map); f += 4)
		if (be32(c + f) != *(const u32*)((const u8*)&g_cpu + f))
			return fail("arm mode/stats", f, be32(c + f), *(const u32*)((const u8*)&g_cpu + f));
	for (u32 i = 0; i < A7_MAP_N; i++)
		if (be32(c + offsetof(A7Cpu, map) + 4 * i) != g_cpu.map[i])
			return fail("arm block map", i, be32(c + offsetof(A7Cpu, map) + 4 * i), g_cpu.map[i]);
	const u8* a = g_sim->ls + g_ac_ea;
	if (memcmp(a, ac.reg, sizeof(ac.reg)))
	{
		for (u32 i = 0; i < sizeof(ac.reg); i++)
			if (a[i] != ac.reg[i])
				return fail("aica reg byte", i, a[i], ac.reg[i]);
	}
	for (u32 f = offsetof(AicaCtl, c_step); f < sizeof(AicaCtl); f += 4)
		if (be32(a + f) != *(const u32*)((const u8*)&ac + f))
			return fail("aica state", f, be32(a + f), *(const u32*)((const u8*)&ac + f));
	if (memcmp(g_aram, g_raram, ARAM))
	{
		for (u32 i = 0; i < ARAM; i++)
			if (g_aram[i] != g_raram[i])
				return fail("aram byte", i, g_aram[i], g_raram[i]);
	}
	return take_log();
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		fprintf(stderr, "usage: harness elf syms seed batches\n");
		return 2;
	}
	FILE* sf = fopen(argv[2], "r");
	char line[256];
	while (sf && fgets(line, sizeof(line), sf))
	{
		const u32 v = (u32)strtoul(line, 0, 16);
		if (strstr(line, "_ZL3cpu")) g_cpu_ea = v;
		if (strstr(line, "_ZL2ac")) g_ac_ea = v;
	}
	if (sf) fclose(sf);
	if (!g_cpu_ea || !g_ac_ea)
	{
		fprintf(stderr, "no cpu/ac symbols in %s\n", argv[2]);
		return 2;
	}
	const u32 seed = (u32)strtoul(argv[3], 0, 0), batches = (u32)strtoul(argv[4], 0, 0);
	g_rng = 0x9E3779B97F4A7C15ull * (seed + 1);
	g_lag = g_storm = (seed % 4) == 3;
	g_log = (u8*)calloc(AICA_LOG, 16);
	g_aram = (u8*)calloc(ARAM, 1);
	g_raram = (u8*)calloc(ARAM, 1);
	g_rg[0] = { EA_BELL, 128, g_bell };
	g_rg[1] = { EA_ECHO, 128, g_echo };
	g_rg[2] = { EA_SETUP, 128, g_setup };
	g_rg[3] = { EA_ABELL, 128, g_abell };
	g_rg[4] = { EA_AECHO, 256, g_aecho };
	g_rg[5] = { EA_RING, sizeof(g_ring), g_ring };
	g_rg[6] = { EA_LOG, AICA_LOG * 16, g_log };
	g_rg[7] = { EA_ARAM, ARAM, g_aram };
	put32(g_setup + offsetof(ArmSetup, ring_ea), (u32)EA_RING);
	put32(g_setup + offsetof(ArmSetup, aram_ea), (u32)EA_ARAM);
	put32(g_setup + offsetof(ArmSetup, aica_bell_ea), (u32)EA_ABELL);
	put32(g_setup + offsetof(ArmSetup, aica_echo_ea), (u32)EA_AECHO);
	put32(g_setup + offsetof(ArmSetup, aica_log_ea), (u32)EA_LOG);
	gen_program(g_aram);
	memcpy(g_raram, g_aram, ARAM);

	// reference, as the PPU starts it: AICA_Init, arm_Init (core off), the twin's constructor and reset()
	ac_init();
	a7_init(&g_cpu);
	g_m = new caique::AicaModel();
	g_m->reset();

	g_sim = (SpuSim*)calloc(1, sizeof(SpuSim));
	spusim_init(g_sim, ea_fn, 0);
	const u32 entry = spusim_load_elf(g_sim, argv[1]);
	if (!entry)
	{
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}
	g_sim->pc = entry;
	g_sim->r[3].w[1] = (u32)EA_BELL;
	g_sim->r[4].w[1] = (u32)EA_ECHO;
	g_sim->r[5].w[1] = (u32)EA_SETUP;
	if (spusim_run(g_sim, 100000000) != SPU_RUN_BLOCKED || be32(g_echo + offsetof(ArmEcho, state)) != ARM_SPE_STATE_RUNNING)
	{
		printf("seed %u: SPU boot failed: %s\n", seed, g_sim->msg);
		return 1;
	}

	const u32 batch_pref = 1 + seed % 4, safe0 = (seed >> 2) & 1;
	u32 safe = safe0, acc = 0, rd_seq = 0, cdda_index = AICA_CDDA_WORDS << 1, n_wr = 0, n_rd = 0, n_aram = 0, n_rst = 0;
	bool ok = true;
	// ARMRST release: the ARM7 starts at its reset vector
	{
		Cmd c = { ARM_CMD_WR, 0, 0x2C00, 0, 2 };
		push_cmd(c);
		a7_set_enabled(&g_cpu, 1);
		ac_write(0x2C00, 0, 2);
	}
	for (u32 b = 0; b < batches && ok; b++)
	{
		// SH4 ARAM stores before the batch (its first command carries the lines)
		for (u32 n = pick(4) == 0 ? 1 + pick(6) : 0; n; n--)
		{
			// Often into the code the ARM7 runs now: a stale cached line would show at once.
			const u32 r = pick(3);
			const u32 h = r == 0 ? (g_cpu.next_pc + pick(64) * 4 - 64) & 0x1FFFFC : r == 1 ? pick(0x10000) & ~3u : pick(ARAM) & ~3u;
			if (pick(4))
				aram_write(h, pick(3) ? gen_insn(false) : rnd(), 4);
			else
				aram_write(h | pick(4), rnd(), 1);
			n_aram++;
		}
		bool rd = false;
		u32 rd_val = 0;
		// A lagging sound SPU also sees a write storm, so the log fills and the ARM7 SPU waits in log_room.
		for (u32 n = g_storm ? 40 : 0; n; n--)
		{
			const u32 a = pick(64) * 0x80 + 4 * (2 + pick(16)), d = pick(0x10000);
			Cmd c = { ARM_CMD_WR, 0, (u16)a, d, 2 };
			push_cmd(c);
			ac_write(a, d, 2);
			n_wr++;
		}
		for (u32 n = 1 + pick(8); n && !rd; n--)
		{
			const u32 k = pick(100);
			Cmd c = { 0, 0, 0, 0, 0 };
			if (k < 78)   // MediumUpdate; its CD-DA sectors first, as the PPU queues them
			{
				const u32 cycles = 2000 + pick(6000);
				acc += cycles;
				u32 samples = acc / 3401;
				acc -= samples * 3401;
				for (u32 i = 0; i < samples; i++)
				{
					if (cdda_index >= AICA_CDDA_WORDS)
					{
						cdda_index = 0;
						Cmd d = { ARM_CMD_CDDA, 0, 0, pick(2) ? pick(AICA_CDDA_SLOTS) : AICA_CDDA_SILENT, 0 };
						push_cmd(d);
						g_fifo[g_fw++ & 15] = d.b;
					}
					cdda_index += 2;
				}
				const u32 batch = pick(20) ? batch_pref : pick(17);
				c = { ARM_CMD_MU, (u8)(batch | (pick(30) ? 0 : 1) << 5 | safe << 7), (u16)samples, cycles, 0 };
				push_cmd(c);
				ref_mu(c.b, c.a, c.flags);
			}
			else if (k < 90)   // SH4 register write
			{
				const u32 sz = pick(10) < 3 ? 1 : pick(4) ? 2 : 4, a = pick_reg() & ~(sz - 1), d0 = pick_data(a);
				const u32 d = sz == 1 ? d0 & 0xFF : sz == 2 ? d0 & 0xFFFF : d0;
				c = { ARM_CMD_WR, 0, (u16)a, d, sz };
				push_cmd(c);
				ac_write(a, d, sz);
				n_wr++;
			}
			else if (k < 96)   // SH4 register read: it ends the batch, the PPU waits for it
			{
				const u32 sz = pick(10) < 3 ? 1 : pick(4) ? 2 : 4, a = pick_reg() & ~(sz - 1);
				c = { ARM_CMD_RD, 0, (u16)a, ++rd_seq, sz };
				push_cmd(c);
				rd_val = ac_read(a, sz);
				rd = true;
				n_rd++;
			}
			else if (k < 98 && pick(8) == 0)   // ARMRST (it also empties the SPU's ARAM cache): hold, then maybe release
			{
				const u32 d = pick(3) ? 0 : 1;
				c = { ARM_CMD_WR, 0, 0x2C00, d, 2 };
				push_cmd(c);
				a7_set_enabled(&g_cpu, !(d & 1));
				ac_write(0x2C00, d, 2);
				n_rst++;
			}
			else if (k < 99)   // the idle rule changed in the menu
			{
				safe = pick(2);
				c = { ARM_CMD_FLUSH, 0, 0, 0, 0 };
				push_cmd(c);
				a7_flush(&g_cpu);
			}
			else
			{
				c = { ARM_CMD_RESET, 0, 0, 0, 0 };
				push_cmd(c);
				a7_reset(&g_cpu);
				Cmd r = { ARM_CMD_WR, 0, 0x2C00, 0, 2 };   // and the driver restarts it
				push_cmd(r);
				a7_set_enabled(&g_cpu, 1);
				ac_write(0x2C00, 0, 2);
			}
		}
		if (g_lag && b == 999)
			g_lag = false;   // the lagging sound SPU wakes up; the storm goes on, so the log wraps off a flush edge
		ok = run_spu() && compare(rd, rd_seq, rd_val);
	}
	printf("seed %u: %s | batches %u cmds %llu | samples %u recs %zu (checks %u) | arm runs %u fiqs %u decodes %u evicts %u"
	       " | aram misses %u puts %u | sh4 wr %u rd %u aram %u armrst %u | log waits %u limit runs %llu | %llu insns\n",
	       seed, ok ? "PASS" : "FAIL", batches, (unsigned long long)g_cmds, ac.smp, g_rlog.size(), r_checks,
	       be32(g_echo + offsetof(ArmEcho, runs)), g_cpu.fiqs, g_cpu.decodes, g_cpu.evicts,
	       be32(g_echo + offsetof(ArmEcho, misses)), be32(g_echo + offsetof(ArmEcho, puts)), n_wr, n_rd, n_aram, n_rst,
	       be32(g_echo + offsetof(ArmEcho, log_waits)), (unsigned long long)g_limit_runs, (unsigned long long)g_sim->insns);
	return ok ? 0 : 1;
}
