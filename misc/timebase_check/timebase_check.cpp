// Plan section 7, bundle 7, PC check (WSL): the TIMING rows on sysgate_check's harness. Each row against the SYS
// GATE, Flycast's TimerTest, TCNT against a step-by-step model, the TMU rate, the SPG rows. Not built.
#define main sysgate_main
#include "../sysgate_check/sysgate_check.cpp"
#undef main
#include <cmath>

extern u32 spg_VblankCountTotal;
extern u32 spg_ScanlineCount;

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

// ---- the rows against the SYS GATE: sysgate_check's timelines with a row on ----
struct Rows { const char* name; int clock, eff, fine, field, hblank; };

static void set_rows(const Rows& rw)
{
	s_clock = rw.clock;
	s_tmu_eff = rw.eff;
	s_fine = rw.fine;
	s_field = rw.field;
	s_hblank = rw.hblank;
	CalculateSync();
}

// The gate-off trace; the two gate modes must give the same one.
static std::string rows_trace(const Scen& sc, const Rows& rw)
{
	set_rows(rw);
	const std::string a = run_forked(sc, 0);
	expect(a != "CHILD FAILED\n", "seed %u, %s: no trace", sc.seed, rw.name);
	expect(run_forked(sc, 1) == a, "seed %u, %s: the SYS GATE trace is not the gate-off one", sc.seed, rw.name);
	expect(run_forked(sc, 2) == a, "seed %u, %s: the SYS GATE BATCH trace is not the gate-off one", sc.seed, rw.name);
	return a;
}

static void check_rows(u32 seeds, u32 slices)
{
	static const Rows off200 = { "rows off", 200, 0, 0, 0, 0 }, off150 = { "rows off at 150 MHz", 150, 0, 0, 0, 0 };
	static const Rows eff200 = { "TMU Clock EFF at 200 MHz", 200, 1, 0, 0, 0 };
	static const Rows eff150 = { "TMU Clock EFF at 150 MHz", 150, 1, 0, 0, 0 };
	static const Rows field = { "SPG Field Bit FRAME", 200, 0, 0, 1, 0 }, hblank = { "HBlank IRQ MODES", 200, 0, 0, 0, 1 };
	static const Rows fine = { "TMU Fine TCNT", 200, 0, 1, 0, 0 }, fine250 = { "TMU Fine TCNT at 250 MHz", 250, 0, 1, 0, 0 };
	static const Rows all150 = { "all rows at 150 MHz", 150, 1, 1, 1, 1 };
	u32 d_eff = 0, d_field = 0, d_hblank = 0;
	for (u32 s = 1; s <= seeds; s++)
	{
		Scen sc = { s, slices, s % 3, (s % 11) == 5, (s % 4) == 1, (s % 13) == 7 ? 3u : 1u, 0 };
		const std::string b200 = rows_trace(sc, off200), b150 = rows_trace(sc, off150);
		expect(rows_trace(sc, eff200) == b200, "seed %u: TMU Clock EFF changes the trace at 200 MHz", s);
		d_eff += rows_trace(sc, eff150) != b150;
		d_field += rows_trace(sc, field) != b200;
		d_hblank += rows_trace(sc, hblank) != b200;
		rows_trace(sc, fine);
		rows_trace(sc, fine250);
		rows_trace(sc, all150);
	}
	set_rows(off200);
	const unsigned long long plan_bad = g_side[0].plan_bad + g_side[1].plan_bad + g_side[2].plan_bad;
	expect(!plan_bad, "the closed-form TMU plan differs from the slice loop %llu times", plan_bad);
	// A row that changes no trace is not under test.
	expect(d_eff > seeds / 2, "TMU Clock EFF at 150 MHz changes only %u of %u traces", d_eff, seeds);
	expect(d_field > 0, "SPG Field Bit FRAME changes no trace");
	expect(d_hblank > seeds / 2, "HBlank IRQ MODES changes only %u of %u traces", d_hblank, seeds);
}

// ---- the parts below run in children: they install handlers and move the reset state ----
static int c_mode, c_acc, c_fine;
static u32 c_seed, c_steps;

