// OPTIMIZATION_PLAN 5.12 S0: one SPU thread sleeps on the doorbell line; the PPU bumps it.
// Measures wake latency and ring cost at boot, then rings at the TA batch points in play.
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <sys/spu.h>
#include <ogc/lwp_watchdog.h>

extern "C" const u8 spe_bell_elf[];
extern "C" int get_spe_bell_preset();

SpeBell g_spe_bell;
u32 g_spe_seq;
u32 g_spe_pending;
bool g_spe_on;

static SpeEcho s_echo;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;
static bool s_up;
static bool s_spu_init_done;

static inline const volatile SpeEcho* echo() { return &s_echo; }

static double tb_us(u64 ticks) { return (double)ticks * 1e6 / (double)ps3_timebase_hz; }

// Spins until the echo shows want, or the budget runs out; false = timed out.
static bool wait_echo_seq(u32 want, u64 budget_ticks)
{
	const u64 t0 = gettime();
	while (echo()->seq != want)
		if (gettime() - t0 > budget_ticks)
			return false;
	return true;
}

static bool wait_echo_state(u32 want, u64 budget_ticks)
{
	const u64 t0 = gettime();
	while (echo()->state != want)
		if (gettime() - t0 > budget_ticks)
			return false;
	return true;
}

static void bell_selftest()
{
	const u64 ms10 = ps3_timebase_hz / 100;

	// Round trip: PPU store -> SPU wake -> getllar -> mfc_put echo -> PPU sees it.
	const u32 N = 1000;
	u64 tmin = ~0ull, tmax = 0, tsum = 0;
	u32 lost = 0;
	for (u32 i = 0; i < N; i++)
	{
		const u64 t0 = gettime();
		ps3spe_ring();
		if (!wait_echo_seq(g_spe_seq, ms10))
		{
			lost++;
			continue;
		}
		const u64 dt = gettime() - t0;
		tsum += dt;
		if (dt < tmin) tmin = dt;
		if (dt > tmax) tmax = dt;
	}
	const u32 ok = N - lost;
	printf("[spe] ping x%u: rtt min %.2f avg %.2f max %.2f us, lost %u\n",
	       N, ok ? tb_us(tmin) : 0.0, ok ? tb_us(tsum) / ok : 0.0, ok ? tb_us(tmax) : 0.0, lost);

	// Ring cost back to back (worst case: the line moves to the SPU between rings),
	// against the same store + lwsync to a line no SPU holds.
	static SpeBell s_plain __attribute__((unused));
	const u32 M = 100000;
	const u32 h0 = echo()->handled;
	u64 t0 = gettime();
	for (u32 i = 0; i < M; i++)
		ps3spe_ring();
	const u64 t_ring = gettime() - t0;
	t0 = gettime();
	for (u32 i = 0; i < M; i++)
	{
		__asm__ volatile("lwsync" ::: "memory");
		s_plain.seq = i;
	}
	const u64 t_plain = gettime() - t0;
	const bool caught = wait_echo_seq(g_spe_seq, ms10);
	printf("[spe] ring x%u: %.1f ns each (plain store+lwsync %.1f ns); SPU handled %u, %s\n",
	       M, tb_us(t_ring) * 1000.0 / M, tb_us(t_plain) * 1000.0 / M,
	       echo()->handled - h0, caught ? "caught up" : "NOT caught up in 10 ms");
}

bool ps3spe_init()
{
	if (!get_spe_bell_preset() || s_up)
		return s_up;

	// lv2 accepts this once per process; a second boot of the renderer skips it.
	if (!s_spu_init_done)
	{
		s32 r = sysSpuInitialize(6, 0);
		printf("[spe] sysSpuInitialize(6,0) -> 0x%08X\n", (u32)r);
		s_spu_init_done = true;
	}
	s32 r = sysSpuImageImport(&s_img, spe_bell_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[spe] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}

	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_spe");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[spe] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}

	g_spe_bell.seq = 0;
	g_spe_bell.quit = 0;
	g_spe_seq = 0;
	g_spe_pending = 0;
	s_echo.state = 0;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_bell");
	sysSpuThreadArgument arg;
	sysSpuThreadArgumentInitialize(arg);
	arg.arg0 = (u64)(uintptr_t)&g_spe_bell;
	arg.arg1 = (u64)(uintptr_t)&s_echo;
	r = sysSpuThreadInitialize(&s_thread, s_group, 0, &s_img, &tattr, &arg);
	if (r == 0)
		r = sysSpuThreadGroupStart(s_group);
	if (r != 0)
	{
		printf("[spe] SPU thread init/start failed: 0x%08X\n", (u32)r);
		sysSpuThreadGroupDestroy(s_group);
		sysSpuImageClose(&s_img);
		return false;
	}
	s_up = true;

	if (!wait_echo_state(SPE_STATE_RUNNING, ps3_timebase_hz / 10))
	{
		printf("[spe] SPU thread did not report in 100 ms; doorbell off\n");
		ps3spe_term();
		return false;
	}
	printf("[spe] SPU thread up\n");
	bell_selftest();
	g_spe_on = true;
	return true;
}

void ps3spe_term()
{
	if (!s_up)
		return;
	g_spe_on = false;
	g_spe_bell.quit = 1;
	ps3spe_ring();
	if (!wait_echo_state(SPE_STATE_EXITED, ps3_timebase_hz / 10))
	{
		printf("[spe] SPU thread did not exit in 100 ms; terminating the group\n");
		sysSpuThreadGroupTerminate(s_group, 0);
	}
	u32 cause, status;
	sysSpuThreadGroupJoin(s_group, &cause, &status);
	sysSpuThreadGroupDestroy(s_group);
	sysSpuImageClose(&s_img);
	s_up = false;
	printf("[spe] SPU thread down\n");
}

// Called from the render stats line, every ~5 s.
void ps3spe_stats(double secs)
{
	if (!g_spe_on || secs <= 0.0)
		return;
	static u32 l_seq, l_handled, l_wakes, l_timeouts;
	const u32 seq = g_spe_seq;
	const volatile SpeEcho* e = echo();
	const u32 handled = e->handled, wakes = e->wakes, timeouts = e->timeouts;
	printf("[spe] rings/s=%.0f handled/s=%.0f wakes/s=%.0f timeouts/s=%.0f rescued=%u lag=%u\n",
	       (seq - l_seq) / secs, (handled - l_handled) / secs, (wakes - l_wakes) / secs,
	       (timeouts - l_timeouts) / secs, e->rescued, seq - e->seq);
	l_seq = seq;
	l_handled = handled;
	l_wakes = wakes;
	l_timeouts = timeouts;
}
