// SH4 interpreter. Accuracy preset (0 Fast/1 Balanced/2 Accurate) sets timeslice and polling periods;
// it is latched at the start of each Sh4_int_Run().

#include "types.h"

#include "sh4_interpreter.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "sh4_opcode_list.h"
#include "sh4_registers.h"
#include "sh4_if.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "plugs/vbaARM/arm_aica.h"  // ARM7 CPU tick (vbaARM)
#include "dmac.h"
#include "dc/mem/sb.h"   // SB_C2DSTAT/SB_C2DLEN/SB_C2DST for the [DMA] probe
#include "dc/gdrom/gdrom_if.h"
#include "dc/maple/maple_if.h"
#include "intc.h"
#include "tmu.h"
#include "dc/mem/sh4_mem.h"
#include "ccn.h"
#include "sh4_sched.h"   // SCHED preset: unified cycle-deadline event scheduler
#include "sys_gate.h"
#include "ps3/ps3_stall.h"
#include "ps3_timebase.h"

// SCHED preset accessor (ps3_presets.cpp). When on, migrated completion/IRQ events
// fire through sh4_sched (deadline-ordered) instead of at their own cascade tier.
extern "C" int get_sched_preset();
extern "C" int get_sys_gate_batch_preset();
#include <time.h>
#include <float.h>

#define GetN(op) (((op) >> 8) & 0xf)
#define GetM(op) (((op) >> 4) & 0xf)

// Volatile: may be cleared from another thread.
volatile bool sh4_int_bCpuRun = false;

// fsca table: [0..0xFFFF] full circle of sin, [0x10000..0x13FFF] wrap copy so cos needs no branch.
f32 sin_table[0x10000 + 0x4000];

static s32 s_timeslice     = SH4_TIMESLICE_ACCURATE;
static s32 s_cpu_timeslice = SH4_TIMESLICE_ACCURATE;   // SH4 cycles the CPU runs per slice
static s32 s_cpu_ratio     = SH4_CPU_RATIO_ACCURATE;
static u32 s_medium_period = SH4_MEDIUM_ACCURATE;
static u32 s_slow_period   = SH4_SLOW_ACCURATE;
static u32 s_vslow_period  = SH4_VSLOW_ACCURATE;

// Selects all timing parameters from the current accuracy preset.
// Called once at the start of each Run() so changes take effect immediately.
static void ApplyAccuracyPreset()
{
	const int preset = get_accuracy_preset();

	if (preset == 0)  // Fast
	{
		s_timeslice     = SH4_TIMESLICE_FAST;
		s_cpu_ratio     = SH4_CPU_RATIO_FAST;
		s_medium_period = SH4_MEDIUM_FAST;
		s_slow_period   = SH4_SLOW_FAST;
		s_vslow_period  = SH4_VSLOW_FAST;
		printf("Sh4: accuracy preset = FAST (timeslice %d)\n", s_timeslice);
	}
	else if (preset == 1)  // Balanced
	{
		s_timeslice     = SH4_TIMESLICE_BALANCED;
		s_cpu_ratio     = SH4_CPU_RATIO_BALANCED;
		s_medium_period = SH4_MEDIUM_BALANCED;
		s_slow_period   = SH4_SLOW_BALANCED;
		s_vslow_period  = SH4_VSLOW_BALANCED;
		printf("Sh4: accuracy preset = BALANCED (timeslice %d)\n", s_timeslice);
	}
	else  // Accurate (default)
	{
		s_timeslice     = SH4_TIMESLICE_ACCURATE;
		s_cpu_ratio     = SH4_CPU_RATIO_ACCURATE;
		s_medium_period = SH4_MEDIUM_ACCURATE;
		s_slow_period   = SH4_SLOW_ACCURATE;
		s_vslow_period  = SH4_VSLOW_ACCURATE;
		printf("Sh4: accuracy preset = ACCURATE (timeslice %d)\n", s_timeslice);
	}

	// SH4 Clock above 200 MHz: the SH4 runs clock/200 times the slice; the modules still advance by s_timeslice.
	// Capped at the menu's 250: the JIT main loop adds the slice as a 16-bit immediate.
	int mhz = get_sh4_clock_preset();
	if (mhz > 250)
		mhz = 250;
	s_cpu_timeslice = mhz > 200 ? (s_timeslice * mhz + 100) / 200 : s_timeslice;
}

