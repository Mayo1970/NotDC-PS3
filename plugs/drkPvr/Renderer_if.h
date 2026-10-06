#pragma once
#include "drkPvr.h"
#include "ta.h"
//#include "TexCache.h"

extern u32 VertexCount;
extern u32 StripCount;
extern u32 FrameCount;

// Render / TA wall-time split. RenderTicks brackets DoRender(); TaTicks brackets each TA DMA batch and
// takes the SQ blocks from the 5.17 samples (ta.cpp), so an SQ block pays no mftb of its own.
extern u64 RenderTicks;
extern u64 TaTicks;
extern u32 TaCalls;

// Sound split: armUpdateARM (plugs/vbaARM/arm_aica.cpp) runs the ARM7 and the AICA synthesis, ~3 mftb per call.
extern u64 ArmTicks;      // arm_Run() only
extern u64 AicaTicks;     // the libAICA_TimeStep() loop, SndWaitTicks included
extern u64 SndWaitTicks;  // the audio pacing sleep: idle time, so the stats line takes it out of aica

// Inside aica, printed as vox:N%(V); aica% minus vox% is the fixed per-sample work. 2 mftb per sample.
extern u64 AicaVoxTicks;     // AICA_Sample()'s channel loop only
extern u32 AicaVoiceSum;     // voices stepped, summed over samples (aica_fast loop only)
extern u32 AicaSampleCount;  // samples generated, to average AicaVoiceSum

// PS3 reads the mftb timebase through ps3/include/ps3_timebase.h; without it every stats-line
// percentage reads 0.0%.
#if HOST_OS == OS_PS3
#include "ps3_timebase.h"
#define PERF_TICKS() gettime()
#define PERF_TICKS_US(t) ((double)ticks_to_microsecs(t))
#else
#define PERF_TICKS() ((u64)0)
#define PERF_TICKS_US(t) (0.0)
#endif

#include "rend.h"

