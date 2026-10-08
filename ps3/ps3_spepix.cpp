// OPTIMIZATION_PLAN 5.12 #4/#5: one SPU thread (spe_pix.c) decodes textures for the render thread and converts the
// direct framebuffer for the SH4 thread, by DMA straight into RSX memory. Each caller waits for its own job.
#include "ps3_spepix.h"
#include "ps3_spe.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <sys/spu.h>
#include <rsx/rsx.h>
#include "ps3_timebase.h"
#include "plugs/drkPvr/drkPvr.h"
#include "plugs/drkPvr/regs.h"

extern "C" const u8 spe_pix_elf[];
extern "C" int get_spe_texdec_preset();
extern "C" int get_spe_fbconv_preset();
extern "C" int get_spe_wait_yield_preset();

static_assert(VRAM_SIZE == SPEPIX_VRAM_SIZE, "SPE Pix VRAM size");
static_assert(sizeof(SpePixBell) == 128 && sizeof(SpePixEcho) == 128, "SPE Pix lock lines");
static_assert(offsetof(SpePixBell, fb) == 64, "SPE Pix bell halves");
// Each wait reads its seq and the word after it with one 64-bit load.
static_assert(offsetof(SpePixEcho, tex_seq) == 0 && offsetof(SpePixEcho, fb_seq) == 8, "SPE Pix echo doublewords");

#define TEX_CHECK_BYTES (4u << 20)
#define FB_CHECK_BYTES  (768u * 4 * 576)

static SpePixBell s_bell;
static SpePixEcho s_echo;
static SpePixSetup s_setup;
static int s_tex_mode, s_fb_mode;
static volatile bool s_up;
static volatile u32 s_failing;
static bool s_yield;
static u32 s_seq_tex, s_seq_fb;
static u32* s_chk_tex[2];
static u32* s_chk_fb;
static sysSpuImage s_img;
static sys_spu_group_t s_group;
static sys_spu_thread_t s_thread;

// Caller-side counters, per stats window.
static u32 s_tex_n, s_tex_skip, s_fb_n, s_fails, s_tex_ok, s_tex_bad, s_fb_ok, s_fb_bad;
static u64 s_tex_wait, s_tex_wait_max, s_fb_wait, s_fb_wait_max;

static inline const volatile SpePixEcho* echo() { return &s_echo; }

static double tb_us(u64 ticks) { return (double)ticks * 1e6 / (double)ps3_timebase_hz; }

