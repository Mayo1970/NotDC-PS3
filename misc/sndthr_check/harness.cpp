// OPTIMIZATION_PLAN 5.14 PC check: the real ps3/ps3_sndthr.cpp with pthreads for lv2 and a fake sound core.
// The core checks one thread at a time, entries in order and once, CD-DA sectors in order, interrupts on the SH4.
#include "../../ps3/ps3_sndthr.cpp"
bool g_arm_spe;   // 5.15 is off in this check
#include <pthread.h>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

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

// ---- fake emulator ----
static pthread_t g_sh4;
static std::atomic<int> g_errors{0};
static void err(const char* what, u64 a, u64 b)
{
	if (g_errors++ < 20)
		printf("ERROR: %s (%llu, %llu)\n", what, (unsigned long long)a, (unsigned long long)b);
}
static bool on_sh4() { return pthread_equal(pthread_self(), g_sh4) != 0; }

static u64 g_seed;
static u32 rnd_sh4()   // driver thread only
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return (u32)g_seed;
}

#define MAX_N 2000000
struct Exp { u32 cycles, samples; };
static Exp g_exp[MAX_N];
static u8 g_kind[MAX_N / 100];   // per produced sector: 1 = disc data, 0 = CD Audio off
static u32 g_prod;               // sectors produced (SH4)
static bool g_line;              // the Holly SPU line (SH4 state)

extern "C" int get_sound_thread_preset() { return 1; }
extern "C" int get_cdda_preset()
{
	if (!on_sh4())
		err("CD-DA read off the SH4 thread", 0, 0);
	const int v = (rnd_sh4() & 7) != 0;
	g_kind[g_prod++] = (u8)v;
	return v;
}
static inline s16 pat(u32 k, u32 i) { return (s16)(k * 2654435761u + i * 40503u); }
void gdrom_get_cdda(s16* sector)
{
	if (!on_sh4())
		err("GD-ROM read off the SH4 thread", 0, 0);
	// A disc read takes time: a sector published too early is then visible to the worker.
	const u64 t_end = gettime() + (rnd_sh4() % 100) * 1000;
	for (u32 i = 0; i < CDDA_WORDS / 2; i++)
		sector[i] = pat(g_prod - 1, i);
	while (gettime() < t_end) {}
	for (u32 i = CDDA_WORDS / 2; i < CDDA_WORDS; i++)
		sector[i] = pat(g_prod - 1, i);
}

bool aica_sh4_irq_apply(bool on)
{
	if (!on_sh4())
		err("Holly line changed off the SH4 thread", 0, 0);
	const bool changed = on != g_line;
	g_line = on;
	return changed;
}

// Core state: only touched by the token holder.
static std::atomic<int> g_in_core{0};
static u64 g_core_idx, g_core_samples, g_core_rng = 88172645463325252ull;
static u32 g_core_cdda;
static bool g_core_irq;
static u64 g_core_by_worker, g_core_by_sh4;
static u32 core_rnd()
{
	g_core_rng ^= g_core_rng << 13; g_core_rng ^= g_core_rng >> 7; g_core_rng ^= g_core_rng << 17;
	return (u32)g_core_rng;
}

