// OPTIMIZATION_PLAN 5.15: PPU side of the ARM7 SPU (spu/spe_arm7.cpp). The SH4 thread is the only producer of the
// command ring; the SPU answers in its echo line. CD-DA sectors and the audio pacing stay here, at nullAICA's samples.
#include "ps3_arm_spe.h"
#include "ps3_aica_spe.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <string.h>
#include <sys/spu.h>
#include "ps3_timebase.h"
#include "spu/spe_aica.h"
#include "spu/spe_arm7.h"

extern "C" const u8 spe_arm7_elf[];
extern "C" int get_spe_arm7_preset();
extern "C" int get_arm7_speed_preset();
extern "C" int get_arm7_batch_preset();
extern "C" int get_arm7_rate_preset();
extern "C" int get_arm7_interleave_preset();
extern "C" int get_arm7_idle_skip_preset();
extern "C" int get_cdda_preset();
extern "C" int get_aica_race_fix_preset();
extern "C" int get_arm7_reset_fix_preset();
extern "C" int get_aica_ca_clamp_preset();
extern "C" int get_spe_aica_dsp_preset();
extern "C" int get_spe_aica_sync_preset();
extern "C" int get_dsp_reg_read_preset();
extern "C" int get_aica_lfore_preset();
extern "C" int get_arm7_str_align_preset();
void arm_p49_print(int spu, const A7P49* c, u32 unal, const u32* unal_addr);   // plugs/vbaARM/arm7.cpp
void FASTCALL gdrom_get_cdda(s16* sector);   // dc/gdrom/gdromv3.cpp
bool aica_sh4_irq_apply(bool on);            // plugs/nullAICA/aica.cpp
extern u64 AicaTicks;

// Commands in flight before the SH4 waits (~12 ms of guest time at 150 MHz). 32 (5.14's limit) made the SH4 wait up
// to 8% of wall in light scenes; the Crazy Taxi popping is SPE AICA Sound's own (plan 6.6), not this lag.
#define LAG_MAX 256

bool g_arm_spe;
u32 g_arm_spe_dlo = 0xFFFF, g_arm_spe_dhi;

static ArmBell s_bell;
static ArmEcho s_echo;
static ArmSetup s_setup;
static ArmCmd s_ring[ARM_CMDS] __attribute__((aligned(128)));
static ArmCmd s_void;    // where commands go once the SPU is gone
static u32 s_head, s_tail_seen, s_rd_seq;
static bool s_dead;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;
static bool s_up;

// The SH4 thread's sample clock: CD-DA refill points and pacing, as libAICA_TimeStep had them.
static u32 s_now, s_cdda_index = AICA_CDDA_WORDS << 1, s_cdda_ord;
static s16 s_sector[AICA_CDDA_WORDS];

static u64 s_win_t0, s_wait_ticks;
static u32 s_reads, s_writes, s_lag_waits, s_lag_max;

// Section 7, bundle 5 (6.6, P20 G2): the SPU twin's hazard map and counts; the SH4 thread tests its writes on it.
static AicaHz s_hz;
static u32 s_hz_ppu[6];   // this window: map hits by the SH4, by a G2 DMA, with a stale map; requests, misses, wait ticks
static u32 s_hz_synced_at, s_hz_blk, s_hz_reqs, s_cfg_sent;
static bool s_hz_ring;    // a DSP ring row is on: the SPU keeps the map even with SPE AICA Sync off

// Section 7, bundle 9 (log only): the SPU's P49 census and unaligned-store counts
static ArmCensus s_census;

static inline const volatile ArmEcho* echo() { return &s_echo; }

static void spe_dead(const char* why)
{
	if (s_dead)
		return;
	s_dead = true;
	s32 exit_st = 0;
	const s32 rx = sysSpuThreadGetExitStatus(s_thread, &exit_st);
	printf("[arm7spe] %s: SPU tail %u of %u, exit 0x%08X/%d; ARM7 and AICA stop (no sound)\n", why, echo()->tail, s_head,
	       (u32)rx, exit_st);
}

