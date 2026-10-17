// P95 (WSL): RTT Async and TA Context Split. The job slot, the waiting RTT, the waits and the context moves of
// ps3_rend.cpp (cut by run.sh) run on pthreads; a model checks job order, streams, lists, registers and waits.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <atomic>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;

#include "enum.inc"

static void fail(const char* what, unsigned a = 0, unsigned b = 0)
{
	printf("FAIL: %s (%u, %u)\n", what, a, b);
	fflush(stdout);
	_exit(1);
}

// -- The globals of ps3_rend.cpp that the cut code reads, with the model's fields in the context.
#define TA_CONTEXTS 4
#define RAW_BYTES 4096
struct TaContext
{
	u32 addr;
	u32 raw_used, n_cont;
	u8 raw[RAW_BYTES];
	bool posted;
	u32 list;   // the list id the stream holds
	u32 lists;  // the list id the decoded lists hold
};
struct RenderRegs
{
	u32 tag;
	u32 pad[63];
};
typedef u32 sys_mutex_t;
typedef u32 sys_cond_t;
typedef u64 sys_ppu_thread_t;

static TaContext s_ctx[TA_CONTEXTS];
static TaContext* s_ta = &s_ctx[0];
static TaContext* s_rc = &s_ctx[0];
static sys_ppu_thread_t s_rend_tid;
static sys_mutex_t s_rmx;
static sys_cond_t s_job_cv, s_done_cv;
static bool s_rend_running;
static volatile int s_rend_quit;
static int s_job_type;
static bool s_job_pending, s_job_busy, s_job_decoded;
static TaContext* s_job_ctx;
static TaContext* s_busy_ctx;
static u32 s_job_raw_end, s_job_cont_end, s_job_fence;
static u64 s_snap_hold, s_job_hold;
static bool s_q_valid;
static TaContext* s_q_ctx;
static u32 s_q_raw_end, s_q_cont_end, s_q_fence;
static RenderRegs s_q_rr;
static u32 s_stat_rtt_q, s_stat_rtt_direct, s_stat_ctx_moves, s_stat_ctx_full;
static double s_stat_rtt_sh4;
static u32 s_stat_dropped, s_stat_fb_defer;
static double s_stat_wait;
static bool s_flip_want;
static RenderRegs g_rr, s_snap, s_job_rr;
static int g_rtt_async_preset, g_ta_ctx_split_preset;
static bool s_ta_fast = true;
static u32 TA_ISP_BASE;

// -- PSL1GHT shims on pthreads: a cond waits on the mutex it was made with.
struct sys_mutex_attr_t { int x; };
struct sys_cond_attr_t { int attr_pshared; };
#define sysMutexAttrInitialize(a) ((a).x = 0)
#define sysCondAttrInitialize(a) ((a).attr_pshared = 1)
#define THREAD_JOINABLE 1
#define SHIMS 64
static pthread_mutex_t g_mx[SHIMS];
static pthread_cond_t g_cv[SHIMS];
static u32 g_cv_mx[SHIMS], g_nmx, g_ncv, g_nth;
static pthread_t g_th[SHIMS];
static __thread bool t_in_listinit;
static std::atomic<u32> g_li_waits;   // SH4 waits on s_done_cv inside a ListInit