// The JIT bakes the timeslice into its mainloop, so it latches the preset before emitting it
// (recSh4_Run() in rec_v2/driver.cpp).
void Sh4_ApplyAccuracyPreset()
{
	ApplyAccuracyPreset();
}

s32 sh4_GetTimeslice()
{
	return s_timeslice;
}

s32 sh4_GetCpuTimeslice()
{
	return s_cpu_timeslice;
}

s32 sh4_GetCpuRatio()
{
	return s_cpu_ratio;
}

void Sh4_int_Run()
{
	// Latch timing parameters for this run session
	ApplyAccuracyPreset();

	sh4_int_bCpuRun = true;

	s32 l = s_cpu_timeslice;

	do
	{
		// Inner loop: dispatch one timeslice worth of opcodes
		do
		{
			const u32 op = ReadMem16(next_pc);
			next_pc += 2;

			OpPtr[op](op);
			l -= s_cpu_ratio;
		} while (l > 0);

		l += s_cpu_timeslice;

		// Peripheral update cascade (TMU, PVR, AICA, DMA, GDRom, Maple …)
		UpdateSystem();
	} while (sh4_int_bCpuRun);

	sh4_int_bCpuRun = false;
}

void Sh4_int_Stop()
{
	if (sh4_int_bCpuRun)
		sh4_int_bCpuRun = false;
	// SYS GATE: the next slice end runs the update, which reports the stop. Any thread may land here.
	Sh4cntx.sys_due = 0;
}

// Debugger single step.
void Sh4_int_Step()
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running, can't step\n");
		return;
	}

	const u32 op = ReadMem16(next_pc);
	next_pc += 2;
	ExecuteOpcode(op);
}

// Debugger skip.
void Sh4_int_Skip()
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running, can't Skip\n");
		return;
	}

	next_pc += 2;
}

void Sh4_int_Reset(bool /*Manual*/)
{
	if (sh4_int_bCpuRun)
	{
		printf("Sh4 Is running, can't Reset\n");
		return;
	}

	next_pc = 0xA0000000u;

	// Integer registers
	memset(r,      0, sizeof(r));
	memset(r_bank, 0, sizeof(r_bank));

	// Control / system registers
	gbr = ssr = spc = sgr = dbr = vbr = 0;
	mach = macl = pr = fpul = 0;

	sr.SetFull(0x700000F0u);
	old_sr = sr;
	UpdateSR();

	// xf[] is not cleared: clearing the shadow bank gave a black frame on boot.
	memset(fr, 0, sizeof(fr));

	fpscr.full = 0x00040001u;
	old_fpscr  = fpscr;
	UpdateFPSCR();

	// Drop in-flight SCHED deadlines so a reset never fires a stale completion.
	sh4_sched_reset();
	Sh4cntx.sys_elapsed = 0;
	Sh4cntx.sys_due = 0;

	printf("Sh4 Reset\n");
}

#ifdef _PS3
// Linked into the EBOOT by ps3/ps3_fsca_table.S.
extern "C" const u8 fsca_table_data[];
extern "C" const u8 fsca_table_data_end[];
#endif

void GenerateSinCos()
{
	printf("Generating sincos tables ...\n");

#ifdef _PS3
	if ((size_t)(fsca_table_data_end - fsca_table_data) != sizeof(f32) * 0x8000)
		die("fsca-table.bin has the wrong size!");

	memcpy(sin_table, fsca_table_data, sizeof(f32) * 0x8000);
#else
	char* path = GetEmuPath("data/fsca-table.bin");
	FILE* tbl  = fopen(path, "rb");
	free(path);

	if (!tbl)
		die("fsca-table.bin is missing!");

	fread(sin_table, sizeof(f32), 0x8000, tbl);
	fclose(tbl);
#endif

	// The table file is little-endian: host_to_le swaps it on big-endian hosts
	for (int i = 0; i < 0x8000; i++)
		(u32&)sin_table[i] = host_to_le<4>((u32&)sin_table[i]);

	// Mirror: second half of the unit circle (sin is antisymmetric)
	sin_table[0x8000] = 0.0f;                    // sin(π) == 0 exactly
	for (int i = 0x8001; i < 0x10000; i++)
		sin_table[i] = -sin_table[i - 0x8000];

	// Overlap region for cos: copy [0..0x3FFF] → [0x10000..0x13FFF]
	for (int i = 0x10000; i < 0x14000; i++)
		sin_table[i] = sin_table[i & 0xFFFF];
}