void arm_aica_run(u32 cycles, u32 samples)
{
	if (g_in_core.exchange(1) != 0)
		err("two threads in sound code", g_core_idx, 0);
	if (g_snd_on_worker == on_sh4())
		err("g_snd_on_worker wrong for this thread", g_core_idx, on_sh4());
	(on_sh4() ? g_core_by_sh4 : g_core_by_worker)++;
	const Exp e = g_exp[g_core_idx];
	if (e.cycles != cycles || e.samples != samples)
		err("entry out of order or repeated", g_core_idx, cycles);
	g_core_idx++;
	for (u32 i = 0; i < samples; i++)
	{
		if (g_core_samples % CDDA_SAMPLES == 0)
		{
			s16 buf[CDDA_WORDS];
			snd_cdda_pop(buf);
			const u32 k = g_core_cdda++;
			for (u32 j = 0; j < CDDA_WORDS; j++)
				if (buf[j] != (g_kind[k] ? pat(k, j) : 0))
				{
					err("CD-DA sector content", k, j);
					break;
				}
		}
		g_core_samples++;
		if ((core_rnd() & 255) == 0)   // UpdateSh4Ints with a new level
		{
			g_core_irq = !g_core_irq;
			snd_irq_set(g_core_irq);
			if (!g_snd_on_worker)
				aica_sh4_irq_apply(g_core_irq);
		}
	}
	const u32 r = core_rnd();
	volatile u32 sink = 0;
	for (u32 i = 0, n = r % 3000; i < n; i++)
		sink += i;
	if ((r >> 20) % 2000 == 0)
		usleep(100 + (r >> 8) % 2000);   // a pacing wait while holding the token
	g_in_core.store(0);
}

int main(int argc, char** argv)
{
	const u64 n = argc > 1 ? strtoull(argv[1], 0, 10) : 300000;
	g_seed = argc > 2 ? strtoull(argv[2], 0, 10) * 2654435761ull + 1 : 12345;
	if (n > MAX_N)
		return 1;
	g_sh4 = pthread_self();
	snd_thread_init();
	u32 acc = 0, locks = 0, stops = 0;
	for (u64 i = 0; i < n; i++)
	{
		const u32 r = rnd_sh4();
		const u32 cycles = (r & 7) == 0 ? (r >> 8) % 20000 : 7168;
		acc += cycles;
		const u32 s = acc / 3401;
		acc %= 3401;
		g_exp[i].cycles = cycles;
		g_exp[i].samples = s;
		snd_push(cycles, s);
		const u32 r2 = rnd_sh4();
		if (r2 % 64 == 0)
		{
			snd_lock(SND_LOCK_REG);
			locks++;
			if (g_in_core.load() != 0)
				err("sound code running under snd_lock", i, 0);
			if (g_core_idx != i + 1)
				err("snd_lock did not catch up", g_core_idx, i + 1);
			if (g_line != g_core_irq)
				err("Holly line not the level of the last entry", i, g_core_irq);
			if ((r2 >> 8) % 4 == 0)
			{
				snd_lock(SND_LOCK_OTHER);
				snd_unlock();
				if (__atomic_load_n(&s_owner, __ATOMIC_RELAXED) != OWN_SH4)
					err("a nested unlock gave the token away", i, 0);
			}
			if ((r2 >> 12) % 1000 == 0)
				snd_thread_stats();
			snd_unlock();
		}
		if ((r2 >> 16) % 50000 == 0)
		{
			snd_thread_stop();
			stops++;
			if (g_core_idx != i + 1)
				err("stop left entries", g_core_idx, i + 1);
		}
		volatile u32 sink = 0;
		for (u32 k = 0, m = (r2 >> 4) % 2000; k < m; k++)
			sink += k;
		if ((r2 >> 20) % 4000 == 0)
			usleep((r2 >> 2) % 4000);   // the SH4 blocks (render wait): the worker goes idle
	}
	snd_thread_stop();
	const u64 sectors = g_core_samples ? (g_core_samples + CDDA_SAMPLES - 1) / CDDA_SAMPLES : 0;
	if (g_core_idx != n)
		err("entries run", g_core_idx, n);
	if (g_core_cdda != sectors || g_prod != sectors)
		err("CD-DA sectors produced/played", g_prod, g_core_cdda);
	if (st_cdda_miss)
		err("CD-DA pops with no sector", st_cdda_miss, 0);
	printf("%s: %llu entries (worker %llu, sh4 %llu), %llu samples, %u sectors, %u locks, %u stops, %d errors\n",
	       g_errors ? "FAIL" : "PASS", (unsigned long long)n, (unsigned long long)g_core_by_worker,
	       (unsigned long long)g_core_by_sh4, (unsigned long long)g_core_samples, g_core_cdda, locks, stops,
	       g_errors.load());
	return g_errors ? 1 : 0;
}
