// OPTIMIZATION_PLAN 5.12 S0: sleeps on a reservation of the bell line and echoes each new seq.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include "spe_bell.h"

#define TAG 1
// 1 ms at the 79.8 MHz timebase: a fallback wake that shows whether reservation events get lost.
#define DEC_TICKS 79800

static SpeBell bell;
static SpeEcho echo;

static void put_echo(uint64_t ea)
{
	mfc_put((void*)&echo, ea, sizeof(echo), TAG, 0, 0);
	mfc_write_tag_mask(1 << TAG);
	mfc_read_tag_status_all();
}

int main(uint64_t bell_ea, uint64_t echo_ea, uint64_t arg3, uint64_t arg4)
{
	uint32_t last = 0;
	int by_timeout = 0;

	echo.state = SPE_STATE_RUNNING;
	put_echo(echo_ea);

	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DEC_TICKS);
	for (;;)
	{
		// The re-check after each getllar closes the race with a store that lands before the reservation.
		mfc_getllar((void*)&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit)
			break;
		if (bell.seq != last)
		{
			if (by_timeout)
				echo.rescued++;
			last = bell.seq;
			echo.seq = last;
			echo.handled++;
			put_echo(echo_ea);
			by_timeout = 0;
			continue;
		}

		uint32_t ev = spu_read_event_status();
		spu_write_event_ack(ev);
		by_timeout = !(ev & MFC_LLR_LOST_EVENT);
		if (ev & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (ev & MFC_DECREMENTER_EVENT)
		{
			echo.timeouts++;
			spu_write_decrementer(DEC_TICKS);
			// Idle counters still reach the PPU about four times a second.
			if ((echo.timeouts & 255) == 0)
				put_echo(echo_ea);
		}
	}

	echo.state = SPE_STATE_EXITED;
	put_echo(echo_ea);
	spu_thread_exit(0);
	return 0;
}
