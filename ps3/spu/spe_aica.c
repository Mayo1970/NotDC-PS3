// OPTIMIZATION_PLAN 5.13 A1: sleeps on a reservation of the bell line, runs the AICA write log through the full
// caique model (spe_aica_model.cpp) and compares its control snapshot with the PPU's at every CHECK record.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include <stddef.h>
#include "spe_aica.h"

#define TAG_ECHO 1
#define TAG_LOG  2
#define TAG_BAD  3
#define DEC_TICKS 79800  // 1 ms at the 79.8 MHz timebase: fallback wake
#define REC_BATCH 64

void am_init(uint32_t aram_ea);
void am_reset(void);
void am_write(uint32_t sample, uint32_t off, uint32_t val);
uint32_t am_check(uint32_t sample, void* snap);
uint32_t am_samples(void);
uint32_t am_misses(void);
void am_prof(uint32_t* out);

static AicaBell bell;
static AicaEcho echo;
static AicaSetup setup;
static AicaRec recs[REC_BATCH] __attribute__((aligned(128)));
static AicaBad bad __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea, log_ea, bad_ea;

static void wait_tag(int tag)
{
	mfc_write_tag_mask(1 << tag);
	mfc_read_tag_status_all();
}

static void put_echo(void)
{
	echo.samples = am_samples();
	echo.misses = am_misses();
	am_prof((uint32_t*)echo.prof);
	mfc_put((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	wait_tag(TAG_ECHO);
}

static void do_check(const AicaRec* r)
{
	const uint32_t h = am_check(r->sample, bad.snap);
	echo.checks++;
	if (h == r->hash)
		return;
	echo.bad++;
	if (echo.bad_n >= AICA_BAD_MAX)
		return;
	bad.check = r->val;
	bad.ppu_hash = r->hash;
	bad.spu_hash = h;
	// Completes before bad_n moves, so the PPU never sees the count before the report.
	mfc_put(&bad, bad_ea + echo.bad_n * sizeof(AicaBad), sizeof(bad), TAG_ECHO, 0, 0);
	wait_tag(TAG_ECHO);
	echo.bad_n++;
}

static uint32_t run_recs(uint32_t t, uint32_t head)
{
	while (t != head)
	{
		const uint32_t idx = t & (AICA_LOG - 1);
		uint32_t n = head - t;
		if (n > REC_BATCH)
			n = REC_BATCH;
		if (n > AICA_LOG - idx)
			n = AICA_LOG - idx;
		mfc_get(recs, log_ea + idx * sizeof(AicaRec), n * sizeof(AicaRec), TAG_LOG, 0, 0);
		wait_tag(TAG_LOG);
		for (uint32_t i = 0; i < n; i++)
		{
			const AicaRec* r = &recs[i];
			if (r->op == AICA_OP_WRITE)
				am_write(r->sample, r->off, r->val);
			else if (r->op == AICA_OP_CHECK)
				do_check(r);
			else if (r->op == AICA_OP_RESET)
				am_reset();
		}
		t += n;
		echo.tail = t;
		put_echo();
	}
	return t;
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_shared)
{
	uint32_t tail = 0;

	bell_ea = arg_bell;
	echo_ea = arg_echo;
	log_ea = arg_shared + offsetof(AicaShared, log);
	bad_ea = arg_shared + offsetof(AicaShared, bad);
	mfc_get(&setup, arg_shared + offsetof(AicaShared, setup), sizeof(setup), TAG_LOG, 0, 0);
	wait_tag(TAG_LOG);
	am_init(setup.aram_ea);
	echo.state = AICA_STATE_RUNNING;
	echo.ls_addr = (uint32_t)&echo;
	put_echo();

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
		const uint32_t head = bell.head;
		if (head != tail)
		{
			const uint32_t t0 = spu_read_decrementer();
			tail = run_recs(tail, head);
			echo.busy += t0 - spu_read_decrementer();
			continue;
		}
		const uint32_t ev = spu_read_event_status();
		spu_write_event_ack(ev);
		if (ev & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (ev & MFC_DECREMENTER_EVENT)
		{
			echo.timeouts++;
			spu_write_decrementer(DEC_TICKS);
		}
	}

	echo.state = AICA_STATE_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
