// OPTIMIZATION_PLAN 5.14: sound thread. The queue has one producer (the SH4 thread) and one consumer (the token
// holder); entries run in order and each exactly once, so the sound side does the same work as before.
#include "ps3_sndthr.h"
#include "ps3_arm_spe.h"
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/sem.h>
#include <sys/systime.h>
#include "ps3_timebase.h"

extern "C" int get_sound_thread_preset();
extern "C" int get_cdda_preset();
void FASTCALL gdrom_get_cdda(s16* sector);   // dc/gdrom/gdromv3.cpp
void arm_aica_run(u32 cycles, u32 samples);  // plugs/vbaARM/arm_aica.cpp
bool aica_sh4_irq_apply(bool on);            // plugs/nullAICA/aica.cpp

#define RING         128   // queued MediumUpdates, power of two
#define POST_FILL    16    // the SH4 wakes a sleeping worker
#define HELP_FILL    32    // the SH4 runs the queue when the worker is not on it (~1.5 ms of guest time at 150 MHz)
#define CRIT_FILL    (RING - 8)
#define CDDA_SLOTS   8
#define CDDA_WORDS   (2352 / 2)
#define CDDA_SAMPLES 588   // cdda_refill takes one 2352-byte sector per 588 AICA samples
// The SH4 above the worker, the worker above the render thread (1000): it never waits behind a render job.
// If the SH4 priority cannot change, the worker goes below both (1001 is the boot priority of the SH4).
#define PRIO_SH4        998
#define PRIO_WORKER     999
#define PRIO_WORKER_LOW 1002

enum { OWN_FREE, OWN_SH4, OWN_WORKER };

struct Entry { u32 cycles, samples; };

bool g_snd_thread;
volatile bool g_snd_on_worker;

static Entry s_ring[RING];
// Shared words, __atomic access only. put: SH4 thread; get: token holder.
static u32 s_head, s_tail, s_owner, s_quit, s_sleeping, s_irq_want, s_cdda_put, s_cdda_get;
static u32 s_depth;   // SH4 nesting of snd_lock
static bool s_up;
static sys_ppu_thread_t s_tid, s_sh4_tid;
static s32 s_sh4_prio = -1;
static sys_sem_t s_sem;

static s16 s_cdda[CDDA_SLOTS][CDDA_WORDS];
static u64 s_samples, s_cdda_next;   // SH4 thread

// Worker counters only grow (__atomic); the SH4 thread keeps its own and the stats take deltas.
static u32 w_run, w_wake, w_empty;
static u64 w_ticks;
static u32 st_push, st_run_s, st_posts, st_crit, st_maxfill, st_irq, st_cdda, st_cdda_miss, st_lock[SND_LOCK_N];
static u64 st_tick_help, st_tick_wait, st_t0;

#define LOAD(v)     __atomic_load_n(&(v), __ATOMIC_RELAXED)
#define STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_RELAXED)

