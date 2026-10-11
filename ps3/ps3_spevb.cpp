// OPTIMIZATION_PLAN 5.12 #2: one SPU thread builds each scene's merged vertex buffer and draw runs on request
// from the render thread (spe_vb.c). The vertex buffer goes by SPU DMA straight into the scene's vring block.
#include "ps3_spevb.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <sys/spu.h>
#include <rsx/rsx.h>
#include "ps3_timebase.h"

extern "C" const u8 spe_vb_elf[];
extern "C" int get_spe_build_preset();
extern "C" int get_spe_wait_yield_preset();

// Vertices in each CHECK copy; a larger scene is checked up to here, the same cap on both sides.
#define CHECK_CAP (160 * 1024)

static SpeVbBell s_bell;
static SpeVbEcho s_echo;
static SpeVbSetup s_setup;
static SpeVbDiag s_diag;
static SpeRun* s_runs;
static u32* s_chk_spu;
static u32* s_chk_dry;
static SpeRun* s_chk_runs;
static int s_mode;
static bool s_up;
static bool s_yield;
static u32 s_seq;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;

// Render thread counters, per stats window.
static u32 s_reqs, s_nruns, s_nvtx, s_fails, s_amis, s_ok, s_bad, s_late;
static u64 s_t_req, s_wait_first_sum, s_wait_first_max, s_wait_sum;
static bool s_first;

static inline const volatile SpeVbEcho* echo() { return &s_echo; }

static double tb_us(u64 ticks) { return (double)ticks * 1e6 / (double)ps3_timebase_hz; }

// The SPU's own echo from LS: on a stall the copy in main memory is stale.
static void dump_ls(const char* why)
{
	static u32 s_dumps;
	if (s_dumps++ >= 3)
		return;
	SpeVbEcho e;
	u64* w = (u64*)&e;
	const u32 ls = echo()->ls_addr;
	s32 r = 0;
	for (u32 i = 0; i < sizeof(e) / 8 && r == 0; i++)
		r = sysSpuThreadReadLocalStorage(s_thread, ls + i * 8, &w[i], 8);
	printf("[spevb] STALL (%s): ls_read=0x%08X ppu_seq=%u | ls echo: stage=%u seq=%u runs=%u flags=%X vb_used=%u builds=%u\n",
	       why, (u32)r, s_seq, e.stage, e.seq, e.runs, e.flags, e.vb_used, e.builds);
}

static void stop_group(bool terminate)
{
	if (terminate)
		sysSpuThreadGroupTerminate(s_group, 0);
	u32 cause, status;
	sysSpuThreadGroupJoin(s_group, &cause, &status);
	sysSpuThreadGroupDestroy(s_group);
	sysSpuImageClose(&s_img);
	s_up = false;
}

// A missed deadline turns the builder off; the render thread takes the PPU path from then on.
static void fail(const char* why)
{
	dump_ls(why);
	printf("[spevb] SPU build %s (20 ms); SPE Build off\n", why);
	s_fails++;
	stop_group(true);
}

static inline void spin_turn(u32* spins)
{
	if (s_yield)
		// db16cyc x4 (PPE nop forms): the SH4 thread on the same core takes these dispatch cycles.
		__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
	(*spins)++;
}

static void note_wait(u64 t0)
{
	const u64 dt = gettime() - t0;
	s_wait_sum += dt;
	if (s_first)
	{
		s_first = false;
		const u64 df = gettime() - s_t_req;
		s_wait_first_sum += df;
		if (df > s_wait_first_max)
			s_wait_first_max = df;
	}
}

int ps3spevb_mode() { return s_up ? s_mode : SPEVB_OFF; }

bool ps3spevb_request(u32 ctx, u32 nvtx, u32 nstrip, u32 nparam, void* vb, u32 cap, u32 flags)
{
	if (!s_up)
		return false;
	s_bell.ctx = ctx;
	s_bell.nvtx = nvtx;
	s_bell.nstrip = nstrip;
	s_bell.nparam = nparam;
	s_bell.vb_ea = (u32)(uintptr_t)vb;
	s_bell.cap = cap;
	s_bell.flags = flags;
	// The fields land before seq; the store of seq kills the SPU's reservation.
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.seq = ++s_seq;
	s_t_req = gettime();
	s_first = true;
	s_reqs++;
	return true;
}

