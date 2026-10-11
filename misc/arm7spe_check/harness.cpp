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
             EA_AECHO = 0x10000400, EA_HZ = 0x10001000, EA_RING = 0x10010000, EA_LOG = 0x10100000, EA_ARAM = 0x20000000 };
static u8 g_bell[128], g_echo[128], g_setup[128], g_abell[128], g_aecho[256], g_hz[sizeof(AicaHz)];
static u8 g_ring[ARM_CMDS * 16];
static u8* g_log;
static u8* g_aram;
struct Region { u64 ea; u32 size; u8* p; };
static Region g_rg[9];

static u8* ea_fn(void*, u64 ea, u32 size)
{
	for (const Region& r : g_rg)
		if (r.p && ea >= r.ea && ea + size <= r.ea + r.size)
			return r.p + (ea - r.ea);
	return 0;
}

// ---- reference: arm_mem.cpp on a flat ARAM in PS3 order, aica_ctl.h, ps3_aica_spe.cpp's twin and log ----
static u8* g_raram;
static void ac_dsp_read(uint32_t a);
#define AC_DSP_READ(a) ac_dsp_read(a)
#include "aica_ctl.h"
static void r_arm_store(u32 h, u32 n);   // spe_arm7.cpp aram_store: the hazard test before the store
static void r_arm_stored(u32 h);         // and the ring's dirty line after it

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
		{
			const u32 h = ((addr + i) ^ 3) & 0x1FFFFF;
			r_arm_store(h, 1);
			g_raram[h] = (u8)(v >> (8 * i));
			r_arm_stored(h);
		}
		return;
	}
	r_arm_store(addr & 0x1FFFFF, 4);
	put32(&g_raram[addr & 0x1FFFFF], v);
	r_arm_stored(addr & 0x1FFFFF);
}
static void a7_wr8(uint32_t addr, uint32_t v)
{
	addr &= 0x00FFFFFF;
	if (addr >= 0x800000)
	{
		ac_arm_write(addr, v & 0xFF, 1);
		return;
	}
	r_arm_store((addr ^ 3) & 0x1FFFFF, 1);
	g_raram[(addr ^ 3) & 0x1FFFFF] = (u8)v;
	r_arm_stored((addr ^ 3) & 0x1FFFFF);
}
#define A7_MAP_N 8192u
#define A7_E68K_OUT ac.e68k_out
#include "arm7_spu.h"

static A7Cpu g_cpu;
static void ac_race(void) { a7_race(&g_cpu); }

// The Looney Tunes fixes (plan 6.5, P44, P31) from the seed's bits 3-5, as ArmSetup.flags; bundle 5 from seed 100
static u32 g_flags, r_resets;

// spe_arm7.cpp arm_reset_edge: an ARM7 reset; ARM7 Reset Fix also clears the FIQ latch
static void ref_reset_edge()
{
	if (g_flags & ARM_SETUP_RESET_FIX)
	{
		r_resets++;
		ac_reset_latch();
	}
}
static void ref_set_enabled(int en)
{
	if (en && !g_cpu.enabled)
		ref_reset_edge();
	a7_set_enabled(&g_cpu, en);
}

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

static void r_catch_up() { g_m->advance(r_now - (u32)g_m->samples, false, 0); }

// ---- bundle 5 reference: the hz code of spe_arm7.cpp again, on the reference twin ----
static AicaHz r_hz, r_hz_out;          // the reference's map and counts, and their copy at the SPU's last put
static u32 r_live, r_synced_at = ~0u, r_blk = ~0u, r_tests, r_syncs, r_seq, r_rd_seq, r_in_arm;
static u32 r_ring_base, r_ring_len, r_plo = 1, r_phi, r_pt;
static bool r_dirty;
static u32 r_fs;                       // the sound SPU's sample count as the ARM7 SPU reads it at this point
// The harness's sound SPU answers a DSPRD with this "register value"
static u32 fake_dsp(u32 seq, u32 off) { return ((seq * 0x9E37u) ^ (off * 31u) ^ 0x5A5Au) & 0xFFFF; }

static void r_push_at(u32 sample, u8 op, u16 off, u32 val, u32 hash) { g_rlog.push_back({ sample, op, off, val, hash }); }
static void r_push(u8 op, u16 off, u32 val, u32 hash)
{
	if (r_phi >= r_plo)   // the waiting RING record first, at its first write's sample
	{
		const u32 v = r_plo | r_phi << 16;
		r_plo = 1;
		r_phi = 0;
		r_hz.ring_recs++;
		r_push_at(r_pt, AICA_OP_RING, 0, v, 0);
	}
	r_push_at(r_now, op, off, val, hash);
}

