#pragma once
// Plan section 7, bundle 10: the DS3 button map and the stick shaping. No PSL1GHT here: misc/pad_check runs it.
#include <math.h>
#include "dc_pad_bits.h"

// The DC control a DS3 button drives: the values of the nullDC.cfg [ps3] Map* keys.
enum
{
	PADMAP_NONE, PADMAP_A, PADMAP_B, PADMAP_X, PADMAP_Y, PADMAP_START,
	PADMAP_UP, PADMAP_DOWN, PADMAP_LEFT, PADMAP_RIGHT, PADMAP_LT, PADMAP_RT,
	PADMAP_COUNT
};

// The DS3 buttons that have a map entry, in the CONTROLS page's order. SELECT opens the in-game menu.
enum
{
	PADBTN_CROSS, PADBTN_CIRCLE, PADBTN_SQUARE, PADBTN_TRIANGLE, PADBTN_START,
	PADBTN_UP, PADBTN_DOWN, PADBTN_LEFT, PADBTN_RIGHT,
	PADBTN_L1, PADBTN_R1, PADBTN_L2, PADBTN_R2, PADBTN_L3, PADBTN_R3,
	PADBTN_COUNT
};

// The fixed map of before bundle 10: L1/R1 are the triggers at full travel, L3/R3 do nothing.
#define PADMAP_DEFAULTS { PADMAP_A, PADMAP_B, PADMAP_X, PADMAP_Y, PADMAP_START, \
	PADMAP_UP, PADMAP_DOWN, PADMAP_LEFT, PADMAP_RIGHT, \
	PADMAP_LT, PADMAP_RT, PADMAP_LT, PADMAP_RT, PADMAP_NONE, PADMAP_NONE }

// ana[b]: 0 = up, else the button's travel (255 for a digital button). Clears kcode bits (active-low) and raises
// the triggers; a map value outside the enum acts as PADMAP_NONE.
static inline void padmap_apply(const int* map, const unsigned char* ana, unsigned short* kcode,
                                unsigned char* lt, unsigned char* rt)
{
	static const unsigned short bit[PADMAP_COUNT] = {
		0, key_CONT_A, key_CONT_B, key_CONT_X, key_CONT_Y, key_CONT_START,
		key_CONT_DPAD_UP, key_CONT_DPAD_DOWN, key_CONT_DPAD_LEFT, key_CONT_DPAD_RIGHT, 0, 0
	};
	for (int b = 0; b < PADBTN_COUNT; b++)
	{
		const unsigned t = (unsigned)map[b];
		if (!ana[b] || t >= PADMAP_COUNT)
			continue;
		*kcode &= (unsigned short)~bit[t];
		if (t == PADMAP_LT && ana[b] > *lt)
			*lt = ana[b];
		if (t == PADMAP_RT && ana[b] > *rt)
			*rt = ana[b];
	}
}

// Stick / D-pad SWAP: the DS3 d-pad gives the DC stick full travel (DC positive y is down), and its own map
// entries no longer act.
static inline void pad_dpad_to_stick(unsigned char* ana, int* x, int* y)
{
	*x = (ana[PADBTN_RIGHT] ? 127 : 0) - (ana[PADBTN_LEFT] ? 128 : 0);
	*y = (ana[PADBTN_DOWN] ? 127 : 0) - (ana[PADBTN_UP] ? 128 : 0);
	ana[PADBTN_UP] = ana[PADBTN_DOWN] = ana[PADBTN_LEFT] = ana[PADBTN_RIGHT] = 0;
}

#define PAD_STICK_DPAD 64

// SWAP: the kcode bits of the DC d-pad that the left stick presses past half travel. x, y: the raw stick minus 128.
static inline unsigned short pad_stick_to_dpad(int x, int y)
{
	return (unsigned short)((x < -PAD_STICK_DPAD ? key_CONT_DPAD_LEFT : 0) | (x > PAD_STICK_DPAD ? key_CONT_DPAD_RIGHT : 0) |
	                        (y < -PAD_STICK_DPAD ? key_CONT_DPAD_UP : 0) | (y > PAD_STICK_DPAD ? key_CONT_DPAD_DOWN : 0));
}

#define PAD_DEADZONE 0.10f

// P84, Flycast gamepad_device.cpp: a radial dead zone of 10% of the travel with a rescale, then maple_devs.h's
// limit of the magnitude to 128. x, y: the raw stick minus 128; the results fit an s8.
static inline void pad_stick_radial(int x, int y, int* ox, int* oy)
{
	const float nx = fabsf((float)x) / 128.0f, ny = fabsf((float)y) / 128.0f;
	const float r2 = nx * nx + ny * ny;
	*ox = *oy = 0;
	if (r2 < PAD_DEADZONE * PAD_DEADZONE || r2 == 0.0f)
		return;
	// Each axis loses its share of the dead radius and is stretched back to the full travel.
	const float k = PAD_DEADZONE / sqrtf(r2);
	int ix = (int)lroundf((nx - nx * k) / (1.0f - nx * k) * 128.0f);
	int iy = (int)lroundf((ny - ny * k) / (1.0f - ny * k) * 128.0f);
	ix = x < 0 ? -ix : ix > 127 ? 127 : ix;
	iy = y < 0 ? -iy : iy > 127 ? 127 : iy;
	float mag = (float)ix * ix + (float)iy * iy;
	if (mag > 128.0f * 128.0f)
	{
		mag = sqrtf(mag) / 128.0f;
		ix = (int)lroundf(ix / mag);
		iy = (int)lroundf(iy / mag);
	}
	*ox = ix;
	*oy = iy;
}
