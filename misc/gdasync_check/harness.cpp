// P3 PC check (WSL): the real gdromv3.cpp and ps3_gdasync.cpp on pthreads and a fake drive, against a plain model.
// Args: level, iterations, seed, CD-DA Miss (0-3), GD-ROM Speed, Timing, Read Ahead; a P34 disc swap at each level.
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <atomic>
#include "types.h"
#include "ps3_timebase.h"
#include "plugins/plugin_manager.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/asic/asic.h"
#include "dc/sh4/sh4_sched.h"
#include "gdromv3.h"
#include "ps3_gdasync.h"
#include <sys/thread.h>
#include <sys/sem.h>
#include <sys/mutex.h>
#include <sys/systime.h>

uint64_t ps3_timebase_hz = 1000000000ull;

// ---- lv2 stand-ins ----
struct Sem { pthread_mutex_t m; pthread_cond_t c; int count, max, used; };
static Sem g_sem[4];

int sysSemCreate(sys_sem_t* s, const sys_sem_attr_t*, int32_t init, int32_t max)
{
	for (u32 i = 0; i < 4; i++)
		if (!g_sem[i].used)
		{
			pthread_mutex_init(&g_sem[i].m, NULL);
			pthread_cond_init(&g_sem[i].c, NULL);
			g_sem[i].count = init;
			g_sem[i].max = max;
			g_sem[i].used = 1;
			*s = i;
			return 0;
		}
	return -1;
}

int sysSemDestroy(sys_sem_t s)
{
	pthread_mutex_destroy(&g_sem[s].m);
	pthread_cond_destroy(&g_sem[s].c);
	g_sem[s].used = 0;
	return 0;
}

int sysSemWait(sys_sem_t s, uint64_t to)
{
	Sem& e = g_sem[s];
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += (long)(to % 1000000) * 1000;
	ts.tv_sec += (time_t)(to / 1000000) + ts.tv_nsec / 1000000000;
	ts.tv_nsec %= 1000000000;
	pthread_mutex_lock(&e.m);
	int r = 0;
	while (e.count == 0 && r != ETIMEDOUT)
		r = pthread_cond_timedwait(&e.c, &e.m, &ts);
	const int got = e.count > 0;
	if (got)
		e.count--;
	pthread_mutex_unlock(&e.m);
	return got ? 0 : 1;
}

int sysSemPost(sys_sem_t s, int32_t n)
{
	Sem& e = g_sem[s];
	pthread_mutex_lock(&e.m);
	int r = 1;
	if (e.count + n <= e.max)
	{
		e.count += n;
		pthread_cond_signal(&e.c);
		r = 0;
	}
	pthread_mutex_unlock(&e.m);
	return r;
}

static pthread_mutex_t g_mtx[4];
static int g_mtx_used;
int sysMutexCreate(sys_mutex_t* m, const sys_mutex_attr_t*)
{
	if (g_mtx_used >= 4)
		return -1;
	pthread_mutex_init(&g_mtx[g_mtx_used], NULL);
	*m = (sys_mutex_t)g_mtx_used++;
	return 0;
}
int sysMutexDestroy(sys_mutex_t m) { return pthread_mutex_destroy(&g_mtx[m]); }
int sysMutexLock(sys_mutex_t m, uint64_t) { return pthread_mutex_lock(&g_mtx[m]); }
int sysMutexUnlock(sys_mutex_t m) { return pthread_mutex_unlock(&g_mtx[m]); }

struct Tramp { void (*fn)(void*); void* arg; };
static void* tramp(void* p)
{
	Tramp t = *(Tramp*)p;
	delete (Tramp*)p;
	t.fn(t.arg);
	return NULL;
}
int sysThreadCreate(sys_ppu_thread_t* tid, void (*fn)(void*), void* arg, int32_t, uint64_t, uint64_t, char*)
{
	pthread_t t;
	if (pthread_create(&t, NULL, tramp, new Tramp{fn, arg}) != 0)
		return -1;
	*tid = (sys_ppu_thread_t)t;
	return 0;
}
int sysThreadJoin(sys_ppu_thread_t tid, uint64_t* rv)
{
	*rv = 0;
	return pthread_join((pthread_t)tid, NULL);
}
void sysThreadExit(uint64_t) { pthread_exit(NULL); }
int sysThreadGetId(sys_ppu_thread_t* tid) { *tid = (sys_ppu_thread_t)pthread_self(); return 0; }
int sysThreadGetPriority(sys_ppu_thread_t, int32_t* p) { *p = 1001; return 0; }
int sysThreadSetPriority(sys_ppu_thread_t, int32_t) { return 0; }
int sysUsleep(uint32_t us) { return usleep(us); }

// ---- the stall probe and the presets ----
extern "C" { int g_jit_ccalls_preset = 0; }
bool g_stall_on;
void stall_push_on(int) {}
void stall_pop_on() {}

