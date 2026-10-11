// OPTIMIZATION_PLAN 5.12: one SPU thread runs a command ring the SH4 thread fills at the TA batch points.
// It decodes each raw range into shadow lists and answers every FENCE; the render thread compares (S2) or draws (S3) them.
#include "ps3_spe.h"
#include "ps3_stall.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <sys/spu.h>
#include "ps3_timebase.h"

extern "C" const u8 spe_bell_elf[];
extern "C" int get_spe_bell_preset();
extern "C" int get_spe_draw_preset();
extern "C" int get_spe_wait_yield_preset();

bool g_spe_on;
bool g_spe_draw;
static bool s_wait_yield;  // plan 5.16: the fence wait hands its dispatch cycles to the SH4 thread

static SpeBell s_bell;
static SpeEcho s_echo;
static SpeShared s_shared;
static SpeCmd* const s_ring = s_shared.ring;
static SpeFence* const s_results = s_shared.results;
static SpeOut* s_out;    // SPE_CTX shadow list sets, written by the SPU
static u32 s_head;       // PPU copy of the bell head, so a push never loads the reserved line
static u32 s_tail_seen;  // last SPU tail read; refreshed only when the ring looks full
static u32 s_fence_id;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;
static bool s_up;
static bool s_spu_init_done;

// SH4 thread counters.
static u32 s_full_waits;
static u64 s_cmd_wait;  // ticks spent in ps3spe_wait_cmd
// Render thread counters (or the SH4 thread with the render thread off).
static u32 s_fences, s_ok, s_bad, s_late;
static u64 s_wait_sum, s_wait_max, s_check_sum, s_check_max;

static inline const volatile SpeEcho* echo() { return &s_echo; }

// Reads the SPU's own echo from LS: on a stall the copy in main memory is stale.
static void dump_ls_echo(const char* why)
{
	static u32 s_dumps;
	if (!s_up || s_dumps++ >= 3)
		return;
	SpeEcho e;
	u64* w = (u64*)&e;
	const u32 ls = echo()->ls_addr;
	s32 r = 0;
	for (u32 i = 0; i < sizeof(e) / 8 && r == 0; i++)
		r = sysSpuThreadReadLocalStorage(s_thread, ls + i * 8, &w[i], 8);
	s32 exit_st = 0;
	s32 rx = sysSpuThreadGetExitStatus(s_thread, &exit_st);
	printf("[spe] STALL (%s): ls_read=0x%08X ls=0x%05X exit=0x%08X/%d | ppu_head=%u ring=%p results=%p echo=%p\n",
	       why, (u32)r, ls, (u32)rx, exit_st, s_head, (void*)s_ring, (void*)s_results, (void*)&s_echo);
	if (r != 0)
		return;
	printf("[spe] STALL ls echo: stage=%u tail=%u head=%u fence=%u handled=%u wakes=%u timeouts=%u data=%u"
	       " | cmd op=%u ctx=%u ea=%08X a=%u b=%u | dma ea=%08X size=%u\n",
	       e.stage, e.tail, e.head, e.fence, e.handled, e.wakes, e.timeouts, e.data_bytes,
	       e.cmd_op, e.cmd_ctx, e.cmd_ea, e.cmd_a, e.cmd_b, e.dma_ea, e.dma_size);
}

static double tb_us(u64 ticks) { return (double)ticks * 1e6 / (double)ps3_timebase_hz; }

// S3: a RESET may wait a whole draw, so the dead-SPU limits allow a long job.
static u64 stall_ticks() { return g_spe_draw ? ps3_timebase_hz : ps3_timebase_hz / 10; }

