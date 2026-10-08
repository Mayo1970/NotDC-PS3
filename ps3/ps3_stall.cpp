// Plan 5.32 stall probe: see ps3_stall.h.
#include "types.h"
#include "ps3_stall.h"
#include <stdio.h>
#include "ps3_timebase.h"

bool g_stall_on;

static const char* const k_name[ST_N] = {"jit", "gd read", "gd dma", "maple", "dma", "rend", "spe", "sys", "log",
                                         "sleep"};

static int s_stack[16];
static u64 s_start[16];
static int s_depth;
static u64 s_t;                 // last push, pop or frame edge: open time goes to the innermost section

static u64 s_frame[ST_N];       // this frame, exclusive ticks
static u64 s_frame_t0;          // its start; 0 = no frame open
static u64 s_worst_wall, s_worst[ST_N];
static u64 s_tot[ST_N], s_max[ST_N];   // this second: exclusive ticks; longest single section, inclusive
static u32 s_n[ST_N], s_frames;

static double ms(u64 t) { return (double)t * 1000.0 / (double)ps3_timebase_hz; }

u64 g_stall_nested;

static inline void stall_add(int cat, u64 t)
{
	s_frame[cat] += t;
	if (cat != ST_SYS)
		g_stall_nested += t;
}

void stall_push_on(int cat)
{
	if (s_depth >= 16)
		return;
	const u64 now = gettime();
	if (s_depth)
		stall_add(s_stack[s_depth - 1], now - s_t);
	s_stack[s_depth] = cat;
	s_start[s_depth++] = now;
	s_t = now;
}

void stall_pop_on()
{
	// A pop with nothing open: the probe went on inside that section.
	if (!s_depth)
		return;
	const u64 now = gettime();
	const int cat = s_stack[--s_depth];
	stall_add(cat, now - s_t);
	const u64 incl = now - s_start[s_depth];
	if (incl > s_max[cat])
		s_max[cat] = incl;
	s_n[cat]++;
	s_t = now;
}

void stall_frame()
{
	const bool was_on = g_stall_on;
	g_stall_on = g_jit_ccalls_preset != 0;
	if (!g_stall_on)
	{
		s_depth = 0;
		s_frame_t0 = 0;
		return;
	}
	const u64 now = gettime();
	if (was_on && s_frame_t0)
	{
		if (s_depth)
			stall_add(s_stack[s_depth - 1], now - s_t);
		const u64 wall = now - s_frame_t0;
		if (wall > s_worst_wall)
		{
			s_worst_wall = wall;
			for (int k = 0; k < ST_N; k++)
				s_worst[k] = s_frame[k];
		}
		for (int k = 0; k < ST_N; k++)
			s_tot[k] += s_frame[k];
		s_frames++;
	}
	else
		s_depth = 0;
	for (int k = 0; k < ST_N; k++)
		s_frame[k] = 0;
	s_frame_t0 = now;
	s_t = now;
}

void stall_dump(double secs)
{
	if (!g_stall_on || !s_frames || secs <= 0.0)
		return;
	char buf[1024];
	u64 known = 0;
	for (int k = 0; k < ST_N; k++)
		known += s_worst[k];
	int n = snprintf(buf, sizeof(buf), "[stall] worst frame %.1f ms:", ms(s_worst_wall));
	for (int k = 0; k < ST_N; k++)
		if (s_worst[k])
			n += snprintf(buf + n, sizeof(buf) - n, " %s %.1f", k_name[k], ms(s_worst[k]));
	n += snprintf(buf + n, sizeof(buf) - n, " other %.1f | %u frames:",
	              ms(s_worst_wall > known ? s_worst_wall - known : 0), s_frames);
	for (int k = 0; k < ST_N && n < (int)sizeof(buf); k++)
		if (s_n[k])
			n += snprintf(buf + n, sizeof(buf) - n, " %s %.1f ms/%u max %.1f", k_name[k], ms(s_tot[k]), s_n[k],
			              ms(s_max[k]));
	printf("%s\n", buf);
	s_worst_wall = 0;
	s_frames = 0;
	for (int k = 0; k < ST_N; k++)
		s_worst[k] = s_tot[k] = s_max[k] = s_n[k] = 0;
}