static void r_ring_wrote(u32 lo, u32 hi)
{
	if (r_phi < r_plo)
	{
		r_plo = lo;
		r_phi = hi;
		r_pt = r_now;
		return;
	}
	if (lo < r_plo) r_plo = lo;
	if (hi > r_phi) r_phi = hi;
}

static void r_hz_map(s32 lag)
{
	r_catch_up();
	memset(r_hz.map, 0, sizeof(r_hz.map));
	if (r_live)
		g_m->hz_mark(r_hz.map, (u32)(lag < 0 ? 0 : lag) + AICA_HZ_MARGIN, AICA_HZ_FWD);
	for (u32 p = r_ring_base >> 8, n = r_ring_len >> 8; n; n--, p++)
		r_hz.map[(p & 8191) >> 3] |= (u8)(0x80 >> (p & 7));
	r_hz.map_sample = r_now;
	r_hz_out = r_hz;
	r_dirty = false;
}
static void r_hz_remap() { r_hz_map((s32)(r_now - r_fs)); }

static void r_hz_tick()
{
	if (r_live || (g_flags & (ARM_SETUP_RING | ARM_SETUP_RING64)))
		r_hz_remap();
	else if (r_dirty)
	{
		memcpy(&r_hz_out.map_sample, &r_hz.map_sample, sizeof(r_hz) - AICA_HZ_MAP);
		r_dirty = false;
	}
}

static void r_ring_update()
{
	u32 base, bytes;
	const bool on = g_m->ring_range(base, bytes) && (g_flags & (bytes <= 65536 ? ARM_SETUP_RING : ARM_SETUP_RING64));
	const u32 len = on ? bytes : 0;
	if (len == r_ring_len && (!len || base == r_ring_base))
		return;
	r_ring_base = base;
	r_ring_len = len;
	r_hz_remap();
}

static void r_ring_sh4(u32 lo, u32 hi)
{
	const u32 rb = r_ring_base >> 7, re = rb + (r_ring_len >> 7) - 1;
	if (re < ARAM / 128)
	{
		if (lo < rb) lo = rb;
		if (hi > re) hi = re;
		if (lo > hi) return;
	}
	r_ring_wrote(lo, hi);
}

static u32 r_hz_test(u32 a, u32 src)
{
	r_hz.tests[src]++;
	r_dirty = true;
	if (((a - r_ring_base) & 0x1FFFFFu) < r_ring_len)
	{
		r_hz.ring_hits++;
		return 2;
	}
	if (!r_live)
		return 0;
	const s32 lag = (s32)(r_now - r_fs);
	if (lag <= 0)
		return 0;
	r_catch_up();
	const s32 d = g_m->hz_hit(a, (u32)lag + AICA_HZ_MARGIN);
	if (d < 0)
		return 0;
	r_hz.hazards[src]++;
	r_hz.near[d < 16 ? 0 : d < 64 ? 1 : d < 256 ? 2 : 3]++;
	return 1;
}

// The answer comes at once here: the harness's sound SPU sets its sample count to the SYNC's
static u32 r_hz_write(u32 a, u32 src)
{
	if (r_now == r_synced_at)
		return ARM_HZ_SYNCED;
	if ((r_now >> 6) != r_blk)
	{
		r_blk = r_now >> 6;
		r_tests = r_syncs = 0;
	}
	if (r_live & (ARM_CFG_BLOCK | ARM_CFG_ALL))   // no test: the hit syncs; BLOCK keeps one sync per block
	{
		r_dirty = true;
		if (r_syncs && !(r_live & ARM_CFG_ALL))
		{
			r_hz.miss++;
			return ARM_HZ_MISS;
		}
	}
	else
	{
		if (r_tests >= AICA_HZ_TESTS)
		{
			r_hz.miss++;
			r_dirty = true;
			return ARM_HZ_MISS;
		}
		r_tests++;
		if (!r_hz_test(a, src))
			return ARM_HZ_NONE;
		if (r_syncs)
		{
			r_hz.miss++;
			return ARM_HZ_MISS;
		}
	}
	r_syncs = 1;
	r_push(AICA_OP_SYNC, 0, ++r_seq, 0);
	r_fs = r_now;
	r_hz.syncs++;
	r_synced_at = r_now;
	r_hz_map(0);
	return ARM_HZ_SYNCED;
}