static int g_level, g_miss, g_dma_fix, g_speed, g_timing, g_dra;
extern "C" {
int get_gd_async_preset() { return g_level; }
int get_debug_loop() { return 0; }
int get_debug_gdrom() { return 0; }
int get_sched_preset() { return 0; }
// Bundle 2: only CD-DA Miss varies here; misc/gd2_check has the rest.
int get_cdda_miss_preset() { return g_miss; }
int get_gd_status_preset() { return 0; }
int get_gd_position_preset() { return 0; }
int get_gd_scd_q_preset() { return 0; }
int get_cdda_rules_preset() { return 0; }
int get_gd_speed_preset() { return g_speed; }
int get_gd_timing_preset() { return g_timing; }
int get_gd_read_ahead_preset() { return g_dra; }
int get_gd_dma_fix_preset() { return g_dma_fix; }
int get_gd_cmd_fix_preset() { return 0; }
int get_gd_set_mode_preset() { return 0; }
int get_disc_swap_preset() { return 1; }
int get_holly_regs_preset() { return 0; }
}

// ---- errors ----
static std::atomic<int> g_errors{0};
static void err(const char* what, u64 a, u64 b)
{
	if (g_errors++ < 20)
		printf("ERROR: %s (%llu, %llu)\n", what, (unsigned long long)a, (unsigned long long)b);
}
void check_fail(const char* what, const char* file, int line)
{
	printf("ERROR: %s at %s:%d\n", what, file, line);
	exit(1);
}
int msgboxf(const char* text, unsigned int, ...)
{
	err("msgboxf", 0, 0);
	printf("  %s\n", text);
	return 0;
}

// ---- the drive ----
static pthread_t g_main;
static std::atomic<int> g_in_drive{0};
static std::atomic<u32> g_delay_us{0};   // the worker's reads take up to this long
static std::atomic<u32> g_sh4_reads{0};
static std::atomic<u32> g_block_worker{0};   // the worker's next read waits at the drive's door
static thread_local u64 t_seed = 88172645463325252ull;
static u32 rnd_t()
{
	t_seed ^= t_seed << 13;
	t_seed ^= t_seed >> 7;
	t_seed ^= t_seed << 17;
	return (u32)t_seed;
}
static std::atomic<u32> g_disc{0};      // P34: the disc in the drive; each one has its own bytes
static std::atomic<u32> g_cdda_reads{0};   // disc reads the SH4 thread did inside a CD-DA pull
static std::atomic<u32> g_worker_reads{0};
static std::atomic<u32> g_worker_fills{0};   // worker reads shorter than a read-ahead batch (32)
static std::atomic<u32> g_worker_busy{0};
static bool g_in_pull;
static inline u8 pat(u32 fad, u32 secsz, u32 j)
{
	return (u8)(fad * 131u + j * 7u + (j >> 8) * 13u + secsz + g_disc.load() * 29u);
}
static bool on_main() { return pthread_equal(pthread_self(), g_main) != 0; }

// As plugs/ImgReader: any thread, under the disc lock. A slow write widens every race window.
void libGDR_ReadSectorLocked(u8* buff, u32 fad, u32 n, u32 secsz)
{
	while (!on_main() && g_block_worker.load())
		usleep(100);
	if (!on_main())
	{
		g_worker_reads++;
		g_worker_fills += n < 32;
		g_worker_busy++;
	}
	gda_lock();
	if (g_in_drive.fetch_add(1) != 0)
		err("two threads in the drive", fad, n);
	memset(buff, 0, n * secsz);
	const u32 d = g_delay_us.load();
	if (!on_main() && d)
		usleep(rnd_t() % (d + 1));
	for (u32 k = 0; k < n; k++)
	{
		for (u32 j = 0; j < secsz; j++)
			buff[k * secsz + j] = pat(fad + k, secsz, j);
		if (!on_main() && d && (rnd_t() & 3) == 0)
			usleep(rnd_t() % 50);
	}
	g_in_drive.fetch_sub(1);
	gda_unlock();
	if (!on_main())
		g_worker_busy--;
}

void FASTCALL libGDR_ReadSector(u8* buff, u32 fad, u32 n, u32 secsz)
{
	if (!on_main())
		err("libGDR_ReadSector off the SH4 thread", fad, n);
	g_sh4_reads++;
	if (g_in_pull)
		g_cdda_reads++;
	libGDR_ReadSectorLocked(buff, fad, n, secsz);
}
void FASTCALL libGDR_GetToc(u32*, u32) {}
u32 FASTCALL libGDR_GetDiscType() { return GdRom; }
void FASTCALL libGDR_GetSessionInfo(u8* out, u8) { memset(out, 0, 6); }
u32 FASTCALL libGDR_GetTrackNumber(u32, u32* elapsed) { *elapsed = 0; return 1; }
void FASTCALL libGDR_GetTrackAdrAndControl(u32, u8* adr, u8* ctrl) { *adr = 1; *ctrl = 4; }
u32 FASTCALL libGDR_GetLeadOut() { return 0xFFFFFFFFu; }   // P82 is in misc/gd2_check

