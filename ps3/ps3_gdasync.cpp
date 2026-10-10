// OPTIMIZATION_POTENTIAL P3: disc reads on a worker thread. The SH4 thread kicks a fill and the GD-ROM holds its DMA
// until the data lands; at + CD-DA the worker also reads CD-DA sectors ahead into a ring keyed by FAD.
#include "types.h"
#include "ps3_gdasync.h"
#include "ps3_stall.h"
#include "ps3_timebase.h"
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/sem.h>
#include <sys/mutex.h>
#include <sys/systime.h>

extern "C" int get_gd_async_preset();
void libGDR_ReadSectorLocked(u8* buff, u32 StartSector, u32 SectorCount, u32 secsz);   // plugs/ImgReader

#define RA_SLOTS 64            // ~0.85 s of CD-DA, slot = FAD & (RA_SLOTS - 1)
#define RA_BATCH 16            // sectors per read-ahead read
#define RA_LOW   32            // the SH4 thread asks for more below this many sectors ahead
#define RA_NONE  0xFFFFFFFFu
#define SECT     2352
#define PRIO     999           // above the SH4 (1001) and render (1000) threads: it spends its time on the disk

#define LOAD(v)     __atomic_load_n(&(v), __ATOMIC_RELAXED)
#define STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_RELAXED)
#define ACQ(v)      __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define REL(v, x)   __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)

static int s_level;
static bool s_up, s_mtx_ok;
static sys_ppu_thread_t s_tid;
static sys_sem_t s_sem;
static sys_mutex_t s_mtx;
static u32 s_quit;

// The fill: the SH4 thread writes it before s_busy goes to 1, the worker reads it after.
static u8* s_dst;
static u32 s_fad, s_count, s_secsz, s_busy;

// CD-DA ring. A slot is valid while its tag is the FAD it holds; the worker clears the tag before it writes.
static u64 s_ra[RA_SLOTS][SECT / 8];
static u32 s_ra_tag[RA_SLOTS];
static u8 s_ra_stage[RA_BATCH * SECT];
static u32 s_ra_want = RA_NONE;   // SH4: the next FAD to play
static u32 s_ra_req;              // SH4: fill the window
static u32 s_ra_ready;            // worker: the ring holds the run up to here
static u32 s_ra_run, s_ra_next;   // worker only: the run [s_ra_run, s_ra_next)
static bool s_ra_missed;          // SH4 only
static u32 s_ra_flush;            // SH4: a disc swap; the worker drops the ring and clears it

// [gdr] counters. The worker's only grow and the dump prints deltas; the SH4 thread's are cleared per dump.
struct Count { u32 n, sec; u64 ticks, max; };
static Count st_sh4[GDR_N], st_wait;
static u32 st_hit, st_miss;
static Count w_fill, w_ra, s_fill_seen, s_ra_seen;

static void add(Count& c, u32 sec, u64 t)
{
	c.n++;
	c.sec += sec;
	c.ticks += t;
	if (t > c.max)
		c.max = t;
}

static void w_add(Count& c, u32 sec, u64 t)
{
	STORE(c.n, LOAD(c.n) + 1);
	STORE(c.sec, LOAD(c.sec) + sec);
	STORE(c.ticks, LOAD(c.ticks) + t);
	if (t > LOAD(c.max))
		STORE(c.max, t);
}

static void do_fill()
{
	const u64 t0 = gettime();
	libGDR_ReadSectorLocked(s_dst, s_fad, s_count, s_secsz);
	w_add(w_fill, s_count, gettime() - t0);
	REL(s_busy, 0u);
}

// One batch of the window [want, want + RA_SLOTS); false when it has nothing to read.
static bool ra_batch()
{
	const u32 want = ACQ(s_ra_want);
	if (want == RA_NONE)
		return false;
	// A seek or a loop: a new run starts at want. The ring holds only the run's last RA_SLOTS sectors.
	if (want < s_ra_run || want > s_ra_next || s_ra_next - want > RA_SLOTS)
		s_ra_run = s_ra_next = want;
	const u32 end = want + RA_SLOTS;
	if (s_ra_next >= end)
		return false;
	const u32 n = end - s_ra_next < RA_BATCH ? end - s_ra_next : RA_BATCH;
	const u64 t0 = gettime();
	libGDR_ReadSectorLocked(s_ra_stage, s_ra_next, n, SECT);
	for (u32 k = 0; k < n; k++)
	{
		const u32 fad = s_ra_next + k, slot = fad & (RA_SLOTS - 1);
		STORE(s_ra_tag[slot], RA_NONE);
		// The cleared tag before the new bytes: a reader that sees one of them then sees the tag change.
		__atomic_thread_fence(__ATOMIC_RELEASE);
		const u8* src = s_ra_stage + k * SECT;
		for (u32 w = 0; w < SECT / 8; w++)
		{
			u64 v;
			memcpy(&v, src + w * 8, 8);
			STORE(s_ra[slot][w], v);
		}
		REL(s_ra_tag[slot], fad);
	}
	s_ra_next += n;
	REL(s_ra_ready, s_ra_next);
	w_add(w_ra, n, gettime() - t0);
	return true;
}