static const u32 k_sec_flag[3] = { SPEVB_OP_DONE, SPEVB_PT_DONE, SPEVB_TR_DONE };

const SpeRun* ps3spevb_run(u32 k, u32 sec)
{
	if (!s_up)
		return 0;
	const u32 end_flags = k_sec_flag[sec] | (sec == SPEVB_SEC_TR ? SPEVB_TR_PPU : 0);
	const u64 t0 = gettime();
	const u64 budget = ps3_timebase_hz / 50;
	u32 spins = 0;
	for (;;)
	{
		// seq and runs in one load: a runs count is never read with another request's seq.
		const u64 sr = *(const volatile u64*)&s_echo.seq;
		if ((u32)(sr >> 32) == s_seq)
		{
			__asm__ volatile("lwsync" ::: "memory");
			const u32 flags = echo()->flags;
			if (flags & end_flags)
			{
				if ((flags & SPEVB_TR_PPU) && sec == SPEVB_SEC_TR)
				{
					note_wait(t0);
					return 0;
				}
				if (k >= echo()->sec_end[sec])
				{
					note_wait(t0);
					return 0;
				}
			}
			// Runs of the next section only go out after this section's flag, so run k is this section's.
			if ((u32)sr > k)
			{
				note_wait(t0);
				__asm__ volatile("lwsync" ::: "memory");
				return &s_runs[k];
			}
		}
		spin_turn(&spins);
		if (!(spins & 15) && gettime() - t0 > budget)
		{
			fail("late");
			return 0;
		}
	}
}

u32 ps3spevb_sec_start(u32 sec) { return sec == SPEVB_SEC_OP ? 0 : echo()->sec_end[sec - 1]; }
u32 ps3spevb_strips(u32 sec) { return echo()->strips[sec]; }
bool ps3spevb_tr_ppu() { return (echo()->flags & SPEVB_TR_PPU) != 0; }
u32 ps3spevb_vb_used() { return echo()->vb_used; }

void ps3spevb_note_alpha(bool same)
{
	if (!same)
		s_amis++;
}

void ps3spevb_end_frame()
{
	s_nruns += echo()->runs;
	s_nvtx += echo()->vb_used;
}

bool ps3spevb_check_bufs(u32** spu_vb, u32** dry_vb, SpeRun** dry_runs, u32* cap)
{
	if (!s_chk_spu)
		return false;
	*spu_vb = s_chk_spu;
	*dry_vb = s_chk_dry;
	*dry_runs = s_chk_runs;
	*cap = CHECK_CAP;
	return true;
}

const SpeRun* ps3spevb_runs() { return s_runs; }

const volatile SpeVbEcho* ps3spevb_wait_final()
{
	if (!s_up)
		return 0;
	const u64 t0 = gettime();
	u32 spins = 0;
	for (;;)
	{
		const u64 sr = *(const volatile u64*)&s_echo.seq;
		if ((u32)(sr >> 32) == s_seq && (echo()->flags & SPEVB_FINAL))
		{
			__asm__ volatile("lwsync" ::: "memory");
			note_wait(t0);
			return echo();
		}
		spin_turn(&spins);
		if (!(spins & 15) && gettime() - t0 > ps3_timebase_hz / 50)
		{
			s_late++;
			fail("late");
			return 0;
		}
	}
}

void ps3spevb_check_result(bool ok)
{
	if (ok)
		s_ok++;
	else
		s_bad++;
}

// SPU DMA into RSX local memory, the vring's kind, before any scene depends on it.
static bool dma_test()
{
	volatile u32* t = (volatile u32*)rsxMemalign(128, 128);
	if (!t)
	{
		printf("[spevb] rsxMemalign for the DMA test failed\n");
		return false;
	}
	s_bell.vb_ea = (u32)(uintptr_t)t;
	s_bell.flags = SPEVB_TEST;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.seq = ++s_seq;
	const u64 t0 = gettime();
	bool done = false;
	while (!done && gettime() - t0 < ps3_timebase_hz / 10)
		done = echo()->seq == s_seq && (echo()->flags & SPEVB_FINAL);
	const double us = tb_us(gettime() - t0);
	u32 bad = 32;
	if (done)
	{
		__asm__ volatile("sync" ::: "memory");
		bad = 0;
		for (u32 k = 0; k < 32; k++)
			bad += t[k] != 0x5EB00000u + (s_seq << 8) + k;
	}
	rsxFree((void*)t);
	printf("[spevb] RSX DMA test: %s (%.1f us, %u of 32 words wrong)\n", done && !bad ? "ok" : "FAILED", us, bad);
	return done && !bad;
}

