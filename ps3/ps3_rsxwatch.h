#pragma once
// RSX stall log (the Sonic Adventure 2 freeze of 2026-10-18): ps3_video.c calls these; one log dump when no flip
// completes for 1 s, one more at 5 s, a line when flips come back. Nothing is logged while flips complete.
#include <rsx/gcm_sys.h>

#ifdef __cplusplus
extern "C" {
#endif

// Before ps3gl_end_frame(): the frame's ps3gl counts and its Native Size scene (0 x 0 = none).
void ps3_rsxwatch_frame(u32 ns_w, u32 ns_h);
// After the flip's flush: the frame is in the ring with the RSX put offset of its last command.
void ps3_rsxwatch_flip(u32 flip);
// ps3_video_begin_frame() found no free buffer.
void ps3_rsxwatch_skip(gcmContextData* ctx, u32 queued, u32 done);

#ifdef __cplusplus
}
#endif