static void worker(void*)
{
	while (!ACQ(s_quit))
	{
		// P34: the ring held the old disc. No reader runs now: the SH4 thread waits in gda_cdda_reset.
		if (ACQ(s_ra_flush))
		{
			for (u32 i = 0; i < RA_SLOTS; i++)
				STORE(s_ra_tag[i], RA_NONE);
			s_ra_run = s_ra_next = 0;
			REL(s_ra_ready, 0u);
			REL(s_ra_flush, 0u);
			continue;
		}
		if (ACQ(s_busy))
		{
			do_fill();
			continue;
		}
		if (s_level >= 2 && __atomic_exchange_n(&s_ra_req, 0u, __ATOMIC_ACQ_REL))
		{
			// A data fill goes first; the next request finishes the window.
			while (!ACQ(s_busy) && !ACQ(s_quit) && !ACQ(s_ra_flush) && ra_batch())
				;
			continue;
		}
		sysSemWait(s_sem, 100 * 1000);
	}
	sysThreadExit(0);
}

void gda_start()
{
	if (s_up)
		return;
	s_level = get_gd_async_preset();
	if (s_level <= 0)
	{
		s_level = 0;
		printf("[gdr] GD-ROM Async OFF: disc reads on the SH4 thread\n");
		return;
	}
	if (s_level > 2)
		s_level = 2;

	for (u32 i = 0; i < RA_SLOTS; i++)
		s_ra_tag[i] = RA_NONE;
	sys_mutex_attr_t ma;
	sysMutexAttrInitialize(ma);
	s32 r = sysMutexCreate(&s_mtx, &ma);
	if (r == 0)
	{
		sys_sem_attr_t sa;
		memset(&sa, 0, sizeof(sa));
		sa.attr_protocol = SYS_SEM_ATTR_PROTOCOL;
		sa.attr_pshared = SYS_SEM_ATTR_PSHARED;
		strncpy(sa.name, "ndcgdr", sizeof(sa.name));
		r = sysSemCreate(&s_sem, &sa, 0, 1);
		if (r == 0)
		{
			STORE(s_quit, 0u);
			r = sysThreadCreate(&s_tid, worker, NULL, PRIO, 0x20000, THREAD_JOINABLE, (char*)"nulldc_gdrom");
			if (r != 0)
				sysSemDestroy(s_sem);
		}
		if (r != 0)
			sysMutexDestroy(s_mtx);
	}
	if (r != 0)
	{
		printf("[gdr] GD-ROM Async: worker start failed: 0x%08X; disc reads stay on the SH4 thread\n", (u32)r);
		s_level = 0;
		return;
	}
	s_mtx_ok = true;
	s_up = true;
	printf("[gdr] GD-ROM Async %s: disc reads on a worker thread (prio %d), CD-DA read-ahead %s\n",
	       s_level >= 2 ? "+ CD-DA" : "DATA", PRIO, s_level >= 2 ? "on" : "off");
}

void gda_stop()
{
	if (!s_up)
		return;
	REL(s_quit, 1u);
	sysSemPost(s_sem, 1);
	u64 rv;
	sysThreadJoin(s_tid, &rv);
	sysSemDestroy(s_sem);
	// The worker drops a fill it did not take; with s_up clear, gda_busy() reads false.
	s_up = false;
	s_level = 0;
	printf("[gdr] worker stopped\n");
}

// The lock stays after gda_stop: a late caller must not find a destroyed one.
void gda_lock()
{
	if (s_mtx_ok)
		sysMutexLock(s_mtx, 0);
}

void gda_unlock()
{
	if (s_mtx_ok)
		sysMutexUnlock(s_mtx);
}

bool gda_async()
{
	return s_up;
}

void gda_fill(uint8_t* dst, uint32_t fad, uint32_t count, uint32_t secsz)
{
	s_dst = dst;
	s_fad = fad;
	s_count = count;
	s_secsz = secsz;
	REL(s_busy, 1u);
	sysSemPost(s_sem, 1);
}

bool gda_busy()
{
	return s_up && ACQ(s_busy) != 0;
}

void gda_wait()
{
	if (!gda_busy())
		return;
	StallScope st_(ST_GDREAD);
	const u64 t0 = gettime();
	while (gda_busy())
		sysUsleep(50);
	add(st_wait, 0, gettime() - t0);
}