static s32 sysMutexCreate(sys_mutex_t* m, sys_mutex_attr_t*)
{
	if (g_nmx >= SHIMS)
		fail("shim mutexes");
	pthread_mutex_init(&g_mx[g_nmx], 0);
	*m = g_nmx++;
	return 0;
}
static s32 sysMutexDestroy(sys_mutex_t) { return 0; }
static s32 sysMutexLock(sys_mutex_t m, u64) { return pthread_mutex_lock(&g_mx[m]); }
static s32 sysMutexUnlock(sys_mutex_t m) { return pthread_mutex_unlock(&g_mx[m]); }
static s32 sysCondCreate(sys_cond_t* c, sys_mutex_t m, sys_cond_attr_t*)
{
	if (g_ncv >= SHIMS)
		fail("shim conds");
	pthread_cond_init(&g_cv[g_ncv], 0);
	g_cv_mx[g_ncv] = m;
	*c = g_ncv++;
	return 0;
}
static s32 sysCondDestroy(sys_cond_t) { return 0; }
static s32 sysCondWait(sys_cond_t c, u64 us)
{
	if (t_in_listinit && c == s_done_cv)
		g_li_waits++;
	if (!us)
		return pthread_cond_wait(&g_cv[c], &g_mx[g_cv_mx[c]]);
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += (long)(us % 1000000) * 1000;
	ts.tv_sec += (time_t)(us / 1000000) + ts.tv_nsec / 1000000000;
	ts.tv_nsec %= 1000000000;
	pthread_cond_timedwait(&g_cv[c], &g_mx[g_cv_mx[c]], &ts);
	return 0;
}
static s32 sysCondSignal(sys_cond_t c) { return pthread_cond_signal(&g_cv[c]); }
static s32 sysCondBroadcast(sys_cond_t c) { return pthread_cond_broadcast(&g_cv[c]); }
struct Tramp { void (*fn)(void*); void* arg; };
static void* tramp(void* p)
{
	Tramp t = *(Tramp*)p;
	delete (Tramp*)p;
	t.fn(t.arg);
	return 0;
}
static s32 sysThreadCreate(sys_ppu_thread_t* id, void (*fn)(void*), void* arg, s32, u64, u64, char*)
{
	if (g_nth >= SHIMS)
		fail("shim threads");
	if (pthread_create(&g_th[g_nth], 0, tramp, new Tramp{ fn, arg }) != 0)
		return -1;
	*id = g_nth++;
	return 0;
}
static s32 sysThreadJoin(sys_ppu_thread_t id, u64*) { return pthread_join(g_th[id], 0); }
static void sysThreadExit(u64) { pthread_exit(0); }

// -- Small per-thread random numbers and delays.
static __thread u32 t_rng = 1;
static u32 rnd(u32 n)
{
	t_rng ^= t_rng << 13;
	t_rng ^= t_rng >> 17;
	t_rng ^= t_rng << 5;
	return n ? t_rng % n : 0;
}
static void jitter(u32 max_us)
{
	const u32 r = rnd(10);
	if (r < 6)
		return;
	if (r < 9)
		sched_yield();
	else
		usleep(rnd(max_us));
}

// -- The model: one record per fence (posted RENDER or RTT job), in post order.
#define MAX_JOBS (1u << 21)
struct Job
{
	int type;
	TaContext* ctx;
	u32 list, raw_end, tag;
	bool queued;
	std::atomic<int> stage;  // 0 posted, 1 replayed, 2 drawn, 3 released
};
static Job* g_jobs;
static std::atomic<u32> g_fence, g_last_release, g_progress, g_flips;
static u32 g_next_decode = 1, r_cur;
static int g_posting = JOB_NONE;
static std::atomic<u32> g_present_tag;
static int g_split_now;
static u32 g_full_before;

static int g_rend_on = 1, g_policy;
static int get_render_thread_preset() { return g_rend_on; }
static int get_drop_policy_preset() { return g_policy; }
static double os_GetSeconds()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static void spe_flush(TaContext*) {}
static void ta_gate_set(bool) {}
static void ta_store_reset() {}
static struct { void ListInit() {} } TaStore;
static void ps3_video_flip_held() { g_flips++; }

// A job the SH4 posts takes a fence under s_rmx; a post behind the job in flight is the waiting RTT.
static u32 spe_fence(TaContext* c)
{
	const u32 id = ++g_fence;
	if (id >= MAX_JOBS)
		fail("too many jobs");
	Job& j = g_jobs[id];
	j.type = g_posting;
	j.ctx = c;
	j.list = c ? c->list : 0;
	j.raw_end = c ? c->raw_used : 0;
	j.tag = s_snap.tag;
	j.queued = s_rend_running && s_job_busy;
	j.stage = 0;
	return id;
}

static void ps3spe_release(u32 id)
{
	if (!id)
		return;
	if (id != g_last_release + 1)
		fail("a fence released out of order", id, g_last_release);
	if (g_jobs[id].stage != 2)
		fail("a fence released before its draw", id, g_jobs[id].stage);
	g_jobs[id].stage = 3;
	g_last_release = id;
}

// No job that is not replayed yet may lose its stream; with TA Context Split ON no held one either.
static void reset_raw(TaContext* c)
{
	const u32 last = g_fence;
	for (u32 id = g_last_release + 1; id <= last; id++)
	{
		const Job& j = g_jobs[id];
		if (j.ctx != c)
			continue;
		if (j.stage < 1)
			fail("a stream reset before its job's replay", id);
		if (j.stage < 3 && g_split_now && s_rend_running && s_stat_ctx_full == g_full_before)
			fail("TA Context Split ON: a held context reset while a free one was there", id);
	}
	c->raw_used = 0;
	c->n_cont = 0;
	c->posted = false;
	c->list = 0;
	memset(c->raw, 0xEE, sizeof(c->raw));
}

