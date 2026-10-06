// OPTIMIZATION_POTENTIAL P3 PC check (WSL): the real dc/gdrom/gdromv3.cpp and ps3/ps3_gdasync.cpp, pthreads for lv2,
// a fake drive. Reads, aborts and CD-DA must give the bytes, interrupts and status of a plain model at every level.
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

static int g_level;
extern "C" int get_gd_async_preset() { return g_level; }
extern "C" int get_debug_loop() { return 0; }
extern "C" int get_debug_gdrom() { return 0; }
extern "C" int get_sched_preset() { return 0; }

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
static thread_local u64 t_seed = 88172645463325252ull;
static u32 rnd_t()
{
	t_seed ^= t_seed << 13;
	t_seed ^= t_seed >> 7;
	t_seed ^= t_seed << 17;
	return (u32)t_seed;
}
static inline u8 pat(u32 fad, u32 secsz, u32 j) { return (u8)(fad * 131u + j * 7u + (j >> 8) * 13u + secsz); }
static bool on_main() { return pthread_equal(pthread_self(), g_main) != 0; }

// As plugs/ImgReader: any thread, under the disc lock. A slow write widens every race window.
void libGDR_ReadSectorLocked(u8* buff, u32 fad, u32 n, u32 secsz)
{
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
}

void FASTCALL libGDR_ReadSector(u8* buff, u32 fad, u32 n, u32 secsz)
{
	if (!on_main())
		err("libGDR_ReadSector off the SH4 thread", fad, n);
	g_sh4_reads++;
	libGDR_ReadSectorLocked(buff, fad, n, secsz);
}
void FASTCALL libGDR_GetToc(u32*, u32) {}
u32 FASTCALL libGDR_GetDiscType() { return GdRom; }
void FASTCALL libGDR_GetSessionInfo(u8* out, u8) { memset(out, 0, 6); }

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

static u32 g_pulls;
static void pull_cdda()
{
	s16 sec[1176];
	gdrom_get_cdda(sec);
	g_pulls++;
	const u8* b = (const u8*)sec;
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
		UpdateGDRom();
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
	const u32 r0 = g_sh4_reads;
	for (u32 i = 0; i < 3000; i++)
	{
		pull_cdda();
		usleep(200);
	}
	const u32 miss = g_sh4_reads - r0;
	printf("  steady: %u misses in 3000 sectors\n", miss);
	if (miss > 5)
		err("read-ahead misses in steady play", miss, 3000);
}

// Seeks both ways: a few misses each, then the read-ahead catches up.
static void phase_seeks()
{
	g_delay_us = 100;
	const u32 r0 = g_sh4_reads;
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
	printf("  seeks: %u misses in %u plays\n", miss, plays);
	if (miss > plays * 4)
		err("read-ahead misses after seeks", miss, plays);
}

// Replays and loops: play goes back inside the worker's run (HW 2026-10-06: ChuChu missed 8 s in a row).
static void phase_loops()
{
	g_delay_us = 100;
	const u32 r0 = g_sh4_reads;
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
	printf("  loops: %u misses in %u plays and loop turns\n", miss, events);
	if (miss > events * 4)
		err("read-ahead misses after loops", miss, events);
}

int main(int argc, char** argv)
{
	g_level = argc > 1 ? atoi(argv[1]) : 1;
	const u32 iters = argc > 2 ? (u32)atoi(argv[2]) : 2000;
	g_seed = argc > 3 ? (u64)atoll(argv[3]) * 2654435761ull + 1 : 1;
	g_main = pthread_self();

	gdrom_reg_Init();
	NotifyEvent_gdrom(DiskChange, 0);
	gda_start();
	gdrom_reg_Reset(false);

	phase_mixed(iters);
	if (g_level >= 2)
	{
		phase_steady();
		phase_seeks();
		phase_loops();
	}
	gda_stop();

	printf("%s: level %d, %u reads, %u CD-DA sectors, %d errors\n", g_errors ? "FAIL" : "PASS", g_level, g_reads,
	       g_pulls, g_errors.load());
	return g_errors ? 1 : 0;
}
