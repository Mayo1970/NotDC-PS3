// JIT IDLE RULE AUTO, PC check (WSL): ps3/ps3_idle_auto.h on host models, one window at a time. The expected
// windows and holds are written out here, not taken from the header's numbers.
#include "ps3/ps3_idle_auto.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

static u32 g_code[8];
static u32 g_n;
void ppc_emit(u32 insn) { g_code[g_n++] = insn; }
void* emit_GetCCPtr() { return &g_code[g_n]; }
#include "emitter/PPCEmit/ppc_emitter.h"

static u32 g_checks, g_fails;
static void expect(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void expect(bool ok, const char* fmt, ...)
{
	g_checks++;
	if (ok || g_fails++ >= 12)
		return;
	va_list ap;
	va_start(ap, fmt);
	fputs("FAIL: ", stdout);
	vfprintf(stdout, fmt, ap);
	fputc('\n', stdout);
	va_end(ap);
}

struct Win { double secs, speed, sleep; };
struct Event { u32 at; int to; u32 hold; };
typedef Win (*Model)(u32 i, int rule);

static std::vector<Event> run(Model m, u32 n)
{
	IdleAuto a;
	idle_auto_init(a);
	std::vector<Event> ev;
	for (u32 i = 1; i <= n; i++)
	{
		const Win w = m(i, a.rule);
		if (idle_auto_step(a, w.secs, w.speed, w.sleep))
			ev.push_back({ i, a.rule, a.hold });
	}
	return ev;
}

static void same(const char* what, const std::vector<Event>& got, const std::vector<Event>& want)
{
	expect(got.size() == want.size(), "%s: %u switches, want %u", what, (u32)got.size(), (u32)want.size());
	for (u32 i = 0; i < got.size() && i < want.size(); i++)
		expect(got[i].at == want[i].at && got[i].to == want[i].to && got[i].hold == want[i].hold,
		       "%s: switch %u at window %u to %d hold %u, want %u to %d hold %u", what, i + 1, got[i].at, got[i].to,
		       got[i].hold, want[i].at, want[i].to, want[i].hold);
}

// The host keeps up at X3: no switch.
static Win m_steady(u32, int) { return { 1.0, 100.0, 0.5 }; }
// Tomb Raider: X3 at 37%, X30 at full speed with little sleep.
static Win m_x30_game(u32, int rule) { return rule ? Win{ 1.0, 37.0, 0.0 } : Win{ 1.0, 99.0, 0.1 }; }
// Donald Duck: menus to 16, the video 17-56 (60% at X3; full speed and 15% sleep at X30), then play.
static Win m_dd(u32 i, int rule)
{
	if (i >= 17 && i <= 56)
		return rule ? Win{ 1.0, 60.0, 0.0 } : Win{ 1.0, 100.0, 0.15 };
	return rule ? Win{ 1.0, 100.0, 0.4 } : Win{ 1.0, 100.0, 0.6 };
}
// X3 never keeps up, X30 has room: every try fails at once.
static Win m_fail(u32, int rule) { return rule ? Win{ 1.0, 70.0, 0.0 } : Win{ 1.0, 100.0, 0.4 }; }
// The first X3 try holds to window 60, the next fails at once.
static Win m_hold_ok(u32 i, int rule)
{
	if (!rule)
		return { 1.0, 100.0, 0.5 };
	return (i <= 3 || i > 60) ? Win{ 1.0, 60.0, 0.0 } : Win{ 1.0, 100.0, 0.5 };
}
// The first X3 try (window 11) goes back after 9 X3 windows: soon. After 10 it is not.
static Win m_soon9(u32 i, int rule)
{
	if (!rule)
		return { 1.0, 100.0, 0.5 };
	return (i <= 3 || i > 17) ? Win{ 1.0, 60.0, 0.0 } : Win{ 1.0, 100.0, 0.5 };
}
static Win m_soon10(u32 i, int rule)
{
	if (!rule)
		return { 1.0, 100.0, 0.5 };
	return (i <= 3 || i > 18) ? Win{ 1.0, 60.0, 0.0 } : Win{ 1.0, 100.0, 0.5 };
}
// Slow and full X3 windows in turn: never three slow ones in a row.
static Win m_turns(u32 i, int) { return { 1.0, (i & 1) ? 60.0 : 100.0, 0.0 }; }

static void t_models()
{
	same("steady", run(m_steady, 600), {});
	same("X30 game", run(m_x30_game, 1000), { { 3, IA_X30, 5 } });
	same("Donald Duck", run(m_dd, 400), { { 19, IA_X30, 5 }, { 59, IA_X3, 5 } });
	same("a try that holds", run(m_hold_ok, 80),
	     { { 3, IA_X30, 5 }, { 11, IA_X3, 5 }, { 63, IA_X30, 5 }, { 71, IA_X3, 5 }, { 74, IA_X30, 10 } });
	same("a try back after 9 X3 windows", run(m_soon9, 25), { { 3, IA_X30, 5 }, { 11, IA_X3, 5 }, { 20, IA_X30, 10 } });
	same("a try back after 10 X3 windows", run(m_soon10, 25), { { 3, IA_X30, 5 }, { 11, IA_X3, 5 }, { 21, IA_X30, 5 } });
	same("slow in turns", run(m_turns, 600), {});

	// Each X30 switch at w with hold h: X3 at w + h + 3 (h waits, 3 spare), X30 again 3 later with the hold doubled.
	std::vector<Event> want = { { 3, IA_X30, 5 } };
	u32 w = 3, h = 5;
	while (true)
	{
		const u32 x3 = w + h + 3;
		if (x3 > 2000)
			break;
		want.push_back({ x3, IA_X3, h });
		w = x3 + 3;
		h = h * 2 > 160 ? 160 : h * 2;
		if (w > 2000)
			break;
		want.push_back({ w, IA_X30, h });
	}
	same("every try fails", run(m_fail, 2000), want);
}

static IdleAuto at_x30()
{
	IdleAuto a;
	idle_auto_init(a);
	a.rule = IA_X30;
	return a;
}

// n windows of one kind; the window of the switch (1-based), or 0.
static u32 feed(IdleAuto& a, u32 n, double secs, double speed, double sleep)
{
	for (u32 i = 1; i <= n; i++)
		if (idle_auto_step(a, secs, speed, sleep))
			return i;
	return 0;
}

static void t_edges()
{
	IdleAuto a;
	idle_auto_init(a);
	expect(a.rule == IA_X3 && a.hold == 5 && a.x3_n == 10 && !a.slow && !a.spare && !a.wait,
	       "init: rule %d hold %u x3_n %u", a.rule, a.hold, a.x3_n);
	expect(feed(a, 10, 1.0, 95.0, 0.0) == 0, "X3 at 95.0%% is not slow");
	idle_auto_init(a);
	expect(feed(a, 10, 1.0, 94.99, 0.0) == 3, "X3 at 94.99%% is slow");

	a = at_x30();
	expect(feed(a, 10, 1.0, 98.0, 0.3) == 3, "X30 at 98.0%% with 30%% sleep tries X3");
	a = at_x30();
	expect(feed(a, 10, 1.0, 97.99, 0.9) == 0, "X30 at 97.99%% does not try X3");
	a = at_x30();
	expect(feed(a, 10, 1.0, 100.0, 0.2999) == 0, "X30 with 29.99%% sleep does not try X3");
	a = at_x30();
	expect(feed(a, 10, 2.0, 100.0, 0.5) == 0, "a 2 s window with 0.5 s sleep is 25%%: no try");
	a = at_x30();
	expect(feed(a, 10, 2.0, 100.0, 0.6) == 3, "a 2 s window with 0.6 s sleep is 30%%: a try");

	// A spare run broken by one window starts again.
	a = at_x30();
	u32 got = 0;
	const Win k_runs[] = { { 1, 100, 0.5 }, { 1, 100, 0.5 }, { 1, 100, 0.1 }, { 1, 100, 0.5 }, { 1, 100, 0.5 },
	                       { 1, 100, 0.5 } };
	for (u32 i = 0; i < 6 && !got; i++)
		if (idle_auto_step(a, k_runs[i].secs, k_runs[i].speed, k_runs[i].sleep))
			got = i + 1;
	expect(got == 6, "a broken spare run: switch at %u, want 6", got);

	// A window of no time changes nothing.
	idle_auto_init(a);
	idle_auto_step(a, 1.0, 60.0, 0.0);
	idle_auto_step(a, 1.0, 60.0, 0.0);
	IdleAuto b = a;
	expect(!idle_auto_step(a, 0.0, 60.0, 0.0) && !memcmp(&a, &b, sizeof(a)), "a window of 0 s changed the state");
	expect(!idle_auto_step(a, -1.0, 60.0, 0.0) && !memcmp(&a, &b, sizeof(a)), "a window of -1 s changed the state");
	expect(idle_auto_step(a, 1.0, 60.0, 0.0) && a.rule == IA_X30, "the third slow window after a 0 s one");

	// The window after a switch counts at once: the hold waits, then 3 spare windows try X3.
	expect(!a.slow && !a.spare && a.wait == 5, "after X30: slow %u spare %u wait %u", a.slow, a.spare, a.wait);
	expect(feed(a, 4, 1.0, 100.0, 1.0) == 0 && a.wait == 1 && !a.spare, "four waits: wait %u spare %u", a.wait,
	       a.spare);
	expect(!idle_auto_step(a, 1.0, 100.0, 1.0) && !a.wait && !a.spare, "the last wait counted as spare");
	expect(feed(a, 3, 1.0, 100.0, 1.0) == 3 && a.rule == IA_X3 && !a.x3_n, "the try: rule %d x3_n %u", a.rule, a.x3_n);
	// And the X3 windows after the try count at once.
	expect(feed(a, 3, 1.0, 60.0, 0.0) == 3 && a.rule == IA_X30 && a.hold == 10, "back at once: rule %d hold %u",
	       a.rule, a.hold);
}

// The patch of a bill word: the words of ngen_Begin's three paths, as the emitter makes them, then other words.
static void t_bill_word()
{
	g_n = 0;
	ppc_addi(ppc_r29, ppc_r29, -3);
	ppc_addic(ppc_r29, ppc_r29, -3, 1);
	expect(g_code[0] == 0x3BBDFFFDu && g_code[1] == 0x37BDFFFDu, "the emitter's bills: %08X %08X", g_code[0], g_code[1]);
	static const struct { u32 in, cycles; bool ok; u32 out; } k[] = {
		{ 0x3BBDFFFDu, 90, true, 0x3BBDFFA6u },     // addi r29,r29,-3 to 90
		{ 0x37BDFFFDu, 224, true, 0x37BDFF20u },    // addic. r29,r29,-3 to 224
		{ 0x3BBDFFA6u, 1, true, 0x3BBDFFFFu },
		{ 0x3BBDFFFDu, 0, true, 0x3BBD0000u },
		{ 0x3BBDFFFDu, 0x8000, true, 0x3BBD8000u },
		{ 0x3BBDFFFDu, 0x8001, false, 0x3BBDFFFDu }, // no 16-bit immediate
		{ 0x3863FFE2u, 90, true, 0x3863FFA6u },     // addi r3,r3,-30: any register to itself
		{ 0x3FBDFFFDu, 90, false, 0x3FBDFFFDu },    // addis
		{ 0x33BDFFFDu, 90, false, 0x33BDFFFDu },    // addic without the record bit
		{ 0x387DFFFDu, 90, false, 0x387DFFFDu },    // addi r3,r29: not to itself
		{ 0x63BD0003u, 90, false, 0x63BD0003u },    // ori r29,r29,3
		{ 0x48000000u, 90, false, 0x48000000u },    // b
	};
	for (u32 i = 0; i < sizeof(k) / sizeof(k[0]); i++)
	{
		u32 w = k[i].in;
		const bool ok = idle_bill_word(w, k[i].cycles);
		expect(ok == k[i].ok && w == k[i].out, "bill word %08X to %u: %d %08X, want %d %08X", k[i].in, k[i].cycles, ok,
		       w, k[i].ok, k[i].out);
	}
}

// Bundle 21, JIT X30 FORWARD AUTO: model windows by the choice now (1 = PLAIN), with the frames on screen.
struct FWin { double secs, speed, sleep, fps; };
struct FEvent { u32 at; int step, plain; u32 hold; };
typedef FWin (*FModel)(u32 i, int plain);

static std::vector<FEvent> frun(FModel m, u32 n)
{
	FwdAuto f;
	fwd_auto_init(f);
	std::vector<FEvent> ev;
	for (u32 i = 1; i <= n; i++)
	{
		const FWin w = m(i, f.plain);
		const int s = fwd_auto_step(f, w.secs, w.speed, w.sleep, w.fps);
		if (s != FA_NONE)
			ev.push_back({ i, s, f.plain, f.hold });
	}
	return ev;
}

static void fsame(const char* what, const std::vector<FEvent>& got, const std::vector<FEvent>& want)
{
	expect(got.size() == want.size(), "%s: %u steps, want %u", what, (u32)got.size(), (u32)want.size());
	for (u32 i = 0; i < got.size() && i < want.size(); i++)
		expect(got[i].at == want[i].at && got[i].step == want[i].step && got[i].plain == want[i].plain &&
		       got[i].hold == want[i].hold, "%s: step %u at window %u: %d plain %d hold %u, want %u: %d plain %d hold %u",
		       what, i + 1, got[i].at, got[i].step, got[i].plain, got[i].hold, want[i].at, want[i].step, want[i].plain,
		       want[i].hold);
}

// SA2 with SPE AICA Defer: PLAIN keeps up, so no try.
static FWin f_sa2(u32, int plain) { return plain ? FWin{ 1.0, 100.0, 0.2, 60.0 } : FWin{ 1.0, 100.0, 0.3, 46.0 }; }
// Sonic Shuffle (HW runs 6, 7): PLAIN slow at 21 frames, X30 at full speed with room at 26.
static FWin f_shuffle(u32, int plain) { return plain ? FWin{ 1.0, 66.0, 0.0, 21.0 } : FWin{ 1.0, 98.0, 0.5, 26.0 }; }
// The Looney Tunes race at 28K: PLAIN at 90% with 54 frames, X30 at 97% with no room and 29.
static FWin f_race(u32, int plain) { return plain ? FWin{ 1.0, 90.0, 0.0, 54.0 } : FWin{ 1.0, 97.0, 0.0, 29.0 }; }
// Its boot video: PLAIN at 70% with 41 frames, X30 at full speed with 60 and little room.
static FWin f_video(u32, int plain) { return plain ? FWin{ 1.0, 70.0, 0.0, 41.0 } : FWin{ 1.0, 100.0, 0.1, 60.0 }; }
// A heavy scene to window 10, then a light one: X30 wins the first, PLAIN the second.
static FWin f_scene(u32 i, int plain)
{
	if (plain)
		return i <= 10 ? FWin{ 1.0, 80.0, 0.0, 20.0 } : FWin{ 1.0, 100.0, 0.4, 30.0 };
	return { 1.0, 100.0, 0.5, 25.0 };
}

// A try that fails at window b with the hold h: the next try waits h windows and a run of 3, then fails 3 later.
static void fails_from(std::vector<FEvent>& want, u32 b, u32 h, int at_try, u32 n)
{
	while (true)
	{
		const u32 t = b + h + 3;
		if (t > n)
			break;
		want.push_back({ t, FA_TRY, at_try, h });
		b = t + 3;
		h = h * 2 > 160 ? 160 : h * 2;
		if (b > n)
			break;
		want.push_back({ b, FA_BACK, !at_try, h });
	}
}

static void t_fwd_models()
{
	fsame("SA2", frun(f_sa2, 600), {});
	std::vector<FEvent> want = { { 3, FA_TRY, 0, 5 }, { 6, FA_KEEP, 0, 5 } };
	fails_from(want, 6, 5, 1, 2000);
	fsame("Sonic Shuffle", frun(f_shuffle, 2000), want);
	want = { { 3, FA_TRY, 0, 5 }, { 6, FA_BACK, 1, 10 } };
	fails_from(want, 6, 10, 0, 2000);
	fsame("the race", frun(f_race, 2000), want);
	fsame("the boot video", frun(f_video, 600), { { 3, FA_TRY, 0, 5 }, { 6, FA_KEEP, 0, 5 } });
	fsame("a scene each", frun(f_scene, 600),
	      { { 3, FA_TRY, 0, 5 }, { 6, FA_KEEP, 0, 5 }, { 14, FA_TRY, 1, 5 }, { 17, FA_KEEP, 1, 5 } });
}

static FwdAuto f_at(int plain)
{
	FwdAuto f;
	fwd_auto_init(f);
	f.plain = plain;
	return f;
}

// n windows of one kind; the window of the first step that is not FA_NONE (1-based), or 0. *step gets it.
static u32 ffeed(FwdAuto& f, u32 n, double secs, double speed, double sleep, double fps, int* step = 0)
{
	for (u32 i = 1; i <= n; i++)
	{
		const int s = fwd_auto_step(f, secs, speed, sleep, fps);
		if (s != FA_NONE)
		{
			if (step)
				*step = s;
			return i;
		}
	}
	return 0;
}

static void t_fwd_edges()
{
	FwdAuto f;
	fwd_auto_init(f);
	expect(f.plain == 1 && !f.trying && f.hold == 5 && !f.wait && !f.run && !f.n, "fwd init: plain %d hold %u", f.plain,
	       f.hold);
	expect(ffeed(f, 10, 1.0, 95.0, 0.0, 30.0) == 0, "PLAIN at 95.0%% is not slow");
	f = f_at(1);
	expect(ffeed(f, 10, 1.0, 94.99, 0.0, 30.0) == 3 && !f.plain && f.trying, "PLAIN at 94.99%% is slow");
	f = f_at(0);
	expect(ffeed(f, 10, 1.0, 98.0, 0.3, 30.0) == 3 && f.plain && f.trying, "X30 at 98.0%% with 30%% sleep tries PLAIN");
	f = f_at(0);
	expect(ffeed(f, 10, 1.0, 97.99, 0.9, 30.0) == 0, "X30 at 97.99%% tries nothing");
	f = f_at(0);
	expect(ffeed(f, 10, 1.0, 100.0, 0.2999, 30.0) == 0, "X30 with 29.99%% sleep tries nothing");
	f = f_at(0);
	expect(ffeed(f, 10, 2.0, 100.0, 0.5, 30.0) == 0, "a 2 s X30 window with 0.5 s sleep is 25%%: no try");
	f = f_at(0);
	expect(ffeed(f, 10, 2.0, 100.0, 0.6, 30.0) == 3, "a 2 s X30 window with 0.6 s sleep is 30%%: a try");

	// The try's outcome: X30 needs more than 1.05 times PLAIN's frames, PLAIN keeps with 0.97 of X30's.
	int s = 0;
	f = f_at(1);
	ffeed(f, 3, 1.0, 60.0, 0.0, 20.0);
	expect(f.base == 20.0 && ffeed(f, 3, 1.0, 100.0, 0.0, 21.0, &s) == 3 && s == FA_BACK && f.plain && f.hold == 10 &&
	       f.wait == 10, "an X30 try at 1.05 times goes back: step %d plain %d hold %u", s, f.plain, f.hold);
	f = f_at(1);
	ffeed(f, 3, 1.0, 60.0, 0.0, 20.0);
	expect(ffeed(f, 3, 1.0, 100.0, 0.0, 21.01, &s) == 3 && s == FA_KEEP && !f.plain && f.hold == 5 && f.wait == 5 &&
	       f.mean > 21.0, "an X30 try above 1.05 times stays: step %d plain %d", s, f.plain);
	f = f_at(0);
	ffeed(f, 3, 1.0, 100.0, 0.5, 100.0);
	expect(f.base == 100.0 && ffeed(f, 3, 1.0, 80.0, 0.0, 97.0, &s) == 3 && s == FA_KEEP && f.plain,
	       "a PLAIN try at 0.97 stays: step %d plain %d", s, f.plain);
	f = f_at(0);
	ffeed(f, 3, 1.0, 100.0, 0.5, 100.0);
	expect(ffeed(f, 3, 1.0, 80.0, 0.0, 96.99, &s) == 3 && s == FA_BACK && !f.plain, "a PLAIN try under 0.97 goes back");

	// A try's mean is over its own 3 windows, whatever their SPEED; the base is the mean of the run.
	f = f_at(1);
	ffeed(f, 2, 1.0, 60.0, 0.0, 10.0);
	ffeed(f, 1, 1.0, 100.0, 0.0, 10.0);
	expect(ffeed(f, 3, 1.0, 60.0, 0.0, 30.0) == 3 && f.base == 30.0, "a broken run starts again: base %.1f", f.base);
	expect(ffeed(f, 2, 1.0, 100.0, 0.5, 40.0) == 0 && ffeed(f, 1, 1.0, 10.0, 0.0, 22.0, &s) == 1 && s == FA_KEEP &&
	       f.mean > 33.9 && f.mean < 34.1, "the try's mean over its windows: %.2f", f.mean);
	f = f_at(1);
	ffeed(f, 1, 1.0, 60.0, 0.0, 10.0);
	ffeed(f, 1, 1.0, 60.0, 0.0, 20.0);
	expect(ffeed(f, 1, 1.0, 60.0, 0.0, 30.0) == 1 && f.base == 20.0, "the base is the run's mean: %.2f", f.base);

	// The wait counts every window, then a run of 3.
	f = f_at(1);
	f.wait = 4;
	expect(ffeed(f, 4, 1.0, 60.0, 0.0, 20.0) == 0 && !f.wait && !f.run, "the wait: wait %u run %u", f.wait, f.run);
	expect(ffeed(f, 3, 1.0, 60.0, 0.0, 20.0) == 3, "the run after the wait");

	// The hold doubles on each failed try up to 160, and a kept try sets it back to 5.
	f = f_at(1);
	u32 holds[8], k = 0;
	for (u32 i = 0; i < 8; i++)
	{
		ffeed(f, 400, 1.0, 60.0, 0.0, 40.0);
		ffeed(f, 3, 1.0, 100.0, 0.0, 20.0);
		holds[k++] = f.hold;
	}
	expect(holds[0] == 10 && holds[1] == 20 && holds[3] == 80 && holds[4] == 160 && holds[7] == 160 && f.plain,
	       "the holds: %u %u %u %u %u", holds[0], holds[1], holds[3], holds[4], holds[7]);
	ffeed(f, 400, 1.0, 60.0, 0.0, 20.0);
	expect(ffeed(f, 3, 1.0, 100.0, 0.0, 40.0, &s) == 3 && s == FA_KEEP && f.hold == 5 && !f.plain, "a kept try: hold %u",
	       f.hold);

	// A window of no time changes nothing.
	f = f_at(1);
	ffeed(f, 2, 1.0, 60.0, 0.0, 20.0);
	FwdAuto g = f;
	expect(fwd_auto_step(f, 0.0, 60.0, 0.0, 20.0) == FA_NONE && !memcmp(&f, &g, sizeof(f)), "a 0 s window");
	expect(fwd_auto_step(f, -1.0, 60.0, 0.0, 20.0) == FA_NONE && !memcmp(&f, &g, sizeof(f)), "a -1 s window");

	// A level switch of JIT IDLE RULE AUTO: a try goes back to its old level; the wait and hold stay.
	f = f_at(1);
	ffeed(f, 3, 1.0, 60.0, 0.0, 20.0);
	ffeed(f, 1, 1.0, 100.0, 0.0, 30.0);
	f.wait = 7;
	fwd_auto_cancel(f);
	expect(f.plain && !f.trying && !f.n && !f.run && f.try_fps == 0.0 && f.wait == 7 && f.hold == 5,
	       "cancel in a try: plain %d trying %d n %u wait %u", f.plain, f.trying, f.n, f.wait);
	f = f_at(0);
	ffeed(f, 2, 1.0, 100.0, 0.5, 30.0);
	fwd_auto_cancel(f);
	expect(!f.plain && !f.run && f.run_fps == 0.0, "cancel in a run: plain %d run %u", f.plain, f.run);
	expect(ffeed(f, 3, 1.0, 100.0, 0.5, 30.0) == 3, "a run after a cancel counts 3 new windows");
}

// JIT AUTO CHECKS FRAMES, the level: an X3 try needs 1K vertices a frame in each of its 3 spare windows.
static u32 feedv(IdleAuto& a, u32 n, double speed, double sleep, double vt)
{
	for (u32 i = 1; i <= n; i++)
		if (idle_auto_step(a, 1.0, speed, sleep, vt))
			return i;
	return 0;
}

static IdleAuto at_x30_frames()
{
	IdleAuto a = at_x30();
	a.frames = 1;
	return a;
}

// Ecco: play at X30 with a little sleep; a loading screen (no 3D) with much sleep between levels.
static Win m_ecco(u32 i, int rule)
{
	const bool loading = (i % 40) >= 30;
	if (rule)
		return loading ? Win{ 1.0, 100.0, 0.6 } : Win{ 1.0, 45.0, 0.0 };
	return loading ? Win{ 1.0, 100.0, 0.7 } : Win{ 1.0, 100.0, 0.2 };
}

static std::vector<Event> runv(Model m, u32 n, int frames)
{
	IdleAuto a;
	idle_auto_init(a);
	a.frames = frames;
	std::vector<Event> ev;
	for (u32 i = 1; i <= n; i++)
	{
		const Win w = m(i, a.rule);
		const double vt = (i % 40) >= 30 ? 0.0 : 9.0;
		if (idle_auto_step(a, w.secs, w.speed, w.sleep, vt))
			ev.push_back({ i, a.rule, a.hold });
	}
	return ev;
}

static void t_frames_level()
{
	IdleAuto a = at_x30_frames();
	expect(feedv(a, 10, 100.0, 0.6, 0.0) == 0, "FRAMES: a loading screen tries no X3");
	a = at_x30_frames();
	expect(feedv(a, 10, 100.0, 0.6, 0.99) == 0, "FRAMES: 0.99K a frame tries no X3");
	a = at_x30_frames();
	expect(feedv(a, 10, 100.0, 0.6, 1.0) == 3 && a.rule == IA_X3, "FRAMES: 1.0K a frame tries X3");
	a = at_x30();
	expect(feedv(a, 10, 100.0, 0.6, 0.0) == 3, "OLD: a loading screen tries X3");
	// A loading window breaks the run; the X3 side keeps its rule.
	a = at_x30_frames();
	expect(feedv(a, 2, 100.0, 0.6, 5.0) == 0 && feedv(a, 1, 100.0, 0.6, 0.0) == 0 &&
	       feedv(a, 2, 100.0, 0.6, 5.0) == 0 && feedv(a, 1, 100.0, 0.6, 5.0) == 1, "FRAMES: a run broken by a loading window");
	IdleAuto b;
	idle_auto_init(b);
	b.frames = 1;
	expect(feedv(b, 10, 60.0, 0.0, 0.0) == 3 && b.rule == IA_X30, "FRAMES: slow X3 windows with no 3D still go to X30");
	// Ecco: OLD tries X3 in each loading screen and the level start pays for it; FRAMES stays at X30.
	const std::vector<Event> old = runv(m_ecco, 400, 0), fr = runv(m_ecco, 400, 1);
	expect(old.size() >= 9, "Ecco OLD: %u switches, want 9 or more", (u32)old.size());
	same("Ecco FRAMES", fr, { { 3, IA_X30, 5 } });
}

// JIT AUTO CHECKS FRAMES, X30 FORWARD AUTO: windows with their K vertices a frame.
struct VWin { double secs, speed, sleep, fps, vt; };

static FwdAuto f_fr(int plain)
{
	FwdAuto f = f_at(plain);
	f.frames = 1;
	return f;
}

static u32 vfeed(FwdAuto& f, u32 n, double speed, double sleep, double fps, double vt, int* step = 0)
{
	for (u32 i = 1; i <= n; i++)
	{
		const int s = fwd_auto_step(f, 1.0, speed, sleep, fps, vt);
		if (s != FA_NONE)
		{
			if (step)
				*step = s;
			return i;
		}
	}
	return 0;
}

static void t_fwd_frames()
{
	int s = 0;
	// A run window needs 2 frames a second on screen.
	FwdAuto f = f_fr(1);
	expect(vfeed(f, 10, 50.0, 0.0, 0.0, 0.0) == 0, "FRAMES: PLAIN slow with no frames tries nothing");
	f = f_fr(1);
	expect(vfeed(f, 10, 50.0, 0.0, 1.99, 1.0) == 0, "FRAMES: 1.99 frames a second tries nothing");
	f = f_fr(1);
	expect(vfeed(f, 10, 50.0, 0.0, 2.0, 1.0, &s) == 3 && s == FA_TRY && !f.plain, "FRAMES: 2 frames a second count");
	f = f_at(1);
	expect(vfeed(f, 10, 50.0, 0.0, 0.0, 0.0, &s) == 3 && s == FA_TRY, "OLD: PLAIN slow with no frames tries X30");
	f = f_fr(1);
	expect(vfeed(f, 10, 95.0, 0.0, 30.0, 9.0) == 0, "FRAMES: PLAIN at 95.0%% is not slow");

	// X30 tries PLAIN when its run's frames per emulated second fall under 0.97 of the peak, with 5% sleep.
	f = f_fr(0);
	expect(vfeed(f, 20, 100.0, 0.1, 60.0, 0.0) == 0, "FRAMES: a steady video at X30 tries nothing");
	f = f_fr(0);
	expect(vfeed(f, 1, 100.0, 0.15, 30.0, 9.0) == 0 && vfeed(f, 1, 100.0, 0.15, 24.0, 9.0) == 0 &&
	       vfeed(f, 1, 100.0, 0.15, 24.0, 9.0, &s) == 1 && s == FA_TRY && f.plain && f.trying,
	       "FRAMES: X30 dropping frames tries PLAIN");
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.15, 30.0, 9.0);
	expect(vfeed(f, 2, 100.0, 0.15, 28.7, 9.0) == 0 && vfeed(f, 5, 100.0, 0.15, 29.2, 9.0) == 0,
	       "FRAMES: a run at 29.13 of 30 a game second is no drop (0.97: 29.1)");
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.15, 30.0, 9.0);
	expect(vfeed(f, 2, 100.0, 0.15, 28.6, 9.0) == 2, "FRAMES: a run at 29.07 of 30 is a drop");
	f = f_fr(0);
	expect(vfeed(f, 1, 100.0, 0.049, 30.0, 9.0) == 0 && vfeed(f, 5, 100.0, 0.049, 24.0, 9.0) == 0,
	       "FRAMES: X30 with 4.9%% sleep tries nothing");
	f = f_fr(0);
	expect(vfeed(f, 1, 100.0, 0.05, 30.0, 9.0) == 0 && vfeed(f, 2, 100.0, 0.05, 24.0, 9.0) == 2,
	       "FRAMES: X30 with 5%% sleep tries PLAIN");
	f = f_fr(0);
	expect(vfeed(f, 1, 97.99, 0.15, 30.0, 9.0) == 0 && vfeed(f, 5, 97.99, 0.15, 24.0, 9.0) == 0,
	       "FRAMES: X30 at 97.99%% tries nothing");
	f = f_at(0);
	expect(vfeed(f, 1, 100.0, 0.15, 30.0, 9.0) == 0 && vfeed(f, 5, 100.0, 0.15, 24.0, 9.0) == 0,
	       "OLD: X30 with 15%% sleep tries nothing");
	// The peak: the best of the last 10 windows, frames per emulated second, windows with frames only.
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.0, 30.0, 9.0);
	vfeed(f, 6, 100.0, 0.0, 25.0, 9.0);
	expect(vfeed(f, 3, 100.0, 0.15, 25.0, 9.0) == 3, "FRAMES: a peak 9 windows back counts");
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.0, 30.0, 9.0);
	vfeed(f, 7, 100.0, 0.0, 25.0, 9.0);
	expect(vfeed(f, 3, 100.0, 0.15, 25.0, 9.0) == 0, "FRAMES: a peak 10 windows back is gone");
	f = f_fr(0);
	vfeed(f, 1, 1.0, 0.0, 1.0, 0.0);	// 100 frames per emulated second, but under 2 on screen
	expect(vfeed(f, 3, 100.0, 0.15, 30.0, 9.0) == 0, "FRAMES: a window with no frames sets no peak");
	f = f_fr(0);
	vfeed(f, 1, 50.0, 0.0, 15.0, 9.0);	// 30 frames per emulated second at half speed
	expect(vfeed(f, 3, 100.0, 0.15, 26.0, 9.0) == 3, "FRAMES: the peak in frames per emulated second");
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.0, 30.0, 9.0);
	expect(vfeed(f, 3, 110.0, 0.15, 31.9, 9.0) == 3, "FRAMES: the drop in frames per emulated second (31.9 at 110%%)");
	f = f_fr(0);
	vfeed(f, 1, 100.0, 0.0, 30.0, 9.0);
	FwdAuto g = f;
	expect(fwd_auto_step(f, 0.0, 100.0, 0.0, 30.0, 9.0) == FA_NONE && !memcmp(&f, &g, sizeof(f)), "FRAMES: a 0 s window");
	f.trying = 0;
	fwd_auto_cancel(f);
	expect(fa_peak(f) == 0.0 && f.run_vt == 0.0 && f.run_efps == 0.0, "FRAMES: a cancel clears the peak and the run");

	// A try at another load (20% and 0.5K off its run's) or with no frames is void: back, the hold kept.
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	expect(vfeed(f, 3, 100.0, 0.0, 40.0, 15.9, &s) == 3 && s == FA_VOID && f.plain && !f.trying && f.hold == 5 &&
	       f.wait == 5, "FRAMES: a try at 15.9K against 20K is void: step %d plain %d hold %u", s, f.plain, f.hold);
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	expect(vfeed(f, 3, 100.0, 0.0, 40.0, 16.1, &s) == 3 && s == FA_KEEP && !f.plain, "FRAMES: 16.1K against 20K counts");
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 1.0);
	expect(vfeed(f, 3, 100.0, 0.0, 40.0, 0.55, &s) == 3 && s == FA_KEEP, "FRAMES: 0.45K off is under the 0.5K floor");
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 1.0);
	expect(vfeed(f, 3, 100.0, 0.0, 40.0, 0.45, &s) == 3 && s == FA_VOID, "FRAMES: 0.55K and 55%% off is void");
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	expect(vfeed(f, 3, 100.0, 0.0, 1.99, 20.0, &s) == 3 && s == FA_VOID && f.plain, "FRAMES: a try with no frames is void");
	f = f_at(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	expect(vfeed(f, 3, 100.0, 0.0, 40.0, 10.0, &s) == 3 && s == FA_KEEP, "OLD: no void");
	f = f_fr(1);
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	vfeed(f, 2, 100.0, 0.0, 40.0, 20.0);
	expect(vfeed(f, 1, 100.0, 0.0, 40.0, 15.0, &s) == 1 && s == FA_KEEP, "FRAMES: the try's load is its mean");
	// A void keeps the hold: the next failed try doubles it from there.
	f = f_fr(1);
	f.hold = 20;
	vfeed(f, 3, 80.0, 0.0, 27.7, 20.0);
	vfeed(f, 3, 100.0, 0.0, 40.0, 10.0, &s);
	expect(s == FA_VOID && f.hold == 20 && f.wait == 20, "FRAMES: a void keeps the hold %u", f.hold);
	vfeed(f, 20, 80.0, 0.0, 27.7, 20.0);
	expect(vfeed(f, 3, 80.0, 0.0, 27.7, 20.0, &s) == 3 && s == FA_TRY, "FRAMES: the next try after the hold");
	expect(vfeed(f, 3, 100.0, 0.0, 27.7, 20.0, &s) == 3 && s == FA_BACK && f.hold == 40, "FRAMES: then a failed try: %u",
	       f.hold);

	// The Looney Tunes race (HW run 10): an X30 try that ran into a lighter scene kept X30 (29.6 against 27.7).
	const VWin base[3] = { { 1, 94.2, 0, 28.7, 21.3 }, { 1, 91.8, 0, 27.5, 23.6 }, { 1, 89.8, 0, 26.9, 28.5 } };
	const VWin tried[3] = { { 1, 98.0, 0, 26.9, 25.8 }, { 1, 105.0, 0, 32.0, 16.4 }, { 1, 101.1, 0.07, 29.8, 14.4 } };
	for (int frames = 0; frames <= 1; frames++)
	{
		f = frames ? f_fr(1) : f_at(1);
		s = FA_NONE;
		for (const VWin& w : base)
			s = fwd_auto_step(f, w.secs, w.speed, w.sleep, w.fps, w.vt);
		expect(s == FA_TRY, "race %d: the X30 try", frames);
		for (const VWin& w : tried)
			s = fwd_auto_step(f, w.secs, w.speed, w.sleep, w.fps, w.vt);
		expect(frames ? s == FA_VOID && f.plain : s == FA_KEEP && !f.plain, "race %d: step %d plain %d", frames, s, f.plain);
	}
}