// P87's slot follows s_ta; ramsq_check and taslot_check test it.
static void ta_slot_sync() {}

static void reset_lists(TaContext* c)
{
	const u32 last = g_fence;
	for (u32 id = g_last_release + 1; id <= last; id++)
		if (g_jobs[id].ctx == c && g_jobs[id].stage < 3)
			fail("lists reset under a job", id, g_jobs[id].stage);
	c->lists = 0;
}

static void rth_mark_decoded();

static void rth_job_decode(TaContext* c, u32 raw_end, u32, u32 fence)
{
	if (fence != g_next_decode)
		fail("jobs out of order: replayed, expected", fence, g_next_decode);
	g_next_decode++;
	Job& j = g_jobs[fence];
	if (j.ctx != c || j.raw_end != raw_end)
		fail("a job's context or stream end changed", fence, raw_end);
	if (j.stage != 0)
		fail("a job replayed twice", fence);
	for (u32 o = 0; o < raw_end; o += 32)
		if (*(const u32*)&c->raw[o] != j.list)
			fail("a job's stream changed before its replay", fence, o);
	jitter(200);
	c->lists = j.list;
	s_rc = c;
	j.stage = 1;
	rth_mark_decoded();
	r_cur = fence;
}

static void rth_job_draw(int type, u64)
{
	if (type == JOB_RENDER || type == JOB_RTT)
	{
		Job& j = g_jobs[r_cur];
		if (j.type != type)
			fail("a job drawn as another type", r_cur, type);
		if (g_rr.tag != j.tag)
			fail("a job drawn with another job's registers", r_cur, g_rr.tag);
		if (j.stage != 1)
			fail("a job drawn before its replay", r_cur, j.stage);
		jitter(400);
		if (j.ctx->lists != j.list)
			fail("a job's lists changed in its draw", r_cur, j.ctx->lists);
		j.stage = 2;
	}
	else if (type == JOB_PRESENT && g_rr.tag != g_present_tag)
		fail("a present drawn with another job's registers", g_rr.tag, g_present_tag);
	else if (type == JOB_FLIP)
		g_flips++;
	g_progress++;
}

#include "extracted.inc"

// -- The SH4 side of the model.
// 0-4 for the patterns; 5-8 a new buffer each time for the redraw pattern.
#define ADDRS 9
static const u32 k_addr[ADDRS] = { 0x100000, 0x200000, 0x300000, 0x400000, 0x500000,
                                   0x600000, 0x700000, 0x800000, 0x900000 };
static u32 g_latest[ADDRS], g_list_id, g_tag;
// A list no STARTRENDER took yet; a list whose slot the old new-buffer rule gave away (it skips a render).
static bool g_pending[ADDRS], g_lost[ADDRS];
static u32 q_seen, direct_seen, early, moves_seen, blocked_ok, held_moves, lost_seen, taken_moves, redraws;
static bool async_ran, split_ran;

static int addr_index(u32 addr)
{
	for (u32 i = 0; i < ADDRS; i++)
		if (k_addr[i] == addr)
			return (int)i;
	return -1;
}

