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
// The first X3 try goes back after 15 X3 windows: still soon.
static Win m_soon15(u32 i, int rule)
{
	if (!rule)
		return { 1.0, 100.0, 0.5 };
	return (i <= 3 || i > 32) ? Win{ 1.0, 60.0, 0.0 } : Win{ 1.0, 100.0, 0.5 };
}
// Slow and full X3 windows in turn: never three slow ones in a row.
static Win m_turns(u32 i, int) { return { 1.0, (i & 1) ? 60.0 : 100.0, 0.0 }; }

static void t_models()
{
	same("steady", run(m_steady, 600), {});
	same("X30 game", run(m_x30_game, 1000), { { 3, IA_X30, 10 } });
	same("Donald Duck", run(m_dd, 400), { { 19, IA_X30, 10 }, { 59, IA_X3, 10 } });
	same("a try that holds", run(m_hold_ok, 100),
	     { { 3, IA_X30, 10 }, { 18, IA_X3, 10 }, { 63, IA_X30, 10 }, { 78, IA_X3, 10 }, { 83, IA_X30, 20 } });
	same("a try back after 15 windows", run(m_soon15, 40), { { 3, IA_X30, 10 }, { 18, IA_X3, 10 }, { 35, IA_X30, 20 } });
	same("slow in turns", run(m_turns, 600), {});

	// Each X30 switch at w with hold h: X3 at w + h + 5 (2 left out, h waits, 3 spare), X30 again 5 later.
	std::vector<Event> want = { { 3, IA_X30, 10 } };
	u32 w = 3, h = 10;
	while (true)
	{
		const u32 x3 = w + h + 5;
		if (x3 > 2000)
			break;
		want.push_back({ x3, IA_X3, h });
		w = x3 + 5;
		h = h * 2 > 320 ? 320 : h * 2;
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
	expect(a.rule == IA_X3 && a.hold == 10 && a.x3_n == 20 && !a.slow && !a.spare && !a.settle && !a.wait,
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

	// After a switch, 2 windows count for nothing, whatever they show.
	expect(a.settle == 2 && !a.slow && !a.spare && a.wait == 10, "after X30: settle %u slow %u spare %u wait %u",
	       a.settle, a.slow, a.spare, a.wait);
	expect(!idle_auto_step(a, 1.0, 100.0, 1.0) && !idle_auto_step(a, 1.0, 100.0, 1.0) && a.wait == 10,
	       "the settle windows moved the wait to %u", a.wait);
	expect(feed(a, 9, 1.0, 100.0, 1.0) == 0 && a.wait == 1 && !a.spare, "nine waits: wait %u spare %u", a.wait,
	       a.spare);
	expect(!idle_auto_step(a, 1.0, 100.0, 1.0) && !a.wait && !a.spare, "the last wait counted as spare");
	expect(feed(a, 3, 1.0, 100.0, 1.0) == 3 && a.rule == IA_X3 && !a.x3_n && a.settle == 2,
	       "the try: rule %d x3_n %u settle %u", a.rule, a.x3_n, a.settle);
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

int main()
{
	t_models();
	t_edges();
	t_bill_word();
	printf("%s: idle auto: %u checks, %u failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
