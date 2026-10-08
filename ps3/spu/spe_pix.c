// OPTIMIZATION_PLAN 5.12 #4/#5: sleeps on a reservation of its bell line. The render thread's texture decodes and the
// SH4 thread's framebuffer converts (spe_pix_job.h) go by DMA straight into RSX memory.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include "spe_bell.h"
#include "spe_pix.h"

static SpePixEcho echo __attribute__((aligned(128)));
static SpePixBell bell __attribute__((aligned(128)));
static SpePixSetup setup __attribute__((aligned(128)));

#include "spe_pix_job.h"

// 1 ms at the 79.8 MHz timebase: a fallback wake in case a reservation event is lost.
#define DEC_TICKS 79800

static void echo_put(uint64_t ea)
{
	mfc_putf((void*)&echo, ea, sizeof(echo), TAG_ECHO, 0, 0);
	px_wait(1u << TAG_ECHO);
}

// Boot test: 32 pattern words into RSX local memory, which the PPU reads back.
static void dma_test(const SpePixTex* q)
{
	static uint32_t pat[32] __attribute__((aligned(128)));
	for (uint32_t k = 0; k < 32; k++)
		pat[k] = 0x5EC00000u + (q->seq << 8) + k;
	mfc_put(pat, q->dst_ea, sizeof(pat), TAG_OUT, 0, 0);
	px_wait(1u << TAG_OUT);
}

static void note(volatile uint32_t* busy, volatile uint32_t* max, uint32_t dt)
{
	*busy += dt;
	if (dt > *max)
		*max = dt;
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_setup)
{
	mfc_get(&setup, arg_setup, sizeof(setup), TAG_ECHO, 0, 0);
	px_wait(1u << TAG_ECHO);
	echo.state = SPE_STATE_RUNNING;
	echo.ls_addr = (uint32_t)&echo;
	echo_put(arg_echo);

	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DEC_TICKS);
	uint32_t last_tex = 0, last_fb = 0;
	for (;;)
	{
		// The re-check after each getllar closes the race with a store that lands before the reservation.
		mfc_getllar((void*)&bell, arg_bell, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit)
			break;
		// The SH4 thread waits on a present; serve it first.
		if (bell.fb.seq != last_fb)
		{
			const SpePixFb q = bell.fb;
			last_fb = q.seq;
			const uint32_t t0 = spu_read_decrementer();
			fb_job(&q);
			note(&echo.fb_busy, &echo.fb_max, t0 - spu_read_decrementer());
			echo.fb_jobs++;
			echo.fb_seq = q.seq;
			echo_put(arg_echo);
			continue;
		}
		if (bell.tex.seq != last_tex)
		{
			const SpePixTex q = bell.tex;
			last_tex = q.seq;
			const uint32_t t0 = spu_read_decrementer();
			uint32_t res = 0;
			if (q.flags & SPEPIX_TEST)
				dma_test(&q);
			else
				res = tex_job(&q);
			note(&echo.tex_busy, &echo.tex_max, t0 - spu_read_decrementer());
			echo.tex_jobs++;
			echo.tex_res = res;
			echo.tex_seq = q.seq;
			echo_put(arg_echo);
			continue;
		}

		echo.stage = PX_STAGE_EVENT;
		const uint32_t ev = spu_read_event_status();
		echo.stage = PX_STAGE_RUN;
		spu_write_event_ack(ev);
		if (ev & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (ev & MFC_DECREMENTER_EVENT)
		{
			echo.timeouts++;
			spu_write_decrementer(DEC_TICKS);
		}
	}

	echo.state = SPE_STATE_EXITED;
	echo_put(arg_echo);
	spu_thread_exit(0);
	return 0;
}