static void model_list_init(u32 ai)
{
	const u32 addr = k_addr[ai];
	TaContext* before = find_context(addr);
	u32 old_addr[TA_CONTEXTS];
	for (u32 i = 0; i < TA_CONTEXTS; i++)
		old_addr[i] = s_ctx[i].addr;
	TA_ISP_BASE = addr | (rnd(64) << 2);  // low bits the 1 MB key drops
	g_split_now = g_ta_ctx_split_preset;
	g_full_before = s_stat_ctx_full;
	const u32 waits0 = g_li_waits;
	t_in_listinit = true;
	ListInit();
	t_in_listinit = false;
	if (find_context(addr) != s_ta)
		fail("the TA's context is not the one of its address", ai);
	const bool moved = before && s_ta != before;
	if (moved)
	{
		moves_seen++;
		if (!g_split_now)
			fail("a context moved with TA Context Split OFF", ai);
		// The second choice: a slot another buffer's taken list had.
		taken_moves += old_addr[s_ta - s_ctx] != 0xFFFFFFFF;
	}
	g_pending[ai] = false;
	for (u32 i = 0; i < TA_CONTEXTS; i++)
	{
		const int oi = addr_index(old_addr[i]);
		if (oi < 0 || s_ctx[i].addr == old_addr[i] || !g_pending[oi])
			continue;
		if (moved && &s_ctx[i] == s_ta)
			fail("a move took a list no STARTRENDER took", (u32)oi, i);
		g_pending[oi] = false;
		g_lost[oi] = true;
		lost_seen++;
	}
	if (g_split_now && s_rend_running && g_li_waits != waits0 && s_stat_ctx_full == g_full_before)
		fail("TA Context Split ON: a list init waited with a free context", ai);
	// The TA's records: each holds the list id, so a replay sees a later list's bytes.
	const u32 id = ++g_list_id;
	const u32 last = g_fence;
	for (u32 f = g_last_release + 1; f <= last; f++)
		if (g_jobs[f].ctx == s_ta && g_jobs[f].stage < 1)
			fail("the TA writes into a stream a job has not replayed", f);
	const u32 n = 1 + rnd(RAW_BYTES / 32 - 1);
	for (u32 i = 0; i < n; i++)
	{
		*(u32*)&s_ta->raw[s_ta->raw_used] = id;
		s_ta->raw_used += 32;
	}
	s_ta->list = id;
	g_latest[ai] = id;
	g_pending[ai] = true;
	g_lost[ai] = false;
}

// False = a dropped render (Frame Drop), or a list the old new-buffer rule gave away.
static bool model_render(u32 ai, bool rtt)
{
	if (g_lost[ai])
		return false;
	g_pending[ai] = false;
	TaContext* rc = find_context(k_addr[ai]);
	if (!rc)
		fail("no context for a list's address", ai);
	if (rc->list != g_latest[ai])
		fail("the context of an address holds another list", rc->list, g_latest[ai]);
	s_snap.tag = ++g_tag;
	g_posting = rtt ? JOB_RTT : JOB_RENDER;
	const u32 f0 = g_fence;
	const bool ok = rth_run(g_posting, rc);
	const u32 f1 = g_fence;
	if (!ok)
	{
		if (rtt)
			fail("an RTT was dropped");
		if (f1 != f0)
			fail("a dropped render took a fence");
		return false;
	}
	if (f1 != f0 + 1)
		fail("a posted job took no fence or two", f0, f1);
	const Job& j = g_jobs[f1];
	if (!rtt || !s_rend_running)
		return true;
	if (!g_rtt_async_preset)
	{
		if (j.stage < 2)
			fail("RTT Async OFF: the SH4 ran on before its RTT was drawn", f1, j.stage);
		blocked_ok++;
		return true;
	}
	async_ran = true;
	if (j.queued)
		q_seen++;
	else
		direct_seen++;
	if (j.stage < 2)
		early++;
	return true;
}

static void model_present()
{
	if (rth_busy())
		return;
	s_snap.tag = ++g_tag;
	g_present_tag = s_snap.tag;
	g_posting = JOB_PRESENT;
	rth_run(JOB_PRESENT, 0);
}

