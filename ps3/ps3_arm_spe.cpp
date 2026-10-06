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
extern "C" int get_arm7_idle_skip_preset();
extern "C" int get_cdda_preset();
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
	int stage = get_arm7_speed_preset(), batch = get_arm7_batch_preset();
	stage = stage < 0 ? 0 : stage > 3 ? 3 : stage;
	batch = batch < 0 ? 0 : batch > 16 ? 16 : batch;
	ArmCmd* c = cmd_slot();
	c->op = ARM_CMD_MU;
	c->flags = (u8)(batch | (stage << 5) | (get_arm7_idle_skip_preset() ? 0x80 : 0));
	c->a = (u16)samples;
	c->b = cycles;
	c->c = 0;
	cmd_push();
	const u32 before = s_now;
	s_now += samples;
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
}