bool gda_cdda(uint8_t* dst, uint32_t fad)
{
	if (s_level < 2)
		return false;
	const u32 slot = fad & (RA_SLOTS - 1);
	bool hit = ACQ(s_ra_tag[slot]) == fad;
	if (hit)
	{
		for (u32 w = 0; w < SECT / 8; w++)
		{
			const u64 v = LOAD(s_ra[slot][w]);
			memcpy(dst + w * 8, &v, 8);
		}
		// The copy before the second look at the tag: a rewrite during the copy is a miss.
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		hit = LOAD(s_ra_tag[slot]) == fad;
	}
	if (hit)
		st_hit++;
	else
	{
		st_miss++;
		s_ra_missed = true;
	}
	return hit;
}

void gda_cdda_next(bool playing, uint32_t fad)
{
	if (s_level < 2)
		return;
	REL(s_ra_want, playing ? fad : RA_NONE);
	if (!playing)
		return;
	// A miss means the run lost the play position (a seek or a loop): the worker starts a new one.
	const s32 ahead = (s32)(ACQ(s_ra_ready) - fad);
	if ((s_ra_missed || ahead < RA_LOW) && !LOAD(s_ra_req))
	{
		REL(s_ra_req, 1u);
		sysSemPost(s_sem, 1);
	}
	s_ra_missed = false;
}

bool gda_cdda_on()
{
	return s_level >= 2;
}

// CD-DA Miss: play starts at fad. Unlike gda_cdda_next it always asks, so the window is read before the first pull.
void gda_cdda_seek(uint32_t fad)
{
	if (s_level < 2)
		return;
	REL(s_ra_want, fad);
	REL(s_ra_req, 1u);
	sysSemPost(s_sem, 1);
}

// P34: before the next image opens. Returns when the worker has dropped its run and every ring tag.
void gda_cdda_reset()
{
	if (s_level < 2)
		return;
	REL(s_ra_want, RA_NONE);
	REL(s_ra_flush, 1u);
	sysSemPost(s_sem, 1);
	while (ACQ(s_ra_flush))
		sysUsleep(50);
	s_ra_missed = false;
}

void gda_count(int who, uint32_t sectors, uint64_t ticks)
{
	if (who >= 0 && who < GDR_N)
		add(st_sh4[who], sectors, ticks);
}

static double ms(u64 t)
{
	return (double)t * 1000.0 / (double)ps3_timebase_hz;
}

// The worker's counts since the last dump; its max is taken and cleared.
static Count take(Count& w, Count& seen)
{
	Count d;
	const u32 n = LOAD(w.n), sec = LOAD(w.sec);
	const u64 t = LOAD(w.ticks);
	d.n = n - seen.n;
	d.sec = sec - seen.sec;
	d.ticks = t - seen.ticks;
	d.max = __atomic_exchange_n(&w.max, (u64)0, __ATOMIC_RELAXED);
	seen.n = n;
	seen.sec = sec;
	seen.ticks = t;
	return d;
}

void gda_stats(double secs)
{
	const Count f = take(w_fill, s_fill_seen), r = take(w_ra, s_ra_seen);
	bool any = f.n || r.n || st_wait.n || st_hit || st_miss;
	for (int k = 0; k < GDR_N; k++)
		any = any || st_sh4[k].n;
	if (any)
	{
		// Per window: reads/sectors total ms(longest ms).
		const Count* s = st_sh4;
		printf("[gdr] %.2f s async=%d sh4: dma %u/%u %.1f(%.1f) pio %u/%u %.1f(%.1f) cdda %u/%u %.1f(%.1f)"
		       " wait %u %.1f(%.1f) | worker: fill %u/%u %.1f(%.1f) ra %u/%u %.1f(%.1f) | ra hit %u miss %u\n",
		       secs, s_level,
		       s[GDR_DMA].n, s[GDR_DMA].sec, ms(s[GDR_DMA].ticks), ms(s[GDR_DMA].max),
		       s[GDR_PIO].n, s[GDR_PIO].sec, ms(s[GDR_PIO].ticks), ms(s[GDR_PIO].max),
		       s[GDR_CDDA].n, s[GDR_CDDA].sec, ms(s[GDR_CDDA].ticks), ms(s[GDR_CDDA].max),
		       st_wait.n, ms(st_wait.ticks), ms(st_wait.max),
		       f.n, f.sec, ms(f.ticks), ms(f.max), r.n, r.sec, ms(r.ticks), ms(r.max), st_hit, st_miss);
	}
	memset(st_sh4, 0, sizeof(st_sh4));
	memset(&st_wait, 0, sizeof(st_wait));
	st_hit = st_miss = 0;
}
