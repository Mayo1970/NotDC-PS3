// OPTIMIZATION_PLAN 5.12 #2: sleeps on a reservation of its bell line; each request from the render thread
// builds one scene's vertex buffer and runs (spe_vb_build.h) into the vring block the request names.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include "spe_vb.h"

static SpeVbEcho echo __attribute__((aligned(128)));
static SpeVbBell bell __attribute__((aligned(128)));
static SpeVbSetup setup __attribute__((aligned(128)));
static SpeVbDiag diag __attribute__((aligned(128)));
static uint64_t echo_ea;

#include "spe_vb_build.h"

// 1 ms at the 79.8 MHz timebase: a fallback wake in case a reservation event is lost.
#define DEC_TICKS 79800

// The diagnostic split goes out first; the fenced echo put lands after it.
static void echo_put(void)
{
	mfc_put((void*)&diag, setup.diag_ea, sizeof(diag), TAG_ECHO, 0, 0);
	mfc_putf((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	vb_wait(1u << TAG_ECHO, VB_STAGE_OUT);
}

// The bench runs before any build, so it borrows the vertex window.
static uint32_t bench_puts(uint64_t ea, uint32_t n, uint32_t size, uint32_t step, int wait_each)
{
	const uint32_t t0 = spu_read_decrementer();
	for (uint32_t k = 0; k < n; k++)
	{
		mfc_put(ls_vb[0], ea + k * step, size, TAG_IN, 0, 0);
		if (wait_each)
			vb_wait(1u << TAG_IN, VB_STAGE_OUT);
	}
	vb_wait(1u << TAG_IN, VB_STAGE_OUT);
	return t0 - spu_read_decrementer();
}

// Boot bench: put bandwidth to RSX local and main memory (1 MB each), put and get round trips.
static void dma_bench(const SpeVbBell* q)
{
	static uint8_t gbuf[256] __attribute__((aligned(128)));
	diag.b_put_rsx = bench_puts(q->vb_ea, 64, 16384, 16384, 0);
	diag.b_put_xdr = bench_puts(q->ctx, 64, 16384, 16384, 0);
	diag.b_lat_rsx = bench_puts(q->vb_ea, 100, 128, 0, 1);
	diag.b_lat_xdr = bench_puts(q->ctx, 100, 128, 0, 1);
	const uint32_t t0 = spu_read_decrementer();
	for (uint32_t k = 0; k < 100; k++)
	{
		mfc_get(gbuf, q->ctx + 64, 192, TAG_IN, 0, 0);
		vb_wait(1u << TAG_IN, VB_STAGE_OUT);
	}
	diag.b_get_xdr = t0 - spu_read_decrementer();
	vb_wait(1u << TAG_ECHO, VB_STAGE_OUT);
	echo.seq = q->seq;
	echo.flags = SPEVB_FINAL;
	echo_put();
}

// Boot test: SPU DMA into RSX local memory, which the PPU reads back.
static void dma_test(const SpeVbBell* q)
{
	static uint32_t pat[32] __attribute__((aligned(128)));
	for (uint32_t k = 0; k < 32; k++)
		pat[k] = 0x5EB00000u + (q->seq << 8) + k;
	mfc_put(pat, q->vb_ea, sizeof(pat), TAG_IN, 0, 0);
	vb_wait((1u << TAG_IN) | (1u << TAG_ECHO), VB_STAGE_OUT);
	echo.seq = q->seq;
	echo.flags = SPEVB_FINAL;
	echo_put();
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_setup)
{
	// Double precision rounds to nearest; spe_vb_build.h builds its exact single results on it.
	spu_mtfpscr(spu_splats(0u));
	echo_ea = arg_echo;
	mfc_get(&setup, arg_setup, sizeof(setup), TAG_IN, 0, 0);
	vb_wait(1u << TAG_IN, VB_STAGE_IN);
	echo.state = SPE_STATE_RUNNING;
	echo.ls_addr = (uint32_t)&echo;
	echo_put();

	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DEC_TICKS);
	uint32_t last = 0;
	for (;;)
	{
		// The re-check after each getllar closes the race with a store that lands before the reservation.
		mfc_getllar((void*)&bell, arg_bell, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit)
			break;
		if (bell.seq != last)
		{
			const SpeVbBell q = bell;
			last = q.seq;
			const uint32_t t0 = spu_read_decrementer();
			if (q.flags & SPEVB_TEST)
				dma_test(&q);
			else if (q.flags & SPEVB_BENCH)
				dma_bench(&q);
			else
				build(&q);
			const uint32_t dt = t0 - spu_read_decrementer();
			vb_wait(1u << TAG_ECHO, VB_STAGE_OUT);
			echo.builds++;
			echo.busy += dt;
			if (dt > echo.max_busy)
				echo.max_busy = dt;
			if (!(q.flags & (SPEVB_TEST | SPEVB_BENCH)))
			{
				diag.t_build += dt;
				diag.n_build++;
			}
			echo_put();
			continue;
		}

		echo.stage = VB_STAGE_EVENT;
		const uint32_t ev = spu_read_event_status();
		echo.stage = VB_STAGE_RUN;
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
	echo_put();
	spu_thread_exit(0);
	return 0;
}
