#pragma once
#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

// RSX display (ps3_video.c). Every call runs on the SH4 thread.
int  ps3_video_init(void);
void ps3_video_term(void);
void ps3_video_size(u32* w, u32* h);
// Returns 0 when no back buffer is free; the caller then skips the frame.
int  ps3_video_begin_frame(void);
void ps3_video_end_frame(void);
// Plan 5.41 (b): flips on the PS3 vsync framehold_vsync() picks for an emulated vblank due at target_tb
// (timebase ticks). Returns the seconds the caller waited for it.
double ps3_video_end_frame_at(u64 target_tb);
// P18: 1 = the depth buffer holds floats (RSX float depth), 0 = fixed-point Z24.
void ps3_video_depth_float(int on);
// Render-to-texture: flush the frame without flipping, then read the drawn rect
// back as native A8R8G8B8 rows (x/y are GL window coordinates, y from the bottom).
void ps3_video_end_frame_noflip(void);
const u32* ps3_video_readback(u32 x, u32 y, u32 w, u32 h);

#ifdef __cplusplus
}
#endif