void Sh4_int_Init()
{
	BuildOpcodeTables();
	GenerateSinCos();
}

void Sh4_int_Term()
{
	Sh4_int_Stop();
	printf("Sh4 Term\n");
}

bool Sh4_int_IsCpuRunning()
{
	return sh4_int_bCpuRun;
}

void ExecuteDelayslot()
{
	const u32 op = IReadMem16(next_pc);
	next_pc += 2;
	if (op != 0)
		ExecuteOpcode(op);
}

void ExecuteDelayslot_RTE()
{
	// Full SR from SSR, T included (original nullDC). Leaving T out corrupted any code
	// interrupted between a compare and its branch. The RTE handler then calls UpdateSR().
	sr.SetFull(ssr);
	ExecuteDelayslot();
}

// UpdateSystem runs every timeslice; Medium (AICA, DMA), Slow (GD-ROM) and VerySlow (Maple, RTC)
// every s_*_period calls, so they scale with the accuracy preset.
s32 rtc_cycles  = 0;
u32 update_cnt  = 0;
u32 gcp_timer   = 0;

// [sys] probe, with JIT C-Calls: 1 gate run in 16 stamps the parts of its slices. Times leave out the nested
// [stall] sections (gd dma, rend, log, sleep), as [stall] sys does.
enum { SP_GATE, SP_ARM, SP_DMA, SP_GD, SP_SPIN, SP_VSLOW, SP_TMU, SP_PVR, SP_VBL, SP_ADMA, SP_SCHED, SP_PLAN,
       SP_INTC, SP_QUIET, SP_N };
static const char* const k_sp_name[SP_N] = { "gate", "arm", "dma", "gd", "spin", "vslow", "tmu", "pvr", "vbl",
                                             "aicadma", "sched", "plan", "intc", "quiet" };
static bool s_sp_on;
static u64 s_sp_t, s_sp_nest;
static u64 s_sp_tot[SP_N];
static u32 s_sp_n[SP_N];
static u32 s_sp_runs, s_sp_samples, s_sp_slices;
static u64 s_sp_irq, s_sp_sync;
static u32 s_sp_irq_n, s_sp_sync_n;
extern u32 spg_VblankCountTotal;

// Ticks since (t0, n0) less the nested sections' ticks in them.
static inline u64 sp_excl(u64 now, u64 nest, u64 t0, u64 n0)
{
	const s64 d = (s64)(now - t0) - (s64)(nest - n0);
	return d > 0 ? (u64)d : 0;
}

static inline void sp_mark(int part)
{
	if (!s_sp_on)
		return;
	const u64 now = gettime(), nest = g_stall_nested;
	s_sp_tot[part] += sp_excl(now, nest, s_sp_t, s_sp_nest);
	s_sp_n[part]++;
	s_sp_t = now;
	s_sp_nest = nest;
}

void FASTCALL VerySlowUpdate()
{
	gcp_timer++;
	rtc_cycles -= (s_timeslice * (s32)s_vslow_period);
	if (rtc_cycles <= 0)
	{
		rtc_cycles += SH4_CLOCK_EFF; // effective clock: 1 RTC second per emulated second, stays synced to video under underclock
		settings.dreamcast.RTC++;
	}
	maple_Update(s_timeslice * s_vslow_period);
	sp_mark(SP_VSLOW);
}

void FASTCALL SlowUpdate()
{
	UpdateGDRom();
	sp_mark(SP_GD);
	gdrom_spinup_tick(s_timeslice * s_slow_period);
	sp_mark(SP_SPIN);

	if (!(update_cnt & (s_vslow_period - 1)))
		VerySlowUpdate();
}

void FASTCALL MediumUpdate()
{
	UpdateAica(s_timeslice * s_medium_period);
	UpdateArm(s_timeslice * s_medium_period);  // ARM7 tick — same cycle count, arm_aica.cpp divides by arm_sh4_bias (8)
	sp_mark(SP_ARM);
	UpdateDMA();
	sp_mark(SP_DMA);

	if (!(update_cnt & (s_slow_period - 1)))
		SlowUpdate();
}

