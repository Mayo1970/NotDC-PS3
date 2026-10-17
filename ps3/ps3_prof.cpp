// Row SH4 Time Profile: the sampler thread (ps3_prof.h). It only reads g_prof_where, so the SH4 thread pays one store
// per block entry and two per marked C call.
#include "types.h"
#include "ps3_prof.h"
#include "ps3_phase.h"
#include "ps3_timebase.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/mem/sh4_mem.h"
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/systime.h>

volatile u32 g_prof_where;

#define PROF_US          500     // sampling period
#define PROF_REPORT_S    5
#define PROF_SLOTS       8192    // sampled blocks of one report; half full stops new ones
#define PROF_CODE_SLOTS  16384   // compiled blocks noted for the dumps
#define PROF_TOP         24
#define PROF_DUMP_TOP    8       // a report dumps its top blocks not dumped before
#define PROF_DUMP_MAX    48
#define PROF_DUMP_WORDS  512

struct ProfBlock { u32 pc, n; };
struct ProfCode { u32 pc, host; u16 hot, total, sh4; };

// One block of state, so the TOC needs one entry for it.
static struct
{
	ProfBlock tab[PROF_SLOTS];
	u32 used, lost;
	u32 parts[PROF_PARTS];
	u32 total;
	ProfCode code[PROF_CODE_SLOTS];
	u32 dumped[PROF_DUMP_MAX];
	u32 dumped_n;
	uintptr_t code_lo, code_hi;
	sys_ppu_thread_t tid;
} s_p;

static const char* const k_part[PROF_PARTS] = { "none", "ta", "mem", "sys", "int", "lookup", "link", "compile", "ifb",
                                                "sync", "other", "render" };

static inline u32 prof_hash(u32 pc) { return (pc >> 1) * 2654435761u; }

// The SH4 thread writes an entry, the sampler reads it: pc goes last, and a dump checks the range again.
void prof_note_block(u32 pc, const void* host, u32 hot_bytes, u32 total_bytes, u32 sh4_bytes)
{
	u32 i = prof_hash(pc) & (PROF_CODE_SLOTS - 1);
	for (u32 k = 0; k < 16; k++, i = (i + 1) & (PROF_CODE_SLOTS - 1))
	{
		ProfCode& c = s_p.code[i];
		if (c.pc != 0 && c.pc != pc)
			continue;
		c.pc = 0;
		__asm__ volatile("lwsync" ::: "memory");
		c.host = (u32)(uintptr_t)host;
		c.hot = (u16)(hot_bytes > 0xFFFF ? 0xFFFF : hot_bytes);
		c.total = (u16)(total_bytes > 0xFFFF ? 0xFFFF : total_bytes);
		c.sh4 = (u16)(sh4_bytes > 0xFFFF ? 0xFFFF : sh4_bytes);
		__asm__ volatile("lwsync" ::: "memory");
		c.pc = pc;
		return;
	}
}

void prof_reset_blocks()
{
	memset(s_p.code, 0, sizeof(s_p.code));
}

static void prof_count(u32 pc)
{
	u32 i = prof_hash(pc) & (PROF_SLOTS - 1);
	for (;;)
	{
		ProfBlock& b = s_p.tab[i];
		if (b.pc == pc)
		{
			b.n++;
			return;
		}
		if (b.pc == 0)
		{
			if (s_p.used >= PROF_SLOTS / 2)
			{
				s_p.lost++;
				return;
			}
			b.pc = pc;
			b.n = 1;
			s_p.used++;
			return;
		}
		i = (i + 1) & (PROF_SLOTS - 1);
	}
}

// The SH4 code (when it is in RAM) and every PPC word of the block, as JIT HOTBLOCKS dumps it.
static void prof_dump(u32 pc, double share)
{
	u32 i = prof_hash(pc) & (PROF_CODE_SLOTS - 1);
	const ProfCode* c = 0;
	for (u32 k = 0; k < 16; k++, i = (i + 1) & (PROF_CODE_SLOTS - 1))
		if (s_p.code[i].pc == pc)
		{
			c = &s_p.code[i];
			break;
		}
	if (!c)
	{
		printf("[prof] --- block %08X: %.1f%% of the time, its code was not noted\n", pc, share);
		return;
	}
	const ProfCode e = *c;
	if (e.pc != pc || e.host < s_p.code_lo || (uintptr_t)e.host + e.total > s_p.code_hi || e.hot > e.total)
		return;
	printf("[prof] --- block %08X host=%08X: %.1f%% of the time, %u SH4 bytes, hot %u B, cold %u B\n", pc, e.host,
	       share, e.sh4, e.hot, e.total - e.hot);
	const u8* sh4 = GetMemPtr(pc, e.sh4 ? e.sh4 : 2);
	for (u32 a = 0; sh4 && a < e.sh4 && a < 512; a += 2)
	{
		const u16 op = *(const u16*)(sh4 + a);
		printf("[prof] sh4 %08X %04X %s\n", pc + a, op, OpDesc[op]->diss);
	}
	const u32* w = (const u32*)(uintptr_t)e.host;
	const u32 wn = e.total / 4 < PROF_DUMP_WORDS ? e.total / 4 : PROF_DUMP_WORDS;
	for (u32 k = 0; k < wn; k += 8)
	{
		char buf[128];
		int m = snprintf(buf, sizeof(buf), "[prof] ppc +%04X", k * 4);
		for (u32 j = 0; j < 8 && k + j < wn; j++)
			m += snprintf(buf + m, sizeof(buf) - m, " %08X", w[k + j]);
		printf("%s\n", buf);
	}
}