// The steps of plugs/ImgReader/ImgReader.cpp (run.sh checks that file's lines): the image changes under the lock,
// after the worker's fill landed and its ring emptied.
bool libGDR_SwapDisc(const char*)
{
	gda_wait();
	gda_cdda_reset();
	gda_lock();
	if (g_in_drive.load() != 0)
		err("disc swap with a thread in the drive", 0, 0);
	g_disc++;
	gda_unlock();
	return true;
}

// gdrom_response.cpp
u8 reply_a1[0x80];
u8 gd_data_0x11[32];
u8 gd_data_0x71[16];
u32 gd_data_0x71_len = 16;
char szExDT[8][32];

// ---- the bus ----
RegisterStruct sb_regs[0x800];
u32 SB_GDSTAR, SB_GDLEN, SB_GDDIR, SB_GDEN, SB_GDST, SB_GDSTARD, SB_GDLEND;
DMAC_DMAOR_type DMAC_DMAOR = { 0x8201 };
static u8 g_ram[1 << 24];
static u32 g_irq_dma, g_irq_cmd;

void WriteMem32_nommu(u32 addr, u32 data)
{
	if (!on_main())
		err("RAM write off the SH4 thread", addr, data);
	if ((addr & 3) || (addr & 0xFFFFFF) > (1u << 24) - 4)
	{
		err("bad DMA address", addr, 0);
		return;
	}
	memcpy(&g_ram[addr & 0xFFFFFF], &data, 4);
}
void asic_RaiseInterrupt(HollyInterruptID id)
{
	if (id == holly_GDROM_DMA)
		g_irq_dma++;
	else if (id == holly_GDROM_CMD)
		g_irq_cmd++;
}
void asic_CancelInterrupt(HollyInterruptID) {}
int sh4_sched_register(int, sh4_sched_callback*) { return 0; }
void sh4_sched_request(int, int) { err("sched used", 0, 0); }
void NotifyEvent_gdrom(u32 info, void* param);

// ---- the guest ----
static u64 g_seed = 1;
static u32 rnd()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 7;
	g_seed ^= g_seed << 17;
	return (u32)g_seed;
}

static void spi(const u8 p[12])
{
	WriteMem_gdrom(GD_COMMAND_Write, ATA_SPI_PACKET, 1);
	for (int i = 0; i < 6; i++)
		WriteMem_gdrom(GD_DATA, p[2 * i] | (p[2 * i + 1] << 8), 2);
}

// The CD-DA model: what gdrom_get_cdda must hand out next.
static struct { bool playing; u32 cur, start, end, rep; } M;

static void cmd_play(u32 start, u32 end, u32 rep)
{
	u8 p[12] = {0};
	p[0] = SPI_CD_PLAY;
	p[1] = 1;
	p[2] = (u8)(start >> 16); p[3] = (u8)(start >> 8); p[4] = (u8)start;
	p[6] = (u8)rep;
	p[8] = (u8)(end >> 16); p[9] = (u8)(end >> 8); p[10] = (u8)end;
	spi(p);
	M.playing = true;
	M.cur = M.start = start;
	M.end = end;
	M.rep = rep & 0xF;
}

static void cmd_seek(u32 fad)
{
	u8 p[12] = {0};
	p[0] = SPI_CD_SEEK;
	p[1] = 1;
	p[2] = (u8)(fad >> 16); p[3] = (u8)(fad >> 8); p[4] = (u8)fad;
	spi(p);
	M.playing = false;
	M.cur = M.start = fad;
}

// CD-DA Miss + SILENCE: a pull the ring did not have is silence and the position stays (gdromv3.cpp CDDA_HOLD_MAX).
static u32 g_pulls, g_held, g_held_run;
static void pull_cdda()
{
	s16 sec[1176];
	g_in_pull = true;
	gdrom_get_cdda(sec);
	g_in_pull = false;
	g_pulls++;
	const u8* b = (const u8*)sec;
	if (M.playing && g_miss >= 2 && g_level >= 2)
	{
		bool silent = true;
		for (u32 j = 0; j < 2352 && silent; j++)
			silent = b[j] == 0;
		if (silent)
		{
			g_held++;
			if (++g_held_run > 150)
				err("CD-DA held past the limit", M.cur, g_held_run);
			return;
		}
	}
	g_held_run = 0;
	if (!M.playing)
	{
		for (u32 j = 0; j < 2352; j++)
			if (b[j])
			{
				err("CD-DA sound while stopped", M.cur, j);
				break;
			}
		return;
	}
	for (u32 j = 0; j < 2352; j++)
		if (b[j] != pat(M.cur, 2352, j))
		{
			err("CD-DA sector wrong", M.cur, j);
			break;
		}
	M.cur++;
	if (M.cur == M.end)
	{
		if (M.rep == 0)
			M.playing = false;
		else
		{
			if (M.rep != 0xF)
				M.rep--;
			M.cur = M.start;
		}
	}
}