// One timeslice of the update without the interrupt check. Touches no SH4 GPRs.
static inline void sys_slice()
{
	sp_mark(SP_GATE);
	if (!(update_cnt & (s_medium_period - 1)))
		MediumUpdate();

	update_cnt++;

	UpdateTMU(s_timeslice);
	sp_mark(SP_TMU);
	const u32 vbl = spg_VblankCountTotal;
	UpdatePvr(s_timeslice);
	sp_mark(spg_VblankCountTotal != vbl ? SP_VBL : SP_PVR);
	UpdateAicaDma(s_timeslice);     // deferred AICA G2-DMA completion IRQ (DMA_FIX)
	sp_mark(SP_ADMA);

	// SCHED: fire due completions before the interrupt check so their IRQs are seen this timeslice.
	if (get_sched_preset())
	{
		sh4_sched_tick(s_timeslice);
		sp_mark(SP_SCHED);
	}
}

int FASTCALL UpdateSystem()
{
	// SYS GATE: SLEEP's loop (an interpreter fallback under the JIT) counts as one more slice end.
	if (g_sys_gate_on)
	{
		Sh4cntx.sys_elapsed++;
		UpdateSystem_gate();
		return UpdateINTC();
	}
	sys_slice();
	return UpdateINTC();
}

// P6 JIT EXIT, set by the JIT for its run: a stop request then also takes the main loop's cold path, which returns.
bool g_jit_exit_on;

// Split UpdateSystem() for the JIT: _no_event() does all of it except the interrupt dispatch (touches no
// GPRs, returns nonzero if one is pending); _handle_event() does only the dispatch.
int FASTCALL UpdateSystem_no_event()
{
	StallScope st_(ST_SYS);
	CCALL(CC_TIMESLICE);
	sys_slice();
	return UpdateINTC_pending() | (int)(g_jit_exit_on && !sh4_int_bCpuRun);
}

// ---- Plan 5.36 SYS GATE ----
// The JIT counts slice ends in Sh4cntx.sys_elapsed and calls UpdateSystem_gate() once the count reaches sys_due.
bool g_sys_gate_on;

enum { GR_MED, GR_TMU, GR_SPG, GR_DMA, GR_SCHED, GR_KICK, GR_N };
static const char* const k_gate_why[GR_N] = { "med", "tmu", "spg", "dma", "sched", "kick" };
static u32 s_gate_runs, s_gate_slices, s_gate_syncs, s_gate_kicks, s_gate_why[GR_N];
static s32 s_gate_plan;        // slice ends left in the last plan; a run below it was kicked
static u32 s_gate_plan_why;

// Slice ends up to the next one that must run the update: the medium tick, a TMU underflow, an SPG event, an
// AICA DMA end. The medium period bounds it, so the dry runs stay short.
static s32 sys_plan()
{
	if (get_sched_preset())
	{
		s_gate_plan_why = GR_SCHED;
		return 1;
	}
	const s32 ts = s_timeslice;
	const u32 m = s_medium_period;
	s32 n = (s32)(((m - (update_cnt & (m - 1))) & (m - 1)) + 1);
	u32 why = GR_MED;
	s32 t = g_sys_batch_on ? tmu_slices_to_event_fast(ts, n) : tmu_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_TMU; }
	t = spg_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_SPG; }
	t = aica_dma_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_DMA; }
	s_gate_plan_why = why;
	return n;
}

// Plan 5.42 SYS GATE BATCH: n slices as one step per module. False, with nothing moved, when one of them is a
// medium tick or holds a module event; the caller then runs them one by one, so the result is the same.
bool g_sys_batch_on;
u32 g_gate_batches, g_gate_batch_slices, g_gate_batch_miss;

static bool sys_slices_quiet(s32 n)
{
	const u32 m = s_medium_period, u = update_cnt & (m - 1);
	const s32 ts = s_timeslice;
	if (u == 0 || u + (u32)n > m || get_sched_preset() || !spg_slices_quiet(ts, n) ||
	    !aica_dma_slices_quiet(ts, n) || !tmu_slices_quiet_run(ts, n))
	{
		g_gate_batch_miss++;
		return false;
	}
	aica_dma_slices_run(ts, n);
	spg_slices_run(ts, n);
	update_cnt += (u32)n;
	g_gate_batches++;
	g_gate_batch_slices += (u32)n;
	sp_mark(SP_QUIET);
	return true;
}