// The diagnostic split's boot bench: what one SPU gets out of DMA to RSX local and to main memory.
static void dma_bench()
{
	const u32 size = 1 << 20;
	void* rsx = rsxMemalign(128, size);
	void* xdr = memalign(128, size);
	if (rsx && xdr)
	{
		s_bell.vb_ea = (u32)(uintptr_t)rsx;
		s_bell.ctx = (u32)(uintptr_t)xdr;
		s_bell.flags = SPEVB_BENCH;
		__asm__ volatile("lwsync" ::: "memory");
		s_bell.seq = ++s_seq;
		const u64 t0 = gettime();
		bool done = false;
		while (!done && gettime() - t0 < ps3_timebase_hz)
			done = echo()->seq == s_seq && (echo()->flags & SPEVB_FINAL);
		__asm__ volatile("lwsync" ::: "memory");
		const volatile SpeVbDiag* d = &s_diag;
		if (done)
			printf("[spevb] bench: 1 MB put to RSX %.0f us (%.0f MB/s), to main %.0f us (%.0f MB/s) | 128 B put+wait"
			       " RSX %.2f us, main %.2f us | 192 B get+wait main %.2f us\n",
			       tb_us(d->b_put_rsx), 1048576.0 / tb_us(d->b_put_rsx), tb_us(d->b_put_xdr),
			       1048576.0 / tb_us(d->b_put_xdr), tb_us(d->b_lat_rsx) / 100, tb_us(d->b_lat_xdr) / 100,
			       tb_us(d->b_get_xdr) / 100);
		else
			printf("[spevb] bench: no answer in 1 s\n");
	}
	if (rsx)
		rsxFree(rsx);
	free(xdr);
}

bool ps3spevb_init()
{
	s_mode = get_spe_build_preset();
	if (s_mode == SPEVB_OFF || s_up)
		return s_up;
	if (!g_spe_on || !g_spe_draw)
	{
		printf("[spevb] SPE Build needs SPE Draw; off\n");
		return false;
	}
	s_yield = get_spe_wait_yield_preset() != 0;
	if (!s_runs)
		s_runs = (SpeRun*)memalign(128, sizeof(SpeRun) * SPEVB_MAX_RUNS);
	if (s_mode == SPEVB_CHECK && !s_chk_spu)
	{
		s_chk_spu = (u32*)memalign(128, CHECK_CAP * SPEVB_VTX_STRIDE);
		s_chk_dry = (u32*)memalign(128, CHECK_CAP * SPEVB_VTX_BYTES);
		s_chk_runs = (SpeRun*)memalign(128, sizeof(SpeRun) * SPEVB_MAX_RUNS);
		if (!s_chk_spu || !s_chk_dry || !s_chk_runs)
		{
			free(s_chk_spu);
			free(s_chk_dry);
			free(s_chk_runs);
			s_chk_spu = s_chk_dry = 0;
			s_chk_runs = 0;
		}
	}
	if (!s_runs || (s_mode == SPEVB_CHECK && !s_chk_spu))
	{
		printf("[spevb] buffer alloc failed; SPE Build off\n");
		return false;
	}

	s32 r = sysSpuImageImport(&s_img, spe_vb_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[spevb] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}
	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_spevb");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[spevb] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}

	memset(&s_bell, 0, sizeof(s_bell));
	memset(&s_echo, 0, sizeof(s_echo));
	memset(&s_setup, 0, sizeof(s_setup));
	memset(&s_diag, 0, sizeof(s_diag));
	s_seq = 0;
	for (u32 i = 0; i < SPE_CTX; i++)
		s_setup.out_ea[i] = (u32)(uintptr_t)ps3spe_out(i);
	s_setup.runs_ea = (u32)(uintptr_t)s_runs;
	s_setup.diag_ea = (u32)(uintptr_t)&s_diag;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_vb");
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
		printf("[spevb] SPU thread init/start failed: 0x%08X\n", (u32)r);
		sysSpuThreadGroupDestroy(s_group);
		sysSpuImageClose(&s_img);
		return false;
	}
	s_up = true;

	const u64 t0 = gettime();
	while (echo()->state != SPE_STATE_RUNNING && gettime() - t0 < ps3_timebase_hz / 10)
		;
	if (echo()->state != SPE_STATE_RUNNING)
	{
		printf("[spevb] SPU thread did not report in 100 ms; SPE Build off\n");
		stop_group(true);
		return false;
	}
	if (!dma_test())
	{
		dump_ls("dma test");
		stop_group(true);
		printf("[spevb] SPE Build off\n");
		return false;
	}
	dma_bench();
	printf("[spevb] SPU thread up, mode %s, wait %s\n", s_mode == SPEVB_ON ? "ON" : "CHECK",
	       s_yield ? "yields (db16cyc)" : "spins");
	return true;
}