// Until the SPU has done command n; false after 100 ms without progress.
static bool wait_tail(u32 n)
{
	u32 t = echo()->tail;
	if ((s32)(t - n) >= 0)
		return true;
	const u64 t0 = gettime();
	u64 moved = t0;
	for (;;)
	{
		const u32 x = echo()->tail;
		if ((s32)(x - n) >= 0)
			break;
		const u64 now = gettime();
		if (x != t)
		{
			t = x;
			moved = now;
		}
		else if (now - moved > ps3_timebase_hz / 10)
		{
			spe_dead("no progress for 100 ms");
			return false;
		}
	}
	s_wait_ticks += gettime() - t0;
	return true;
}

// The SPU's tail is read once LAG_MAX commands went out since the last read; max is the largest lag found then.
static ArmCmd* cmd_slot()
{
	if (s_dead)
		return &s_void;
	if (s_head - s_tail_seen >= LAG_MAX)
	{
		s_tail_seen = echo()->tail;
		const u32 lag = s_head - s_tail_seen;
		if (lag > s_lag_max)
			s_lag_max = lag;
		if (lag >= LAG_MAX)
		{
			s_lag_waits++;
			if (!wait_tail(s_head - LAG_MAX / 2))
				return &s_void;
			s_tail_seen = echo()->tail;
		}
	}
	ArmCmd* c = &s_ring[s_head & (ARM_CMDS - 1)];
	c->dlo = (u16)g_arm_spe_dlo;
	c->dhi = (u16)g_arm_spe_dhi;
	g_arm_spe_dlo = 0xFFFF;
	g_arm_spe_dhi = 0;
	return c;
}

static void cmd_push()
{
	if (s_dead)
		return;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.head = ++s_head;
}

// cdda_refill: the sector nullAICA would read at this sample (zeros with CD Audio off), host order
static void cdda_sector()
{
	if (get_cdda_preset())
	{
		gdrom_get_cdda(s_sector);
		u8* b = (u8*)s_sector;
		for (u32 i = 0; i < sizeof(s_sector); i += 2)
		{
			const u8 t = b[i];
			b[i] = b[i + 1];
			b[i + 1] = t;
		}
	}
	else
		memset(s_sector, 0, sizeof(s_sector));
	bool silent = true;
	for (u32 i = 0; i < AICA_CDDA_WORDS && silent; i++)
		silent = s_sector[i] == 0;
	s_cdda_ord++;
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_CDDA;
	c->b = silent ? AICA_CDDA_SILENT : ps3aica_cdda_slot(s_sector, s_cdda_ord);
	cmd_push();
}

void arm_spe_update(u32 cycles, u32 samples)
{
	const u64 t0 = gettime();
	for (u32 i = 0; i < samples; i++)
	{
		if (s_cdda_index >= AICA_CDDA_WORDS)
		{
			s_cdda_index = 0;
			cdda_sector();
		}
		s_cdda_index += 2;
	}
	const int lvl = get_spe_aica_sync_preset();
	const u32 cfg = lvl <= 0 ? 0 : ARM_CFG_SYNC | (lvl == 2 ? ARM_CFG_BLOCK : lvl >= 3 ? ARM_CFG_ALL : 0);
	if (cfg != s_cfg_sent)   // the row is live
	{
		s_cfg_sent = cfg;
		ArmCmd* f = cmd_slot();
		f->op = ARM_CMD_CFG;
		f->b = cfg;
		cmd_push();
	}
	// "ARM7 Rate" (plan 6.3): ticks per AICA sample; 0 = SH4 cycles / 20, the old rate
	static const u16 k_arm7_rate[4] = { 0, 256, 384, 512 };
	int stage = get_arm7_speed_preset(), batch = get_arm7_batch_preset();
	const int rate = get_arm7_rate_preset();
	stage = stage < 0 ? 0 : stage > 3 ? 3 : stage;
	batch = batch < 0 ? 0 : batch > 16 ? 16 : batch;
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_MU;
	c->flags = (u8)(batch | (stage << 5) | (get_arm7_idle_skip_preset() ? 0x80 : 0));
	c->a = (u16)samples;
	c->b = cycles;
	c->c = k_arm7_rate[rate < 0 || rate > 3 ? 0 : rate] | (get_arm7_interleave_preset() ? 1u << 16 : 0);
	cmd_push();
	const u32 before = s_now;
	s_now += samples;
	// The map covers AICA_HZ_FWD samples after the twin's last one; past that the SH4 cannot rely on it.
	g_aram_hz_stale = g_aram_hz && (cfg || s_hz_ring) && s_now - *(volatile u32*)&s_hz.map_sample > AICA_HZ_STALE;
	if ((before ^ s_now) & ~(AICA_BLOCK - 1u))   // a block of 64 samples ended: ps3aica_step's pacing point
		ps3aica_pace(s_now & ~(AICA_BLOCK - 1u));
	aica_sh4_irq_apply(echo()->irq != 0);
	AicaTicks += gettime() - t0;
}

