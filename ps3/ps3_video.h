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
// Render-to-texture: flush the frame without flipping, then read the drawn rect back as native A8R8G8B8 rows
// (GL window x/y, y from the bottom). fixed (P75): 1 = the read-back that returns; 0 = PSL1GHT's, which does not.
void ps3_video_end_frame_noflip(void);
const u32* ps3_video_readback(u32 x, u32 y, u32 w, u32 h, int fixed);
// P51: the same rows of the frame in progress, before its end; the frame goes on after the call.
const u32* ps3_video_grab(u32 x, u32 y, u32 w, u32 h);
// P76 Draw Retry: waits up to ms for a free back buffer; 1 = ps3_video_begin_frame() will take one.
int  ps3_video_wait_free(u32 ms);
// P60: starts a frame on the RTT surface, which is never shown; 0 = there is none.
int  ps3_video_begin_rtt(void);
// P60 Present Rule: ends a frame that stays in the back buffer; flips it later (0 = none waits); 1 = one waits.
void ps3_video_end_frame_hold(void);
int  ps3_video_flip_held(void);
int  ps3_video_held(void);
// P60 Keep Last Frame: copies the last flipped frame into the frame in progress (0 = none); the menu's frames
// are forgotten, so no carry takes one.
int  ps3_video_carry(void);
void ps3_video_forget_frame(void);
// Native Size: the next draws go to w x h of the RSX colour memory mem (pitch bytes a row); scene = 1 for the scene's
// pass, whose old pixels a carry then keeps. 0 = no memory. ps3_video_target_frame() goes back to the display buffer.
int  ps3_video_target(void* mem, u32 pitch, u32 w, u32 h, int scene);
void ps3_video_target_frame(void);

#ifdef __cplusplus
}
#endif