static inline bool own(u32 who)
{
	u32 free_v = OWN_FREE;
	return __atomic_compare_exchange_n(&s_owner, &free_v, who, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static inline void disown()
{
	__atomic_store_n(&s_owner, (u32)OWN_FREE, __ATOMIC_RELEASE);
}

static inline u32 fill()
{
	return __atomic_load_n(&s_head, __ATOMIC_RELAXED) - __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
}

// Token holder: the queued MediumUpdates, oldest first. Each tail store frees its slot at once.
static u32 run_queue()
{
	u32 n = 0;
	u32 t = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
	for (;;)
	{
		const u32 h = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
		if (t == h)
			return n;
		while (t != h)
		{
			const Entry e = s_ring[t & (RING - 1)];
			arm_aica_run(e.cycles, e.samples);
			t++;
			n++;
			__atomic_store_n(&s_tail, t, __ATOMIC_RELEASE);
		}
	}
}

// A short spin covers a worker run of a few entries; past that, sleep so the spin leaves the core to it.
static inline void wait_step(u32& spins)
{
	if (++spins > 64)
		sysUsleep(10);
}

static inline void irq_poll()
{
	if (aica_sh4_irq_apply(LOAD(s_irq_want) != 0))
		st_irq++;
}

static void worker(void*)
{
	u32 empty = 0;
	while (!__atomic_load_n(&s_quit, __ATOMIC_ACQUIRE))
	{
		if (fill() != 0 && own(OWN_WORKER))
		{
			const u64 t0 = gettime();
			g_snd_on_worker = true;
			const u32 n = run_queue();
			g_snd_on_worker = false;
			disown();
			STORE(w_ticks, LOAD(w_ticks) + (gettime() - t0));
			STORE(w_run, LOAD(w_run) + n);
			STORE(w_wake, LOAD(w_wake) + 1);
			empty = 0;
			continue;   // entries queued during the run go now
		}
		if (fill() == 0)
		{
			empty++;
			STORE(w_empty, LOAD(w_empty) + 1);
		}
		// Empty, or the SH4 holds the token and runs the queue. 2 ms sleeps after ~10 ms without work
		// (menus, pauses); the SH4 posts when entries pile up.
		__atomic_store_n(&s_sleeping, 1u, __ATOMIC_SEQ_CST);
		sysSemWait(s_sem, empty > 200 ? 2000 : 50);
		__atomic_store_n(&s_sleeping, 0u, __ATOMIC_RELAXED);
	}
	sysThreadExit(0);
}

static bool start()
{
	sys_sem_attr_t a;
	memset(&a, 0, sizeof(a));
	a.attr_protocol = SYS_SEM_ATTR_PROTOCOL;
	a.attr_pshared = SYS_SEM_ATTR_PSHARED;
	strncpy(a.name, "ndcsnd", sizeof(a.name));
	s32 r = sysSemCreate(&s_sem, &a, 0, 1);
	s32 wprio = PRIO_WORKER;
	if (r == 0)
	{
		__atomic_store_n(&s_quit, 0u, __ATOMIC_RELAXED);
		__atomic_store_n(&s_sleeping, 0u, __ATOMIC_RELAXED);
		if (sysThreadGetId(&s_sh4_tid) != 0 || sysThreadGetPriority(s_sh4_tid, &s_sh4_prio) != 0 ||
		    sysThreadSetPriority(s_sh4_tid, PRIO_SH4) != 0)
		{
			s_sh4_prio = -1;
			wprio = PRIO_WORKER_LOW;
		}
		r = sysThreadCreate(&s_tid, worker, NULL, wprio, 0x40000, THREAD_JOINABLE, (char*)"nulldc_sound");
		if (r != 0)
		{
			if (s_sh4_prio >= 0)
				sysThreadSetPriority(s_sh4_tid, s_sh4_prio);
			sysSemDestroy(s_sem);
		}
	}
	if (r != 0)
	{
		printf("[sndthr] start failed: 0x%08X; sound stays on the SH4 thread\n", (u32)r);
		g_snd_thread = false;
		return false;
	}
	s_up = true;
	st_t0 = gettime();
	if (s_sh4_prio >= 0)
		printf("[sndthr] up: ARM7 + AICA on their own thread (prio %d), SH4 thread prio %d -> %d, queue %u, help at %u\n",
		       (int)wprio, (int)s_sh4_prio, PRIO_SH4, RING, HELP_FILL);
	else
		printf("[sndthr] up: ARM7 + AICA on their own thread (prio %d, below the render thread: SH4 priority not"
		       " changed), queue %u, help at %u\n", (int)wprio, RING, HELP_FILL);
	return true;
}

// Called before the entry that plays them is queued, at the MediumUpdate where cdda_refill read them before.
static void produce_cdda(u32 samples)
{
	const u64 end = s_samples + samples;
	while (s_cdda_next < end)
	{
		u32 spins = 0;
		const u32 put = LOAD(s_cdda_put);
		while (put - __atomic_load_n(&s_cdda_get, __ATOMIC_ACQUIRE) >= CDDA_SLOTS)
		{
			// Never with RING 128 (under 300 samples queued); kept so a change cannot overwrite a sector.
			if (s_depth || own(OWN_SH4))
			{
				st_run_s += run_queue();
				if (!s_depth)
					disown();
			}
			else
				wait_step(spins);
		}
		s16* slot = s_cdda[put & (CDDA_SLOTS - 1)];
		if (get_cdda_preset())
			gdrom_get_cdda(slot);
		else
			memset(slot, 0, sizeof(s_cdda[0]));
		__atomic_store_n(&s_cdda_put, put + 1, __ATOMIC_RELEASE);
		s_cdda_next += CDDA_SAMPLES;
		st_cdda++;
	}
	s_samples = end;
}

// The worker is behind: run the queue here if it is not on it; near full, wait for room.
static void help(u32 n)
{
	const u64 t0 = gettime();
	if (own(OWN_SH4))
	{
		st_run_s += run_queue();
		disown();
		st_tick_help += gettime() - t0;
		return;
	}
	if (n < CRIT_FILL)
		return;
	st_crit++;
	u32 spins = 0;
	while (fill() >= CRIT_FILL)
	{
		if (own(OWN_SH4))
		{
			st_run_s += run_queue();
			disown();
			break;
		}
		wait_step(spins);
	}
	st_tick_wait += gettime() - t0;
}

static inline void wake()
{
	u32 one = 1;
	if (__atomic_compare_exchange_n(&s_sleeping, &one, 0u, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
	{
		sysSemPost(s_sem, 1);
		st_posts++;
	}
}

void snd_thread_init()
{
	g_snd_thread = get_sound_thread_preset() != 0 && !g_arm_spe;   // 5.15 moves the same work to an SPU
	if (g_snd_thread)
		printf("[sndthr] Sound Thread ON: the worker starts with the guest\n");
}

void snd_push(u32 cycles, u32 samples)
{
	if (s_depth || (!s_up && !start()))
	{
		// The SH4 owns the token (never today) or there is no worker: run it here, as before.
		if (g_snd_thread)
			produce_cdda(samples);
		arm_aica_run(cycles, samples);
		return;
	}
	produce_cdda(samples);
	const u32 h = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
	s_ring[h & (RING - 1)].cycles = cycles;
	s_ring[h & (RING - 1)].samples = samples;
	__atomic_store_n(&s_head, h + 1, __ATOMIC_RELEASE);
	st_push++;
	const u32 n = fill();
	if (n > st_maxfill)
		st_maxfill = n;
	if (n >= HELP_FILL)
		help(n);
	else if (n >= POST_FILL)
		wake();
	irq_poll();
}

void snd_lock(u32 why)
{
	if (!g_snd_thread || s_depth++)
		return;
	st_lock[why < SND_LOCK_N ? why : (u32)SND_LOCK_OTHER]++;
	const u64 t0 = gettime();
	u32 spins = 0;
	while (!own(OWN_SH4))
		wait_step(spins);
	const u64 t1 = gettime();
	st_run_s += run_queue();
	irq_poll();
	st_tick_wait += t1 - t0;
	st_tick_help += gettime() - t1;
}

void snd_unlock()
{
	if (!g_snd_thread || !s_depth || --s_depth)
		return;
	disown();
}

void snd_irq_set(bool on)
{
	STORE(s_irq_want, (u32)on);
}

void snd_cdda_pop(s16* sector)
{
	const u32 g = LOAD(s_cdda_get);
	if (g == __atomic_load_n(&s_cdda_put, __ATOMIC_ACQUIRE))
	{
		// Never: the SH4 reads each sector before it queues the samples that play it.
		memset(sector, 0, sizeof(s_cdda[0]));
		st_cdda_miss++;
		return;
	}
	memcpy(sector, s_cdda[g & (CDDA_SLOTS - 1)], sizeof(s_cdda[0]));
	__atomic_store_n(&s_cdda_get, g + 1, __ATOMIC_RELEASE);
}

void snd_thread_stop()
{
	if (!s_up)
		return;
	__atomic_store_n(&s_quit, 1u, __ATOMIC_RELEASE);
	sysSemPost(s_sem, 1);
	u64 rv;
	sysThreadJoin(s_tid, &rv);
	sysSemDestroy(s_sem);
	s_up = false;
	if (s_sh4_prio >= 0)
		sysThreadSetPriority(s_sh4_tid, s_sh4_prio);
	// The worker is gone, so the token is the SH4's; it runs the guest time the worker did not reach.
	if (!s_depth)
		__atomic_store_n(&s_owner, (u32)OWN_SH4, __ATOMIC_SEQ_CST);
	st_run_s += run_queue();
	irq_poll();
	if (!s_depth)
		disown();
	printf("[sndthr] down\n");
}

void snd_thread_stats()
{
	if (!g_snd_thread || !s_up)
		return;
	const u64 now = gettime();
	const double w = (double)(now - st_t0) / (double)ps3_timebase_hz;
	if (w < 5.0)
		return;
	st_t0 = now;
	static u32 l_run, l_wake, l_empty;
	static u64 l_ticks;
	const u32 run = LOAD(w_run), wake_n = LOAD(w_wake), empty = LOAD(w_empty);
	const u64 ticks = LOAD(w_ticks);
	const double tw = (double)ps3_timebase_hz * w / 100.0;
	const u32 total = (run - l_run) + st_run_s;
	const double k = total ? 100.0 / total : 0.0;
	printf("[sndthr] entries/s %.0f run by worker %.1f%% sh4 %.1f%% | worker busy %.1f%% wakes/s %.0f (empty %.0f)"
	       " | sh4 help %.2f%% wait %.2f%% posts/s %.0f crit %u maxfill %u | locks/s reg %.0f menu %u stats %u other %u"
	       " | irq/s %.0f cdda/s %.1f miss %u\n",
	       st_push / w, (run - l_run) * k, st_run_s * k, (ticks - l_ticks) / tw, (wake_n - l_wake) / w,
	       (empty - l_empty) / w, st_tick_help / tw, st_tick_wait / tw, st_posts / w, st_crit, st_maxfill,
	       st_lock[SND_LOCK_REG] / w, st_lock[SND_LOCK_MENU], st_lock[SND_LOCK_STATS], st_lock[SND_LOCK_OTHER],
	       st_irq / w, st_cdda / w, st_cdda_miss);
	l_run = run;
	l_wake = wake_n;
	l_empty = empty;
	l_ticks = ticks;
	st_push = st_run_s = st_posts = st_crit = st_maxfill = st_irq = st_cdda = 0;
	memset(st_lock, 0, sizeof(st_lock));
	st_tick_help = st_tick_wait = 0;
}
