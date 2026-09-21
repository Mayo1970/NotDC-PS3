#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"
#include <math.h>

// Wall-time split for the stats line (rationale in Renderer_if.h, not included to keep the renderer
// out of the ARM core).
extern u64 ArmTicks;
extern u64 AicaTicks;

#if HOST_OS == OS_WII || HOST_OS == OS_PS3
// config.h poisons BIG_ENDIAN/LITTLE_ENDIAN; libogc defines them. Same dance
// as arm7.cpp.
#ifdef BIG_ENDIAN
#  undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#  undef LITTLE_ENDIAN
#endif
#include <ogc/lwp_watchdog.h>   // gettime() - inline mftb read, no call
#define ARM_PERF_TICKS() gettime()
#else
#define ARM_PERF_TICKS() ((u64)0)
#endif

// nullAICA sample step (declared here to keep its header out of the ARM core): one 44.1 kHz stereo
// sample per call, pushed to the audio sink.
extern void libAICA_TimeStep();

// Per-game ARM7 speed (game_presets arm7_speed): bias = arm_sh4_bias << stage (0 ~10 MHz, 1 ~5, 2 ~2.5);
// the AICA driver over-polls, so this saves host CPU, but stage 2 broke audio timing on hardware.
extern "C" int get_arm7_speed_preset();

// ARM7 slice batching (arm7_batch): run the ARM once per N calls with all N calls' cycles; same clock and
// AICA stepping, fewer SH4<->ARM switches. 1 = every call (default).
extern "C" int get_arm7_batch_preset();

// SH4 cycles per AICA sample (SH4_CLOCK_EFF / 44100, ~4535 at 200 MHz) with a fractional accumulator;
// cached per underclock preset so the hot loop stays division-free and sound follows the video clock.
static u32 aica_sample_cycles = SH4_CLOCK / 44100;
static int aica_clock_mhz     = 200;

// Runs ARM7 and AICA in lockstep from the SH4 timeslice (like devcast SoundCPU::Update): step AICA per
// accumulated sample, then run the ARM for the slice.
void FASTCALL armUpdateARM(u32 Cycles)
{
	static u32 aica_cycle_acc = 0;
	static u32 arm_batch_acc = 0;     // SH4 cycles owed to the ARM (arm7_batch > 1)
	static u32 arm_batch_calls = 0;

	// ARM7 slice (cycles scaled by arm_sh4_bias and the arm7_speed stage); the clock reads bracket only
	// the run, so deferred calls (arm7_batch > 1) cost nothing to measure.
	{
		int stage = get_arm7_speed_preset();
		if (stage < 0) stage = 0;
		if (stage > 3) stage = 3;
		int batch = get_arm7_batch_preset();
		u32 ticks;
		if (batch <= 1)
		{
			ticks = (Cycles / arm_sh4_bias) >> stage;
			arm_batch_acc = 0;
			arm_batch_calls = 0;
		}
		else
		{
			if (batch > 16) batch = 16;
			arm_batch_acc += Cycles;
			if (++arm_batch_calls < (u32)batch)
				goto arm_deferred;                  // this call only banks cycles
			ticks = (arm_batch_acc / arm_sh4_bias) >> stage;
			arm_batch_acc = 0;
			arm_batch_calls = 0;
		}
		{
			const u64 t_arm = ARM_PERF_TICKS();
			arm_Run(ticks);
			ArmTicks += ARM_PERF_TICKS() - t_arm;
		}
	}
arm_deferred:

	// Refresh the cached samples-per-cycle only when the underclock preset
	// changed (never during gameplay — set at game load / in the menu).
	{
		int mhz = get_sh4_clock_preset();
		if (mhz != aica_clock_mhz)
		{
			aica_clock_mhz     = mhz;
			aica_sample_cycles = (u32)((u64)mhz * 1000000u / 44100u);
		}
	}

	// Advance the AICA sample generator by however many whole samples elapsed
	// during this slice, carrying the remainder for exact long-run timing.
	aica_cycle_acc += Cycles;
	if (aica_cycle_acc >= aica_sample_cycles)
	{
		// Clock reads skipped when no sample ran (about 1 call in 5 in ACCURATE).
		const u64 t_aica = ARM_PERF_TICKS();
		do
		{
			aica_cycle_acc -= aica_sample_cycles;
			libAICA_TimeStep();
		}
		while (aica_cycle_acc >= aica_sample_cycles);

		AicaTicks += ARM_PERF_TICKS() - t_aica;
	}
}
