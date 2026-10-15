#pragma once
// JIT IDLE RULE AUTO (P71): the decoder bills X3 BACKWARD while the host keeps up, X30 while it falls behind. One
// step per stats window; misc/idleauto_check runs it on the PC.
#include "types.h"

#define IA_X3        1
#define IA_X30       0
#define IA_LOW       95.0   // SPEED % under which an X3 window is slow
#define IA_SLOW_N    3      // slow X3 windows in a row that go to X30
#define IA_FULL      98.0   // an X30 window must reach this SPEED %
#define IA_SPARE     0.3    // and the pacer must sleep this share of it
#define IA_SPARE_N   3      // such X30 windows in a row that try X3 again
#define IA_HOLD_MIN  5      // X30 windows before an X3 try
#define IA_HOLD_MAX  160
#define IA_SOON      10     // an X3 try that goes back within this many windows doubles the hold
#define IA_LOAD_K    1.0    // JIT AUTO CHECKS FRAMES: a spare window needs this many K vertices a frame

struct IdleAuto
{
	int rule;              // IA_X3 or IA_X30
	u32 slow, spare;       // the windows in a row that count to a switch
	u32 wait;              // X30 windows left before a try
	u32 hold, x3_n;        // the next wait, and the X3 windows since the last try
	int frames;            // JIT AUTO CHECKS: 1 = FRAMES (no X3 try from a loading screen), 0 = OLD
};

static inline void idle_auto_init(IdleAuto& a)
{
	a.rule = IA_X3;
	a.slow = a.spare = a.wait = 0;
	a.hold = IA_HOLD_MIN;
	a.x3_n = IA_SOON;      // the boot is no try: its first fall back keeps the shortest hold
	a.frames = 0;
}

// A compiled block's bill word, `addi` or `addic.` of a register to itself: the same word billing cycles now. False
// for any other word.
static inline bool idle_bill_word(u32& w, u32 cycles)
{
	const u32 op = w >> 26;
	if ((op != 14 && op != 13) || ((w >> 21) & 31) != ((w >> 16) & 31) || cycles > 0x8000)
		return false;
	w = (w & 0xFFFF0000u) | ((0u - cycles) & 0xFFFFu);
	return true;
}

// One stats window of secs seconds; sleep_s is the pacer's sleep in it, vt_k its K vertices a frame. True when the
// rule changed: the caller patches the bill of every compiled block (ps3_driver.cpp).
static inline bool idle_auto_step(IdleAuto& a, double secs, double speed, double sleep_s, double vt_k = 0.0)
{
	if (secs <= 0.0)
		return false;
	if (a.rule == IA_X3)
	{
		a.x3_n++;
		a.slow = speed < IA_LOW ? a.slow + 1 : 0;
		if (a.slow < IA_SLOW_N)
			return false;
		a.hold = a.x3_n < IA_SOON ? (a.hold * 2 < IA_HOLD_MAX ? a.hold * 2 : IA_HOLD_MAX) : IA_HOLD_MIN;
		a.wait = a.hold;
		a.rule = IA_X30;
	}
	else
	{
		if (a.wait)
		{
			a.wait--;
			return false;
		}
		a.spare = speed >= IA_FULL && sleep_s >= IA_SPARE * secs ? a.spare + 1 : 0;
		if (a.frames && vt_k < IA_LOAD_K)	// FRAMES: a loading screen tells nothing of the game's X3 cost
			a.spare = 0;
		if (a.spare < IA_SPARE_N)
			return false;
		a.x3_n = 0;
		a.rule = IA_X3;
	}
	a.slow = a.spare = 0;
	return true;
}

// Bundle 21, JIT X30 FORWARD AUTO: at the X30 level, PLAIN (a forward-branch block bills its own cycles) or X30, kept
// by the frames a second on screen. A slow PLAIN run tries X30, a spare X30 run tries PLAIN.
#define FA_N          3      // windows of a run, and of a try
#define FA_X30_GAIN   1.05   // an X30 try stays only with more than this many times PLAIN's frames
#define FA_PLAIN_KEEP 0.97   // a PLAIN try stays with this share of X30's frames: PLAIN keeps the game's own timing
#define FA_HOLD_MIN   5      // windows after a try before a run counts
#define FA_HOLD_MAX   160
// JIT AUTO CHECKS FRAMES: X30 tries PLAIN when it drops the game's frames with some sleep; a try at another load,
// or with no frames, is void.
#define FA_MIN_FPS    2.0    // fewer frames a second on screen: the window counts for no run, the try is void
#define FA_SPARE      0.05   // the share of sleep an X30 run window needs
#define FA_DROP       0.97   // an X30 run under this share of the peak frames per emulated second drops frames
#define FA_PEAK_N     10     // the peak: the best of this many windows at the X30 level
#define FA_LOAD_DIFF  0.20   // a try whose K vertices a frame differ from its run's by this share...
#define FA_LOAD_MIN_K 0.5    // ...and by this many K is void

enum { FA_NONE, FA_TRY, FA_KEEP, FA_BACK, FA_VOID };

struct FwdAuto
{
	int plain;             // 1 = PLAIN now
	int trying;            // 1 = the level now is a try
	u32 run, n;            // the windows of the run, and of the try
	double run_fps, base;  // frames a second summed over the run; the run's mean as the try began
	double try_fps, mean;  // summed over the try; the last try's mean
	u32 wait, hold;        // windows left before a run counts; the wait after the next failed try
	int frames;            // JIT AUTO CHECKS: 1 = FRAMES, 0 = OLD
	double run_vt, base_vt, try_vt, mean_vt;	// FRAMES: K vertices a frame, as the frames a second above
	double run_efps;       // FRAMES: frames per emulated second summed over the run
	double peak[FA_PEAK_N];	// FRAMES: frames per emulated second of the last windows (0: no frames)
	u32 peak_i;
};

