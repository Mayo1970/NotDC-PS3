// OPTIMIZATION_PLAN 5.12 S3: sleeps on a reservation of the bell line, runs the command ring, pulls each raw
// TA range into the decoder (spe_dec.c) and answers every FENCE with the context's position and list counts.
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include <stddef.h>
#include "spe_bell.h"
#include "spe_dec.h"

#define TAG_ECHO 1
#define TAG_CMD  2
#define TAG_BUF  3  // and 4: one per raw buffer
// 1 ms at the 79.8 MHz timebase: a fallback wake that shows whether reservation events get lost.
#define DEC_TICKS 79800
#define CMD_BATCH 64
#define CHUNK     16384  // largest single MFC transfer

typedef struct
{
	uint32_t pos, ncont, gaps;
	uint32_t need;  // last fence of this context; S3 keeps its lists until the render thread releases it
} CtxState;

static SpeBell bell;
SpeEcho echo;
static SpeSetup setup;
static SpeCmd cmds[CMD_BATCH] __attribute__((aligned(128)));
static SpeFence res __attribute__((aligned(128)));
static uint8_t buf[2][CHUNK] __attribute__((aligned(128)));
static CtxState ctx[SPE_CTX];
static uint64_t bell_ea, echo_ea, ring_ea, results_ea;
static int dec_fired;  // a decrementer event taken by wait_release; the main loop re-arms it

static void wait_tag(int tag, uint32_t stage)
{
	echo.stage = stage;
	mfc_write_tag_mask(1 << tag);
	mfc_read_tag_status_all();
	echo.stage = SPE_STAGE_RUN;
}

