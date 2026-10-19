// OPTIMIZATION_PLAN 5.12 #3: one SPU thread hashes the textures of each scene's params on request from the render
// thread (spe_tex.c); ps3tex_bind reads its hashes instead of reading VRAM.
#include "ps3_spetex.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <sys/spu.h>
#include "ps3_timebase.h"
#include "plugs/drkPvr/drkPvr.h"
#include "plugs/drkPvr/regs.h"

extern "C" const u8 spe_tex_elf[];
extern "C" int get_spe_tex_preset();
extern "C" int get_spe_wait_yield_preset();

static_assert(VRAM_SIZE == SPETEX_VRAM_SIZE, "SPE Tex VRAM size");
static_assert(sizeof(SpeTexRes) == 8, "SpeTexRes is one 64-bit load");

static SpeTexBell s_bell;
static SpeTexEcho s_echo;
static SpeTexSetup s_setup;
static SpeTexRes* s_res;
static int s_mode;
static bool s_up;
static bool s_yield;
static u32 s_seq;
// The tag this frame's slots carry, 0 with no request; and the frame's slot count.
static u32 s_tag;
static u32 s_nparam;
static u64 s_t_req;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;

// Render thread counters, per stats window.
static u32 s_reqs, s_used, s_skips, s_none, s_waits, s_ok, s_bad, s_fails;
static u64 s_wait_sum, s_wait_max;

static inline const volatile SpeTexEcho* echo() { return &s_echo; }

static double tb_us(u64 ticks) { return (double)ticks * 1e6 / (double)ps3_timebase_hz; }

// The SPU's own echo from LS: on a stall the copy in main memory is stale.
static void dump_ls(const char* why)
{
	static u32 s_dumps;
	if (s_dumps++ >= 3)
		return;
	SpeTexEcho e;
	u64* w = (u64*)&e;
	const u32 ls = echo()->ls_addr;
	s32 r = 0;
	for (u32 i = 0; i < sizeof(e) / 8 && r == 0; i++)
		r = sysSpuThreadReadLocalStorage(s_thread, ls + i * 8, &w[i], 8);
	printf("[spetex] STALL (%s): ls_read=0x%08X ppu_seq=%u | ls echo: stage=%u seq=%u reqs=%u params=%u hashed=%u\n",
	       why, (u32)r, s_seq, e.stage, e.seq, e.reqs, e.params, e.hashed);
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
	s_tag = 0;
}

// A missed deadline turns the SPU hash off; ps3tex_bind hashes on the PPU from then on.
static void fail(const char* why)
{
	dump_ls(why);
	printf("[spetex] SPU hash %s (50 ms); SPE Tex Hash off\n", why);
	s_fails++;
	stop_group(true);
}

int ps3spetex_mode() { return s_up ? s_mode : SPETEX_OFF; }

void ps3spetex_request(const void* params, u32 nparam, u32 bg_pcw, u32 bg_tsp, u32 bg_tcw, u32 text_control,
                       u32 pal_ram_ctrl)
{
	s_tag = 0;
	if (!s_up)
		return;
	s_bell.params_ea = (u32)(uintptr_t)params;
	s_bell.nparam = nparam;
	s_bell.bg_pcw = bg_pcw;
	s_bell.bg_tsp = bg_tsp;
	s_bell.bg_tcw = bg_tcw;
	s_bell.text_control = text_control;
	s_bell.pal_ram_ctrl = pal_ram_ctrl;
	// Slots start at tag 0, so a request number with zero tag bits is skipped.
	if ((++s_seq & SPETEX_SEQ) == 0)
		++s_seq;
	// The fields land before seq; the store of seq kills the SPU's reservation.
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.seq = s_seq;
	s_tag = s_seq & SPETEX_SEQ;
	s_nparam = nparam;
	s_t_req = gettime();
	s_reqs++;
}

static inline void spin_turn(u32* spins)
{
	if (s_yield)
		// db16cyc x4 (PPE nop forms): the SH4 thread on the same core takes these dispatch cycles.
		__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
	(*spins)++;
}

bool ps3spetex_hash(u32 idx, u32* h)
{
	if (!s_tag)
		return false;
	if (idx >= s_nparam && idx != SPETEX_BG)
	{
		s_none++;
		return false;
	}
	// Hash and tag in one doubleword: a slot is never read half old, half new.
	const volatile u64* slot = (const volatile u64*)&s_res[idx];
	u64 v = *slot;
	if (((u32)v & SPETEX_SEQ) != s_tag)
	{
		const u64 t0 = gettime();
		u32 spins = 0;
		do
		{
			spin_turn(&spins);
			if (!(spins & 15) && gettime() - s_t_req > ps3_timebase_hz / 20)
			{
				fail("late");
				return false;
			}
			v = *slot;
		} while (((u32)v & SPETEX_SEQ) != s_tag);
		const u64 dt = gettime() - t0;
		s_waits++;
		s_wait_sum += dt;
		if (dt > s_wait_max)
			s_wait_max = dt;
	}
	if ((u32)v & SPETEX_SKIP)
	{
		s_skips++;
		return false;
	}
	*h = (u32)(v >> 32);
	s_used++;
	return true;
}