static inline void fwd_auto_init(FwdAuto& f)
{
	f.plain = 1;
	f.trying = 0;
	f.run = f.n = 0;
	f.run_fps = f.base = f.try_fps = f.mean = 0.0;
	f.wait = 0;
	f.hold = FA_HOLD_MIN;
	f.frames = 0;
	f.run_vt = f.base_vt = f.try_vt = f.mean_vt = f.run_efps = 0.0;
	for (u32 i = 0; i < FA_PEAK_N; i++)
		f.peak[i] = 0.0;
	f.peak_i = 0;
}

// JIT IDLE RULE AUTO left X30: a try ends at the level before it, and the run starts again.
static inline void fwd_auto_cancel(FwdAuto& f)
{
	f.run_vt = f.try_vt = f.run_efps = 0.0;	// FRAMES: so do its loads, and the peak
	for (u32 i = 0; i < FA_PEAK_N; i++)
		f.peak[i] = 0.0;
	if (f.trying)
		f.plain = !f.plain;
	f.trying = 0;
	f.run = f.n = 0;
	f.run_fps = f.try_fps = 0.0;
}

// FRAMES: the best frames per emulated second of the last FA_PEAK_N windows at the X30 level.
static inline double fa_peak(const FwdAuto& f)
{
	double p = 0.0;
	for (u32 i = 0; i < FA_PEAK_N; i++)
		if (f.peak[i] > p)
			p = f.peak[i];
	return p;
}

// FRAMES: a run window has frames on screen; PLAIN's is slow, X30's is at full speed with some sleep.
static inline bool fa_counts(const FwdAuto& f, double secs, double speed, double sleep_s, double fps)
{
	if (fps < FA_MIN_FPS)
		return false;
	if (f.plain)
		return speed < IA_LOW;
	return speed >= IA_FULL && sleep_s >= FA_SPARE * secs;
}

// FRAMES, at a run's end: its load for the try's test; an X30 run that drops no frames tries no PLAIN.
static inline bool fa_run_tries(FwdAuto& f)
{
	f.base_vt = f.run_vt / FA_N;
	return f.plain || f.run_efps / FA_N < FA_DROP * fa_peak(f);
}

// FRAMES: a try at another load, or with no frames, says nothing of its level.
static inline bool fa_void(const FwdAuto& f)
{
	if (f.mean < FA_MIN_FPS || f.base < FA_MIN_FPS)
		return true;
	const double d = f.mean_vt > f.base_vt ? f.mean_vt - f.base_vt : f.base_vt - f.mean_vt;
	const double big = f.mean_vt > f.base_vt ? f.mean_vt : f.base_vt;
	return d > FA_LOAD_DIFF * big && d > FA_LOAD_MIN_K;
}

// FRAMES: a void try goes back to the level before it; the hold stays, so the next try comes as soon as before.
static inline int fa_void_back(FwdAuto& f)
{
	f.trying = 0;
	f.n = 0;
	f.try_fps = 0.0;
	f.plain = !f.plain;
	f.wait = f.hold;
	return FA_VOID;
}

// One window at the X30 level; vt_k: its K vertices a frame. FA_TRY, FA_BACK and FA_VOID change the level (the
// caller patches the bills); FA_KEEP ends a try at its level.
static inline int fwd_auto_step(FwdAuto& f, double secs, double speed, double sleep_s, double fps, double vt_k = 0.0)
{
	if (secs <= 0.0)
		return FA_NONE;
	const double efps = speed > 0.0 ? fps * 100.0 / speed : 0.0;	// frames per emulated second
	if (f.frames)
	{
		f.peak[f.peak_i] = fps >= FA_MIN_FPS ? efps : 0.0;
		f.peak_i = (f.peak_i + 1) % FA_PEAK_N;
	}
	if (f.trying)
	{
		f.try_fps += fps;
		f.try_vt += vt_k;
		if (++f.n < FA_N)
			return FA_NONE;
		f.mean = f.try_fps / FA_N;
		f.mean_vt = f.try_vt / FA_N;
		f.try_vt = 0.0;
		const bool keep = f.plain ? f.mean >= f.base * FA_PLAIN_KEEP : f.mean > f.base * FA_X30_GAIN;
		if (f.frames && fa_void(f))
			return fa_void_back(f);
		f.trying = 0;
		f.n = 0;
		f.try_fps = 0.0;
		if (keep)
		{
			f.hold = FA_HOLD_MIN;
			f.wait = f.hold;
			return FA_KEEP;
		}
		f.plain = !f.plain;
		f.hold = f.hold * 2 < FA_HOLD_MAX ? f.hold * 2 : FA_HOLD_MAX;
		f.wait = f.hold;
		return FA_BACK;
	}
	if (f.wait)
	{
		f.wait--;
		return FA_NONE;
	}
	const bool counts = f.frames ? fa_counts(f, secs, speed, sleep_s, fps) :
	                    f.plain ? speed < IA_LOW : speed >= IA_FULL && sleep_s >= IA_SPARE * secs;
	if (!counts)
	{
		f.run = 0;
		f.run_fps = 0.0;
		return FA_NONE;
	}
	if (!f.run)
		f.run_vt = f.run_efps = 0.0;	// FRAMES: a new run
	f.run_fps += fps;
	f.run_vt += vt_k;
	f.run_efps += efps;
	if (++f.run < FA_N)
		return FA_NONE;
	f.base = f.run_fps / FA_N;
	f.run = 0;
	f.run_fps = 0.0;
	if (f.frames && !fa_run_tries(f))
		return FA_NONE;
	f.plain = !f.plain;
	f.trying = 1;
	return FA_TRY;
}