// The slices the gate skipped and this one, then the next plan. Same return as _no_event().
int FASTCALL UpdateSystem_gate()
{
	StallScope st_(ST_SYS);
	CCALL(CC_TIMESLICE);
	s32 k = Sh4cntx.sys_elapsed;
	s_sp_on = g_stall_on && (++s_sp_runs & 15) == 0;
	if (s_sp_on)
	{
		s_sp_samples++;
		s_sp_slices += (u32)k;
		s_sp_t = gettime();
		s_sp_nest = g_stall_nested;
	}
	s_gate_runs++;
	s_gate_slices += k;
	s_gate_why[k < s_gate_plan ? GR_KICK : s_gate_plan_why]++;
	Sh4cntx.sys_elapsed = 0;
	// The last slice stays whole: the plan puts the event there.
	if (g_sys_batch_on && k > 2 && sys_slices_quiet(k - 1))
		k = 1;
	while (k-- > 0)
		sys_slice();
	s_gate_plan = sys_plan();
	sp_mark(SP_PLAN);
	Sh4cntx.sys_due = s_gate_plan;
	const int rv = UpdateINTC_pending() | (int)(g_jit_exit_on && !sh4_int_bCpuRun);
	sp_mark(SP_INTC);
	s_sp_on = false;
	return rv;
}

// None of the skipped slices holds an event, so running them before a write changes nothing the guest saw.
void sys_gate_sync_slow()
{
	s32 k = Sh4cntx.sys_elapsed;
	if (k <= 0)
		return;
	StallScope st_(ST_SYS);
	const bool timed = g_stall_on;
	const u64 t0 = timed ? gettime() : 0, n0 = g_stall_nested;
	s_gate_syncs++;
	s_gate_slices += k;
	Sh4cntx.sys_elapsed = 0;
	Sh4cntx.sys_due -= k;
	s_gate_plan -= k;
	if (g_sys_batch_on && k > 1 && sys_slices_quiet(k))
		k = 0;
	while (k-- > 0)
		sys_slice();
	if (timed)
	{
		s_sp_sync += sp_excl(gettime(), g_stall_nested, t0, n0);
		s_sp_sync_n++;
	}
}

void sys_gate_kick_slow()
{
	s_gate_kicks++;
	Sh4cntx.sys_due = 0;
}

s32 sys_gate_pending_slow()
{
	return Sh4cntx.sys_elapsed;
}

// By the JIT main loop, once, before the first block runs.
void sys_gate_install()
{
	g_sys_gate_on = true;
	Sh4cntx.sys_elapsed = 0;
	Sh4cntx.sys_due = 0;
	s_gate_plan = 0;
	g_sys_batch_on = get_sys_gate_batch_preset() != 0;
	tmu_gate_install();
}

// ---- P11 TMU Fine TCNT ----
// The JIT stores its cycle count in Sh4cntx.jit_cycles; the TMU reads how far the slice has run.
bool g_slice_part_on;

s32 sys_slice_part_slow()
{
	const s32 in = s_cpu_timeslice - Sh4cntx.jit_cycles;
	if (in <= 0)
		return 0;
	// SH4 Clock above 200: the SH4's longer slice is one module slice.
	return s_cpu_timeslice == s_timeslice ? in : (s32)((s64)in * s_timeslice / s_cpu_timeslice);
}

// By the JIT main loop, once, before the first block runs.
void sys_slice_part_install()
{
	g_slice_part_on = true;
	Sh4cntx.jit_cycles = s_cpu_timeslice;
	tmu_gate_install();
}