u32 arm_spe_read(u32 addr, u32 size)
{
	if (s_dead)
		return 0;
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_RD;
	c->a = (u16)(addr & 0x7FFF);
	c->b = ++s_rd_seq;
	c->c = size;
	cmd_push();
	s_reads++;
	if (!wait_tail(s_head))
		return 0;
	__asm__ volatile("lwsync" ::: "memory");
	const u32 v = echo()->rd_val;
	aica_sh4_irq_apply(echo()->irq != 0);
	return v;
}

// MCIEB / MCIPD / MCIRE wait for the SPU, so the Holly line is right before the SH4 goes on (an interrupt ack).
void arm_spe_write(u32 addr, u32 data, u32 size)
{
	const u32 a = addr & 0x7FFF;
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_WR;
	c->a = (u16)a;
	c->b = data;
	c->c = size;
	cmd_push();
	s_writes++;
	if (a >= 0x28B4 && a < 0x28C0 && !s_dead && wait_tail(s_head))
		aica_sh4_irq_apply(echo()->irq != 0);
}

void arm_spe_hz_hit(u32 addr, u32 src)
{
	s_hz_ppu[src]++;
	if (g_aram_hz_stale)
		s_hz_ppu[2]++;
	if (s_dead || s_now == s_hz_synced_at)   // synced at this sample: the sound SPU has no sample left to play
		return;
	if ((s_now >> 6) != s_hz_blk)
	{
		s_hz_blk = s_now >> 6;
		s_hz_reqs = 0;
	}
	// Level 2 asks once per block (that hit syncs with no test), level 3 at each sample with a hit
	const int lvl = get_spe_aica_sync_preset();
	if (s_hz_reqs >= AICA_HZ_TESTS && lvl < 3)
	{
		s_hz_ppu[4]++;
		return;
	}
	s_hz_reqs = lvl == 2 ? AICA_HZ_TESTS : s_hz_reqs + 1;
	s_hz_ppu[3]++;
	const u64 t0 = gettime();
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_SYNC;
	c->b = addr;
	c->c = src;
	cmd_push();
	if (!wait_tail(s_head))
		return;
	s_hz_ppu[5] += (u32)(gettime() - t0);
	__asm__ volatile("lwsync" ::: "memory");
	const u32 res = echo()->hz_res;
	if (res == ARM_HZ_SYNCED)
		s_hz_synced_at = s_now;
	else if (res == ARM_HZ_DEAD)   // the sound SPU did not answer the twin: no more requests
		g_aram_hz = 0;
	g_aram_hz_stale = g_aram_hz && s_now - *(volatile u32*)&s_hz.map_sample > AICA_HZ_STALE;
}

void arm_spe_aram_all()
{
	g_arm_spe_dlo = 0;
	g_arm_spe_dhi = (ARAM_SIZE >> 7) - 1;
}

void arm_spe_reset()
{
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_RESET;
	cmd_push();
}

void arm_spe_flush()
{
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_FLUSH;
	cmd_push();
}