// The SPU's own echo from LS: on a stall the copy in main memory is stale.
static void dump_ls(const char* why)
{
	static u32 s_dumps;
	if (s_dumps++ >= 3)
		return;
	SpePixEcho e;
	u64* w = (u64*)&e;
	const u32 ls = echo()->ls_addr;
	s32 r = 0;
	for (u32 i = 0; i < sizeof(e) / 8 && r == 0; i++)
		r = sysSpuThreadReadLocalStorage(s_thread, ls + i * 8, &w[i], 8);
	printf("[spepix] STALL (%s): ls_read=0x%08X ppu tex/fb seq=%u/%u | ls echo: stage=%u tex/fb seq=%u/%u jobs=%u/%u\n",
	       why, (u32)r, s_seq_tex, s_seq_fb, e.stage, e.tex_seq, e.fb_seq, e.tex_jobs, e.fb_jobs);
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

// A missed deadline turns the SPU off; both callers take the PPU paths from then on. One caller stops the group.
static void fail(const char* why)
{
	if (!__sync_bool_compare_and_swap(&s_failing, 0, 1))
		return;
	dump_ls(why);
	printf("[spepix] SPU job %s (50 ms); SPE Tex Decode and SPE FB Convert off\n", why);
	s_fails++;
	stop_group(true);
}

int ps3spepix_tex_mode() { return s_up ? s_tex_mode : SPEPIX_OFF; }
int ps3spepix_fb_mode() { return s_up ? s_fb_mode : SPEPIX_OFF; }
int ps3spepix_fb_preset() { return get_spe_fbconv_preset(); }

static inline void spin_turn(u32* spins)
{
	if (s_yield)
		// db16cyc x4 (PPE nop forms): the SH4 thread on the same core takes these dispatch cycles.
		__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
	(*spins)++;
}

// Waits until the echo word at seq_word holds seq; the doubleword it starts is returned.
static bool wait_seq(const volatile u32* seq_word, u32 seq, const char* why, u64* v, u64* waited)
{
	const u64 t0 = gettime();
	u32 spins = 0;
	for (;;)
	{
		*v = *(const volatile u64*)seq_word;
		if ((u32)(*v >> 32) == seq)
			break;
		spin_turn(&spins);
		if (!(spins & 15) && (gettime() - t0 > ps3_timebase_hz / 20 || !s_up))
		{
			fail(why);
			return false;
		}
	}
	__asm__ volatile("lwsync" ::: "memory");
	*waited = gettime() - t0;
	return true;
}

bool ps3spepix_tex(const SpePixTex* j, u32* res)
{
	if (!s_up)
		return false;
	SpePixTex* const b = &s_bell.tex;
	b->fmt = j->fmt;
	b->w = j->w;
	b->h = j->h;
	b->addr = j->addr;
	b->pitch = j->pitch;
	b->flags = j->flags;
	b->pal_base = j->pal_base;
	b->vq_cb = j->vq_cb;
	b->pal_ctrl = j->pal_ctrl;
	b->dst_ea = j->dst_ea;
	b->dst_pitch = j->dst_pitch;
	// The fields land before seq; the store of seq kills the SPU's reservation.
	__asm__ volatile("lwsync" ::: "memory");
	b->seq = ++s_seq_tex;
	u64 v, dt;
	if (!wait_seq(&s_echo.tex_seq, s_seq_tex, "tex late", &v, &dt))
		return false;
	*res = (u32)v;
	s_tex_n++;
	s_tex_wait += dt;
	if (dt > s_tex_wait_max)
		s_tex_wait_max = dt;
	return true;
}

bool ps3spepix_fb(const SpePixFb* j)
{
	if (!s_up)
		return false;
	SpePixFb* const b = &s_bell.fb;
	b->sof1 = j->sof1;
	b->depth = j->depth;
	b->w = j->w;
	b->h = j->h;
	b->line_bytes = j->line_bytes;
	b->dst_ea = j->dst_ea;
	b->dst_pitch = j->dst_pitch;
	__asm__ volatile("lwsync" ::: "memory");
	b->seq = ++s_seq_fb;
	u64 v, dt;
	if (!wait_seq(&s_echo.fb_seq, s_seq_fb, "fb late", &v, &dt))
		return false;
	s_fb_n++;
	s_fb_wait += dt;
	if (dt > s_fb_wait_max)
		s_fb_wait_max = dt;
	return true;
}

void ps3spepix_tex_skip() { s_tex_skip++; }

u32* ps3spepix_tex_check_buf(int ppu) { return s_chk_tex[ppu ? 1 : 0]; }
u32* ps3spepix_fb_check_buf() { return s_chk_fb; }

void ps3spepix_tex_check(bool ok, const char* what)
{
	if (ok)
	{
		s_tex_ok++;
		return;
	}
	s_tex_bad++;
	static u32 s_logged;
	if (s_logged++ < 8)
		printf("[spepix] tex check MISMATCH %s\n", what);
}

void ps3spepix_fb_check(bool ok, const char* what)
{
	if (ok)
	{
		s_fb_ok++;
		return;
	}
	s_fb_bad++;
	static u32 s_logged;
	if (s_logged++ < 8)
		printf("[spepix] fb check MISMATCH %s\n", what);
}

// SPU DMA into RSX local memory, where the textures live, before any job depends on it.
static bool dma_test()
{
	volatile u32* t = (volatile u32*)rsxMemalign(128, 128);
	if (!t)
	{
		printf("[spepix] rsxMemalign for the DMA test failed\n");
		return false;
	}
	SpePixTex j;
	memset(&j, 0, sizeof(j));
	j.flags = SPEPIX_TEST;
	j.dst_ea = (u32)(uintptr_t)t;
	u32 res;
	const u64 t0 = gettime();
	const bool done = ps3spepix_tex(&j, &res);
	const double us = tb_us(gettime() - t0);
	u32 bad = 32;
	if (done)
	{
		__asm__ volatile("sync" ::: "memory");
		bad = 0;
		for (u32 k = 0; k < 32; k++)
			bad += t[k] != 0x5EC00000u + (s_seq_tex << 8) + k;
	}
	rsxFree((void*)t);
	printf("[spepix] RSX DMA test: %s (%.1f us, %u of 32 words wrong)\n", done && !bad ? "ok" : "FAILED", us, bad);
	return done && !bad;
}

bool ps3spepix_init()
{
	s_tex_mode = get_spe_texdec_preset();
	s_fb_mode = get_spe_fbconv_preset();
	if ((s_tex_mode == SPEPIX_OFF && s_fb_mode == SPEPIX_OFF) || s_up)
		return s_up;
	s_yield = get_spe_wait_yield_preset() != 0;
	const u32 vram_ea = (u32)(uintptr_t)PVRPARAMS.vram, pal_ea = (u32)(uintptr_t)PALETTE_RAM;
	if ((vram_ea & 15) || (pal_ea & 3))
	{
		printf("[spepix] VRAM %08X or PALETTE_RAM %08X misaligned; SPE Pix off\n", vram_ea, pal_ea);
		return false;
	}
	ps3spe_sys_init();
	if (s_tex_mode == SPEPIX_CHECK && !s_chk_tex[0])
	{
		s_chk_tex[0] = (u32*)memalign(128, TEX_CHECK_BYTES);
		s_chk_tex[1] = (u32*)memalign(128, TEX_CHECK_BYTES);
	}
	if (s_fb_mode == SPEPIX_CHECK && !s_chk_fb)
		s_chk_fb = (u32*)memalign(128, FB_CHECK_BYTES);
	if ((s_tex_mode == SPEPIX_CHECK && (!s_chk_tex[0] || !s_chk_tex[1])) || (s_fb_mode == SPEPIX_CHECK && !s_chk_fb))
	{
		printf("[spepix] CHECK buffer alloc failed; SPE Pix off\n");
		return false;
	}

	s32 r = sysSpuImageImport(&s_img, spe_pix_elf, SPU_IMAGE_PROTECT);
	if (r != 0)
	{
		printf("[spepix] sysSpuImageImport failed: 0x%08X\n", (u32)r);
		return false;
	}
	sysSpuThreadGroupAttribute gattr;
	sysSpuThreadGroupAttributeInitialize(gattr);
	sysSpuThreadGroupAttributeName(gattr, "nulldc_spepix");
	r = sysSpuThreadGroupCreate(&s_group, 1, 100, &gattr);
	if (r != 0)
	{
		printf("[spepix] sysSpuThreadGroupCreate failed: 0x%08X\n", (u32)r);
		sysSpuImageClose(&s_img);
		return false;
	}

	memset(&s_bell, 0, sizeof(s_bell));
	memset(&s_echo, 0, sizeof(s_echo));
	memset(&s_setup, 0, sizeof(s_setup));
	s_seq_tex = s_seq_fb = 0;
	s_failing = 0;
	s_setup.vram_ea = vram_ea;
	s_setup.pal_ea = pal_ea;

	sysSpuThreadAttribute tattr;
	sysSpuThreadAttributeInitialize(tattr);
	sysSpuThreadAttributeName(tattr, "spe_pix");
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
		printf("[spepix] SPU thread init/start failed: 0x%08X\n", (u32)r);
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
		printf("[spepix] SPU thread did not report in 100 ms; SPE Pix off\n");
		stop_group(true);
		return false;
	}
	if (!dma_test())
	{
		if (s_up)
		{
			dump_ls("dma test");
			stop_group(true);
		}
		printf("[spepix] SPE Pix off\n");
		return false;
	}
	static const char* const k_mode[] = { "OFF", "ON", "CHECK" };
	printf("[spepix] SPU thread up, tex %s, fb %s, wait %s | vram %08X pal %08X\n", k_mode[s_tex_mode % 3],
	       k_mode[s_fb_mode % 3], s_yield ? "yields (db16cyc)" : "spins", vram_ea, pal_ea);
	return true;
}