bool ps3spetex_job(u32 job_ea, u32 seq)
{
	if (!s_up)
		return false;
	s_bell.job_ea = job_ea;
	// The job and its field land before job_seq, whose store kills the SPU's reservation.
	__asm__ volatile("lwsync" ::: "memory");
	s_bell.job_seq = seq;
	return true;
}

void ps3spetex_check(u32 idx, u32 tsp, u32 tcw, u32 spu, u32 ppu)
{
	if (spu == ppu)
	{
		s_ok++;
		return;
	}
	s_bad++;
	// A texture the game rewrote between the request and the bind differs for one request; a bug, every request.
	static u32 s_logged;
	if (s_logged++ < 8)
		printf("[spetex] check MISMATCH req %u slot %u tsp %08X tcw %08X spu/ppu %08X/%08X\n", s_seq, idx, tsp, tcw, spu,
		       ppu);
}

bool ps3spetex_init()
{
	s_mode = get_spe_tex_preset();
	if (s_mode == SPETEX_OFF || s_up)
		return s_up;
	s_yield = get_spe_wait_yield_preset() != 0;
	const u32 pal_ea = (u32)(uintptr_t)PALETTE_RAM;
	if (pal_ea & 3)
	{
		printf("[spetex] PALETTE_RAM %08X not word aligned; SPE Tex Hash off\n", pal_ea);
		return false;
	}
	ps3spe_sys_init();
	if (!s_res)
		s_res = (SpeTexRes*)memalign(128, sizeof(SpeTexRes) * SPETEX_SLOTS);
	if (!s_res)
	{
		printf("[spetex] result slots alloc failed; SPE Tex Hash off\n");
		return false;
	}
	memset(s_res, 0, sizeof(SpeTexRes) * SPETEX_SLOTS);

	s32 r = sysSpuImageImport(&s_img, spe_tex_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[spetex] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}
	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_spetex");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[spetex] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}

	memset(&s_bell, 0, sizeof(s_bell));
	memset(&s_echo, 0, sizeof(s_echo));
	memset(&s_setup, 0, sizeof(s_setup));
	s_seq = 0;
	s_tag = 0;
	s_setup.vram_ea = (u32)(uintptr_t)PVRPARAMS.vram;
	s_setup.pal_ea = pal_ea;
	s_setup.res_ea = (u32)(uintptr_t)s_res;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_tex");
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
		printf("[spetex] SPU thread init/start failed: 0x%08X\n", (u32)r);
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
		printf("[spetex] SPU thread did not report in 100 ms; SPE Tex Hash off\n");
		stop_group(true);
		return false;
	}
	printf("[spetex] SPU thread up, mode %s, wait %s | vram %08X pal %08X slots %08X\n",
	       s_mode == SPETEX_ON ? "ON" : "CHECK", s_yield ? "yields (db16cyc)" : "spins", s_setup.vram_ea, pal_ea,
	       s_setup.res_ea);
	return true;
}

void ps3spetex_term()
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
		printf("[spetex] SPU thread did not exit in 100 ms; terminating the group\n");
	stop_group(stuck);
	printf("[spetex] SPU thread down\n");
}

// Called from the render stats line, every ~5 s.
void ps3spetex_stats(double secs)
{
	if (!s_mode || secs <= 0.0)
		return;
	static u32 l_reqs, l_busy, l_params, l_hashed, l_dups, l_skips, l_bytes, l_wraps;
	const volatile SpeTexEcho* e = echo();
	const u32 reqs = e->reqs, busy = e->busy, params = e->params, hashed = e->hashed, dups = e->dups;
	const u32 skips = e->skips, bytes = e->bytes, wraps = e->wraps;
	const u32 nr = reqs - l_reqs;
	printf("[spetex] mode=%s up=%u reqs/s=%.0f | spu busy %.2f%% req avg %.0f max %.0f us | per req: params %u hashed %u"
	       " dups %u skips %u KB %u wraps %u | binds: spu %u skip %u none %u | wait %u avg %.1f max %.1f us, total %.2f ms"
	       " | fail=%u check ok=%u bad=%u\n",
	       s_mode == SPETEX_ON ? "ON" : "CHECK", (u32)s_up, s_reqs / secs,
	       (busy - l_busy) * 100.0 / ((double)ps3_timebase_hz * secs), nr ? tb_us(busy - l_busy) / nr : 0.0,
	       tb_us(e->max_busy), nr ? (params - l_params) / nr : 0, nr ? (hashed - l_hashed) / nr : 0,
	       nr ? (dups - l_dups) / nr : 0, nr ? (skips - l_skips) / nr : 0, nr ? (bytes - l_bytes) / nr / 1024 : 0,
	       wraps - l_wraps, s_used, s_skips, s_none, s_waits, s_waits ? tb_us(s_wait_sum) / s_waits : 0.0,
	       tb_us(s_wait_max), tb_us(s_wait_sum) / 1000.0, s_fails, s_ok, s_bad);
	l_reqs = reqs;
	l_busy = busy;
	l_params = params;
	l_hashed = hashed;
	l_dups = dups;
	l_skips = skips;
	l_bytes = bytes;
	l_wraps = wraps;
	s_reqs = s_used = s_skips = s_none = s_waits = s_ok = s_bad = 0;
	s_wait_sum = s_wait_max = 0;
}