static void cmd_read(u32 fad, u32 count, u32 secsz)
{
	WriteMem_gdrom(GD_FEATURES_Write, 1, 1);   // DMA
	u8 p[12] = {0};
	p[0] = SPI_CD_READ;
	p[1] = secsz == 2340 ? 0xE6 : secsz == 2352 ? 0x22 : 0x20;
	p[2] = (u8)(fad >> 16); p[3] = (u8)(fad >> 8); p[4] = (u8)fad;
	p[8] = (u8)(count >> 16); p[9] = (u8)(count >> 8); p[10] = (u8)count;
	spi(p);
}

static void dma_regs(u32 dst, u32 len)
{
	SB_GDSTAR = dst;
	SB_GDLEN = len;
	SB_GDDIR = 1;
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(1);
	sb_regs[(SB_GDST_addr - SB_BASE) >> 2].writeFunction(1);   // GDROM_DmaStart runs one update
}

// One guest DMA to its end, the AICA pulling CD-DA in between; false on a timeout.
static bool dma(u32 dst, u32 len, bool cdda)
{
	const u32 irq0 = g_irq_dma;
	dma_regs(dst, len);
	const u64 t0 = gettime();
	while (SB_GDST & 1)
	{
		if (cdda && (rnd() & 3) == 0)
			pull_cdda();
		// As SlowUpdate: with GD-ROM Speed REAL the chunk waits count down in the tick.
		UpdateGDRom();
		gdrom_spinup_tick(14336);
		if ((rnd() & 7) == 0)
			usleep(rnd() % 30);
		if (gettime() - t0 > 20000000000ull)
		{
			err("DMA never ends", dst, len);
			return false;
		}
	}
	if (g_irq_dma != irq0 + 1)
		err("DMA end interrupts", g_irq_dma - irq0, len);
	return true;
}

static u32 pick_secsz()
{
	const u32 r = rnd() % 8;
	return r < 6 ? 2048 : r == 6 ? 2340 : 2352;
}

// Whole sectors that make a multiple of 32 bytes, as the GD-DMA needs.
static u32 pick_count(u32 secsz, u32 maxc)
{
	const u32 step = secsz == 2048 ? 1 : secsz == 2352 ? 2 : 8;
	u32 c = 1 + rnd() % maxc;
	return (c + step - 1) / step * step;
}

static u32 g_reads;
static void test_read(u32 fad, u32 count, u32 secsz, bool cdda)
{
	const u32 total = count * secsz;
	const u32 dst = 0x100000 + (rnd() % 0x800) * 32;
	memset(&g_ram[dst], 0xA5, total);
	const u32 cmd0 = g_irq_cmd;
	cmd_read(fad, count, secsz);
	u32 done = 0;
	while (done < total)
	{
		u32 len = total - done;
		if (rnd() & 1)
		{
			len = (1 + rnd() % (len / 32)) * 32;
			if (len > total - done)
				len = total - done;
		}
		if (!dma(dst + done, len, cdda))
			return;
		done += len;
	}
	if (g_irq_cmd != cmd0 + 1)
		err("command end interrupts", g_irq_cmd - cmd0, count);
	if (ReadMem_gdrom(GD_STATUS_Read, 1) & 0x80)
		err("BSY after the read", fad, count);
	for (u32 i = 0; i < total; i++)
		if (g_ram[dst + i] != pat(fad + i / secsz, secsz, i % secsz))
		{
			err("DMA byte wrong", fad + i / secsz, i % secsz);
			break;
		}
	g_reads++;
}

// A read cut off mid-DMA (DMA disabled, then NOP or soft reset); the next read must get its own bytes.
static void test_abort(bool cdda)
{
	const u32 secsz = pick_secsz(), count = 33 + rnd() % 64;
	const u32 c = (count + 7) / 8 * 8;
	cmd_read(150 + rnd() % 400000, c, secsz);
	dma_regs(0x200000, c * secsz);
	for (u32 n = rnd() % 4; n; n--)
		UpdateGDRom();
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
	WriteMem_gdrom(GD_COMMAND_Write, (rnd() & 1) ? ATA_NOP : ATA_SOFT_RESET, 1);
	test_read(150 + rnd() % 400000, pick_count(secsz, 80), secsz, cdda);
}

static void phase_mixed(u32 iters)
{
	for (u32 i = 0; i < iters; i++)
	{
		if ((i & 63) == 0)
			g_delay_us = (rnd() & 7) == 0 ? 3000 : rnd() % 500;
		const u32 r = rnd() % 100;
		if (r < 50)
		{
			const u32 secsz = pick_secsz();
			test_read(150 + rnd() % 500000, pick_count(secsz, (rnd() & 3) ? 40 : 300), secsz, true);
		}
		else if (r < 60)
			test_abort(true);
		else if (r < 75)
		{
			const u32 s = 150 + rnd() % 500000;
			cmd_play(s, s + 1 + rnd() % 400, (rnd() & 7) == 0 ? 0xF : rnd() % 4);
		}
		else if (r < 80)
			cmd_seek(150 + rnd() % 500000);
		else
			for (u32 n = 1 + rnd() % 50; n; n--)
				pull_cdda();
	}
}