// JIT AUTO CHECKS + X3 TRY (bundle 22): windows with their K vertices, frames a vblank and frames a second.
struct TWin { double secs, speed, sleep, vt, fpv, fps; };
typedef TWin (*TModel)(u32 i, int rule);

static std::vector<Event> trun(TModel m, u32 n, int frames)
{
	IdleAuto a;
	idle_auto_init(a);
	a.frames = frames;
	std::vector<Event> ev;
	for (u32 i = 1; i <= n; i++)
	{
		const TWin w = m(i, a.rule);
		if (idle_auto_step(a, w.secs, w.speed, w.sleep, w.vt, w.fpv, w.fps))
			ev.push_back({ i, a.rule, a.hold });
	}
	return ev;
}

// Ikaruga (HW 2026-10-16): X3 at 93% from the boot, X30 at full speed with room at a frame each vblank.
static TWin t_ikaruga(u32, int rule) { return rule ? TWin{ 1, 93, 0, 5, 1.0, 56 } : TWin{ 1, 100, 0.5, 5, 1.0, 60 }; }
// Rez: a slow loading screen to window 3, then 30 frames at both levels, X3 at 99%.
static TWin t_rez(u32 i, int rule)
{
	if (i <= 3)
		return { 1, 60, 0, 0, 0.0, 0 };
	return rule ? TWin{ 1, 99, 0.1, 6, 0.5, 30 } : TWin{ 1, 100, 0.45, 6, 0.5, 30 };
}
// Donald Duck's play on 2026-10-15: X30 drops to 15 frames a second, X3 gives 30 at full speed.
static TWin t_dd(u32 i, int rule)
{
	if (i <= 3)
		return { 1, 60, 0, 0, 0.0, 0 };
	return rule ? TWin{ 1, 100, 0.2, 5, 0.5, 30 } : TWin{ 1, 100, 0.4, 5, 0.25, 15 };
}
// Sonic Adventure: 26.3 of 30 at X30, 29.7 at X3.
static TWin t_sonic(u32 i, int rule)
{
	if (i <= 3)
		return { 1, 60, 0, 0, 0.0, 0 };
	return rule ? TWin{ 1, 99, 0.1, 9, 0.495, 29.7 } : TWin{ 1, 100, 0.4, 9, 0.438, 26.3 };
}