void ps3spepix_term()
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
		printf("[spepix] SPU thread did not exit in 100 ms; terminating the group\n");
	stop_group(stuck);
	printf("[spepix] SPU thread down\n");
}

// Called from the render stats line, every ~5 s.
void ps3spepix_stats(double secs)
{
	if ((!s_tex_mode && !s_fb_mode) || secs <= 0.0)
		return;
	static u32 l_tj, l_tb, l_kb, l_fj, l_fb, l_wakes;
	const volatile SpePixEcho* e = echo();
	const u32 tj = e->tex_jobs, tb = e->tex_busy, kb = e->tex_kb, fj = e->fb_jobs, fb = e->fb_busy, wakes = e->wakes;
	const u32 ntj = tj - l_tj, nfj = fj - l_fj;
	printf("[spepix] tex=%u fb=%u up=%u | tex jobs %u (%.0f/s) spu avg %.0f max %.0f us, %.0f KB/s, wait total %.1f ms max"
	       " %.0f us, skip %u | fb jobs %u spu avg %.0f max %.0f us, wait total %.1f ms max %.0f us | spu busy %.2f%%"
	       " wakes/s %.0f | fail=%u check tex ok=%u bad=%u fb ok=%u bad=%u\n",
	       s_tex_mode, s_fb_mode, (u32)s_up, s_tex_n, s_tex_n / secs, ntj ? tb_us(tb - l_tb) / ntj : 0.0,
	       tb_us(e->tex_max), (kb - l_kb) / secs, tb_us(s_tex_wait) / 1000.0, tb_us(s_tex_wait_max), s_tex_skip, s_fb_n,
	       nfj ? tb_us(fb - l_fb) / nfj : 0.0, tb_us(e->fb_max), tb_us(s_fb_wait) / 1000.0, tb_us(s_fb_wait_max),
	       (tb - l_tb + fb - l_fb) * 100.0 / ((double)ps3_timebase_hz * secs), (wakes - l_wakes) / secs, s_fails,
	       s_tex_ok, s_tex_bad, s_fb_ok, s_fb_bad);
	l_tj = tj;
	l_tb = tb;
	l_kb = kb;
	l_fj = fj;
	l_fb = fb;
	l_wakes = wakes;
	s_tex_n = s_tex_skip = s_fb_n = s_tex_ok = s_tex_bad = s_fb_ok = s_fb_bad = 0;
	s_tex_wait = s_tex_wait_max = s_fb_wait = s_fb_wait_max = 0;
}
