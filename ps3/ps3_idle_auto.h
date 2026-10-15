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

struct IdleAuto
{
	int rule;              // IA_X3 or IA_X30
	u32 slow, spare;       // the windows in a row that count to a switch
	u32 wait;              // X30 windows left before a try
	u32 hold, x3_n;        // the next wait, and the X3 windows since the last try
};

static inline void idle_auto_init(IdleAuto& a)
{
	a.rule = IA_X3;
	a.slow = a.spare = a.wait = 0;
	a.hold = IA_HOLD_MIN;
	a.x3_n = IA_SOON;      // the boot is no try: its first fall back keeps the shortest hold
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

// One stats window of secs seconds; sleep_s is the pacer's sleep in it. True when the rule changed: the caller then
// patches the bill of every compiled block (ps3_driver.cpp), so the next window runs all at the new level.
static inline bool idle_auto_step(IdleAuto& a, double secs, double speed, double sleep_s)
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

enum { FA_NONE, FA_TRY, FA_KEEP, FA_BACK };

struct FwdAuto
{
	int plain;             // 1 = PLAIN now
	int trying;            // 1 = the level now is a try
	u32 run, n;            // the windows of the run, and of the try
	double run_fps, base;  // frames a second summed over the run; the run's mean as the try began
	double try_fps, mean;  // summed over the try; the last try's mean
	u32 wait, hold;        // windows left before a run counts; the wait after the next failed try
};

static inline void fwd_auto_init(FwdAuto& f)
{
	f.plain = 1;
	f.trying = 0;
	f.run = f.n = 0;
	f.run_fps = f.base = f.try_fps = f.mean = 0.0;
	f.wait = 0;
	f.hold = FA_HOLD_MIN;
}

// JIT IDLE RULE AUTO left X30: a try ends at the level before it, and the run starts again.
static inline void fwd_auto_cancel(FwdAuto& f)
{
	if (f.trying)
		f.plain = !f.plain;
	f.trying = 0;
	f.run = f.n = 0;
	f.run_fps = f.try_fps = 0.0;
}

// One window at the X30 level. FA_TRY and FA_BACK change the level (the caller patches the bills); FA_KEEP ends a
// try at its level.
static inline int fwd_auto_step(FwdAuto& f, double secs, double speed, double sleep_s, double fps)
{
	if (secs <= 0.0)
		return FA_NONE;
	if (f.trying)
	{
		f.try_fps += fps;
		if (++f.n < FA_N)
			return FA_NONE;
		f.mean = f.try_fps / FA_N;
		const bool keep = f.plain ? f.mean >= f.base * FA_PLAIN_KEEP : f.mean > f.base * FA_X30_GAIN;
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
	const bool counts = f.plain ? speed < IA_LOW : speed >= IA_FULL && sleep_s >= IA_SPARE * secs;
	if (!counts)
	{
		f.run = 0;
		f.run_fps = 0.0;
		return FA_NONE;
	}
	f.run_fps += fps;
	if (++f.run < FA_N)
		return FA_NONE;
	f.base = f.run_fps / FA_N;
	f.run = 0;
	f.run_fps = 0.0;
	f.plain = !f.plain;
	f.trying = 1;
	return FA_TRY;
}