static u32 tfeed(IdleAuto& a, u32 n, double speed, double sleep, double vt, double fpv, double fps = 30.0,
                 u32 fwd = 0)
{
	for (u32 i = 1; i <= n; i++)
		if (idle_auto_step(a, 1.0, speed, sleep, vt, fpv, fps, fwd))
			return i;
	return 0;
}

static IdleAuto at_x30_try()
{
	IdleAuto a = at_x30();
	a.frames = 2;
	return a;
}

// An X30 run of 3 spare windows at fpv, then 3 X3 windows of the try at t1, t2, t3: the window of the switch back, or 0.
static u32 try_case(IdleAuto& a, double fpv, double t1, double t2, double t3, double x3_speed = 100.0)
{
	expect(tfeed(a, 3, 100.0, 0.5, 5.0, fpv) == 3 && a.rule == IA_X3 && a.try_n == 3, "+ X3 TRY: the try starts");
	const double t[3] = { t1, t2, t3 };
	for (u32 i = 0; i < 3; i++)
		if (idle_auto_step(a, 1.0, x3_speed, 0.1, 5.0, t[i], 30.0))
			return i + 1;
	return 0;
}

static void t_try_level()
{
	// Each lost try goes back after its 3 windows: the switches of a try that always fails.
	std::vector<Event> fails = { { 3, IA_X30, 5 } };
	u32 w = 3, h = 5;
	while (true)
	{
		const u32 x3 = w + h + 3;
		if (x3 > 2000)
			break;
		fails.push_back({ x3, IA_X3, h });
		w = x3 + 3;
		h = h * 2 > 160 ? 160 : h * 2;
		if (w > 2000)
			break;
		fails.push_back({ w, IA_X30, h });
	}
	same("+ X3 TRY: Ikaruga tries no X3", trun(t_ikaruga, 2000, 2), { { 3, IA_X30, 5 } });
	expect(trun(t_ikaruga, 2000, 1).size() > 10, "FRAMES: Ikaruga tries X3 again and again");
	same("+ X3 TRY: Rez's tries go back", trun(t_rez, 2000, 2), fails);
	same("FRAMES: Rez stays at its first try", trun(t_rez, 2000, 1), { { 3, IA_X30, 5 }, { 11, IA_X3, 5 } });
	same("+ X3 TRY: Donald Duck keeps X3", trun(t_dd, 2000, 2), { { 3, IA_X30, 5 }, { 11, IA_X3, 5 } });
	same("+ X3 TRY: Sonic Adventure keeps X3", trun(t_sonic, 2000, 2), { { 3, IA_X30, 5 }, { 11, IA_X3, 5 } });
	// Levels OLD and FRAMES take no account of the frames a vblank, the frames a second or a FORWARD try.
	for (int lv = 0; lv <= 1; lv++)
	{
		const std::vector<Event> a0 = runv(m_ecco, 400, lv);
		IdleAuto a;
		idle_auto_init(a);
		a.frames = lv;
		std::vector<Event> a1;
		for (u32 i = 1; i <= 400; i++)
		{
			const Win x = m_ecco(i, a.rule);
			if (idle_auto_step(a, x.secs, x.speed, x.sleep, (i % 40) >= 30 ? 0.0 : 9.0, (i * 7 % 13) / 10.0,
			                   (i * 5 % 7) * 1.0, i % 4))
				a1.push_back({ i, a.rule, a.hold });
			expect(!a.try_n && !a.lost, "level %d: no try state (try_n %u lost %d)", lv, a.try_n, a.lost);
		}
		same(lv ? "FRAMES ignores the new inputs" : "OLD ignores the new inputs", a1, a0);
	}

	// No try at a frame each vblank, from 0.97 up; a run broken by such a window starts again.
	IdleAuto a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.5, 5.0, 0.97) == 0, "+ X3 TRY: 0.97 frames a vblank tries no X3");
	a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.5, 5.0, 0.9699) == 3, "+ X3 TRY: 0.9699 frames a vblank tries X3");
	a = at_x30_try();
	expect(tfeed(a, 2, 100.0, 0.5, 5.0, 0.5) == 0 && tfeed(a, 1, 100.0, 0.5, 5.0, 1.0) == 0 &&
	       tfeed(a, 2, 100.0, 0.5, 5.0, 0.5) == 0 && tfeed(a, 1, 100.0, 0.5, 5.0, 0.5) == 1,
	       "+ X3 TRY: a run broken by a full-rate window");
	a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.5, 0.99, 0.5) == 0, "+ X3 TRY: the FRAMES load test stays");
	a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.29, 5.0, 0.5) == 0, "+ X3 TRY: the sleep test stays");

	// No try while an X30 FORWARD try runs; the run counts again from the window after it.
	a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.5, 5.0, 0.5, 30.0, IA_FWD_TRY) == 0, "+ X3 TRY: no try during a FORWARD try");
	expect(tfeed(a, 3, 100.0, 0.5, 5.0, 0.5) == 3 && a.rule == IA_X3, "+ X3 TRY: the try 3 windows after it");
	a = at_x30_try();
	expect(tfeed(a, 2, 100.0, 0.5, 5.0, 0.5) == 0 && tfeed(a, 1, 100.0, 0.5, 5.0, 0.5, 30.0, IA_FWD_TRY) == 0 &&
	       tfeed(a, 2, 100.0, 0.5, 5.0, 0.5) == 0 && tfeed(a, 1, 100.0, 0.5, 5.0, 0.5) == 1,
	       "+ X3 TRY: a FORWARD try breaks the run");
	a = at_x30();
	a.frames = 1;
	expect(tfeed(a, 10, 100.0, 0.5, 5.0, 0.5, 30.0, IA_FWD_TRY) == 3, "FRAMES: a FORWARD try stops no X3 try");
	// The FORWARD test is at X30 only: an X3 try under test goes on.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	expect(!idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.6, 30.0, IA_FWD_TRY) && a.try_n == 2,
	       "+ X3 TRY: the FORWARD flag at X3 changes nothing: try_n %u", a.try_n);

	// A try that begins at FORWARD X30 is not judged: it stays at X3 as at FRAMES, and only the slow rule ends it.
	a = at_x30_try();
	expect(tfeed(a, 3, 100.0, 0.5, 5.0, 0.5, 30.0, IA_FWD_X30) == 3 && a.rule == IA_X3 && !a.try_n,
	       "+ X3 TRY: a try at FORWARD X30 starts unjudged: try_n %u", a.try_n);
	expect(tfeed(a, 20, 100.0, 0.1, 5.0, 0.5) == 0 && a.rule == IA_X3, "+ X3 TRY: an unjudged try stays at X3");
	expect(tfeed(a, 3, 60.0, 0.0, 5.0, 0.5) == 3 && a.rule == IA_X30 && !a.lost, "+ X3 TRY: the slow rule ends it");
	a = at_x30_try();
	expect(tfeed(a, 10, 100.0, 0.5, 5.0, 0.5, 30.0, IA_FWD_TRY | IA_FWD_X30) == 0, "+ X3 TRY: both flags: no try");
	// Only the start counts: FORWARD X30 during a judged try changes nothing.
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.5, 0.5, 0.5) == 3 && a.lost, "+ X3 TRY: a try at FORWARD PLAIN is judged");
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	expect(!idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0, IA_FWD_X30) &&
	       !idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0, IA_FWD_X30) &&
	       idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0, IA_FWD_X30) && a.lost,
	       "+ X3 TRY: FORWARD X30 after the start does not stop the verdict");
	// Donald Duck (HW run 2): the try at FORWARD X30 with its loading screen stays at X3 through it.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.7, 3.8, 0.49, 29.5, IA_FWD_X30);
	const TWin dd2[] = { { 1, 100.0, 0.7, 3.9, 0.5, 30.0 }, { 1, 92.7, 0.4, 3.8, 0.27, 15.9 }, { 1, 104.6, 0.2, 0, 0, 0 },
	                     { 1, 100.0, 0.3, 0, 0, 0 }, { 1, 100.0, 0.2, 0, 0, 0 }, { 1, 95.8, 0.1, 0, 0, 0 },
	                     { 1, 99.7, 0.2, 0, 0, 0 }, { 1, 92.6, 0.2, 3.1, 0.31, 18.5 }, { 1, 100.0, 0.7, 3.4, 0.5, 30.0 } };
	u32 moved = 0;
	for (const TWin& x : dd2)
		moved += idle_auto_step(a, x.secs, x.speed, x.sleep, x.vt, x.fpv, x.fps, IA_FWD_X30);
	expect(!moved && a.rule == IA_X3, "+ X3 TRY: Donald Duck's try at FORWARD X30 stays at X3: %u switches", moved);
	// A level with the X30 flag in a model: Rez at FORWARD X30 keeps its first try, as at FRAMES.
	idle_auto_init(a);
	a.frames = 2;
	std::vector<Event> ev;
	for (u32 i = 1; i <= 2000; i++)
	{
		const TWin x = t_rez(i, a.rule);
		if (idle_auto_step(a, x.secs, x.speed, x.sleep, x.vt, x.fpv, x.fps, IA_FWD_X30))
			ev.push_back({ i, a.rule, a.hold });
	}
	same("+ X3 TRY: Rez at FORWARD X30 keeps its first try", ev, { { 3, IA_X30, 5 }, { 11, IA_X3, 5 } });

	// The base is the run's mean; the run of before a broken window is not in it.
	a = at_x30_try();
	tfeed(a, 2, 100.0, 0.5, 5.0, 0.9);
	tfeed(a, 1, 100.0, 0.0, 5.0, 0.9);
	expect(tfeed(a, 1, 100.0, 0.5, 5.0, 0.4) == 0 && tfeed(a, 1, 100.0, 0.5, 5.0, 0.5) == 0 &&
	       tfeed(a, 1, 100.0, 0.5, 5.0, 0.6) == 1 && a.base_fpv > 0.4999 && a.base_fpv < 0.5001,
	       "+ X3 TRY: the base is the new run's mean: %.4f", a.base_fpv);

	// The verdict: the try's mean over its 3 windows against 1.03 times the base, at the third window.
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.514, 0.514, 0.514) == 3 && a.rule == IA_X30 && a.lost && a.hold == 10,
	       "+ X3 TRY: 0.514 against 0.5 goes back: rule %d lost %d hold %u", a.rule, a.lost, a.hold);
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.516, 0.516, 0.516) == 0 && a.rule == IA_X3 && !a.try_n,
	       "+ X3 TRY: 0.516 against 0.5 stays: rule %d try_n %u", a.rule, a.try_n);
	a = at_x30_try();
	expect(try_case(a, 0.0, 0.0, 0.0, 0.0) == 3 && a.rule == IA_X30 && a.lost, "+ X3 TRY: no gain at 0 goes back");
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.3, 0.3, 0.9) == 3 && a.rule == IA_X30, "+ X3 TRY: the try's mean, not its last window");
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.9, 0.9, 0.3) == 0 && a.rule == IA_X3, "+ X3 TRY: the try's mean, not its first windows");
	expect(a.try_fpv > 2.0999 && a.try_fpv < 2.1001, "+ X3 TRY: the sum of the try: %.4f", a.try_fpv);
	// A kept try is plain X3: slow windows still go back, the hold set back after 10 X3 windows.
	expect(tfeed(a, 6, 100.0, 0.1, 5.0, 0.6) == 0 && tfeed(a, 3, 60.0, 0.0, 5.0, 0.6) == 3 && a.rule == IA_X30 &&
	       !a.lost && a.hold == 5, "+ X3 TRY: a kept try ends slow: lost %d hold %u", a.lost, a.hold);
	// A slow try goes back by the slow rule, and its line says so.
	a = at_x30_try();
	expect(try_case(a, 0.5, 0.9, 0.9, 0.9, 60.0) == 3 && a.rule == IA_X30 && !a.lost, "+ X3 TRY: a slow try");
	// The verdict counts only the try's windows: two slow ones and a lost verdict.
	a = at_x30_try();
	expect(tfeed(a, 3, 100.0, 0.5, 5.0, 0.5) == 3, "+ X3 TRY: the try again");
	expect(!idle_auto_step(a, 1.0, 60.0, 0.0, 5.0, 0.5, 30.0) && !idle_auto_step(a, 1.0, 60.0, 0.0, 5.0, 0.5, 30.0) &&
	       idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0) && a.lost, "+ X3 TRY: two slow windows, then the verdict");

	// A try window under 2 frames a second (a loading screen) is not judged: the verdict waits for 3 that are.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	expect(!idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0) && !idle_auto_step(a, 1.0, 100.0, 0.1, 0.0, 0.0, 1.99) &&
	       a.try_n == 2 && !idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 2.0) && a.try_n == 1 &&
	       idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0) && a.lost && a.try_fpv > 1.4999 && a.try_fpv < 1.5001,
	       "+ X3 TRY: a loading window is not judged: try_n %u sum %.4f", a.try_n, a.try_fpv);
	// Donald Duck (HW 2026-10-16): a try with a loading window, 0.32 against 0.50, is judged on play windows only.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	expect(!idle_auto_step(a, 1.0, 100.0, 0.6, 3.7, 0.49, 29.5) && !idle_auto_step(a, 1.0, 99.9, 0.6, 4.0, 0.45, 26.9) &&
	       !idle_auto_step(a, 1.0, 96.6, 0.2, 0.0, 0.02, 1.0) && a.try_n == 1 &&
	       !idle_auto_step(a, 1.0, 100.0, 0.5, 4.0, 0.62, 37.0) && a.rule == IA_X3,
	       "+ X3 TRY: Donald Duck's try judged on its play windows: rule %d try_n %u", a.rule, a.try_n);
	// A slow loading screen in a try still goes back by the slow rule.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	expect(tfeed(a, 3, 60.0, 0.0, 0.0, 0.0, 0.0) == 3 && a.rule == IA_X30 && !a.lost, "+ X3 TRY: a slow loading try");

	// A window of no time changes nothing in a try.
	a = at_x30_try();
	tfeed(a, 3, 100.0, 0.5, 5.0, 0.5);
	idle_auto_step(a, 1.0, 100.0, 0.1, 5.0, 0.5, 30.0);
	IdleAuto b = a;
	expect(!idle_auto_step(a, 0.0, 100.0, 0.1, 5.0, 0.9, 30.0) && !memcmp(&a, &b, sizeof(a)), "+ X3 TRY: a 0 s window");
	// The try state starts empty, and a try needs no state of an older one.
	idle_auto_init(a);
	expect(!a.try_n && !a.lost && a.run_fpv == 0.0 && a.base_fpv == 0.0 && a.try_fpv == 0.0, "+ X3 TRY: init");
	a = at_x30_try();
	try_case(a, 0.5, 0.9, 0.9, 0.9);
	tfeed(a, 3, 60.0, 0.0, 5.0, 0.9);
	a.wait = 0;
	expect(try_case(a, 0.5, 0.514, 0.514, 0.514) == 3 && a.try_fpv > 1.5419 && a.try_fpv < 1.5421,
	       "+ X3 TRY: a new try starts its sum at 0: %.4f", a.try_fpv);
}

int main()
{
	t_models();
	t_edges();
	t_bill_word();
	t_fwd_models();
	t_fwd_edges();
	t_frames_level();
	t_fwd_frames();
	t_try_level();
	printf("%s: idle auto: %u checks, %u failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