static void in_child(void (*fn)())
{
	int fd[2];
	if (pipe(fd))
		exit(2);
	fflush(stdout);
	fflush(stderr);
	const pid_t p = fork();
	if (p == 0)
	{
		close(fd[0]);
		// The code under test prints; the FAIL lines go to stderr.
		freopen("/dev/null", "w", stdout);
		g_checks = g_fails = 0;
		fn();
		const u32 v[2] = { g_checks, g_fails };
		_exit(write(fd[1], v, sizeof(v)) == (ssize_t)sizeof(v) ? 0 : 3);
	}
	close(fd[1]);
	u32 v[2] = { 0, 0 };
	const bool got = read(fd[0], v, sizeof(v)) == (ssize_t)sizeof(v);
	close(fd[0]);
	int st = 0;
	waitpid(p, &st, 0);
	g_checks += v[0];
	g_fails += v[1];
	expect(got && WIFEXITED(st) && WEXITSTATUS(st) == 0, "a child stopped early (mode %d, accuracy %d)", c_mode, c_acc);
}

static void setup(bool fine)
{
	s_accuracy = c_acc;
	s_batch = c_mode == 2;
	s_fine = fine;
	Sh4_ApplyAccuracyPreset();
	CalculateSync();
	if (c_mode)
		sys_gate_install();
	if (fine)
		sys_slice_part_install();
	// No interrupt is taken: these parts look at the registers only.
	sr.IMASK = 15;
	UpdateSR();
}

// One slice end as the JIT main loop takes it: every slice with the gate off, at the due count with it on.
static void tb_slice_end()
{
	if (!c_mode)
		UpdateSystem_no_event();
	else if (++Sh4cntx.sys_elapsed >= Sh4cntx.sys_due)
		UpdateSystem_gate();
	g_trace.clear();
}

static u32 rd(u32 addr, u32 size) { return reg_read(TMU, addr, size); }
static void wr(u32 addr, u32 data, u32 size) { reg_write(TMU, addr, data, size); }

// ---- Flycast's TimerTest (tests/src/hw/sh4/modules/TimerTest.cpp), on our slices ----
struct TT
{
	const char* name;
	u32 n, pass, late;
	bool unf_due;
	std::string diff;
};
static s32 tt_in;

// Flycast's tick(): whole slices go through the update, the rest stays inside the slice.
static void tt_tick(u32 cyc)
{
	const s32 cs = sh4_GetCpuTimeslice();
	tt_in += (s32)cyc;
	while (tt_in >= cs)
	{
		tt_in -= cs;
		Sh4cntx.jit_cycles = cs - tt_in;
		tb_slice_end();
	}
	Sh4cntx.jit_cycles = cs - tt_in;
}

static void tt_eq(TT& t, u32 got, u32 want)
{
	t.n++;
	if (got == want)
	{
		t.pass++;
		return;
	}
	char b[64];
	snprintf(b, sizeof(b), " #%u %X not %X", t.n, got, want);
	t.diff += b;
}

// A TCR read that Flycast expects with the underflow flag.
static void tt_unf(TT& t, u32 got)
{
	if (got != 0x100)
		t.unf_due = true;
	tt_eq(t, got, 0x100);
}

static TT tt_begin(const char* name)
{
	sys_sync();
	tmu_Reset(false);
	tt_in = 0;
	Sh4cntx.jit_cycles = sh4_GetCpuTimeslice();
	return TT{ name, 0, 0, 0, false, "" };
}

// A flag that was not there at Flycast's point must be there after the slice end ("value only").
static TT tt_end(TT t)
{
	if (t.unf_due)
	{
		tt_tick((u32)(sh4_GetCpuTimeslice() - tt_in));
		t.late = (rd(TMU_TCR0_addr, 2) & 0x100) != 0;
	}
	return t;
}

static TT tt_init()
{
	TT t = tt_begin("init");
	for (u32 ch = 0; ch < 3; ch++)
		tt_eq(t, rd(k_tcnt[ch], 4), 0xffffffff);
	for (u32 ch = 0; ch < 3; ch++)
		tt_eq(t, rd(k_tcor[ch], 4), 0xffffffff);
	for (u32 ch = 0; ch < 3; ch++)
		tt_eq(t, rd(k_tcr[ch], 2), 0);
	tt_eq(t, rd(TMU_TSTR_addr, 1), 0);
	return tt_end(t);
}

