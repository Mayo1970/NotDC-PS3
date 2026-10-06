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

// SCHED preset accessor (ps3_presets.cpp). When on, migrated completion/IRQ events
// fire through sh4_sched (deadline-ordered) instead of at their own cascade tier.
extern "C" int get_sched_preset();
#include <time.h>
#include <float.h>

#define GetN(op) (((op) >> 8) & 0xf)
#define GetM(op) (((op) >> 4) & 0xf)

// Volatile: may be cleared from another thread.
volatile bool sh4_int_bCpuRun = false;

// fsca table: [0..0xFFFF] full circle of sin, [0x10000..0x13FFF] wrap copy so cos needs no branch.
f32 sin_table[0x10000 + 0x4000];

static s32 s_timeslice     = SH4_TIMESLICE_ACCURATE;
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

s32 sh4_GetCpuRatio()
{
	return s_cpu_ratio;
}

void Sh4_int_Run()
{
	// Latch timing parameters for this run session
	ApplyAccuracyPreset();

	sh4_int_bCpuRun = true;

	s32 l = s_timeslice;

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

		l += s_timeslice;

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
}

void FASTCALL SlowUpdate()
{
	UpdateGDRom();
	gdrom_spinup_tick(s_timeslice * s_slow_period);

	if (!(update_cnt & (s_vslow_period - 1)))
		VerySlowUpdate();
}

void FASTCALL MediumUpdate()
{
	UpdateAica(s_timeslice * s_medium_period);
	UpdateArm(s_timeslice * s_medium_period);  // ARM7 tick — same cycle count, arm_aica.cpp divides by arm_sh4_bias (8)
	UpdateDMA();

	if (!(update_cnt & (s_slow_period - 1)))
		SlowUpdate();
}

// One timeslice of the update without the interrupt check. Touches no SH4 GPRs.
static inline void sys_slice()
{
	if (!(update_cnt & (s_medium_period - 1)))
		MediumUpdate();

	update_cnt++;

	UpdateTMU(s_timeslice);
	UpdatePvr(s_timeslice);
	UpdateAicaDma(s_timeslice);     // deferred AICA G2-DMA completion IRQ (DMA_FIX)

	// SCHED: fire due completions before the interrupt check so their IRQs are seen this timeslice.
	if (get_sched_preset())
		sh4_sched_tick(s_timeslice);
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
	s32 t = tmu_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_TMU; }
	t = spg_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_SPG; }
	t = aica_dma_slices_to_event(ts, n);
	if (t < n) { n = t; why = GR_DMA; }
	s_gate_plan_why = why;
	return n;
}

// The slices the gate skipped and this one, then the next plan. Same return as _no_event().
int FASTCALL UpdateSystem_gate()
{
	StallScope st_(ST_SYS);
	CCALL(CC_TIMESLICE);
	s32 k = Sh4cntx.sys_elapsed;
	s_gate_runs++;
	s_gate_slices += k;
	s_gate_why[k < s_gate_plan ? GR_KICK : s_gate_plan_why]++;
	Sh4cntx.sys_elapsed = 0;
	while (k-- > 0)
		sys_slice();
	s_gate_plan = sys_plan();
	Sh4cntx.sys_due = s_gate_plan;
	return UpdateINTC_pending() | (int)(g_jit_exit_on && !sh4_int_bCpuRun);
}

// None of the skipped slices holds an event, so running them before a write changes nothing the guest saw.
void sys_gate_sync_slow()
{
	s32 k = Sh4cntx.sys_elapsed;
	if (k <= 0)
		return;
	StallScope st_(ST_SYS);
	s_gate_syncs++;
	s_gate_slices += k;
	Sh4cntx.sys_elapsed = 0;
	Sh4cntx.sys_due -= k;
	s_gate_plan -= k;
	while (k-- > 0)
		sys_slice();
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
	tmu_gate_install();
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
	printf("%s\n", buf);
	s_gate_runs = s_gate_slices = s_gate_syncs = s_gate_kicks = 0;
	for (u32 i = 0; i < GR_N; i++)
		s_gate_why[i] = 0;
}

int FASTCALL UpdateSystem_handle_event()
{
	StallScope st_(ST_SYS);
	return UpdateINTC();
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
