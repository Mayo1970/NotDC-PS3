// SH4 Clock above 200 MHz, PC check (WSL): sysgate_check's harness with the clock as a variable. The SH4's cycles per
// slice follow the clock; the modules' slice, and so their trace, stay at 200 MHz time. Not built.
#define main sysgate_main
#include "../sysgate_check/sysgate_check.cpp"
#undef main
#include <cmath>

static u32 g_checks, g_fails;
static void expect(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void expect(bool ok, const char* fmt, ...)
{
	g_checks++;
	if (ok || g_fails++ >= 8)
		return;
	va_list ap;
	va_start(ap, fmt);
	fputs("FAIL: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static const s32 k_ts[3]  = { 1792, 896, 448 };
static const u32 k_med[3] = { 4, 8, 8 };

// Not the code's own formula: clock/200 times the slice above 200 MHz, to the nearest cycle; 250 is the limit.
static s32 want_cpu_slice(s32 ts, int clk)
{
	if (clk <= 200)
		return ts;
	return (s32)lround((double)ts * (clk > 250 ? 250 : clk) / 200.0);
}

static void set_clock(int clk)
{
	s_clock = clk;
	CalculateSync();
}

// One timeline at one clock: its trace, and the batch counts it added (a plan on the wrong slice moves only these).
static std::string run_at(const Scen& sc, int mode, int clk, Side& d)
{
	set_clock(clk);
	const Side a = g_side[mode];
	const std::string t = run_forked(sc, mode);
	const Side& b = g_side[mode];
	d.batches = b.batches - a.batches;
	d.slices = b.slices - a.slices;
	d.miss = b.miss - a.miss;
	return t;
}

// The module traces of sysgate_check's timelines: equal to 200 MHz at every clock above it, different at 150.
static void check_traces(u32 seeds, u32 slices)
{
	static const int k_over[] = { 205, 220, 250 };
	u32 under_diff = 0;
	for (u32 s = 1; s <= seeds; s++)
	{
		Scen sc = { s, slices, s % 3, (s % 11) == 5, (s % 4) == 1, (s % 13) == 7 ? 3u : 1u, 0 };
		const int mode = (int)(s % 3);
		Side d0, d;
		const std::string base = run_at(sc, mode, 200, d0);
		expect(base != "CHILD FAILED\n", "seed %u: no trace at 200 MHz", s);
		for (int clk : k_over)
		{
			expect(run_at(sc, mode, clk, d) == base, "seed %u mode %d: the module trace at %d MHz is not the 200 MHz one",
			       s, mode, clk);
			expect(d.batches == d0.batches && d.slices == d0.slices && d.miss == d0.miss,
			       "seed %u mode %d: the gate batches at %d MHz are not the 200 MHz ones", s, mode, clk);
		}
		under_diff += run_at(sc, mode, 150, d) != base;
	}
	set_clock(200);
	expect(under_diff == seeds, "only %u of %u traces change at 150 MHz: the traces do not see the time bases",
	       under_diff, seeds);
}

// Both slices and the time-base clock, for every clock a nullDC.cfg can hold.
static void check_slices()
{
	for (int acc = 0; acc < 3; acc++)
		for (int clk = 100; clk <= 300; clk++)
		{
			s_accuracy = acc;
			s_clock = clk;
			Sh4_ApplyAccuracyPreset();
			const s32 ts = sh4_GetTimeslice(), cs = sh4_GetCpuTimeslice();
			expect(ts == k_ts[acc], "acc %d clock %d: module slice %d", acc, clk, ts);
			expect(cs == want_cpu_slice(k_ts[acc], clk), "acc %d clock %d: cpu slice %d, not %d", acc, clk, cs,
			       want_cpu_slice(k_ts[acc], clk));
			expect(cs >= ts && cs <= 32767, "acc %d clock %d: cpu slice %d is no addi immediate", acc, clk, cs);
			expect(SH4_CLOCK_EFF == (u32)(clk < 200 ? clk : 200) * 1000000u, "clock %d: time bases at %u Hz", clk,
			       SH4_CLOCK_EFF);
		}
	s_clock = 200;
}

// The real Sh4_int_Run on a guest of one counted opcode: its ops per slice follow the clock, the AICA's cycles do not.
static u64 g_ops;
static u32 g_start, g_run_slices;
static void FASTCALL count_op(u32 op)
{
	(void)op;
	g_ops++;
	if (update_cnt - g_start >= g_run_slices - 1)
		sh4_int_bCpuRun = false;
}

static void check_interpreter()
{
	static const int k_clk[] = { 150, 200, 205, 220, 250 };
	g_run_slices = 3000;
	OpPtr[0] = count_op;
	for (int acc = 0; acc < 3; acc++)
		for (int clk : k_clk)
		{
			s_accuracy = acc;
			set_clock(clk);
			g_trace.clear();
			g_ops = 0;
			g_start = update_cnt;
			Sh4_int_Run();
			const u64 cs = (u64)want_cpu_slice(k_ts[acc], clk), ratio = (u64)sh4_GetCpuRatio();
			const u64 want_ops = (g_run_slices * cs + ratio - 1) / ratio;
			expect(update_cnt - g_start == g_run_slices, "acc %d clock %d: %u slices ran", acc, clk, update_cnt - g_start);
			expect(g_ops == want_ops, "acc %d clock %d: %llu ops in %u slices, not %llu", acc, clk,
			       (unsigned long long)g_ops, g_run_slices, (unsigned long long)want_ops);
			const u32 want_cyc = (u32)k_ts[acc] * k_med[acc];
			u32 ticks = 0, bad = 0;
			for (size_t p = g_trace.find(" aica "); p != std::string::npos; p = g_trace.find(" aica ", p + 1))
			{
				ticks++;
				bad += (u32)atoi(g_trace.c_str() + p + 6) != want_cyc;
			}
			expect(ticks + 1 >= g_run_slices / k_med[acc] && !bad, "acc %d clock %d: %u AICA ticks, %u not of %u cycles",
			       acc, clk, ticks, bad, want_cyc);
		}
	set_clock(200);
}

int main(int argc, char** argv)
{
	const u32 seeds = argc > 1 ? (u32)atoi(argv[1]) : 60;
	const u32 slices = argc > 2 ? (u32)atoi(argv[2]) : 6000;
	boot();
	check_traces(seeds, slices);
	check_slices();
	check_interpreter();
	fprintf(stderr, "%s: %u checks, %u failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