// [sys] line: ms/s per part (the sampled runs scaled to all runs, one mftb per stamp taken out), then ns per call.
static void sys_probe_stats(double secs)
{
	if (s_sp_samples)
	{
		const double tick_ns = 1e9 / (double)ps3_timebase_hz;
		const int reads = 1024;
		const u64 m0 = gettime();
		for (int i = 0; i < reads; i++)
			(void)gettime();
		const double mftb_ns = (double)(gettime() - m0) * tick_ns / (reads + 1);
		const double scale = (double)s_sp_runs / s_sp_samples;
		char buf[1024];
		int n = snprintf(buf, sizeof(buf), "[sys] runs %.0f/s sampled %u (%u slices) | ms/s:", s_sp_runs / secs,
		                 s_sp_samples, s_sp_slices);
		double sum = 0.0;
		for (int i = 0; i < SP_N && n < (int)sizeof(buf); i++)
		{
			if (!s_sp_n[i])
				continue;
			const double ns = (double)s_sp_tot[i] * tick_ns - s_sp_n[i] * mftb_ns;
			const double ms = (ns > 0.0 ? ns : 0.0) * scale / 1e6 / secs;
			sum += ms;
			n += snprintf(buf + n, sizeof(buf) - n, " %s %.2f", k_sp_name[i], ms);
		}
		const double irq = ((double)s_sp_irq * tick_ns - s_sp_irq_n * mftb_ns) / 1e6 / secs;
		const double sync = ((double)s_sp_sync * tick_ns - s_sp_sync_n * mftb_ns) / 1e6 / secs;
		if (n < (int)sizeof(buf))
			n += snprintf(buf + n, sizeof(buf) - n, " = %.1f | irq %.2f (%.0f/s) sync %.2f (%.0f/s) | ns/call:", sum,
			              irq, s_sp_irq_n / secs, sync, s_sp_sync_n / secs);
		for (int i = 0; i < SP_N && n < (int)sizeof(buf); i++)
			if (s_sp_n[i])
				n += snprintf(buf + n, sizeof(buf) - n, " %s %.0f", k_sp_name[i],
				              (double)s_sp_tot[i] * tick_ns / s_sp_n[i] - mftb_ns);
		if (n < (int)sizeof(buf))
			snprintf(buf + n, sizeof(buf) - n, " | mftb %.1f ns", mftb_ns);
		printf("%s\n", buf);
	}
	s_sp_runs = s_sp_samples = s_sp_slices = s_sp_irq_n = s_sp_sync_n = 0;
	s_sp_irq = s_sp_sync = 0;
	for (int i = 0; i < SP_N; i++)
	{
		s_sp_tot[i] = 0;
		s_sp_n[i] = 0;
	}
}

void sys_gate_stats(double secs)
{
	if (!g_sys_gate_on || secs <= 0.0)
		return;
	char buf[256];
	int n = snprintf(buf, sizeof(buf), "[gate] slices=%.0f/s runs=%.0f/s syncs=%.0f/s kicks=%.0f/s |",
	                 s_gate_slices / secs, s_gate_runs / secs, s_gate_syncs / secs, s_gate_kicks / secs);
	for (u32 i = 0; i < GR_N && n < (int)sizeof(buf); i++)
		n += snprintf(buf + n, sizeof(buf) - n, " %s %.0f", k_gate_why[i], s_gate_why[i] / secs);
	if (g_sys_batch_on && n < (int)sizeof(buf))
		snprintf(buf + n, sizeof(buf) - n, " | batch runs=%.0f/s slices=%.0f/s miss=%.0f/s", g_gate_batches / secs,
		         g_gate_batch_slices / secs, g_gate_batch_miss / secs);
	g_gate_batches = g_gate_batch_slices = g_gate_batch_miss = 0;
	printf("%s\n", buf);
	s_gate_runs = s_gate_slices = s_gate_syncs = s_gate_kicks = 0;
	for (u32 i = 0; i < GR_N; i++)
		s_gate_why[i] = 0;
	sys_probe_stats(secs);
}

int FASTCALL UpdateSystem_handle_event()
{
	StallScope st_(ST_SYS);
	if (!g_stall_on)
		return UpdateINTC();
	const u64 t0 = gettime(), n0 = g_stall_nested;
	const int rv = UpdateINTC();
	s_sp_irq += sp_excl(gettime(), g_stall_nested, t0, n0);
	s_sp_irq_n++;
	return rv;
}

// The plain interpreter emulates no cache; the cached engine uses this to drop
// its decoded blocks (CCR write in ccn.cpp, ELF load in nullDC.cpp).
void sh4_int_resetcache() { sh4_cached_flush(); }

// Engine choice, from nullDC.cfg [ps3] SH4Cached. Latched by Get_Sh4Interpreter,
// which runs once at plugin init, so it cannot change mid-run.
extern "C" int get_sh4_cached_preset();

void Get_Sh4Interpreter(sh4_if* rv)
{
	rv->Run          = get_sh4_cached_preset() ? Sh4_int_Run_Cached : Sh4_int_Run;
	rv->Stop         = Sh4_int_Stop;
	rv->Step         = Sh4_int_Step;
	rv->Skip         = Sh4_int_Skip;
	rv->Reset        = Sh4_int_Reset;
	rv->Init         = Sh4_int_Init;
	rv->Term         = Sh4_int_Term;
	rv->IsCpuRunning = Sh4_int_IsCpuRunning;
	rv->ResetCache   = sh4_int_resetcache;
}
