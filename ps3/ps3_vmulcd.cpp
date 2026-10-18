// Plan section 7, bundle 16 (P37): the VMU screens on the TV (Flycast push_vmu_screen and ImguiVmuTexture).
#include "ps3_vmulcd.h"
#include "ps3_ui.h"
#include "ps3_presets.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define VMU_W 48
#define VMU_H 32
#define VMU_SCREENS 8

// One picture per VMU slot (bus * 2 + slot). The render thread reads while the SH4 thread writes: a torn
// picture lasts one frame.
static u8 s_lcd[VMU_SCREENS][VMU_W * VMU_H];
static volatile u8 s_lcd_on[VMU_SCREENS];

void ps3_vmulcd_push(u32 bus, u32 port, const u8* img)
{
	if (bus >= 4 || port >= 2 || !img)
		return;
	const u32 id = bus * 2 + port;
	memcpy(s_lcd[id], img, sizeof(s_lcd[id]));
	if (!s_lcd_on[id])
		printf("[vmu] %c%u: first LCD picture\n", (char)('A' + bus), port + 1);
	s_lcd_on[id] = 1;
}

void ps3_vmulcd_reset(void)
{
	for (u32 id = 0; id < VMU_SCREENS; id++)
		s_lcd_on[id] = 0;
}

void ps3_vmulcd_unplug(u32 bus)
{
	if (bus < 4)
		s_lcd_on[bus * 2] = s_lcd_on[bus * 2 + 1] = 0;
}

void ps3_vmulcd_init(void)
{
	if (g_vmu_screen_preset)
		ps3ui_init();
}

int ps3_vmulcd_draw(u32 sw, u32 sh)
{
	if (!g_vmu_screen_preset)
		return 0;
	u32 any = 0;
	for (u32 id = 0; id < VMU_SCREENS; id++)
		any |= s_lcd_on[id];
	if (!any || !ps3ui_overlay_begin())
		return 0;

	// Flycast's box of 96x64 for 720 lines, at the right edge (the FPS text has the left one), one under the other.
	float dot = floorf(2.0f * sh / 720.0f + 0.5f);
	if (dot < 1.0f)
		dot = 1.0f;
	const float w = VMU_W * dot, h = VMU_H * dot;
	const float x0 = floorf(sw - sw * 0.025f - w);
	float y0 = floorf(sh * 0.05f);
	for (u32 id = 0; id < VMU_SCREENS; id++)
	{
		if (!s_lcd_on[id])
			continue;
		// Opaque, as the FPS box: a frame that kept its colour still holds the last picture.
		ps3ui_rect(x0 - dot, y0 - dot, w + 2.0f * dot, h + 2.0f * dot, 0xFF000000u);
		ps3ui_rect(x0, y0, w, h, 0xFFFFFFFFu);
		for (u32 y = 0; y < VMU_H; y++)
		{
			const u8* row = &s_lcd[id][vmulcd_src(0, y)];
			// One quad per run of lit dots.
			for (u32 x = 0; x < VMU_W; x++)
			{
				if (row[x])
					continue;
				u32 end = x + 1;
				while (end < VMU_W && !row[end])
					end++;
				ps3ui_rect(x0 + x * dot, y0 + y * dot, (end - x) * dot, dot, 0xFF000000u);
				x = end;
			}
		}
		y0 += h + 4.0f * dot;
	}
	ps3ui_overlay_end();
	return 1;
}
