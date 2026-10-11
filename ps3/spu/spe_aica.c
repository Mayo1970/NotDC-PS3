// OPTIMIZATION_PLAN 5.13 A1/A1b/A2: sleeps on a reservation of the bell line, runs the AICA write log through the caique
// model and compares its control snapshot with the PPU's at every CHECK. Sound mode: its output goes to the audio ring.
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

void am_init(const AicaSetup* su, uint64_t dbad_ea, uint64_t isabad_ea, uint64_t cdda_ea, volatile uint32_t* where);
void am_dsp_selftest(void);
void am_dsp_stats(uint32_t* out);
void am_reset(void);
void am_write(uint32_t sample, uint32_t off, uint32_t val);
uint32_t am_check(uint32_t sample, void* snap);
void am_cdda(uint32_t sample, uint32_t val);
void am_batch(uint32_t gain, uint32_t peak_seq);
void am_sync(uint32_t sample);
void am_ring(uint32_t sample, uint32_t lo, uint32_t hi);
uint32_t am_dsp_read(uint32_t sample, uint32_t off);
void am_hz_stats(uint32_t* out);
void am_snd_flush(void);
void am_snd_stats(uint32_t* out);
uint32_t am_samples(void);
uint32_t am_misses(void);
void am_prof(uint32_t* out);
extern char _end[];

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
	am_dsp_stats((uint32_t*)&echo.dsp_flags);
	am_snd_stats((uint32_t*)&echo.snd_frames);
	am_hz_stats((uint32_t*)echo.clip);
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
			{
				do_check(r);
				am_snd_flush();   // sound mode: the block's frames to the ring
			}
			else if (r->op == AICA_OP_CDDA)
				am_cdda(r->sample, r->val);
			else if (r->op == AICA_OP_RESET)
				am_reset();
			else if (r->op == AICA_OP_SYNC)
			{
				am_sync(r->sample);
				echo.sync = r->val;
			}
			else if (r->op == AICA_OP_RING)
				am_ring(r->sample, r->val & 0xFFFF, r->val >> 16);
			else if (r->op == AICA_OP_DSPRD)
				echo.dsp_rd = (r->val << 16) | (am_dsp_read(r->sample, r->off) & 0xFFFF);
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
	am_init(&setup, arg_shared + offsetof(AicaShared, dbad), arg_shared + offsetof(AicaShared, isabad),
	        arg_shared + offsetof(AicaShared, cdda), &echo.phase);
	echo.state = AICA_STATE_RUNNING;
	echo.ls_addr = (uint32_t)&echo;
	echo.ls_end = (uint32_t)_end;
	put_echo();
	// Before the first record: the PPU only queues writes meanwhile.
	am_dsp_selftest();
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
			am_batch(bell.gain, bell.peak_seq);
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