// Spins until the echo shows want, or the budget runs out; false = timed out.
static bool wait_echo_tail(u32 want, u64 budget_ticks)
{
	const u64 t0 = gettime();
	while (echo()->tail != want)
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

// A full ring waits for the SPU; one that stays full for 100 ms means the SPU is gone.
static SpeCmd* ring_slot()
{
	if (s_head - s_tail_seen >= SPE_RING)
	{
		StallScope st_(ST_SPE);
		s_full_waits++;
		const u64 t0 = gettime();
		while (s_head - (s_tail_seen = echo()->tail) >= SPE_RING)
		{
			if (gettime() - t0 > stall_ticks())
			{
				dump_ls_echo("ring full");
				printf("[spe] command ring full for %u ms; SPE off\n", (u32)(stall_ticks() * 1000 / ps3_timebase_hz));
				g_spe_on = false;
				return 0;
			}
		}
	}
	return &s_ring[s_head & (SPE_RING - 1)];
}

static void push(u32 op, u32 ctx, u32 ea, u32 a, u32 b)
{
	SpeCmd* m = ring_slot();
	if (!m)
		return;
	m->op = (u8)op;
	m->ctx = (u8)ctx;
	m->pad = 0;
	m->ea = ea;
	m->a = a;
	m->b = b;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.head = ++s_head;
}

u32 ps3spe_data(u32 ctx, const u8* raw, u32 from, u32 to)
{
	push(SPE_OP_DATA, ctx, (u32)(uintptr_t)(raw + from), to - from, to);
	return s_head;
}

void ps3spe_wait_cmd(u32 cmd)
{
	if (!g_spe_on || (s32)(echo()->tail - cmd) >= 0)
		return;
	StallScope st_(ST_SPE);
	const u64 t0 = gettime();
	while ((s32)(echo()->tail - cmd) < 0)
	{
		if (gettime() - t0 > stall_ticks())
		{
			dump_ls_echo("raw reuse");
			printf("[spe] SPU still on the raw bytes after %u ms; SPE off\n", (u32)(stall_ticks() * 1000 / ps3_timebase_hz));
			g_spe_on = false;
			break;
		}
	}
	s_cmd_wait += gettime() - t0;
}

void ps3spe_reset(u32 ctx, u32 opt) { push(SPE_OP_RESET, ctx, 0, opt, 0); }

static u32 s_boot_opt;
void ps3spe_boot_opt(u32 opt) { s_boot_opt = opt; }
void ps3spe_cont(u32 ctx, u32 pos) { push(SPE_OP_CONT, ctx, 0, 0, pos); }
void ps3spe_soft() { push(SPE_OP_SOFT, 0, 0, 0, 0); }

u32 ps3spe_fence(u32 ctx, u32 pos)
{
	if (++s_fence_id == 0)
		s_fence_id = 1;
	push(SPE_OP_FENCE, ctx, 0, s_fence_id, pos);
	return g_spe_on ? s_fence_id : 0;
}

const SpeFence* ps3spe_wait_fence(u32 id)
{
	s_fences++;
	// S3: after a RESET that waited for a draw, the SPU still has to decode the frame.
	const u64 budget = g_spe_draw ? ps3_timebase_hz / 50 : ps3_timebase_hz / 200;
	const u64 t0 = gettime();
	u32 spins = 0;
	while ((s32)(echo()->fence - id) < 0)
	{
		if (s_wait_yield)
		{
			// db16cyc x4 (PPE nop forms): the SH4 thread on the same core takes these dispatch cycles.
			// The timebase is read only every 16th turn.
			__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
			if (++spins & 15)
				continue;
		}
		if (gettime() - t0 > budget)
		{
			dump_ls_echo("late fence");
			s_late++;
			return 0;
		}
	}
	const u64 dt = gettime() - t0;
	s_wait_sum += dt;
	if (dt > s_wait_max)
		s_wait_max = dt;
	// Orders the result and list loads after the fence load; the SPU put them in the same order.
	__asm__ volatile("lwsync" ::: "memory");
	return &s_results[id % SPE_RESULTS];
}

const SpeOut* ps3spe_out(u32 ctx) { return &s_out[ctx % SPE_CTX]; }

void ps3spe_result(bool ok, u64 ticks)
{
	if (ok)
		s_ok++;
	else
		s_bad++;
	s_check_sum += ticks;
	if (ticks > s_check_max)
		s_check_max = ticks;
}

void ps3spe_release(u32 id)
{
	if (!g_spe_draw || !id)
		return;
	// The draw's list loads must complete before the SPU can see the release.
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.released = id;
}

static void ring_selftest()
{
	const u64 ms10 = ps3_timebase_hz / 100;

	// Round trip: PPU push -> SPU wake -> ring DMA -> echo put -> PPU sees the tail.
	const u32 N = 1000;
	u64 tmin = ~0ull, tmax = 0, tsum = 0;
	u32 lost = 0;
	for (u32 i = 0; i < N; i++)
	{
		const u64 t0 = gettime();
		push(SPE_OP_NOP, 0, 0, 0, 0);
		if (!wait_echo_tail(s_head, ms10))
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

	// Push cost back to back, kept under the ring size so no push waits.
	const u32 M = SPE_RING / 2;
	u64 t0 = gettime();
	for (u32 i = 0; i < M; i++)
		push(SPE_OP_NOP, 0, 0, 0, 0);
	const u64 t_push = gettime() - t0;
	const bool caught = wait_echo_tail(s_head, ms10);
	printf("[spe] push x%u: %.1f ns each; %s\n", M, tb_us(t_push) * 1000.0 / M,
	       caught ? "SPU caught up" : "SPU NOT caught up in 10 ms");
}

// lv2 accepts this once per process; the TA SPE (5.12) and the AICA SPE (5.13) share it.
void ps3spe_sys_init()
{
	if (s_spu_init_done)
		return;
	s32 r = sysSpuInitialize(6, 0);
	printf("[spe] sysSpuInitialize(6,0) -> 0x%08X\n", (u32)r);
	s_spu_init_done = true;
}

bool ps3spe_init()
{
	if ((!get_spe_bell_preset() && !get_spe_draw_preset()) || s_up)
		return s_up;
	g_spe_draw = get_spe_draw_preset() != 0;
	s_wait_yield = get_spe_wait_yield_preset() != 0;

	ps3spe_sys_init();
	if (!s_out)
	{
		s_out = (SpeOut*)memalign(128, sizeof(SpeOut) * SPE_CTX);
		if (!s_out)
		{
			printf("[spe] shadow lists alloc failed (%u KB); SPE off\n", (u32)(sizeof(SpeOut) * SPE_CTX / 1024));
			return false;
		}
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

	s_bell.head = 0;
	s_bell.quit = 0;
	s_bell.released = 0;
	s_head = s_tail_seen = 0;
	s_fence_id = 0;
	memset(&s_echo, 0, sizeof(s_echo));
	memset(s_results, 0, sizeof(s_shared.results));
	memset(&s_shared.setup, 0, sizeof(s_shared.setup));
	for (u32 i = 0; i < SPE_CTX; i++)
		s_shared.setup.out_ea[i] = (u32)(uintptr_t)&s_out[i];
	s_shared.setup.draw = g_spe_draw;
	s_shared.setup.opt = s_boot_opt;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_bell");
	sysSpuThreadArgument arg;
	sysSpuThreadArgumentInitialize(arg);
	arg.arg0 = (u64)(uintptr_t)&s_bell;
	arg.arg1 = (u64)(uintptr_t)&s_echo;
	arg.arg2 = (u64)(uintptr_t)&s_shared;
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
		printf("[spe] SPU thread did not report in 100 ms; SPE off\n");
		ps3spe_term();
		return false;
	}
	printf("[spe] SPU thread up, %s, fence wait %s\n", g_spe_draw ? "draw from SPU lists (S3)" : "compare with the PPU replay (S2)",
	       s_wait_yield ? "yields (db16cyc)" : "spins");
	ring_selftest();
	g_spe_on = true;
	return true;
}

void ps3spe_term()
{
	if (!s_up)
		return;
	g_spe_on = false;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.quit = 1;
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
	static u32 l_head, l_bytes, l_wakes, l_timeouts, l_busy, l_rel_waits, l_rel_ticks;
	const u32 head = s_head;
	const volatile SpeEcho* e = echo();
	const u32 bytes = e->data_bytes, wakes = e->wakes, timeouts = e->timeouts, busy = e->busy;
	const u32 rel_waits = e->rel_waits, rel_ticks = e->rel_ticks;
	const u32 n = s_ok + s_bad;
	printf("[spe] cmds/s=%.0f data KB/s=%.0f wakes/s=%.0f timeouts/s=%.0f rescued=%u lag=%u full=%u gaps=%u cont_err=%u"
	       " | spu busy %.1f%% | fences=%u ok=%u bad=%u late=%u wait avg %.1f max %.1f us check avg %.0f max %.0f us"
	       " | vtx_of=%u odd=%u bad=%u pfull=%u wden=%u | rel waits/s=%.0f wait %.1f%% cmdwait=%.1fms\n",
	       (head - l_head) / secs, (bytes - l_bytes) / 1024.0 / secs, (wakes - l_wakes) / secs,
	       (timeouts - l_timeouts) / secs, e->rescued, head - e->tail, s_full_waits, e->gaps, e->cont_errs,
	       (busy - l_busy) * 100.0 / ((double)ps3_timebase_hz * secs),
	       s_fences, s_ok, s_bad, s_late,
	       n ? tb_us(s_wait_sum) / n : 0.0, tb_us(s_wait_max), n ? tb_us(s_check_sum) / n : 0.0, tb_us(s_check_max),
	       e->vtx_of, e->odd, e->bad, e->pfull, e->wden,
	       (rel_waits - l_rel_waits) / secs, (rel_ticks - l_rel_ticks) * 100.0 / ((double)ps3_timebase_hz * secs),
	       tb_us(s_cmd_wait) / 1000.0);
	l_head = head;
	l_bytes = bytes;
	l_wakes = wakes;
	l_timeouts = timeouts;
	l_busy = busy;
	l_rel_waits = rel_waits;
	l_rel_ticks = rel_ticks;
	s_cmd_wait = 0;
	s_fences = s_ok = s_bad = s_late = 0;
	s_wait_sum = s_wait_max = s_check_sum = s_check_max = 0;
}