static TT tt_precision()
{
	TT t = tt_begin("precision");
	wr(TMU_TCOR0_addr, 1000, 4);
	wr(TMU_TCNT0_addr, 1000, 4);
	wr(TMU_TSTR_addr, 1, 1);
	tt_eq(t, rd(TMU_TCOR0_addr, 4), 1000);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 1000);
	tt_eq(t, rd(TMU_TSTR_addr, 1), 1);
	tt_eq(t, rd(TMU_TCR0_addr, 2), 0);
	tt_tick(999 << 4);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 1);
	tt_eq(t, rd(TMU_TCR0_addr, 2), 0);
	tt_tick(2 << 4);
	tt_unf(t, rd(TMU_TCR0_addr, 2));
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 999);
	// Flycast: Sh4cntx.cycle_counter -= 1 << 4, the cycles inside the timeslice
	tt_tick(1 << 4);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 998);
	return tt_end(t);
}

static TT tt_small_tcor()
{
	TT t = tt_begin("smallTCOR");
	// Flycast runs 16 ticks of 0x7fffffff here for its 64-bit clock; ours has no clock since boot.
	for (int i = 0; i < 16; i++)
		UpdateTMU(0x7fffffff);
	wr(TMU_TCOR0_addr, 2, 4);
	wr(TMU_TCNT0_addr, 2, 4);
	wr(TMU_TSTR_addr, 1, 1);
	tt_tick(5 << 4);
	tt_unf(t, rd(TMU_TCR0_addr, 2));
	wr(TMU_TCR0_addr, 0, 2);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 0);
	tt_tick(1 << 4);
	tt_unf(t, rd(TMU_TCR0_addr, 2));
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 1);
	return tt_end(t);
}

static TT tt_underflow()
{
	TT t = tt_begin("underflow");
	wr(TMU_TCOR0_addr, 100, 4);
	wr(TMU_TCNT0_addr, 100, 4);
	wr(TMU_TSTR_addr, 1, 1);
	tt_tick(100 << 4);
	tt_eq(t, rd(TMU_TCR0_addr, 2), 0);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 0);
	tt_tick(1 << 4);
	// "this shouldn't affect the timer"
	wr(TMU_TSTR_addr, rd(TMU_TSTR_addr, 1), 1);
	tt_eq(t, rd(TMU_TCNT0_addr, 4), 99);
	tt_unf(t, rd(TMU_TCR0_addr, 2));
	return tt_end(t);
}

// With TMU Fine TCNT: asserts in the case, those that pass as Flycast wrote them, flags that come at the slice end.
// smallTCOR is the known difference: TCOR 2 underflows more than once inside one slice (modulo, not Flycast's clamp).
struct TTWant { u32 n, pass, late; };
static const TTWant k_tt_want[4] = { { 10, 10, 0 }, { 9, 8, 1 }, { 4, 0, 1 }, { 4, 4, 0 } };

static void timer_test()
{
	setup(c_fine != 0);
	const TT t[4] = { tt_init(), tt_precision(), tt_small_tcor(), tt_underflow() };
	char line[512];
	int n = snprintf(line, sizeof(line), "  TimerTest, TMU Fine TCNT %s, slice %d:", c_fine ? "ON" : "OFF", sh4_GetTimeslice());
	u32 total = 0, pass = 0;
	for (int i = 0; i < 4; i++)
	{
		total += t[i].n;
		pass += t[i].pass;
		n += snprintf(line + n, sizeof(line) - n, " %s %u/%u%s", t[i].name, t[i].pass, t[i].n, t[i].late ? " (+1 late)" : "");
		if (c_fine)
			expect(t[i].n == k_tt_want[i].n && t[i].pass == k_tt_want[i].pass && t[i].late == k_tt_want[i].late,
			       "TimerTest %s (mode %d, slice %d): %u of %u, %u late:%s", t[i].name, c_mode, sh4_GetTimeslice(),
			       t[i].pass, t[i].n, t[i].late, t[i].diff.c_str());
	}
	// Without the row the precision and underflow cases must fail: they are P11's rule.
	if (!c_fine)
		expect(t[1].pass < t[1].n && t[3].pass < t[3].n, "TimerTest passes with TMU Fine TCNT off (slice %d)",
		       sh4_GetTimeslice());
	if (!c_mode)
		fprintf(stderr, "%s = %u of %u as written\n", line, pass, total);
}

// ---- TMU Fine TCNT against a model that counts step by step in continuous time ----
struct MChan
{
	bool on;
	u32 sh;
	u64 phase;
	u32 cnt, cor, unf;
};

// TMU cycles of SH4 cycles, M_FRAC + 16 fraction bits, by its own formula (EFF: x 200 / clock).
enum { M_FRAC = 12 };
static u32 m_mul;
static u64 m_units(u64 cyc) { return (cyc * m_mul) >> 16; }