// One frame of one of five patterns; the rows and the drop policy change every few frames (they are live).
static void model_frame(u32 f)
{
	if (f % 37 == 0)
	{
		g_rtt_async_preset = (int)rnd(2);
		g_ta_ctx_split_preset = (int)rnd(2);
		g_policy = (int)rnd(3);
	}
	if (g_ta_ctx_split_preset && s_rend_running)
		split_ran = true;
	const u32 moves0 = moves_seen;
	const u32 pick = rnd(26);
	switch (pick < 6 ? 0 : pick < 10 ? 1 : pick < 15 ? 2 : pick < 20 ? 3 : pick < 22 ? 5 : pick < 24 ? 6 : 4)
	{
	case 0:  // Rez: the main scene and the RTT in one buffer
		model_list_init(0);
		model_render(0, false);
		model_list_init(0);
		model_render(0, true);
		break;
	case 1:  // the RTT first, in its own buffer
		model_list_init(2);
		model_render(2, true);
		model_list_init(0);
		model_render(0, false);
		break;
	case 2:  // two buffers, one a frame, a redraw now and then
	{
		const u32 a = f & 1;
		model_list_init(a);
		model_render(a, rnd(4) == 0);
		if (rnd(5) == 0)
			model_render(a, true);
		break;
	}
	case 3:  // a list waits unrendered while a buffer in a job gets its next list; a drop leaves the slot free
		model_list_init(0);
		if (!model_render(0, false))
			break;
		model_list_init(1);
		model_list_init(0);
		model_render(0, true);
		model_render(1, rnd(2) == 0);
		break;
	case 5:  // three lists wait while the Rez pair runs: a move may find no slot
		model_list_init(1);
		model_list_init(2);
		model_list_init(3);
		model_list_init(0);
		model_render(0, false);
		model_list_init(0);
		model_render(0, true);
		for (u32 a = 1; a <= 3; a++)
			model_render(a, rnd(2) == 0);
		break;
	case 6:  // a redraw after a new buffer's list init: the slot of the last replay stays
	{
		model_list_init(3);
		if (!model_render(3, false))
			break;
		rth_wait_idle();
		const u32 n = 5 + redraws % 4;
		model_list_init(n);
		model_render(3, rnd(2) == 0);
		model_render(n, false);
		redraws++;
		break;
	}
	default:  // every buffer, one list each
		for (u32 k = 0; k < 2 + rnd(3); k++)
		{
			const u32 a = rnd(5);
			model_list_init(a);
			model_render(a, rnd(2) == 0);
		}
		break;
	}
	held_moves += moves_seen != moves0;
	if (rnd(4) == 0)
		model_present();
	if (rnd(6) == 0)
		rth_flip_held();
	if (rnd(50) == 0)
	{
		rth_wait_idle();
		if (s_rend_running && (s_q_valid || s_job_busy))
			fail("the render thread is idle with a job left");
	}
	g_progress++;
}

static void* watchdog(void*)
{
	u32 last = g_progress, still = 0;
	for (;;)
	{
		sleep(1);
		const u32 now = g_progress;
		still = now == last ? still + 1 : 0;
		last = now;
		if (still >= 10)
			fail("no progress for 10 s: a deadlock", now);
	}
	return 0;
}

int main(int argc, char** argv)
{
	const u32 frames = argc > 1 ? (u32)atoi(argv[1]) : 20000;
	t_rng = argc > 2 ? (u32)atoi(argv[2]) * 2654435761u + 1 : 1;
	g_jobs = new Job[MAX_JOBS];
	for (u32 i = 0; i < TA_CONTEXTS; i++)
		s_ctx[i].addr = 0xFFFFFFFF;
	pthread_t wd;
	pthread_create(&wd, 0, watchdog, 0);
	if (!rth_start())
		fail("the render thread did not start");
	for (u32 f = 0; f < frames; f++)
	{
		// The middle tenth runs with the render thread off: every job inline.
		if (f == frames * 6 / 10)
		{
			rth_stop();
			g_rend_on = 0;
			if (rth_start())
				fail("the render thread started with its row off");
		}
		else if (f == frames * 7 / 10)
		{
			g_rend_on = 1;
			if (!rth_start())
				fail("the render thread did not start again");
		}
		model_frame(f);
	}
	rth_wait_idle();
	if (s_q_valid || s_job_busy || s_flip_want)
		fail("work left at the end", s_q_valid, s_job_busy);
	rth_stop();
	if (g_next_decode != g_fence + 1 || g_last_release != g_fence)
		fail("a posted job was not replayed or released", g_next_decode, g_last_release);
	if (s_stat_rtt_q != q_seen || s_stat_rtt_direct != direct_seen)
		fail("RTT counts: queued and direct", s_stat_rtt_q, s_stat_rtt_direct);
	if (s_stat_ctx_moves != moves_seen)
		fail("context moves counted", s_stat_ctx_moves, moves_seen);
	if (frames >= 2000 && (!async_ran || !q_seen || !early || !blocked_ok))
		fail("too little tested: async, queued, early, blocking", q_seen, early);
	if (frames >= 2000 && (!split_ran || !held_moves || !taken_moves || !s_stat_ctx_full || !redraws))
		fail("too little tested: moves, moves to a taken list's slot, full", held_moves, taken_moves);
	printf("rttq: %u frames, %u jobs (%u RTT queued, %u direct, %u back early, %u blocking), %u moves (%u to a "
	       "taken list's slot), %u full, %u list-init waits, %u lists lost by the new-buffer rule, %u flips: PASS\n",
	       frames, (u32)g_fence, q_seen, direct_seen, early, blocked_ok, moves_seen, taken_moves, s_stat_ctx_full,
	       (u32)g_li_waits, lost_seen, (u32)g_flips);
	return 0;
}
