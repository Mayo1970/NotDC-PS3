// Plans 5.36 and 5.42 PC check (WSL): the real TMU, INTC, SPG, PVR register, AICA DMA and SH4 update code in forked
// children: SYS GATE off, on, and on with SYS GATE BATCH. One guest timeline, the same trace in all three. Not built.
#include "types.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/sys_gate.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/tmu.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/mem/sb.h"
#include "dc/aica/aica_if.h"
#include "plugs/drkPvr/regs.h"
#include "plugs/drkPvr/spg.h"
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#include <algorithm>
#include <string>
#include <vector>

// The code under test keeps these in its own files, without a header that names them.
u32 FASTCALL libPvr_ReadReg(u32 addr, u32 size);
void FASTCALL libPvr_WriteReg(u32 addr, u32 data, u32 size);
void Write_SB_ADST(u32 data);
void aica_sb_Init();
extern u32 tmu_regs_CNT[3];
extern u16 tmu_regs_CR[3];
extern u8  TMU_TSTR;
extern u32 CCN_INTEVT;
extern u32 update_cnt;
extern u32 spg_CurrentScanline;
extern s32 spg_ScanlineSh4CycleCounter;
extern s32 aica_dma_delay_cycles;
extern u32 g_gate_batches, g_gate_batch_slices, g_gate_batch_miss;

// ---- settings the code under test reads ----
static int s_accuracy, s_sched, s_render_delay, s_exit_fix = 1, s_dma_fix = 1, s_batch, s_clock = 200;
extern "C" int get_sys_gate_batch_preset() { return s_batch; }
extern "C" int get_accuracy_preset()      { return s_accuracy; }
extern "C" int get_sched_preset()         { return s_sched; }
extern "C" int get_render_delay_preset()  { return s_render_delay; }
extern "C" int get_exit_fix_preset()      { return s_exit_fix; }
extern "C" int get_dma_fix_preset()       { return s_dma_fix; }
extern "C" int get_speed_limiter_preset() { return 0; }
extern "C" int get_x_scaler_preset()      { return 1; }
extern "C" int get_sh4_cached_preset()    { return 0; }
extern "C" int get_sh4_clock_preset()     { return s_clock; }   // misc/clock_check moves it
// Bundle 7 rows: TMUEFF (with CLOCK=150), FINE, FIELD and HBLANK in the environment turn one on;
// misc/timebase_check moves them.
static int s_tmu_eff, s_fine, s_field, s_hblank;
extern "C" int get_tmu_clock_preset()     { return s_tmu_eff; }
extern "C" int get_spg_field_bit_preset() { return s_field; }
extern "C" int get_hblank_irq_preset()    { return s_hblank; }