// Steady play: the read-ahead must hand out nearly every sector.
static void phase_steady()
{
	g_delay_us = 100;
	cmd_play(10000, 13000, 0);
	const u32 r0 = g_sh4_reads, h0 = g_held;
	for (u32 i = 0; i < 3000; i++)
	{
		pull_cdda();
		usleep(200);
	}
	const u32 miss = g_sh4_reads - r0;
	printf("  steady: %u misses, %u held in 3000 sectors\n", miss, g_held - h0);
	if (miss > 5 || g_held - h0 > 20)
		err("read-ahead misses in steady play", miss, g_held - h0);
}

// Seeks both ways: a few misses each, then the read-ahead catches up.
static void phase_seeks()
{
	g_delay_us = 100;
	const u32 r0 = g_sh4_reads, h0 = g_held;
	const u32 plays = 60;
	for (u32 p = 0; p < plays; p++)
	{
		const u32 s = 150 + rnd() % 500000;
		cmd_play(s, s + 5000, 0);
		for (u32 i = 0; i < 100; i++)
		{
			pull_cdda();
			usleep(200);
		}
	}
	const u32 miss = g_sh4_reads - r0;
	printf("  seeks: %u misses, %u held in %u plays\n", miss, g_held - h0, plays);
	if (miss > plays * 4 || g_held - h0 > plays * 12)
		err("read-ahead misses after seeks", miss, g_held - h0);
}

// Replays and loops: play goes back inside the worker's run (HW 2026-10-06: ChuChu missed 8 s in a row).
static void phase_loops()
{
	g_delay_us = 100;
	const u32 r0 = g_sh4_reads, h0 = g_held;
	u32 events = 0;
	struct { u32 start, len, rep, pulls; } k[] = {
		{ 20000, 600, 0, 600 }, { 20000, 600, 0, 600 },   // a song, then the same song again
		{ 30000, 300, 0xF, 1500 },                        // a long loop, 5 turns
		{ 40000, 40, 0xF, 400 },                          // a short loop, 10 turns
	};
	for (u32 p = 0; p < sizeof(k) / sizeof(k[0]); p++)
	{
		cmd_play(k[p].start, k[p].start + k[p].len, k[p].rep);
		events += 1 + (k[p].rep ? k[p].pulls / k[p].len : 0);
		for (u32 i = 0; i < k[p].pulls; i++)
		{
			pull_cdda();
			usleep(200);
		}
	}
	const u32 miss = g_sh4_reads - r0;
	printf("  loops: %u misses, %u held in %u plays and loop turns\n", miss, g_held - h0, events);
	if (miss > events * 4 || g_held - h0 > events * 12)
		err("read-ahead misses after loops", miss, g_held - h0);
}

// CD-DA Miss: a play, then the time to the first sample (13 ms on the PS3; here 20 ms, as WSL sleeps are long).
// With PLAY AHEAD the ring has the start of the play by then; without it the first pull always misses.
static void phase_play_ahead()
{
	g_delay_us = 100;
	const u32 r0 = g_cdda_reads, h0 = g_held, plays = 40;
	for (u32 p = 0; p < plays; p++)
	{
		const u32 s = 150 + rnd() % 500000;
		cmd_play(s, s + 5000, 0);
		usleep(20000);
		for (u32 i = 0; i < 20; i++)
		{
			pull_cdda();
			usleep(200);
		}
	}
	const u32 miss = g_cdda_reads - r0 + g_held - h0;
	printf("  play ahead: %u misses in %u plays\n", miss, plays);
	if (g_miss >= 1 && miss > plays / 8)
		err("misses at the start of a play with PLAY AHEAD", miss, plays);
}