static void r_arm_store(u32 h, u32 n)
{
	if (!(r_hz.map[h >> 11] & (0x80 >> ((h >> 8) & 7))))
		return;
	r_hz.arm_hits++;
	r_dirty = true;
	r_hz_write(n == 1 ? h ^ 3 : h, 2);
}

static void r_arm_stored(u32 h)
{
	if (((h - r_ring_base) & 0x1FFFFFu) < r_ring_len)
		r_ring_wrote(h >> 7, h >> 7);
}

static void ac_dsp_read(uint32_t a)
{
	r_hz.dsp_rd[r_in_arm]++;
	r_dirty = true;
	if (!(g_flags & ARM_SETUP_DSPRD))
		return;
	const u32 off = a & 0x7FFC;
	r_push(AICA_OP_DSPRD, (u16)off, ++r_rd_seq, 0);
	r_fs = r_now;
	ac_wr16(off + 2, fake_dsp(r_rd_seq, off));
}

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
	if ((off == 0x2804 || (off >= 0x3400 && off < 0x3C00)) && (g_flags & (ARM_SETUP_RING | ARM_SETUP_RING64)))
		r_ring_update();
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
	r_hz_tick();
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
	r_in_arm = 1;
	a7_run(&g_cpu, ticks);
	r_in_arm = 0;
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
		else if (r < 63 && i + 2 < 16384)
		{
			w[i++] = 0xE3A00020;   // MOV r0, #0x20
			w[i++] = 0xE58800B8;   // STR r0, [r8, #0xB8]: MCIPD SCPU, the ARM7 interrupts the SH4 (AICA Race Fix)
		}
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
	w[f++] = 0xE3A01020;   // MOV r1, #0x20
	w[f++] = 0xE50A1448;   // STR r1, [r10, #-0x448]  MCIPD SCPU: the ARM7 interrupts the SH4 (AICA Race Fix)
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
	if ((w == 0x289C || w == 0x28B4) && pick(2)) return pick(0x800) | (w == 0x28B4 ? 0x20 : 0);   // MCIEB: SCPU often on
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
static bool g_hz_seed;   // seed 100 and up: bundle 5
static u32 p_synced_at = ~0u, p_blk = ~0u, p_reqs, n_sync_cmd, n_ppu_miss;   // the PPU's side (ps3_arm_spe.cpp)
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
	if (g_dhi >= g_dlo && r_ring_len)   // run_cmd: the SH4's lines inside the ring, before the command itself
		r_ring_sh4(g_dlo, g_dhi);
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
		// Bundle 5: the sound SPU plays up to a SYNC or a DSPRD and answers it, lagging or not
		if (x.op == AICA_OP_SYNC)
			put32(g_aecho + offsetof(AicaEcho, sync), x.val);
		else if (x.op == AICA_OP_DSPRD)
			put32(g_aecho + offsetof(AicaEcho, dsp_rd), x.val << 16 | fake_dsp(x.val, x.off));
		if (x.op == AICA_OP_SYNC || x.op == AICA_OP_DSPRD)
			put32(g_aecho + offsetof(AicaEcho, samples), x.sample);
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
		const int rc = spusim_run(g_sim, g_hz_seed ? 20000 : 4000000);   // short: the SPU may wait for an answer
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

