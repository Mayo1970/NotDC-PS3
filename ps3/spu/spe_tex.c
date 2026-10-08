// OPTIMIZATION_PLAN 5.12 #3: sleeps on a reservation of its bell line; each request from the render thread hashes
// the textures of one scene's params (spe_tex_hash.h) into the result slots ps3tex_bind reads.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include "spe_tex.h"

static SpeTexEcho echo __attribute__((aligned(128)));
static SpeTexBell bell __attribute__((aligned(128)));
static SpeTexSetup setup __attribute__((aligned(128)));

#include "spe_tex_hash.h"

// 1 ms at the 79.8 MHz timebase: a fallback wake in case a reservation event is lost.
#define DEC_TICKS 79800

static void echo_put(uint64_t ea)
{
	mfc_putf((void*)&echo, ea, sizeof(echo), TAG_ECHO, 0, 0);
	tx_wait(1u << TAG_ECHO);
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_setup)
{
	mfc_get(&setup, arg_setup, sizeof(setup), TAG_ECHO, 0, 0);
	tx_wait(1u << TAG_ECHO);
	echo.state = SPE_STATE_RUNNING;
	echo.ls_addr = (uint32_t)&echo;
	echo_put(arg_echo);

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
			const SpeTexBell q = bell;
			last = q.seq;
			const uint32_t t0 = spu_read_decrementer();
			run(&q);
			const uint32_t dt = t0 - spu_read_decrementer();
			echo.reqs++;
			echo.busy += dt;
			if (dt > echo.max_busy)
				echo.max_busy = dt;
			echo.seq = q.seq;
			echo_put(arg_echo);
			continue;
		}

		echo.stage = TX_STAGE_EVENT;
		const uint32_t ev = spu_read_event_status();
		echo.stage = TX_STAGE_RUN;
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