// + LOOP AHEAD: turns take the loop head, then a new run; a loop that fits the ring is not read again. The first pass
// may wait for the disc; the turns may not. CD-DA Miss 2 shows the gaps this level removes (6 or so a turn).
static void phase_loop_ahead()
{
	g_delay_us = 100;
	const u32 r0 = g_cdda_reads;
	u32 turns = 0, held = 0, reread = 0;
	struct { u32 start, len, pulls; } k[] = {
		{ 50000, 300, 1500 },   // the head, then a new run
		{ 60000, 100, 800 },
		{ 70000, 70, 700 },
		{ 80000, 40, 400 },     // fits the ring
		{ 90000, 20, 200 },     // shorter than the head
	};
	for (u32 p = 0; p < sizeof(k) / sizeof(k[0]); p++)
	{
		cmd_play(k[p].start, k[p].start + k[p].len, 0xF);
		u32 h0 = 0, w0 = 0;
		for (u32 i = 0; i < k[p].pulls; i++)
		{
			if (i == k[p].len)
				h0 = g_held;
			if (i == 2 * k[p].len)
				w0 = g_worker_reads;
			pull_cdda();
			usleep(200);
		}
		turns += k[p].pulls / k[p].len - 1;
		held += g_held - h0;
		if (k[p].len <= 64)
			reread += g_worker_reads - w0;
	}
	cmd_seek(150);
	printf("  loop ahead: %u held in %u loop turns, %u reads of loops that fit the ring, %u SH4 reads\n", held, turns,
	       reread, g_cdda_reads - r0);
	if (g_miss >= 3 && (held > turns / 4 || reread > 2 || g_cdda_reads != r0))
		err("+ LOOP AHEAD: gaps or reads at loop turns", held, reread);
}

// + SILENCE with a worker that does not answer: after 150 silent pulls the SH4 thread reads the sector itself.
static void phase_cap()
{
	g_delay_us = 100;
	g_block_worker = 1;
	const u32 s = 150 + rnd() % 500000, r0 = g_cdda_reads, h0 = g_held;
	cmd_play(s, s + 5000, 0);
	for (u32 i = 0; i < 151; i++)
		pull_cdda();
	if (g_held - h0 != 150 || g_cdda_reads - r0 != 1)
		err("SILENCE limit: held pulls and SH4 reads", g_held - h0, g_cdda_reads - r0);
	g_block_worker = 0;
	for (u32 i = 0; i < 100; i++)
	{
		pull_cdda();
		usleep(200);
	}
	printf("  cap: %u held, then %u SH4 read\n", g_held - h0, g_cdda_reads - r0);
}

// GD-ROM Timing: the updates a DMA takes. FIXED: the same whatever the disc's time (the SH4 waits in UpdateGDRom);
// HOST: the DMA waits for the disc, update by update.
static u32 dma_updates(u32 fad, u32 count, u32 delay)
{
	g_delay_us = delay;
	const u32 dst = 0x100000;
	cmd_read(fad, count, 2048);
	dma_regs(dst, count * 2048);
	u32 n = 0;
	const u64 t0 = gettime();
	while (SB_GDST & 1)
	{
		UpdateGDRom();
		gdrom_spinup_tick(14336);
		n++;
		if (gettime() - t0 > 20000000000ull)
		{
			err("GD-ROM Timing: DMA never ends", fad, count);
			break;
		}
	}
	for (u32 i = 0; i < count * 2048; i++)
		if (g_ram[dst + i] != pat(fad + i / 2048, 2048, i % 2048))
		{
			err("GD-ROM Timing: byte wrong", fad + i / 2048, i % 2048);
			break;
		}
	return n;
}

static void phase_timing()
{
	u32 fixed_diff = 0, host_more = 0, fixed_extra = 0;
	for (u32 k = 0; k < 12; k++)
	{
		const u32 fad = 150 + rnd() % 400000, count = 33 + rnd() % 64;
		const u32 fast = dma_updates(fad, count, 0), slow = dma_updates(fad, count, 3000);
		fixed_diff += fast != slow;
		host_more += slow > fast;
		// FIXED at GD-ROM Speed FAST: the start write moves 32000 bytes, then each update 32000, fills or not.
		const u32 total = count * 2048, want = total > 32000 ? (total - 32000 + 31999) / 32000 : 0;
		fixed_extra += g_timing == 1 && !g_speed && (fast != want || slow != want);
	}
	printf("  timing: %u of 12 reads took other updates with a slow disc\n", host_more + (g_timing ? fixed_diff : 0));
	if (g_timing && fixed_diff)
		err("FIXED: the updates depend on the disc's time", fixed_diff, 0);
	if (fixed_extra)
		err("FIXED: updates past GD-ROM Speed's", fixed_extra, 0);
	if (!g_timing && !host_more)
		err("HOST: no DMA waited for a slow disc", 0, 0);
}