static bool compare(bool rd, u32 rd_seq, u32 rd_val, int hz_res = -1)
{
	const u8* e = g_echo;
	if (hz_res >= 0 && be32(e + offsetof(ArmEcho, hz_res)) != (u32)hz_res)
		return fail("SYNC answer", be32(e + offsetof(ArmEcho, hz_res)), (u32)hz_res, 0);
	// The map and the counts as the SPU last put them (not its wait time)
	for (u32 i = 0; i < AICA_HZ_MAP; i++)
		if (g_hz[i] != r_hz_out.map[i])
			return fail("hz map byte", i, g_hz[i], r_hz_out.map[i]);
	for (u32 f = AICA_HZ_MAP; f < sizeof(AicaHz); f += 4)
		if (f != offsetof(AicaHz, wait_ticks) && be32(g_hz + f) != *(const u32*)((const u8*)&r_hz_out + f))
			return fail("hz count", f - AICA_HZ_MAP, be32(g_hz + f), *(const u32*)((const u8*)&r_hz_out + f));
	if (be32(e + offsetof(ArmEcho, tail)) != g_head)
		return fail("echo tail", be32(e + offsetof(ArmEcho, tail)), g_head, 0);
	if (be32(e + offsetof(ArmEcho, irq)) != ac.sh4_irq)
		return fail("echo irq", be32(e + offsetof(ArmEcho, irq)), ac.sh4_irq, 0);
	if (be32(e + offsetof(ArmEcho, samples)) != ac.smp)
		return fail("echo samples", be32(e + offsetof(ArmEcho, samples)), ac.smp, 0);
	if (rd && (be32(e + offsetof(ArmEcho, rd_seq)) != rd_seq || be32(e + offsetof(ArmEcho, rd_val)) != rd_val))
		return fail("register read", be32(e + offsetof(ArmEcho, rd_seq)), be32(e + offsetof(ArmEcho, rd_val)), rd_val);
	const u32 fx[6] = { g_cpu.races, ac.raises, r_resets, ac.stale, ac.ca_reads, ac.ca_clamped };
	for (u32 i = 0; i < 6; i++)
		if (be32(e + offsetof(ArmEcho, races) + 4 * i) != fx[i])
			return fail("echo fix count", i, be32(e + offsetof(ArmEcho, races) + 4 * i), fx[i]);
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
	g_flags = argc > 5 ? (u32)strtoul(argv[5], 0, 0) : ((seed >> 3) & 1 ? ARM_SETUP_RACE_FIX : 0) | ((seed >> 4) & 1 ? ARM_SETUP_RESET_FIX : 0) |
	          ((seed >> 5) & 1 ? ARM_SETUP_CA_CLAMP : 0);
	// Bundle 5 (plan section 7), seeds from 100: seed - 100 bit 0 LFORE Keep, 1 DSP Ring RAM, 2 DSP Ring 64K, 3 DSP Reg
	// Reads, 4 SPE AICA Sync, 5 its + BLOCK SYNC level, 6 + SAMPLE SYNC. The harness is then the PPU and the sound SPU.
	g_hz_seed = seed >= 100;
	const u32 hzk = g_hz_seed ? seed - 100 : 0;
	const u32 hz_on = ARM_CFG_SYNC | (hzk & 64 ? ARM_CFG_ALL : hzk & 32 ? ARM_CFG_BLOCK : 0);
	if (g_hz_seed && argc <= 5)
		g_flags = ARM_SETUP_RACE_FIX | ARM_SETUP_RESET_FIX | ARM_SETUP_CA_CLAMP | (hzk & 1 ? ARM_SETUP_LFORE : 0) |
		          (hzk & 2 ? ARM_SETUP_RING : 0) | (hzk & 4 ? ARM_SETUP_RING64 : 0) | (hzk & 8 ? ARM_SETUP_DSPRD : 0);
	const bool hz_sync = (hzk & 16) != 0;
	g_rng = 0x9E3779B97F4A7C15ull * (seed + 1);
	g_storm = (seed % 4) == 3;
	g_lag = g_storm && !g_hz_seed;   // a lagging sound SPU would not answer in time
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
	g_rg[8] = { EA_HZ, sizeof(g_hz), g_hz };
	put32(g_setup + offsetof(ArmSetup, ring_ea), (u32)EA_RING);
	put32(g_setup + offsetof(ArmSetup, aram_ea), (u32)EA_ARAM);
	put32(g_setup + offsetof(ArmSetup, aica_bell_ea), (u32)EA_ABELL);
	put32(g_setup + offsetof(ArmSetup, aica_echo_ea), (u32)EA_AECHO);
	put32(g_setup + offsetof(ArmSetup, aica_log_ea), (u32)EA_LOG);
	put32(g_setup + offsetof(ArmSetup, flags), g_flags);
	put32(g_setup + offsetof(ArmSetup, hz_ea), (u32)EA_HZ);
	gen_program(g_aram);
	memcpy(g_raram, g_aram, ARAM);

	// reference, as the PPU starts it: AICA_Init, arm_Init (core off), the twin's constructor and reset()
	ac_init();
	ac.race_fix = (g_flags & ARM_SETUP_RACE_FIX) != 0;
	ac.ca_clamp = (g_flags & ARM_SETUP_CA_CLAMP) != 0;
	ac.lfore_keep = (g_flags & ARM_SETUP_LFORE) != 0;
	a7_init(&g_cpu, (g_flags & ARM_SETUP_RESET_FIX) != 0);
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
		ref_set_enabled(1);
		ac_write(0x2C00, 0, 2);
		Cmd e = { ARM_CMD_WR, 0, 0x28B4, 0x20, 2 };   // MCIEB: the ARM7's SCPU interrupts reach the SH4
		push_cmd(e);
		ac_write(0x28B4, 0x20, 2);
	}
	auto sh4_wr = [&](u32 a, u32 d) {
		Cmd c = { ARM_CMD_WR, 0, (u16)a, d, 2 };
		push_cmd(c);
		ac_write(a, d, 2);
		n_wr++;
	};
	// Bundle 5: slots that play on the ARM7's stack and data, and a DSP program whose ring is the first pages
	auto hz_setup = [&]() {
		for (u32 i = 0; i < 8; i++)
		{
			const u32 b = pick(64) * 0x80, fmt = pick(3), sa = pick(2) ? 0xE000 + pick(0x4000) : pick(ARAM), lsa = pick(300);
			sh4_wr(b + 0x04, sa & 0xFFFF);
			sh4_wr(b + 0x08, lsa);
			sh4_wr(b + 0x0C, lsa + 200 + pick(3000));
			sh4_wr(b + 0x10, 31);            // fast attack, no decay
			sh4_wr(b + 0x14, 15 << 10 | 4);
			sh4_wr(b + 0x18, (pick(3) ? 0 : 0xF) << 11 | pick(0x400));
			sh4_wr(b + 0x1C, 0);
			sh4_wr(b + 0x00, 0x4000 | 1 << 9 | fmt << 7 | (sa >> 16));   // KYONB, LPCTL
		}
		sh4_wr(0x0000, (ac_rd16(0x0002) & 0x7FFF) | 0x8000);   // KYONEX
		if (g_flags & (ARM_SETUP_RING | ARM_SETUP_RING64))
		{
			sh4_wr(0x2804, ((g_flags & ARM_SETUP_RING64) && pick(2) ? 3 : pick(3)) << 13 | (pick(4) ? 0 : pick(8)));
			sh4_wr(0x3400 + 16 * pick(128), 1 + pick(0xFFFF));   // a program word: the ring is in use
		}
	};
	auto hz_cfg = [&](u32 live) {   // SPE AICA Sync from the menu
		Cmd c = { ARM_CMD_CFG, 0, 0, live, 0 };
		push_cmd(c);
		r_live = live;
		r_hz_remap();
	};
	// The PPU's part (aram_hz_write, arm_spe_hz_hit): a store to a marked page asks the SPU first and waits
	auto ppu_store = [&](u32 h, u32 v, u32 n) -> bool {
		const u32 a = n == 4 ? h : h ^ 3;
		if (g_hz_seed && (g_hz[a >> 11] & (0x80 >> ((a >> 8) & 7))) && ac.smp != p_synced_at)
		{
			if ((ac.smp >> 6) != p_blk)
			{
				p_blk = ac.smp >> 6;
				p_reqs = 0;
			}
			const u32 lvl = r_live & ARM_CFG_ALL ? 3 : r_live & ARM_CFG_BLOCK ? 2 : 1;   // the row as the PPU reads it
			if (p_reqs >= AICA_HZ_TESTS && lvl < 3)
				n_ppu_miss++;
			else
			{
				p_reqs = lvl == 2 ? AICA_HZ_TESTS : p_reqs + 1;
				n_sync_cmd++;
				Cmd c = { ARM_CMD_SYNC, 0, 0, a, pick(2) };
				push_cmd(c);
				const u32 res = r_hz_write(a, c.c);
				if (!run_spu() || !compare(false, 0, 0, (int)res))
					return false;
				if (res == ARM_HZ_SYNCED)
					p_synced_at = ac.smp;
			}
		}
		aram_write(h, v, n);
		return true;
	};
	if (g_hz_seed)
	{
		if (hz_sync)
			hz_cfg(hz_on);
		hz_setup();
	}
	for (u32 b = 0; b < batches && ok; b++)
	{
		if (g_hz_seed && b && b % 400 == 0)
			hz_setup();
		if (hz_sync && b && b % 700 == 0)
			hz_cfg(r_live ? 0 : hz_on);
		// SH4 ARAM stores before the batch (its first command carries the lines)
		for (u32 n = (g_hz_seed ? pick(2) : pick(4)) == 0 ? 1 + pick(6) : 0; n && ok; n--)
		{
			// Often into the code the ARM7 runs now: a stale cached line would show at once.
			const u32 r = pick(3);
			u32 h = r == 0 ? (g_cpu.next_pc + pick(64) * 4 - 64) & 0x1FFFFC : r == 1 ? pick(0x10000) & ~3u : pick(ARAM) & ~3u;
			if (g_hz_seed && pick(2))   // bundle 5: near where a slot plays
			{
				r_catch_up();
				const int ch = (int)pick(64);
				const caique::Slot& sl = g_m->slot[ch];
				const u32 r0 = g_m->chr(ch, 0), fmt = (r0 >> 7) & 3, sa = (r0 & 0x7F) << 16 | g_m->chr(ch, 4);
				if (sl.enabled)
					h = (sa + (fmt == 0 ? 2 * sl.CA : fmt == 1 ? sl.CA : sl.CA / 2) + pick(700) - 500) & 0x1FFFFC;
			}
			if (pick(4))
				ok = ppu_store(h, pick(3) ? gen_insn(false) : rnd(), 4);
			else
				ok = ppu_store(h | pick(4), rnd(), 1);
			n_aram++;
		}
		if (!ok)
			break;
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
				if (a == 0x2C00)   // a random write that lands on ARMRST: the SPU applies it as such
					ref_set_enabled(!(d & 1));
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
				ref_set_enabled(!(d & 1));
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
				ref_reset_edge();
				Cmd r = { ARM_CMD_WR, 0, 0x2C00, 0, 2 };   // and the driver restarts it
				push_cmd(r);
				ref_set_enabled(1);
				ac_write(0x2C00, 0, 2);
			}
		}
		if (g_lag && b == 999)
			g_lag = false;   // the lagging sound SPU wakes up; the storm goes on, so the log wraps off a flush edge
		ok = run_spu() && compare(rd, rd_seq, rd_val);
		if (g_hz_seed)   // the sound SPU plays on between batches, some samples behind
		{
			const u32 lagk = pick(4) ? pick(160) : 0, fs = ac.smp > lagk ? ac.smp - lagk : 0;
			if (fs > r_fs)
				r_fs = fs;
			put32(g_aecho + offsetof(AicaEcho, samples), r_fs);
		}
	}
	if (g_hz_seed)
		printf("seed %u bundle 5 (flags %u, sync row %d): map hits sh4 %u arm %u | tests %u/%u/%u hazards %u/%u/%u | ring hits %u"
		       " recs %u | syncs %u miss %u (ppu %u) | dsp reads sh4 %u arm %u\n", seed, g_flags, hz_sync ? (int)hz_on : 0, n_sync_cmd,
		       r_hz.arm_hits, r_hz.tests[0], r_hz.tests[1], r_hz.tests[2], r_hz.hazards[0], r_hz.hazards[1], r_hz.hazards[2],
		       r_hz.ring_hits, r_hz.ring_recs, r_hz.syncs, r_hz.miss, n_ppu_miss, r_hz.dsp_rd[0], r_hz.dsp_rd[1]);
	printf("seed %u: %s | fixes %u: races %u raises %u resets %u stale %u ca %u/%u | batches %u cmds %llu | samples %u"
	       " recs %zu (checks %u) | arm runs %u fiqs %u decodes %u evicts %u"
	       " | aram misses %u puts %u | sh4 wr %u rd %u aram %u armrst %u | log waits %u limit runs %llu | %llu insns\n",
	       seed, ok ? "PASS" : "FAIL", g_flags, g_cpu.races, ac.raises, r_resets, ac.stale, ac.ca_clamped, ac.ca_reads,
	       batches, (unsigned long long)g_cmds, ac.smp, g_rlog.size(), r_checks,
	       be32(g_echo + offsetof(ArmEcho, runs)), g_cpu.fiqs, g_cpu.decodes, g_cpu.evicts,
	       be32(g_echo + offsetof(ArmEcho, misses)), be32(g_echo + offsetof(ArmEcho, puts)), n_wr, n_rd, n_aram, n_rst,
	       be32(g_echo + offsetof(ArmEcho, log_waits)), (unsigned long long)g_limit_runs, (unsigned long long)g_sim->insns);
	return ok ? 0 : 1;
}