// ---- the trace: what the guest can see, stamped with the slice end it follows ----
static u32 g_slice;
static std::string g_trace;
static void tr(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void tr(const char* fmt, ...)
{
	char b[160];
	va_list ap;
	va_start(ap, fmt);
	int n = snprintf(b, sizeof(b), "%u ", g_slice);
	vsnprintf(b + n, sizeof(b) - n, fmt, ap);
	va_end(ap);
	g_trace += b;
	g_trace += '\n';
}

// ---- stubs: the parts of the emulator the update calls, reduced to trace entries ----
void libAICA_Update(u32 c) { tr("aica %u", c); }
void libARM_Update(u32 c)  { tr("arm %u", c); }
void UpdateDMA()           { tr("dma"); }
void UpdateGDRom()         { tr("gdrom"); }
void gdrom_spinup_tick(u32 c) { (void)c; }
void maple_Update(u32 c)   { tr("maple %u", c); }
double os_GetSeconds() { return 0.0; }

// Per normal interrupt bit: the raises and the line of the last one, for misc/timebase_check.
static u32 g_holly_n[32], g_holly_line[32];
static void holly_raise(HollyInterruptID id)
{
	tr("holly %d", (int)id);
	if ((id >> 8) == 0)
	{
		g_holly_n[id & 31]++;
		g_holly_line[id & 31] = spg_CurrentScanline;
	}
	InterruptPend(sh4_IRL_9, 1);
}
pvr_init_params PVRPARAMS = { holly_raise, 0 };
void asic_RaiseInterrupt(HollyInterruptID id) { holly_raise(id); }

// StartRender's countdown part, with its plan 5.36 hooks (ps3_rend.cpp does not build on the PC).
void StartRender()
{
	sys_sync();
	if (get_render_delay_preset())
	{
		render_isp_pending_cycles = 800000;
		render_tsp_pending_cycles = 850000;
		render_vd_pending_cycles  = 900000;
		render_end_pending_cycles = 0;
	}
	else
		render_end_pending_cycles = 50000;
	sys_kick();
}

// SR writes go through UpdateSR, which runs SRdecode (sh4_registers.cpp does more, none of it seen here).
bool UpdateSR()
{
	return SRdecode();
}

#include "stubs.inc"

// ---- the guest: register accesses through the same table logic as sh4_internal_reg.cpp ----
static u32 reg_read(Array<RegisterStruct>& t, u32 addr, u32 size)
{
	RegisterStruct& e = t[(addr & 0xFF) >> 2];
	if (e.flags & REG_READ_DATA)
		return size == 4 ? *e.data32 : size == 2 ? *e.data16 : *e.data8;
	return e.readFunction ? e.readFunction() : 0;
}

static void reg_write(Array<RegisterStruct>& t, u32 addr, u32 data, u32 size)
{
	RegisterStruct& e = t[(addr & 0xFF) >> 2];
	if (e.flags & REG_WRITE_DATA)
	{
		if (size == 4) *e.data32 = data;
		else if (size == 2) *e.data16 = (u16)data;
		else *e.data8 = (u8)data;
		return;
	}
	if (e.writeFunction)
		e.writeFunction(data);
}

static u64 g_seed;
static u32 rnd(u32 n)
{
	g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17;
	return (u32)((g_seed >> 11) % n);
}

static const u32 k_tcnt[3] = { TMU_TCNT0_addr, TMU_TCNT1_addr, TMU_TCNT2_addr };
static const u32 k_tcor[3] = { TMU_TCOR0_addr, TMU_TCOR1_addr, TMU_TCOR2_addr };
static const u32 k_tcr[3]  = { TMU_TCR0_addr, TMU_TCR1_addr, TMU_TCR2_addr };
static const u32 PVR = 0x005F8000;

struct Scen
{
	u32 seed, slices, accuracy, sched, render_delay, exit_fix, gate;
};

// Plan 5.42: k slices in one sync, past the plan too, so events land anywhere in the run the batch tries.
static void forced_slices(bool gate, u32 k)
{
	g_slice += k;
	if (gate)
	{
		Sh4cntx.sys_elapsed += (s32)k;
		sys_sync();
	}
	else
		for (u32 n = 0; n < k; n++)
			UpdateSystem_no_event();
}

// One guest action between two slice ends. Every draw happens in every mode, so the timelines stay in step.
static void guest_action(bool gate, int& isr_wait)
{
	const u32 a = rnd(40), x = rnd(0x10000), y = rnd(0x10000), ch = rnd(3);
	switch (a)
	{
	case 0: case 1: case 2:
		tr("tcnt%u=%08X", ch, reg_read(TMU, k_tcnt[ch], 4));
		break;
	case 3: case 4: case 5:
		tr("spg_status=%08X", libPvr_ReadReg(PVR + SPG_STATUS_addr, 4));
		break;
	case 6:
		reg_write(TMU, TMU_TSTR_addr, x & 7, 1);
		break;
	case 7:
		// A third of the writes put the underflow a few steps away, inside the plan made before them.
		reg_write(TMU, k_tcnt[ch], (x % 3) == 0 ? (y & 7) : (x & 1) ? (y & 0x3FF) : (y << 8), 4);
		break;
	case 8:
		reg_write(TMU, k_tcor[ch], (x & 1) ? (y & 0x3FF) + 1 : (y << 6), 4);
		break;
	case 9: case 10:
		// prescaler 0..7 (5..7 keep the old rate), UNIE on or off, UNF cleared
		reg_write(TMU, k_tcr[ch], (x % 8) | ((y & 1) ? 0x20 : 0), 2);
		break;
	case 11:
		libPvr_WriteReg(PVR + SPG_VBLANK_INT_addr, (x % 300) | ((y % 300) << 16), 4);
		break;
	case 12:
	{
		// vbstart in the low half, vbend in the high half of SPG_VBLANK
		libPvr_WriteReg(PVR + SPG_VBLANK_addr, (x % 280) | ((y % 280) << 16), 4);
		break;
	}
	case 13:
		if ((x & 7) == 0)
			libPvr_WriteReg(PVR + SPG_LOAD_addr, (200 + x % 400) | ((200 + y % 400) << 16), 4);
		break;
	case 14:
		if ((x & 7) == 0)
			libPvr_WriteReg(PVR + SPG_CONTROL_addr, (y & 0x10) ? 0x150 : 0x100, 4);
		break;
	case 15:
		if ((x & 7) == 0)
			libPvr_WriteReg(PVR + FB_R_CTRL_addr, (y & 1) ? 0x800000 : 0, 4);
		break;
	case 16:
		if ((x & 3) == 0)
			libPvr_WriteReg(PVR + STARTRENDER_addr, 1, 4);
		break;
	case 17:
		if ((x & 3) == 0)
			spg_SchedListEndIrq(y % 5);
		break;
	case 18:
		if ((x & 3) == 0)
		{
			SB_ADEN = 1;
			SB_ADDIR = 0;
			SB_ADLEN = 32 + (y & 0x3FFC);
			Write_SB_ADST(1);
		}
		break;
	case 19:
		// ldc to SR: a new interrupt mask
		sr.IMASK = x & 15;
		UpdateSR();
		break;
	case 23:
		if ((x & 3) == 0)
			forced_slices(gate, 1 + y % 8);
		break;
	case 24:
		// Plan 5.42: a countdown that ends exactly on a slice end, then that many slices, or one more, in one sync.
		if ((x & 3) == 0)
		{
			const u32 k = 2 + y % 6;
			sys_sync();
			if (x & 4)
				render_end_pending_cycles = (s32)k * sh4_GetTimeslice();
			else
				aica_dma_delay_cycles = (s32)k * sh4_GetTimeslice();
			sys_kick();
			forced_slices(gate, k + ((x >> 3) & 1));
		}
		break;
	case 25:
		// Plan 5.42: a list IRQ armed (Render Delay), then slices in one sync while it is pending.
		if ((x & 3) == 0)
		{
			spg_SchedListEndIrq(y % 5);
			forced_slices(gate, 2 + (y >> 4) % 4);
		}
		break;
	case 26:
		// Bundle 7: the HBlank compare line and mode 0..3 (HBlank IRQ MODES reads them)
		libPvr_WriteReg(PVR + SPG_HBLANK_INT_addr, (x % 300) | ((y & 3) << 12), 4);
		break;
	case 21: case 22:
		// a sync with no write after it: the plan made before it must still hold
		sys_sync();
		break;
	case 20:
		// sleep: the interpreter's loop of UpdateSystem() calls, one slice end each, until an interrupt
		if ((x & 3) == 0 && !sr.BL)
			for (u32 n = 0; n < 1 + (y & 7); n++)
			{
				g_slice++;
				if (UpdateSystem())
				{
					tr("sleep intr %03X", CCN_INTEVT);
					isr_wait = 1 + (int)(y % 5);
					break;
				}
			}
		break;
	default:
		break;
	}
	// The interrupt handler: drops the sources a few actions after the dispatch, then rte.
	if (isr_wait > 0 && --isr_wait == 0)
	{
		InterruptPend(sh4_IRL_9, 0);
		for (u32 c = 0; c < 3; c++)
			reg_write(TMU, k_tcr[c], tmu_regs_CR[c] & ~0x100u, 2);
		sr.BL = 0;
		UpdateSR();
		tr("rte");
	}
}

// Plan 5.42: the closed-form TMU plan against the slice loop, on the live state. Neither changes it.
static u32 g_plan_checks, g_plan_bad;
static void plan_check()
{
	const s32 ts = sh4_GetTimeslice();
	for (s32 max = 1; max <= 9; max++)
	{
		g_plan_checks++;
		g_plan_bad += tmu_slices_to_event_fast(ts, max) != tmu_slices_to_event(ts, max);
	}
}

// TMU Fine TCNT (FINE): the SH4 cycles run inside the slice, as the JIT stores them; a slice end has used the
// slice up, plus a block's overshoot. The draws are the same in every mode.
static s32 g_in;
static void fine_step(bool end)
{
	if (!s_fine)
		return;
	const s32 cs = sh4_GetCpuTimeslice();
	if (end)
		g_in = (s32)rnd(96);
	else
		g_in += (s32)rnd((u32)cs / 3);
	Sh4cntx.jit_cycles = cs - g_in;
}

// The JIT main loop's slice end: every slice with the gate off, only at the due count with it on.
static void slice_end(bool gate, int& isr_wait)
{
	g_slice++;
	fine_step(true);
	plan_check();
	int pend;
	if (!gate)
		pend = UpdateSystem_no_event();
	else
	{
		Sh4cntx.sys_elapsed++;
		if (Sh4cntx.sys_elapsed < Sh4cntx.sys_due)
			return;
		pend = UpdateSystem_gate();
	}
	if (pend && UpdateSystem_handle_event())
	{
		tr("intr %03X", CCN_INTEVT);
		isr_wait = 1 + rnd(8);
	}
}

// mode: 0 gate off, 1 gate on, 2 gate on with the batch.
static std::string run(const Scen& sc, int mode)
{
	const bool gate = mode != 0;
	s_batch = mode == 2;
	g_trace.clear();
	g_slice = 0;
	g_seed = 0x9E3779B97F4A7C15ull ^ ((u64)sc.seed * 0x100000001B3ull);
	s_accuracy = sc.accuracy;
	s_sched = sc.sched;
	s_render_delay = sc.render_delay;
	s_exit_fix = sc.exit_fix;
	// STALL=1: the [sys] probe's stamps run too; the traces must not change.
	g_stall_on = getenv("STALL") != 0;
	Sh4_ApplyAccuracyPreset();
	if (gate)
		sys_gate_install();
	if (s_fine)
		sys_slice_part_install();
	g_in = 0;
	int isr_wait = 0;
	for (u32 i = 0; i < sc.slices; i++)
	{
		const u32 acts = rnd(4);
		for (u32 k = 0; k < acts; k++)
		{
			fine_step(false);
			guest_action(gate, isr_wait);
		}
		slice_end(gate, isr_wait);
	}
	// The end state, with the skipped slices run.
	sys_sync();
	if (gate && getenv("STATS"))
	{
		sys_gate_stats(1.0);
		fflush(stdout);
	}
	tr("end cnt %08X %08X %08X cr %04X %04X %04X tstr %X upd %u spg %08X line %u ctr %d aicadma %d",
	   tmu_regs_CNT[0], tmu_regs_CNT[1], tmu_regs_CNT[2], tmu_regs_CR[0], tmu_regs_CR[1], tmu_regs_CR[2],
	   TMU_TSTR, update_cnt, SPG_STATUS.full, spg_CurrentScanline, spg_ScanlineSh4CycleCounter,
	   aica_dma_delay_cycles);
	// After the trace, for the parent only: the batch counts and the plan checks.
	char b[128];
	snprintf(b, sizeof(b), "\x01%u %u %u %u %u\n", g_gate_batches, g_gate_batch_slices, g_gate_batch_miss, g_plan_checks,
	         g_plan_bad);
	return g_trace + b;
}

// Each mode in its own child, from the same start state.
struct Side { u64 batches, slices, miss, plan_checks, plan_bad; };
static Side g_side[3];

static std::string run_forked(const Scen& sc, int gate)
{
	int fd[2];
	if (pipe(fd))
		exit(2);
	fflush(stdout);
	pid_t p = fork();
	if (p == 0)
	{
		close(fd[0]);
		// The code under test prints (TMU modes 5-7); only the trace goes back.
		if (!getenv("STATS"))
			freopen("/dev/null", "w", stdout);
		std::string t = run(sc, gate);
		size_t off = 0;
		while (off < t.size())
		{
			ssize_t w = write(fd[1], t.data() + off, t.size() - off);
			if (w <= 0)
				_exit(3);
			off += (size_t)w;
		}
		_exit(0);
	}
	close(fd[1]);
	std::string out;
	char buf[65536];
	ssize_t n;
	while ((n = read(fd[0], buf, sizeof(buf))) > 0)
		out.append(buf, (size_t)n);
	close(fd[0]);
	int st = 0;
	waitpid(p, &st, 0);
	const size_t cut = out.rfind('\x01');
	if (!WIFEXITED(st) || WEXITSTATUS(st) || cut == std::string::npos)
		return "CHILD FAILED\n";
	u32 v[5] = { 0, 0, 0, 0, 0 };
	sscanf(out.c_str() + cut + 1, "%u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &v[4]);
	Side& sd = g_side[gate];
	sd.batches += v[0]; sd.slices += v[1]; sd.miss += v[2]; sd.plan_checks += v[3]; sd.plan_bad += v[4];
	out.resize(cut);
	return out;
}

static void boot()
{
	Regs_Init();
	Regs_Reset(false);
	tmu_Init();
	intc_Init();
	tmu_Reset(false);
	intc_Reset(false);
	spg_Reset(false);
	aica_sb_Init();   // registers the scheduler's G2-DMA end, for the Scheduler seeds
	sr.SetFull(0x40000000u);   // MD=1, BL=0, IMASK=0
	UpdateSR();
	// The first three TMU channels running, as the BIOS leaves them, so the plan has timers to watch.
	reg_write(TMU, TMU_TCR0_addr, 0x20, 2);
	reg_write(TMU, TMU_TCOR0_addr, 4000, 4);
	reg_write(TMU, TMU_TCNT0_addr, 4000, 4);
	reg_write(TMU, TMU_TSTR_addr, 1, 1);
}

int main(int argc, char** argv)
{
	const u32 seeds = argc > 1 ? (u32)atoi(argv[1]) : 300;
	const u32 slices = argc > 2 ? (u32)atoi(argv[2]) : 4000;
	s_tmu_eff = getenv("TMUEFF") != 0;
	s_fine = getenv("FINE") != 0;
	s_field = getenv("FIELD") != 0;
	s_hblank = getenv("HBLANK") != 0;
	if (getenv("CLOCK"))
		s_clock = atoi(getenv("CLOCK"));
	boot();
	u32 fails = 0, runs = 0;
	size_t lines = 0;
	for (u32 s = 1; s <= seeds; s++)
	{
		Scen sc = { s, slices, s % 3, (s % 11) == 5, (s % 4) == 1, (s % 13) == 7 ? 3u : 1u, 0 };
		const std::string a = run_forked(sc, 0);
		const std::string b = run_forked(sc, 1);
		const std::string c = run_forked(sc, 2);
		if (getenv("DUMP") && s == (u32)atoi(getenv("DUMP")))
			fputs(a.c_str(), stdout);
		runs++;
		lines += (size_t)std::count(a.begin(), a.end(), '\n');
		for (int mode = 1; mode <= 2; mode++)
		{
			const std::string& o = mode == 1 ? b : c;
			if (a == o)
				continue;
			if (fails < 4)
			{
				size_t i = 0;
				while (i < a.size() && i < o.size() && a[i] == o[i])
					i++;
				size_t ls = a.rfind('\n', i);
				ls = ls == std::string::npos ? 0 : ls + 1;
				printf("FAIL seed %u (acc %u sched %u rdelay %u exit %u): off  %s\n", s, sc.accuracy, sc.sched,
				       sc.render_delay, sc.exit_fix, a.substr(ls, a.find('\n', ls) - ls).c_str());
				printf("  %*s %s %s\n", 33, "", mode == 1 ? "on  " : "batch", o.substr(ls, o.find('\n', ls) - ls).c_str());
			}
			fails++;
			break;
		}
	}
	// The batch must have run, in the batch mode only, and the two TMU plans must agree in every mode.
	u64 plan_checks = 0, plan_bad = 0;
	for (int i = 0; i < 3; i++)
	{
		plan_checks += g_side[i].plan_checks;
		plan_bad += g_side[i].plan_bad;
	}
	const Side& bt = g_side[2];
	const bool cover = bt.batches > runs && !g_side[0].batches && !g_side[1].batches;
	printf("  batch: %llu runs, %llu slices, %llu not quiet; TMU plan: %llu checks, %llu differ\n",
	       (unsigned long long)bt.batches, (unsigned long long)bt.slices, (unsigned long long)bt.miss,
	       (unsigned long long)plan_checks, (unsigned long long)plan_bad);
	if (!cover)
		printf("FAIL: the batch did not run as planned\n");
	if (plan_bad)
		printf("FAIL: the closed-form TMU plan differs from the slice loop\n");
	const bool bad = fails || !cover || plan_bad;
	printf("%s: %u of %u timelines equal in the 3 modes, %zu trace lines\n", bad ? "FAIL" : "PASS", runs - fails,
	       runs, lines);
	return bad ? 1 : 0;
}
