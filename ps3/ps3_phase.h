#pragma once

// Diagnostic: where the SH4 thread is when it stops. A freeze shows up in the
// monitor line as a frozen pc and vbl, which only says "blocked in host code".
// Each host entry point the SH4 thread can block in stamps a phase here, and
// the [hb] line prints it, so the stuck call names itself. `seq` separates a
// thread stuck in one call from one still looping through phases.

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	PH_GUEST = 0,   // running SH4 code
	PH_DECODE,      // cached interpreter, decoding a block
	PH_RENDER,      // StartRender, before the draw
	PH_BEGINFRAME,  // ps3_video_begin_frame
	PH_DRAW,        // draw_scene: the per-strip GL submission
	PH_ENDFRAME,    // ps3_video_end_frame: flush + queue the flip
	PH_RTT,         // render to texture
	PH_READBACK,    // ps3_video_readback: rsxFinish
	PH_TEX,         // texture decode / upload
	PH_VBLANK,      // VBlank present path
	PH_PHASE_COUNT
};

extern volatile unsigned g_sh4_phase;
extern volatile unsigned g_sh4_phase_seq;
extern const char* const g_sh4_phase_name[PH_PHASE_COUNT];

#define SH4_PHASE(p) do { g_sh4_phase = (p); g_sh4_phase_seq++; } while (0)

#ifdef __cplusplus
}
#endif