void ps3spevb_term()
{
	if (!s_up)
		return;
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.quit = 1;
	const u64 t0 = gettime();
	while (echo()->state != SPE_STATE_EXITED && gettime() - t0 < ps3_timebase_hz / 10)
		;
	const bool stuck = echo()->state != SPE_STATE_EXITED;
	if (stuck)
		printf("[spevb] SPU thread did not exit in 100 ms; terminating the group\n");
	stop_group(stuck);
	printf("[spevb] SPU thread down\n");
}

// Called from the render stats line, every ~5 s.
void ps3spevb_stats(double secs)
{
	if (!s_mode || secs <= 0.0)
		return;
	static u32 l_builds, l_busy;
	const u32 builds = echo()->builds, busy = echo()->busy;
	const u32 nb = builds - l_builds;
	printf("[spevb] mode=%s up=%u builds/s=%.0f runs/s=%.0f vtx/s=%.0f | spu busy %.1f%% build avg %.0f max %.0f us"
	       " | wait first avg %.1f max %.1f us, total %.1f ms | trppu=%u fail=%u amis=%u | check ok=%u bad=%u late=%u\n",
	       s_mode == SPEVB_ON ? "ON" : "CHECK", (u32)s_up, s_reqs / secs, s_nruns / secs, s_nvtx / secs,
	       (busy - l_busy) * 100.0 / ((double)ps3_timebase_hz * secs), nb ? tb_us(busy - l_busy) / nb : 0.0,
	       tb_us(echo()->max_busy), s_reqs ? tb_us(s_wait_first_sum) / s_reqs : 0.0, tb_us(s_wait_first_max),
	       tb_us(s_wait_sum) / 1000.0, echo()->trppu, s_fails, s_amis, s_ok, s_bad, s_late);
	l_builds = builds;
	l_busy = busy;
	s_reqs = s_nruns = s_nvtx = s_amis = s_ok = s_bad = s_late = 0;
	s_wait_first_sum = s_wait_first_max = s_wait_sum = 0;

	// The diagnostic split, per build of the window. Waits sit inside the passes they stop.
	static SpeVbDiag l;
	const volatile SpeVbDiag* d = &s_diag;
	SpeVbDiag c;
	memcpy(&c, (const void*)d, sizeof(c));
	const u32 n = c.n_build - l.n_build;
	if (n)
		printf("[spevb] split/build: %.0f us = waits slot %.0f pub %.0f slide %.0f in %.0f qfull %.0f | key pass %.0f"
		       " sort %.0f tr pass %.0f | strips %u vin %u vout %u pubs %u slides %u cmds %u KB %u slowmul %u\n",
		       tb_us(c.t_build - l.t_build) / n, tb_us(c.t_slot - l.t_slot) / n, tb_us(c.t_pub - l.t_pub) / n,
		       tb_us(c.t_slide - l.t_slide) / n, tb_us(c.t_in - l.t_in) / n, tb_us(c.t_qfull - l.t_qfull) / n,
		       tb_us(c.t_key - l.t_key) / n, tb_us(c.t_sort - l.t_sort) / n, tb_us(c.t_tr - l.t_tr) / n,
		       (c.n_strip - l.n_strip) / n, (c.n_vin - l.n_vin) / n, (c.n_vout - l.n_vout) / n,
		       (c.n_pub - l.n_pub) / n, (c.n_slide - l.n_slide) / n, (c.n_cmd - l.n_cmd) / n,
		       (c.kb_out - l.kb_out) / n, (c.n_slowmul - l.n_slowmul) / n);
	l = c;
}