static void m_run(MChan& c, u64 d)
{
	if (!c.on)
		return;
	c.phase += d;
	u64 steps = c.phase >> c.sh;
	c.phase &= (1ull << c.sh) - 1;
	for (; steps; steps--)
	{
		if (c.cnt)
			c.cnt--;
		else
		{
			// the step that underflows loads TCOR and counts
			c.cnt = c.cor - 1;
			c.unf++;
		}
	}
}

static void model_check()
{
	const bool chk = c_fine != 0;
	setup(chk);
	const s32 cs = sh4_GetCpuTimeslice(), ts = sh4_GetTimeslice();
	const int mhz = (s_tmu_eff && s_clock < 200) ? s_clock : 200;
	m_mul = (u32)llround(200.0 * (double)(1u << (M_FRAC + 16)) / mhz);
	const u64 uts = m_units((u64)ts);
	// One update first: it latches the TMU rate.
	tb_slice_end();
	sys_sync();
	tmu_Reset(false);
	g_seed = 0xD1B54A32D192ED03ull ^ ((u64)c_seed * 0x100000001B3ull);

	MChan m[3];
	u32 mode[3], samp[3] = { 0, 0, 0 }, snap[3] = { 0, 0, 0 }, dut_unf[3] = { 0, 0, 0 };
	u64 t_commit[3] = { 0, 0, 0 };
	Sh4cntx.jit_cycles = cs;
	for (u32 ch = 0; ch < 3; ch++)
	{
		mode[ch] = rnd(5);
		m[ch] = MChan{ true, 4 + M_FRAC + 2 * mode[ch], 0, rnd(3000), 300 + rnd(3000), 0 };
		wr(k_tcr[ch], mode[ch], 2);
		wr(k_tcor[ch], m[ch].cor, 4);
		wr(k_tcnt[ch], m[ch].cnt, 4);
	}
	wr(TMU_TSTR_addr, 7, 1);

	u64 slices = 0, t = 0;
	s32 in = 0;
	u32 reads = 0, moved = 0, differ = 0;
	// Model time of (slices, in): whole slices at the slice's count, as the TMU takes them off at a slice end.
	auto now = [&]() -> u64 {
		const u64 part = cs == ts ? (u64)in : (u64)in * ts / cs;
		return slices * uts + uts * (part / ts) + m_units(part % ts);
	};
	// At the next slice boundary the model's underflow counts are noted: the update brings the TMU there.
	auto advance = [&](u64 to) {
		const u64 b = (slices + 1) * uts;
		if (t < b && to >= b)
		{
			for (MChan& c : m)
				m_run(c, b - t);
			t = b;
			for (u32 ch = 0; ch < 3; ch++)
				snap[ch] = m[ch].unf;
		}
		for (MChan& c : m)
			m_run(c, to - t);
		t = to;
	};
	// A write commits the channel: the TMU has every underflow up to now.
	auto committed = [&](u32 ch) {
		t_commit[ch] = t;
		samp[ch] = m[ch].unf;
	};
	// Counts and clears the TMU's flags; they must be the model's at the TMU's last sync point.
	auto observe = [&](const char* at) {
		for (u32 ch = 0; ch < 3; ch++)
		{
			const u32 cr = rd(k_tcr[ch], 2);
			if (cr & 0x100)
			{
				dut_unf[ch]++;
				wr(k_tcr[ch], cr & 7, 2);
			}
			if (chk)
				expect(dut_unf[ch] == samp[ch], "seed %u mode %d: channel %u has %u underflows %s, the model %u", c_seed,
				       c_mode, ch, dut_unf[ch], at, samp[ch]);
		}
	};
	auto sync_point = [&]() {
		wr(TMU_TSTR_addr, rd(TMU_TSTR_addr, 1), 1);
		for (u32 ch = 0; ch < 3; ch++)
			committed(ch);
		observe("after a TSTR rewrite");
	};

	// Time moves d cycles on in the slice; the JIT's count follows.
	auto go = [&](s32 d) {
		in += d;
		advance(now());
		Sh4cntx.jit_cycles = cs - in;
	};
	// The slice end as the JIT takes it: the next slice's count is stored, then the update or the gate's count.
	auto end_slice = [&]() {
		in -= cs;
		slices++;
		Sh4cntx.jit_cycles = cs - in;
		tb_slice_end();
		for (u32 ch = 0; ch < 3; ch++)
			if (t_commit[ch] <= slices * uts)
				samp[ch] = snap[ch];
		observe("at a slice end");
	};
	auto read_tcnt = [&](u32 ch) {
		const u32 v = rd(k_tcnt[ch], 4);
		reads++;
		moved += v != tmu_regs_CNT[ch];
		differ += v != m[ch].cnt;
		if (chk)
			expect(v == m[ch].cnt, "seed %u mode %d: TCNT%u reads %08X, the model %08X (slice %llu, %d cycles in)",
			       c_seed, c_mode, ch, v, m[ch].cnt, (unsigned long long)slices, in);
	};
	auto write_tcnt = [&](u32 ch, u32 v) {
		m[ch].cnt = v;
		wr(k_tcnt[ch], v, 4);
		committed(ch);
		observe("after a TCNT write");
	};
	// A write past the slice end leaves a committed part for the next slice. Then slices with no TMU access (the
	// gate skips them) and a write early in a slice: the batch must not run over that part. Called at a slice start.
	auto episode = [&]() {
		const u32 ch = rnd(3);
		const s32 b = 40 + (s32)rnd(60);
		go(cs + b - in);
		write_tcnt(ch, 1000000 + rnd(1000));
		end_slice();
		for (u32 n = 2 + rnd(4); n; n--)
		{
			go(cs + (s32)rnd(4) - in);
			end_slice();
		}
		go(1 + (s32)rnd((u32)b / 2));
		write_tcnt(ch, 1000000 + rnd(1000));
		go(1 + (s32)rnd(8));
		for (u32 c = 0; c < 3; c++)
			read_tcnt(c);
	};

	u32 over = rnd(128), ends = 0;
	for (u32 step = 0; step < c_steps; step++)
	{
		go(1 + (s32)rnd((u32)cs / 3));
		// The slice end: the slice is used up, plus a block's overshoot.
		if (in >= cs + (s32)over)
		{
			end_slice();
			over = rnd(128);
			if (++ends % 29 == 0)
				episode();
			continue;
		}
		const u32 a = rnd(12), ch = rnd(3), x = rnd(0x10000);
		if (a < 6)
			read_tcnt(ch);
		else if (a == 6)
			write_tcnt(ch, (x & 3) ? rnd(3000) : rnd(48));
		else if (a == 7)
		{
			const u32 v = rnd(8) | rnd(8);
			wr(TMU_TSTR_addr, v, 1);
			for (u32 c = 0; c < 3; c++)
			{
				committed(c);
				m[c].on = (v >> c) & 1;
			}
			observe("after a TSTR write");
		}
		else if (a == 8)
		{
			// A new prescaler: the count goes on from here at the new rate, the prescaler from 0. The write also
			// sets the flag, so an underflow the TMU has yet to report is counted first.
			if (m[ch].unf != samp[ch])
				sync_point();
			const u32 nm = rnd(5);
			wr(k_tcr[ch], nm, 2);
			committed(ch);
			if (nm != mode[ch])
			{
				mode[ch] = nm;
				m[ch].sh = 4 + M_FRAC + 2 * nm;
				m[ch].phase = 0;
			}
		}
		else if (a == 9)
		{
			// TCOR has no handler: the TMU takes it at its next update, so the write follows a commit here.
			sync_point();
			m[ch].cor = 300 + rnd(3000);
			wr(k_tcor[ch], m[ch].cor, 4);
		}
		else if (a == 10)
			sync_point();
	}
	if (!c_mode && c_seed == 1)
		fprintf(stderr, "  TCNT model, TMU Fine TCNT %s, clock %d, TMU Clock %s, slice %d: %u reads, %u differ, %u past the slice's start\n",
		        chk ? "ON" : "OFF", s_clock, s_tmu_eff ? "EFF" : "SH4", ts, reads, differ, moved);
	if (chk)
		expect(moved > reads / 8, "only %u of %u TCNT reads saw cycles inside a slice", moved, reads);
	else
		// Without the row the reads stay at the slice's start: the model must see that.
		expect(differ > reads / 8, "TMU Fine TCNT off: only %u of %u TCNT reads differ from the model", differ, reads);
}

