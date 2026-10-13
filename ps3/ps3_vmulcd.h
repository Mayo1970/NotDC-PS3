#pragma once
#include "types.h"

// Plan section 7, bundle 16 (P37): the VMU screens on the TV, row "VMU Screen".
// img: 48x32 bytes from the VMU device, 0x00 = a lit dot. Called on the SH4 thread.
void ps3_vmulcd_push(u32 bus, u32 port, const u8* img);
// No VMU has a picture any more (the devices are made again).
void ps3_vmulcd_reset(void);
// The UI's quads must be ready before the render thread starts.
void ps3_vmulcd_init(void);
// Draws over the frame the renderer began. Returns 1 when it drew: the caller's GL state cache is stale then.
int ps3_vmulcd_draw(u32 sw, u32 sh);
// The dot of a screen position. The device's decode mirrors each row; Flycast draws the rows bottom-up, which
// turns the raw picture 180 degrees: a VMU in a pad is upside down.
static inline u32 vmulcd_src(u32 screen_x, u32 screen_y)
{
	return (31 - screen_y) * 48 + screen_x;
}
