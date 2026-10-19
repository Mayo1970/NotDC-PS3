// OPTIMIZATION_PLAN 5.12 #3: sleeps on a reservation of its bell line; each request from the render thread hashes
// the textures of one scene's params (spe_tex_hash.h). G17: RSX Direct jobs (rsxd_job.h) go first, also mid-hash.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include "spe_tex.h"

static SpeTexEcho echo __attribute__((aligned(128)));
static SpeTexBell bell __attribute__((aligned(128)));
static SpeTexSetup setup __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea;
static uint32_t job_last, dec_due;

static void job_poll(void);
#define TX_POLL() job_poll()
#define RSXD_BIG __attribute__((noinline))
#include "spe_tex_hash.h"
#include "rsxd_job.h"

// 1 ms at the 79.8 MHz timebase: a fallback wake in case a reservation event is lost.
#define DEC_TICKS 79800

static void echo_put(void)
{
	mfc_putf((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	tx_wait(1u << TAG_ECHO);
}

static void rj_get(void* ls, uint32_t ea, uint32_t bytes)
{
	mfc_get(ls, ea, bytes, RJ_TAG_IN, 0, 0);
	tx_wait(1u << RJ_TAG_IN);
}

static void rj_put(const void* ls, uint32_t ea, uint32_t bytes, uint32_t tag)
{
	if (bytes)
		mfc_put((volatile void*)ls, ea, bytes, tag, 0, 0);
}

static void rj_putf(const void* ls, uint32_t ea, uint32_t bytes, uint32_t tag)
{
	mfc_putf((volatile void*)ls, ea, bytes, tag, 0, 0);
}

static void rj_wait(uint32_t tag)
{
	tx_wait(1u << tag);
}

static uint32_t rj_ticks(void)
{
	return spu_read_decrementer();
}

static void job_run(void)
{
	job_last = bell.job_seq;
	const uint32_t t0 = spu_read_decrementer();
	rj_run(bell.job_ea);
	echo.jobs++;
	echo.job_busy += t0 - spu_read_decrementer();
	echo_put();
}

// Between two params of a hash: a write to the bell line ended the reservation; a new job runs now.
static void job_poll(void)
{
	if (!spu_stat_event_status())
		return;
	const uint32_t ev = spu_read_event_status();
	spu_write_event_ack(ev);
	// A decrementer write here would break the request's busy time: the main loop re-arms it before its wait.
	dec_due |= ev & MFC_DECREMENTER_EVENT;
	mfc_getllar((void*)&bell, bell_ea, 0, 0);
	mfc_read_atomic_status();
	if (bell.job_seq != job_last)
	{
		echo.job_preempt++;
		job_run();
	}
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_setup)
{
	bell_ea = arg_bell;
	echo_ea = arg_echo;
	mfc_get(&setup, arg_setup, sizeof(setup), TAG_ECHO, 0, 0);
	tx_wait(1u << TAG_ECHO);
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
		if (bell.job_seq != job_last)
		{
			job_run();
			continue;
		}
		if (bell.seq != last)
		{
			const SpeTexBell q = bell;
			last = q.seq;
			const uint32_t t0 = spu_read_decrementer(), j0 = echo.job_busy;
			run(&q);
			// The RSX Direct jobs run inside the hash count in job_busy, not in the request's time.
			const uint32_t dt = t0 - spu_read_decrementer() - (echo.job_busy - j0);
			echo.reqs++;
			echo.busy += dt;
			if (dt > echo.max_busy)
				echo.max_busy = dt;
			echo.seq = q.seq;
			echo_put();
			continue;
		}

		if (dec_due)
		{
			dec_due = 0;
			spu_write_decrementer(DEC_TICKS);
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
	echo_put();
	spu_thread_exit(0);
	return 0;
}