static bool wait_state(u32 want, u64 budget)
{
	const u64 t0 = gettime();
	while (echo()->state != want)
		if (gettime() - t0 > budget)
			return false;
	return true;
}

bool arm_spe_init(u8* aica_ram)
{
	if (!get_spe_arm7_preset() || s_up)
		return s_up;
	if (!g_aica_spe_snd)
	{
		printf("[arm7spe] SPE ARM7 needs SPE AICA Sound; the ARM7 stays on the PPU\n");
		return false;
	}
	ps3spe_sys_init();
	memset(&s_bell, 0, sizeof(s_bell));
	memset(&s_echo, 0, sizeof(s_echo));
	memset(&s_setup, 0, sizeof(s_setup));
	s_setup.ring_ea = (u32)(uintptr_t)s_ring;
	s_setup.aram_ea = (u32)(uintptr_t)aica_ram;
	ps3aica_spe_eas(&s_setup.aica_bell_ea, &s_setup.aica_echo_ea, &s_setup.aica_log_ea);
	const u32 af = ps3aica_setup_flags();
	s_hz_ring = (af & (AICA_SETUP_RING | AICA_SETUP_RING64)) != 0;
	s_setup.flags = (get_aica_race_fix_preset() ? ARM_SETUP_RACE_FIX : 0) |
	                (get_arm7_reset_fix_preset() ? ARM_SETUP_RESET_FIX : 0) |
	                (get_aica_ca_clamp_preset() ? ARM_SETUP_CA_CLAMP : 0) |
	                (get_aica_lfore_preset() ? ARM_SETUP_LFORE : 0) | ((af & AICA_SETUP_RING) ? ARM_SETUP_RING : 0) |
	                ((af & AICA_SETUP_RING64) ? ARM_SETUP_RING64 : 0) |
	                (get_dsp_reg_read_preset() && get_spe_aica_dsp_preset() ? ARM_SETUP_DSPRD : 0) |
	                (get_arm7_str_align_preset() ? ARM_SETUP_STR_ALIGN : 0);
	memset(&s_census, 0, sizeof(s_census));
	s_setup.census_ea = (u32)(uintptr_t)&s_census;
	memset(&s_hz, 0, sizeof(s_hz));
	memset(s_hz_ppu, 0, sizeof(s_hz_ppu));
	s_setup.hz_ea = (u32)(uintptr_t)&s_hz;
	s_hz_synced_at = s_hz_blk = s_cfg_sent = ~0u;
	s_hz_reqs = 0;
	s_head = s_tail_seen = s_rd_seq = 0;
	s_dead = false;
	g_arm_spe_dlo = 0xFFFF;
	g_arm_spe_dhi = 0;

	s32 r = sysSpuImageImport(&s_img, spe_arm7_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[arm7spe] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}
	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_arm7");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[arm7spe] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}
	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_arm7");
	sysSpuThreadArgument arg;
	sysSpuThreadArgumentInitialize(arg);
	arg.arg0 = (u64)(uintptr_t)&s_bell;
	arg.arg1 = (u64)(uintptr_t)&s_echo;
	arg.arg2 = (u64)(uintptr_t)&s_setup;
	r = sysSpuThreadInitialize(&s_thread, s_group, 0, &s_img, &tattr, &arg);
	if (r == 0)
		r = sysSpuThreadGroupStart(s_group);
	if (r != 0)
	{
		printf("[arm7spe] SPU thread init/start failed: 0x%08X\n", (u32)r);
		sysSpuThreadGroupDestroy(s_group);
		sysSpuImageClose(&s_img);
		return false;
	}
	s_up = true;
	if (!wait_state(ARM_SPE_STATE_RUNNING, ps3_timebase_hz / 10))
	{
		printf("[arm7spe] SPU thread did not report in 100 ms; the ARM7 stays on the PPU\n");
		arm_spe_term();
		return false;
	}
	g_arm_spe = true;
	g_aram_hz = s_hz.map;
	s_win_t0 = gettime();
	printf("[arm7spe] SPU thread up: ARM7, nullAICA registers and the caique control twin on the SPU;"
	       " SPU LS used %u of 256 KB\n", echo()->ls_end >> 10);
	return true;
}