// ---- TMU Clock ----
// The TMU step before bundle 7: SH4 cycles, no fraction bits.
static bool old_step(u32& pres, u32& cnt, u32 shift, u32 cor, u32 clc)
{
	pres += clc;
	u32 steps = pres >> shift;
	pres &= (1u << shift) - 1u;
	if (steps == 0)
		return false;
	bool unf = false;
	if (steps > cnt)
	{
		steps -= cnt;
		unf = true;
		if (cor > 0 && steps > cor)
			steps %= cor;
		cnt = cor;
	}
	cnt -= steps;
	return unf;
}

static u32 any_count()
{
	const u32 k = rnd(4);
	return k == 0 ? rnd(6) : k == 1 ? rnd(200) : rnd(100000);
}

static void clock_child()
{
	setup(false);
	const u32 ts = (u32)sh4_GetTimeslice();
	g_seed = 0x9E3779B97F4A7C15ull ^ (u64)c_acc;
	// The SH4's rate, equal to the old step count by count: SH4 at any clock, EFF at 200 MHz and above.
	static const int k_same[][2] = { { 200, 0 }, { 200, 1 }, { 150, 0 }, { 100, 0 }, { 220, 0 }, { 220, 1 }, { 250, 1 } };
	for (const auto& cfg : k_same)
	{
		s_clock = cfg[0];
		s_tmu_eff = cfg[1];
		for (u32 trial = 0; trial < 40; trial++)
		{
			tmu_Reset(false);
			u32 pres[3] = { 0, 0, 0 }, cnt[3], cor[3], sh[3], on = 0;
			bool unf[3] = { false, false, false };
			for (u32 ch = 0; ch < 3; ch++)
			{
				const u32 mode = rnd(5);
				sh[ch] = 4 + 2 * mode;
				cor[ch] = any_count();
				cnt[ch] = any_count();
				on |= (rnd(4) != 0) << ch;
				wr(k_tcr[ch], mode, 2);
				wr(k_tcor[ch], cor[ch], 4);
				wr(k_tcnt[ch], cnt[ch], 4);
			}
			wr(TMU_TSTR_addr, on, 1);
			for (u32 s = 0; s < 400; s++)
			{
				UpdateTMU(ts);
				for (u32 ch = 0; ch < 3; ch++)
				{
					if ((on >> ch) & 1)
						unf[ch] |= old_step(pres[ch], cnt[ch], sh[ch], cor[ch], ts);
					expect(tmu_regs_CNT[ch] == cnt[ch] && ((tmu_regs_CR[ch] >> 8) & 1) == (u32)unf[ch],
					       "clock %d, TMU Clock %d, slice %u: TCNT%u %08X flag %u, the old step %08X flag %d", cfg[0],
					       cfg[1], s, ch, tmu_regs_CNT[ch], (tmu_regs_CR[ch] >> 8) & 1, cnt[ch], (int)unf[ch]);
				}
			}
		}
	}
	// EFF below 200 MHz: 50 MHz of Pclk per emulated second, so 200 / clock TMU cycles per SH4 cycle.
	static const int k_clk[] = { 100, 150, 175, 199 };
	for (int clk : k_clk)
		for (int eff = 0; eff < 2; eff++)
		{
			s_clock = clk;
			s_tmu_eff = eff;
			tmu_Reset(false);
			wr(TMU_TCR0_addr, 0, 2);
			wr(TMU_TCOR0_addr, 0xffffffff, 4);
			wr(TMU_TCNT0_addr, 0xffffffff, 4);
			wr(TMU_TSTR_addr, 1, 1);
			const u32 n = 200000;
			for (u32 s = 0; s < n; s++)
				UpdateTMU(ts);
			const u32 steps = 0xffffffffu - tmu_regs_CNT[0];
			const double want = (double)n * ts * (eff ? 200.0 / clk : 1.0) / 16.0;
			expect(fabs(steps - want) <= 1.0 + (eff ? want * 1e-6 : 0.0), "clock %d, TMU Clock %d: %u steps in %u slices, not %.1f",
			       clk, eff, steps, n, want);
		}
}