// Row GD-ROM Read Ahead: 8-sector steps in each format, twice from 1000 sectors back; with the row far fewer disc reads
// than steps, without it one each. Then the ring's 2352-byte sectors read as 2048 bytes must come from the disc.
static void phase_stream()
{
	g_delay_us = 200;
	static const u32 fmts[3] = { 2048, 2340, 2352 };
	u32 reads = 0, steps = 0, fad = 0;
	for (u32 f = 0; f < 3; f++)
	{
		fad = 150 + rnd() % 400000;
		for (u32 pass = 0; pass < 2; pass++)
		{
			const u32 w0 = g_worker_reads;
			for (u32 i = 0; i < 60; i++, fad += 8)
			{
				test_read(fad, 8, fmts[f], false);
				usleep(500);
			}
			reads += g_worker_reads - w0;
			steps += 60;
			if (pass == 0)
				fad -= 1000;
		}
	}
	usleep(3000);
	test_read(fad, 8, 2048, false);
	printf("  stream: %u disc reads for %u steps of 8 sectors\n", reads, steps);
	if (g_dra && reads > steps / 2)
		err("Read Ahead: the steps still read the disc", reads, steps);
	if (!g_dra && reads < steps)
		err("no Read Ahead: fewer disc reads than steps", reads, steps);

	// A step back to just before the run, then the step it skipped: the run starts again at the step back, so only
	// the first step and the step back read the disc (2 of 5). Each step waits for an idle worker.
	const s32 at[5] = { 0, 8, 16, -8, 0 };
	u32 fills = 0;
	for (u32 i = 0; i < 10; i++)
	{
		const u32 f0 = 150 + 8 + rnd() % 400000, w0 = g_worker_fills;
		for (u32 k = 0; k < 5; k++)
		{
			test_read(f0 + at[k], 8, 2048, false);
			for (u32 still = 0; still < 20; )
			{
				const u32 w = g_worker_reads;
				usleep(100);
				still = (w == g_worker_reads && g_worker_busy == 0) ? still + 1 : 0;
			}
		}
		fills += g_worker_fills - w0;
	}
	printf("  stream back: %u disc fills for 50 steps\n", fills);
	if (g_dra && fills > 25)
		err("Read Ahead: no new run after a step back", fills, 20);
}

// P22 with the worker: SB_GDLEN 0 on an empty buffer. The fill is in flight, nothing moves, and the DMA must not end.
static void phase_len0()
{
	g_dma_fix = 1;
	g_delay_us = 500;
	for (u32 n = 0; n < 20; n++)
	{
		const u32 fad = 150 + rnd() % 400000, dst = 0x100000 + (rnd() % 0x800) * 32;
		memset(&g_ram[dst], 0xA5, 64 * 2048);
		const u32 cmd0 = g_irq_cmd;
		cmd_read(fad, 64, 2048);
		if (!dma(dst, 32 * 2048, false))
			break;
		const u32 dma0 = g_irq_dma;
		dma_regs(dst + 32 * 2048, 0);
		const u64 t0 = gettime();
		while (g_irq_cmd == cmd0 && gettime() - t0 < 20000000000ull)
		{
			UpdateGDRom();
			gdrom_spinup_tick(14336);
			usleep(20);
		}
		if (g_irq_cmd != cmd0 + 1 || g_irq_dma != dma0 || !(SB_GDST & 1) || SB_GDLEND != 32 * 2048)
			err("DMA of length 0", g_irq_dma - dma0, SB_GDLEND);
		for (u32 i = 0; i < 64 * 2048; i++)
			if (g_ram[dst + i] != pat(fad + i / 2048, 2048, i % 2048))
			{
				err("DMA of length 0: byte wrong", fad + i / 2048, i % 2048);
				break;
			}
		sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
	}
	g_dma_fix = 0;
	printf("  len0: 20 reads through a DMA of length 0\n");
}

static void pio_in(const u8 p[12], u8* out, u32 n)
{
	spi(p);
	for (u32 i = 0; i < n; i += 2)
	{
		const u32 w = ReadMem_gdrom(GD_DATA, 2);
		out[i] = (u8)w;
		out[i + 1] = (u8)(w >> 8);
	}
}

static void want_sense(u32 key, u32 asc, u32 ascq)
{
	u8 p[12] = {0}, r[10];
	p[0] = SPI_REQ_ERROR;
	p[4] = 10;
	pio_in(p, r, 10);
	if (r[2] != key || r[8] != asc || r[9] != ascq)
		err("sense after a disc swap", (r[2] << 16) | (r[8] << 8) | r[9], (key << 16) | (asc << 8) | ascq);
}