void arm_spe_term()
{
	if (!s_up)
		return;
	g_arm_spe = false;
	g_aram_hz = 0;
	g_aram_hz_stale = false;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.quit = 1;
	if (!wait_state(ARM_SPE_STATE_EXITED, ps3_timebase_hz / 10))
	{
		printf("[arm7spe] SPU thread did not exit in 100 ms; terminating the group\n");
		sysSpuThreadGroupTerminate(s_group, 0);
	}
	u32 cause, status;
	sysSpuThreadGroupJoin(s_group, &cause, &status);
	sysSpuThreadGroupDestroy(s_group);
	sysSpuImageClose(&s_img);
	s_up = false;
	printf("[arm7spe] SPU thread down\n");
}

// [aicafix]: the SPU's counts since the last call (races, raises, resets, stale, CA reads, CA clamped).
void arm_spe_fix_take(u32 out[6])
{
	static u32 last[6];
	const volatile ArmEcho* e = echo();
	const u32 now[6] = { e->races, e->raises, e->resets, e->stale, e->ca_reads, e->ca_clamped };
	for (int i = 0; i < 6; i++)
	{
		out[i] = now[i] - last[i];
		last[i] = now[i];
	}
}

// Every ~5 s: rates per wall second, and what the SH4 thread waited for.
void arm_spe_stats(double)
{
	if (!g_arm_spe)
		return;
	const u64 now = gettime();
	if (now - s_win_t0 < 5 * ps3_timebase_hz)
		return;
	const double w = (double)(now - s_win_t0) / (double)ps3_timebase_hz;
	s_win_t0 = now;
	static ArmEcho l;
	const volatile ArmEcho* e = echo();
	ArmEcho c;
	memcpy(&c, (const void*)e, sizeof(c));
	const double tw = (double)ps3_timebase_hz * w / 100.0;
	printf("[arm7spe] per s: cmds %.0f runs %.0f ticks %.0f fiq %.0f | samples %.0f lag %d cmds (max %u) | recs %.0f"
	       " checks %.0f mon %.0f | ARAM misses %.0f puts %.0f | map decodes %u evicts %u | spu busy %.1f%% wakes %.0f"
	       " | sh4 reads %.0f writes %.0f wait %.2f%% lag-waits %u | log waits %u drops %u cdda-under %u\n",
	       (c.cmds - l.cmds) / w, (c.runs - l.runs) / w, (double)(c.ticks - l.ticks) / w, (c.fiqs - l.fiqs) / w,
	       (c.samples - l.samples) / w, (s32)(s_head - c.tail), s_lag_max, (c.recs - l.recs) / w,
	       (c.checks - l.checks) / w, (c.mon_reads - l.mon_reads) / w, (c.misses - l.misses) / w, (c.puts - l.puts) / w,
	       c.decodes - l.decodes, c.evicts - l.evicts, (c.busy - l.busy) * 100.0 / (79800000.0 * w),
	       (c.wakes - l.wakes) / w, s_reads / w, s_writes / w, s_wait_ticks / tw, s_lag_waits, c.log_waits, c.log_drops,
	       c.cdda_under);
	l = c;
	s_reads = s_writes = s_lag_waits = s_lag_max = 0;
	s_wait_ticks = 0;
	ps3aica_hz_print(&s_hz, w, s_hz_ppu);
	memset(s_hz_ppu, 0, sizeof(s_hz_ppu));
	// The SPU's put is not atomic over the two lines: take a copy that reads the same twice.
	static ArmCensus ca, cb;
	for (int tries = 0; tries < 4; tries++)
	{
		memcpy(&ca, (const void*)&s_census, sizeof(ca));
		__asm__ volatile("lwsync" ::: "memory");
		memcpy(&cb, (const void*)&s_census, sizeof(cb));
		if (!memcmp(&ca, &cb, sizeof(ca)))
			break;
	}
	arm_p49_print(1, &ca.p49, ca.unal, ca.unal_addr);
}