static void prof_report(double secs)
{
	const u32 total = s_p.total ? s_p.total : 1;
	u32 parts = 0;
	for (u32 k = 0; k < PROF_PARTS; k++)
		parts += s_p.parts[k];
	char buf[512];
	int m = snprintf(buf, sizeof(buf), "[prof] %.1f s, %u samples (%.0f/s): game %.1f%% |", secs, s_p.total,
	                 s_p.total / secs, 100.0 * (s_p.total - parts) / total);
	for (u32 k = 1; k < PROF_PARTS; k++)
		m += snprintf(buf + m, sizeof(buf) - m, " %s %.1f", k_part[k], 100.0 * s_p.parts[k] / total);
	snprintf(buf + m, sizeof(buf) - m, " none %.1f | blocks %u lost %u", 100.0 * s_p.parts[PROF_NONE] / total,
	         s_p.used, s_p.lost);
	printf("%s\n", buf);

	// The top blocks by samples: a selection into top[], largest first.
	u32 top[PROF_TOP], tn = 0;
	for (u32 i = 0; i < PROF_SLOTS; i++)
	{
		const ProfBlock& b = s_p.tab[i];
		if (!b.pc || (tn == PROF_TOP && b.n <= s_p.tab[top[PROF_TOP - 1]].n))
			continue;
		u32 j = tn < PROF_TOP ? tn++ : PROF_TOP - 1;
		while (j > 0 && s_p.tab[top[j - 1]].n < b.n)
		{
			top[j] = top[j - 1];
			j--;
		}
		top[j] = i;
	}
	for (u32 a = 0; a < tn; a += 8)
	{
		m = snprintf(buf, sizeof(buf), "[prof] top %u-%u:", a + 1, a + 8 < tn ? a + 8 : tn);
		for (u32 j = a; j < a + 8 && j < tn; j++)
			m += snprintf(buf + m, sizeof(buf) - m, " %08X %.2f", s_p.tab[top[j]].pc, 100.0 * s_p.tab[top[j]].n / total);
		printf("%s\n", buf);
	}
	for (u32 a = 0; a < tn && a < PROF_DUMP_TOP && s_p.dumped_n < PROF_DUMP_MAX; a++)
	{
		const u32 pc = s_p.tab[top[a]].pc;
		bool seen = false;
		for (u32 j = 0; j < s_p.dumped_n && !seen; j++)
			seen = s_p.dumped[j] == pc;
		if (seen)
			continue;
		s_p.dumped[s_p.dumped_n++] = pc;
		prof_dump(pc, 100.0 * s_p.tab[top[a]].n / total);
	}
	fflush(stdout);
	memset(s_p.tab, 0, sizeof(s_p.tab));
	memset(s_p.parts, 0, sizeof(s_p.parts));
	s_p.used = s_p.lost = s_p.total = 0;
}

static void prof_thread(void*)
{
	u64 t0 = gettime();
	const u64 every = (u64)PROF_REPORT_S * ps3_timebase_hz;
	for (;;)
	{
		sysUsleep(PROF_US);
		const u32 w = g_prof_where;
		s_p.total++;
		if ((w & 0xFFFFFF00u) == 0xFFFFFF00u)
		{
			u32 k = w & 0xFF;
			// A memory call while StartRender or an RTT runs is the render's.
			if (k == PROF_MEM && g_sh4_phase != PH_GUEST)
				k = PROF_RENDER;
			s_p.parts[k < PROF_PARTS ? k : PROF_OTHER]++;
		}
		else if (!w)
			s_p.parts[PROF_NONE]++;
		else
			prof_count(w);
		const u64 now = gettime();
		if (now - t0 >= every)
		{
			prof_report((double)(now - t0) / (double)ps3_timebase_hz);
			t0 = now;
		}
	}
}

void prof_start(uintptr_t code_lo, uintptr_t code_hi)
{
	s_p.code_lo = code_lo;
	s_p.code_hi = code_hi;
	// Above the SH4 and render threads (1000 and up), so a sample is taken on time.
	const s32 r = sysThreadCreate(&s_p.tid, prof_thread, NULL, 200, 0x10000, 0, (char*)"nulldc_prof");
	printf("[prof] SH4 Time Profile %s: a sample each %u us, a report each %u s\n", r == 0 ? "on" : "FAILED to start",
	       PROF_US, PROF_REPORT_S);
}