// P34: the drive is busy for one emulated second, reports the change once, and nothing of the old disc comes back.
static void phase_swap()
{
	g_delay_us = 100;
	for (u32 n = 0; n < 24; n++)
	{
		const u32 s = 10000 + (n & 3) * 16;
		// + LOOP AHEAD: a repeat count, so the loop head holds the old disc's first sectors at the swap.
		cmd_play(s, s + 3000, g_miss >= 3 ? 1 : 0);
		for (u32 i = 0; i < 100; i++)
		{
			pull_cdda();
			usleep(100);
		}
		// Every third swap comes while a DMA runs: the drive must not hang on the buffer the swap emptied.
		const bool mid_dma = n % 3 == 2;
		if (mid_dma)
		{
			cmd_read(150 + rnd() % 400000, 64, 2048);
			dma_regs(0x200000, 64 * 2048);
			UpdateGDRom();
		}
		gdrom_disc_swap("next disc");
		M.playing = false;
		if ((ReadMem_gdrom(GD_SECTNUM, 1) & 0xF) != GD_BUSY)
			err("status after a disc swap", ReadMem_gdrom(GD_SECTNUM, 1), n);
		if (mid_dma)
		{
			for (u32 i = 0; i < 4; i++)
				UpdateGDRom();
			sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
			WriteMem_gdrom(GD_COMMAND_Write, ATA_SOFT_RESET, 1);
		}
		pull_cdda();
		// The soft reset of the DMA case clears the key and keeps the ASC, as Flycast's.
		want_sense(mid_dma ? 0 : 2, 4, 1);
		// 200 MHz: one second less a tick is still busy, then the drive is ready.
		for (u32 c = 14336; c < 200000000u - 14336; c += 14336)
			gdrom_spinup_tick(14336);
		if ((ReadMem_gdrom(GD_SECTNUM, 1) & 0xF) != GD_BUSY)
			err("drive ready before the second is over", n, 0);
		gdrom_spinup_tick(14336);
		gdrom_spinup_tick(14336);
		if ((ReadMem_gdrom(GD_SECTNUM, 1) & 0xF) != GD_PAUSE)
			err("drive not ready after the second", ReadMem_gdrom(GD_SECTNUM, 1), n);
		u8 p[12] = {0};
		p[0] = 0x70;
		spi(p);
		if (!(ReadMem_gdrom(GD_STATUS_Read, 1) & 1))
			err("no CHECK with unit attention waiting", n, 0);
		want_sense(6, 0x28, 0);
		want_sense(0, 0, 0);
		spi(p);
		if (ReadMem_gdrom(GD_STATUS_Read, 1) & 1)
			err("CHECK after the sense was read", n, 0);
		// The sectors the ring held at the swap (the play was 100 sectors in). The worker cannot answer yet, so
		// a slot that kept its tag would hand out the old disc's bytes.
		g_block_worker = 1;
		cmd_play(s + 100, s + 3000, 0);
		for (u32 i = 0; i < 8; i++)
			pull_cdda();
		g_block_worker = 0;
		for (u32 i = 0; i < 100; i++)
		{
			pull_cdda();
			usleep(100);
		}
		// The loop head the old disc left: the same test on its sectors.
		if (g_miss >= 3)
		{
			g_block_worker = 1;
			cmd_play(s, s + 3000, 0);
			for (u32 i = 0; i < 8; i++)
				pull_cdda();
			g_block_worker = 0;
		}
		test_read(150 + rnd() % 400000, pick_count(2048, 40), 2048, true);
		// Read Ahead: the ring held the sectors after a read of the old disc; the same read now needs the new bytes.
		if (g_dra)
		{
			const u32 f = 150 + rnd() % 400000;
			test_read(f, 8, 2048, false);
			usleep(3000);
			gdrom_disc_swap("next disc");
			M.playing = false;
			for (u32 c = 0; c < 200000000u + 2 * 14336; c += 14336)
				gdrom_spinup_tick(14336);
			u8 q[12] = {0};
			q[0] = 0x70;
			spi(q);
			want_sense(6, 0x28, 0);
			want_sense(0, 0, 0);
			test_read(f + 8, 8, 2048, false);
		}
	}
	printf("  swap: 24 disc swaps\n");
}

int main(int argc, char** argv)
{
	g_level = argc > 1 ? atoi(argv[1]) : 1;
	const u32 iters = argc > 2 ? (u32)atoi(argv[2]) : 2000;
	g_seed = argc > 3 ? (u64)atoll(argv[3]) * 2654435761ull + 1 : 1;
	g_miss = argc > 4 ? atoi(argv[4]) : 0;
	g_speed = argc > 5 ? atoi(argv[5]) : 0;
	g_timing = argc > 6 ? atoi(argv[6]) : 0;
	g_dra = argc > 7 ? atoi(argv[7]) : 0;
	g_main = pthread_self();

	gdrom_reg_Init();
	NotifyEvent_gdrom(DiskChange, 0);
	gda_start();
	gdrom_reg_Reset(false);

	phase_mixed(iters);
	if (g_level >= 2)
	{
		const u32 r0 = g_cdda_reads;
		phase_steady();
		phase_seeks();
		phase_loops();
		phase_play_ahead();
		// + SILENCE: with a worker that answers, the SH4 thread reads no CD-DA sector at all.
		if (g_miss >= 2 && g_cdda_reads != r0)
			err("SH4 thread CD-DA reads with SILENCE", g_cdda_reads - r0, 0);
		if (g_miss >= 2)
		{
			phase_loop_ahead();
			phase_cap();
		}
	}
	if (g_level >= 1)
	{
		phase_stream();
		phase_timing();
	}
	phase_len0();
	phase_swap();
	gda_stop();

	printf("%s: level %d, miss %d, speed %d, timing %d, read ahead %d, %u reads, %u CD-DA sectors (%u held), %d errors\n",
	       g_errors ? "FAIL" : "PASS", g_level, g_miss, g_speed, g_timing, g_dra, g_reads, g_pulls, g_held,
	       g_errors.load());
	return g_errors ? 1 : 0;
}
