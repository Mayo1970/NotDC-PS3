// OPTIMIZATION_PLAN 5.12 S1: one SPU thread runs a command ring the SH4 thread fills at the TA batch
// points. It pulls each raw range and answers every FENCE; the render thread checks it against its replay.
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/spu.h>
#include <ogc/lwp_watchdog.h>

extern "C" const u8 spe_bell_elf[];
extern "C" int get_spe_bell_preset();

bool g_spe_on;

static SpeBell s_bell;
static SpeEcho s_echo;
static SpeShared s_shared;
static SpeCmd* const s_ring = s_shared.ring;
static SpeFence* const s_results = s_shared.results;
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
// Render thread counters (or the SH4 thread with the render thread off).
static u32 s_fences, s_ok, s_bad, s_late, s_bad_logged;
static u64 s_wait_sum, s_wait_max;

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
		s_full_waits++;
		const u64 t0 = gettime();
		while (s_head - (s_tail_seen = echo()->tail) >= SPE_RING)
		{
			if (gettime() - t0 > ps3_timebase_hz / 10)
			{
				dump_ls_echo("ring full");
				printf("[spe] command ring full for 100 ms; SPE off\n");
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

void ps3spe_data(u32 ctx, const u8* raw, u32 from, u32 to)
{
	push(SPE_OP_DATA, ctx, (u32)(uintptr_t)(raw + from), to - from, to);
}

void ps3spe_reset(u32 ctx) { push(SPE_OP_RESET, ctx, 0, 0, 0); }
void ps3spe_cont(u32 ctx, u32 pos) { push(SPE_OP_CONT, ctx, 0, 0, pos); }
void ps3spe_soft() { push(SPE_OP_SOFT, 0, 0, 0, 0); }

u32 ps3spe_fence(u32 ctx, u32 pos)
{
	if (++s_fence_id == 0)
		s_fence_id = 1;
	push(SPE_OP_FENCE, ctx, 0, s_fence_id, pos);
	return g_spe_on ? s_fence_id : 0;
}

void ps3spe_hash(SpeHash* h, const u8* p, u32 bytes)
{
	const u32* w = (const u32*)p;
	u32 a0 = h->a[0], a1 = h->a[1], a2 = h->a[2], a3 = h->a[3];
	u32 b0 = h->b[0], b1 = h->b[1], b2 = h->b[2], b3 = h->b[3];
	for (u32 i = 0; i < bytes / 16; i++, w += 4)
	{
		a0 += w[0]; b0 += a0;
		a1 += w[1]; b1 += a1;
		a2 += w[2]; b2 += a2;
		a3 += w[3]; b3 += a3;
	}
	h->a[0] = a0; h->a[1] = a1; h->a[2] = a2; h->a[3] = a3;
	h->b[0] = b0; h->b[1] = b1; h->b[2] = b2; h->b[3] = b3;
}

void ps3spe_check(u32 id, u32 ctx, u32 pos, u32 ncont, const SpeHash* h)
{
	if (!id)
		return;
	s_fences++;
	const u64 t0 = gettime();
	while ((s32)(echo()->fence - id) < 0)
	{
		if (gettime() - t0 > ps3_timebase_hz / 200)
		{
			dump_ls_echo("late fence");
			s_late++;
			return;
		}
	}
	const u64 dt = gettime() - t0;
	s_wait_sum += dt;
	if (dt > s_wait_max)
		s_wait_max = dt;
	// Orders the result loads after the fence load; the SPU put them in the same order.
	__asm__ volatile("lwsync" ::: "memory");
	const SpeFence* r = &s_results[id % SPE_RESULTS];
	if (r->id == id && r->ctx == ctx && r->pos == pos && r->ncont == ncont &&
	    memcmp(&r->hash, h, sizeof(*h)) == 0)
	{
		s_ok++;
		return;
	}
	s_bad++;
	if (s_bad_logged++ < 8)
		printf("[spe] fence %u MISMATCH: spu id=%u ctx=%u pos=%u ncont=%u gaps=%u a0=%08X b0=%08X | "
		       "ppu ctx=%u pos=%u ncont=%u a0=%08X b0=%08X\n",
		       id, r->id, r->ctx, r->pos, r->ncont, r->gaps, r->hash.a[0], r->hash.b[0],
		       ctx, pos, ncont, h->a[0], h->b[0]);
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

	s_bell.head = 0;
	s_bell.quit = 0;
	s_head = s_tail_seen = 0;
	s_fence_id = 0;
	memset(&s_echo, 0, sizeof(s_echo));
	memset(s_results, 0, sizeof(s_shared.results));

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
	printf("[spe] SPU thread up\n");
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
	static u32 l_head, l_bytes, l_wakes, l_timeouts;
	const u32 head = s_head;
	const volatile SpeEcho* e = echo();
	const u32 bytes = e->data_bytes, wakes = e->wakes, timeouts = e->timeouts;
	printf("[spe] cmds/s=%.0f data KB/s=%.0f wakes/s=%.0f timeouts/s=%.0f rescued=%u lag=%u full=%u gaps=%u cont_err=%u"
	       " | fences=%u ok=%u bad=%u late=%u wait avg %.1f max %.1f us\n",
	       (head - l_head) / secs, (bytes - l_bytes) / 1024.0 / secs, (wakes - l_wakes) / secs,
	       (timeouts - l_timeouts) / secs, e->rescued, head - e->tail, s_full_waits, e->gaps, e->cont_errs,
	       s_fences, s_ok, s_bad, s_late,
	       s_ok + s_bad ? tb_us(s_wait_sum) / (s_ok + s_bad) : 0.0, tb_us(s_wait_max));
	l_head = head;
	l_bytes = bytes;
	l_wakes = wakes;
	l_timeouts = timeouts;
	s_fences = s_ok = s_bad = s_late = 0;
	s_wait_sum = s_wait_max = 0;
}