// ---- the SPG rows ----
static void frames(u32 n)
{
	const u32 v0 = spg_VblankCountTotal;
	for (u32 guard = 0; spg_VblankCountTotal - v0 < n && guard < 400000; guard++)
		tb_slice_end();
}

static void spg_child()
{
	setup(false);
	// Interlaced, the reset line counts: a line is at least 3 slices long.
	libPvr_WriteReg(PVR + SPG_CONTROL_addr, 0x150, 4);
	// One frame first: the line counter starts before line 0.
	frames(1);
	for (int frame = 0; frame < 2; frame++)
	{
		s_field = frame;
		u32 st = libPvr_ReadReg(PVR + SPG_STATUS_addr, 4), lines = 0, flips = 0, wraps = 0, bad = 0;
		for (u32 guard = 0; wraps < 4 && guard < 400000; guard++)
		{
			tb_slice_end();
			const u32 n = libPvr_ReadReg(PVR + SPG_STATUS_addr, 4);
			const bool flip = ((n ^ st) >> 10) & 1;
			if ((n & 0x3FF) != (st & 0x3FF))
			{
				lines++;
				flips += flip;
				wraps += (n & 0x3FF) == 0;
				bad += flip != (frame ? (n & 0x3FF) == 0 : true);
			}
			else
				bad += flip;
			st = n;
		}
		expect(!bad && wraps == 4 && flips == (frame ? wraps : lines),
		       "SPG Field Bit %s, mode %d: %u flips in %u lines and %u frames, %u at a wrong line", frame ? "FRAME" : "LINE",
		       c_mode, flips, lines, wraps, bad);
	}
	s_field = 0;

	// HBlank IRQ: rows off or on, SPG_HBLANK_INT's mode and compare line. 3 frames from vblank to vblank.
	static const u32 k_hb[][3] = { { 1, 0, 100 }, { 1, 0, 0 }, { 1, 2, 7 }, { 1, 1, 50 }, { 1, 3, 50 }, { 0, 0, 100 }, { 0, 2, 7 } };
	for (const auto& h : k_hb)
	{
		s_hblank = (int)h[0];
		libPvr_WriteReg(PVR + SPG_HBLANK_INT_addr, h[2] | (h[1] << 12), 4);
		frames(1);
		g_holly_n[5] = 0;
		frames(3);
		const u32 want = !h[0] || h[1] == 0 ? 3 : h[1] == 2 ? 3 * spg_ScanlineCount : 0;
		const u32 line = !h[0] ? (u32)SPG_VBLANK.vbstart : h[2];
		expect(g_holly_n[5] == want && (h[0] && h[1] ? true : g_holly_line[5] == line),
		       "HBlank IRQ %s, mode %u line %u, gate mode %d: %u interrupts in 3 frames (last at line %u), not %u",
		       h[0] ? "MODES" : "VBLANK", h[1], h[2], c_mode, g_holly_n[5], g_holly_line[5], want);
	}
	s_hblank = 0;

	// VBLANK INT Reset
	g_vblank_int_reset_preset = 0;
	Regs_Reset(false);
	expect(SPG_VBLANK_INT.full == 0x01500104, "VBLANK INT Reset OLD: %08X", SPG_VBLANK_INT.full);
	g_vblank_int_reset_preset = 1;
	Regs_Reset(false);
	expect(SPG_VBLANK_INT.full == 0x00150104 && SPG_VBLANK_INT.vblank_out_interrupt_line_number == 21 &&
	       SPG_VBLANK_INT.vblank_in_interrupt_line_number == 260,
	       "VBLANK INT Reset FLYCAST: %08X", SPG_VBLANK_INT.full);
}

int main(int argc, char** argv)
{
	const u32 seeds = argc > 1 ? (u32)atoi(argv[1]) : 40;
	const u32 slices = argc > 2 ? (u32)atoi(argv[2]) : 4000;
	c_steps = argc > 3 ? (u32)atoi(argv[3]) : 20000;
	boot();
	check_rows(seeds, slices);

	static const int k_rate[][2] = { { 200, 0 }, { 150, 1 }, { 150, 0 }, { 175, 1 }, { 250, 0 } };
	for (c_acc = 0; c_acc < 3; c_acc++)
	{
		for (c_mode = 0; c_mode < 3; c_mode++)
		{
			for (c_fine = 1; c_fine >= 0; c_fine--)
				in_child(timer_test);
			c_fine = 1;
			for (const auto& rate : k_rate)
			{
				s_clock = rate[0];
				s_tmu_eff = rate[1];
				for (c_seed = 1; c_seed <= 4; c_seed++)
					in_child(model_check);
			}
			s_clock = 200;
			s_tmu_eff = 0;
			in_child(spg_child);
		}
		c_mode = 0;
		c_fine = 0;
		c_seed = 1;
		in_child(model_check);
		in_child(clock_child);
	}
	fprintf(stderr, "%s: %u checks, %u failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
