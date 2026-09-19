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

#ifdef __cplusplus
}
#endif