static void put_echo(void)
{
	mfc_put((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	wait_tag(TAG_ECHO, SPE_STAGE_ECHO);
}

// Double buffered: the next chunk is in flight while this one is decoded.
static void do_data(const SpeCmd* m)
{
	CtxState* c = &ctx[m->ctx & (SPE_CTX - 1)];
	uint32_t left = m->a;
	if (m->b - left != c->pos)
	{
		c->gaps++;
		echo.gaps++;
	}
	c->pos = m->b;
	echo.data_bytes += left;
	if (!left)
		return;

	uint64_t ea = m->ea;
	uint32_t n = left < CHUNK ? left : CHUNK;
	int cur = 0;
	echo.dma_ea = (uint32_t)ea;
	echo.dma_size = n;
	mfc_get(buf[0], ea, n, TAG_BUF, 0, 0);
	while (left)
	{
		const uint32_t rest = left - n;
		const uint32_t nn = rest < CHUNK ? rest : CHUNK;
		if (nn)
			mfc_get(buf[cur ^ 1], ea + n, nn, TAG_BUF + (cur ^ 1), 0, 0);
		echo.dma_ea = (uint32_t)ea;
		echo.dma_size = n;
		wait_tag(TAG_BUF + cur, SPE_STAGE_DATA);
		dec_records(m->ctx, (const uint32_t*)buf[cur], n / 32);
		ea += n;
		left = rest;
		n = nn;
		cur ^= 1;
	}
}

// The fenced put keeps the echo behind the result, so the PPU never sees the id before the data.
static void do_fence(const SpeCmd* m)
{
	CtxState* c = &ctx[m->ctx & (SPE_CTX - 1)];
	c->need = m->a;
	dec_fence(m->ctx, &res);
	res.id = m->a;
	res.ctx = m->ctx;
	res.pos = c->pos;
	res.ncont = c->ncont;
	res.gaps = c->gaps;
	echo.dma_ea = (uint32_t)(results_ea + (m->a % SPE_RESULTS) * sizeof(SpeFence));
	echo.dma_size = sizeof(res);
	mfc_put(&res, results_ea + (m->a % SPE_RESULTS) * sizeof(SpeFence), sizeof(res), TAG_ECHO, 0, 0);
	echo.fence = m->a;
	mfc_putf((void*)&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	wait_tag(TAG_ECHO, SPE_STAGE_FENCE);
}

// S3: the render thread may still draw the lists a RESET would overwrite. Its release store kills the
// reservation, as a bell ring does. The decrementer is not written here, so the busy count stays valid.
static void wait_release(uint32_t need)
{
	if (!need)
		return;
	const uint32_t t0 = spu_read_decrementer();
	int waited = 0;
	for (;;)
	{
		mfc_getllar((void*)&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.quit || (int32_t)(bell.released - need) >= 0)
			break;
		waited = 1;
		echo.stage = SPE_STAGE_RELEASE;
		const uint32_t ev = spu_read_event_status();
		echo.stage = SPE_STAGE_RUN;
		spu_write_event_ack(ev);
		if (ev & MFC_DECREMENTER_EVENT)
			dec_fired = 1;
	}
	if (waited)
	{
		echo.rel_waits++;
		echo.rel_ticks += t0 - spu_read_decrementer();
	}
}

static void exec_cmd(const SpeCmd* m)
{
	CtxState* c = &ctx[m->ctx & (SPE_CTX - 1)];
	echo.cmd_op = m->op;
	echo.cmd_ctx = m->ctx;
	echo.cmd_ea = m->ea;
	echo.cmd_a = m->a;
	echo.cmd_b = m->b;
	switch (m->op)
	{
	case SPE_OP_DATA:
		do_data(m);
		break;
	case SPE_OP_RESET:
		if (setup.draw)
			wait_release(c->need);
		c->pos = c->ncont = c->gaps = 0;
		dec_reset(m->ctx, m->a);
		break;
	case SPE_OP_CONT:
		if (m->b != c->pos)
			echo.cont_errs++;
		c->ncont++;
		break;
	case SPE_OP_FENCE:
		do_fence(m);
		break;
	default:
		// SOFT: the PPU replay never runs the decoder's SoftReset either.
		break;
	}
}

// The tail goes out after every batch, so a PPU waiting on a full ring sees space early.
static uint32_t run_cmds(uint32_t t, uint32_t head)
{
	while (t != head)
	{
		const uint32_t idx = t & (SPE_RING - 1);
		uint32_t n = head - t;
		if (n > CMD_BATCH)
			n = CMD_BATCH;
		if (n > SPE_RING - idx)
			n = SPE_RING - idx;
		echo.dma_ea = (uint32_t)(ring_ea + idx * sizeof(SpeCmd));
		echo.dma_size = n * sizeof(SpeCmd);
		mfc_get(cmds, ring_ea + idx * sizeof(SpeCmd), n * sizeof(SpeCmd), TAG_CMD, 0, 0);
		wait_tag(TAG_CMD, SPE_STAGE_RING);
		for (uint32_t i = 0; i < n; i++)
			exec_cmd(&cmds[i]);
		t += n;
		echo.tail = t;
		put_echo();
	}
	return t;
}

static void on_timeout(void)
{
	echo.timeouts++;
	spu_write_decrementer(DEC_TICKS);
	// Idle counters still reach the PPU about four times a second.
	if ((echo.timeouts & 255) == 0)
		put_echo();
}

// A ring that lands after a decrementer wake still raises its own lost event: that is no rescue.
static int llr_pending(void)
{
	if (!spu_stat_event_status())
		return 0;
	const uint32_t ev = spu_read_event_status();
	spu_write_event_ack(ev);
	if (ev & MFC_DECREMENTER_EVENT)
		on_timeout();
	if (ev & MFC_LLR_LOST_EVENT)
	{
		echo.wakes++;
		return 1;
	}
	return 0;
}

int main(uint64_t arg_bell, uint64_t arg_echo, uint64_t arg_shared)
{
	uint32_t tail = 0;
	int by_timeout = 0;

	// Double precision rounds to nearest; spe_dec.c builds its exact single results on it.
	spu_mtfpscr(spu_splats(0u));
	bell_ea = arg_bell;
	echo_ea = arg_echo;
	ring_ea = arg_shared + offsetof(SpeShared, ring);
	results_ea = arg_shared + offsetof(SpeShared, results);
	mfc_get(&setup, arg_shared + offsetof(SpeShared, setup), sizeof(setup), TAG_CMD, 0, 0);
	wait_tag(TAG_CMD, SPE_STAGE_RING);
	dec_init(&setup);
	echo.state = SPE_STATE_RUNNING;
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
			if (by_timeout && !llr_pending())
				echo.rescued++;
			by_timeout = 0;
			echo.head = head;
			echo.handled++;
			const uint32_t t0 = spu_read_decrementer(), r0 = echo.rel_ticks;
			tail = run_cmds(tail, head);
			echo.busy += (t0 - spu_read_decrementer()) - (echo.rel_ticks - r0);
			if (dec_fired)
			{
				dec_fired = 0;
				on_timeout();
			}
			continue;
		}

		echo.stage = SPE_STAGE_EVENT;
		const uint32_t ev = spu_read_event_status();
		echo.stage = SPE_STAGE_RUN;
		spu_write_event_ack(ev);
		by_timeout = !(ev & MFC_LLR_LOST_EVENT);
		if (ev & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (ev & MFC_DECREMENTER_EVENT)
			on_timeout();
	}

	echo.state = SPE_STATE_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
