// Plan 5.36 PC check (WSL): the real TMU, INTC, SPG, PVR register, AICA DMA and SH4 update code, run with the SYS
// GATE off and on in forked children. One guest timeline must give the same trace in both. Not in the build.
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
extern u32 tmu_regs_CNT[3];
extern u16 tmu_regs_CR[3];
extern u8  TMU_TSTR;
extern u32 CCN_INTEVT;
extern u32 update_cnt;
extern u32 spg_CurrentScanline;
extern s32 spg_ScanlineSh4CycleCounter;
extern s32 aica_dma_delay_cycles;

// ---- settings the code under test reads ----
static int s_accuracy, s_sched, s_render_delay, s_exit_fix = 1, s_dma_fix = 1;
extern "C" int get_accuracy_preset()      { return s_accuracy; }
extern "C" int get_sched_preset()         { return s_sched; }
extern "C" int get_render_delay_preset()  { return s_render_delay; }
extern "C" int get_exit_fix_preset()      { return s_exit_fix; }
extern "C" int get_dma_fix_preset()       { return s_dma_fix; }
extern "C" int get_speed_limiter_preset() { return 0; }
extern "C" int get_x_scaler_preset()      { return 1; }
extern "C" int get_sh4_cached_preset()    { return 0; }
extern "C" int get_sh4_clock_preset()     { return 200; }

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

static void holly_raise(HollyInterruptID id)
{
	tr("holly %d", (int)id);
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

// One guest action between two slice ends. Every draw happens in both modes, so the timelines stay in step.
static void guest_action(int& isr_wait)
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
		reg_write(TMU, k_tcnt[ch], (x & 1) ? (y & 0x3FF) : (y << 8), 4);
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

// The JIT main loop's slice end: every slice with the gate off, only at the due count with it on.
static void slice_end(bool gate, int& isr_wait)
{
	g_slice++;
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

static std::string run(const Scen& sc, bool gate)
{
	g_trace.clear();
	g_slice = 0;
	g_seed = 0x9E3779B97F4A7C15ull ^ ((u64)sc.seed * 0x100000001B3ull);
	s_accuracy = sc.accuracy;
	s_sched = sc.sched;
	s_render_delay = sc.render_delay;
	s_exit_fix = sc.exit_fix;
	Sh4_ApplyAccuracyPreset();
	if (gate)
		sys_gate_install();
	int isr_wait = 0;
	for (u32 i = 0; i < sc.slices; i++)
	{
		const u32 acts = rnd(4);
		for (u32 k = 0; k < acts; k++)
			guest_action(isr_wait);
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
	return g_trace;
}

// Each mode in its own child, from the same start state.
static std::string run_forked(const Scen& sc, bool gate)
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
	if (!WIFEXITED(st) || WEXITSTATUS(st))
		return "CHILD FAILED\n";
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
	boot();
	u32 fails = 0, runs = 0;
	size_t lines = 0;
	for (u32 s = 1; s <= seeds; s++)
	{
		Scen sc = { s, slices, s % 3, (s % 11) == 5, (s % 4) == 1, (s % 13) == 7 ? 3u : 1u, 0 };
		const std::string a = run_forked(sc, false);
		const std::string b = run_forked(sc, true);
		if (getenv("DUMP") && s == (u32)atoi(getenv("DUMP")))
			fputs(a.c_str(), stdout);
		runs++;
		lines += (size_t)std::count(a.begin(), a.end(), '\n');
		if (a != b)
		{
			if (fails < 4)
			{
				size_t i = 0;
				while (i < a.size() && i < b.size() && a[i] == b[i])
					i++;
				size_t ls = a.rfind('\n', i);
				ls = ls == std::string::npos ? 0 : ls + 1;
				printf("FAIL seed %u (acc %u sched %u rdelay %u exit %u): off  %s\n", s, sc.accuracy, sc.sched,
				       sc.render_delay, sc.exit_fix, a.substr(ls, a.find('\n', ls) - ls).c_str());
				printf("  %*s on   %s\n", 33, "", b.substr(ls, b.find('\n', ls) - ls).c_str());
			}
			fails++;
		}
	}
	printf("%s: %u of %u timelines equal, %zu trace lines\n", fails ? "FAIL" : "PASS", runs - fails, runs, lines);
	return fails ? 1 : 0;
}
