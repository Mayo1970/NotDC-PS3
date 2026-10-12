#pragma once
// Plan section 7, bundle 12: the screen format, the screen scaler, the FPS text's lines and the screenshot's rows.
// No RSX and no registers here: misc/screen_check runs it.
#include <stdint.h>
#include <math.h>
#include <string.h>

// The Screen Format row.
enum { SCREEN_43, SCREEN_STRETCH, SCREEN_WIDE };

struct ScreenView
{
	int x, w;         // the viewport's columns; it takes every line of the display
	float cx0, cw;    // the canvas columns it shows, in TA pixels
};

// A scene's viewport and the canvas columns in it; cw is the canvas width of the 4:3 picture. STRETCH (P69) puts it
// on every column. WIDE (P53, Flycast rend.WideScreen) keeps the picture's place and adds canvas at both sides.
static inline void screen_view(uint32_t sw, uint32_t sh, int format, float cw, ScreenView* v)
{
	uint32_t w43 = sh * 4 / 3;
	if (w43 > sw)
		w43 = sw;
	const bool full = format == SCREEN_STRETCH || format == SCREEN_WIDE;
	v->x = full ? 0 : (int)((sw - w43) / 2);
	v->w = full ? (int)sw : (int)w43;
	v->cw = format == SCREEN_WIDE ? cw * (float)sw / (float)w43 : cw;
	v->cx0 = (cw - v->cw) * 0.5f;
}

// Flycast isClipped: the region tiles (tx0..tx1, end not included) and the FB clip leave a part of the TA's width
// (glob_x) out. The FB clip counts pixels after the scaler, so it is taken times the scaler's X factor.
static inline bool screen_x_clipped(uint32_t fx0, uint32_t fx1, uint32_t tx0, uint32_t tx1, uint32_t glob_x,
                                    uint32_t scaler_ctl)
{
	const float xs = ((scaler_ctl >> 16) & 1) ? 2.0f : 1.0f;
	const float x0 = fmaxf(roundf((float)fx0 * xs), (float)tx0);
	const float x1 = fminf(roundf((float)(fx1 + 1) * xs), (float)tx1);
	return x0 > 0.0f || x1 < (float)glob_x;
}

// P10 Screen Scaler, the canvas factors. Flycast getPvrFramebufferSize: with no interlace the scaler's vertical
// factor (1024 = 1) sets the screen's TA lines. Reicast: VO_CONTROL bit 8 shows each pixel twice: half the columns.
static inline void screen_scaler(uint32_t scaler_ctl, uint32_t vo_control, bool interlace, float* sx, float* sy)
{
	const uint32_t vs = scaler_ctl & 0xFFFF;
	if (!interlace && vs != 0 && (vs < 1024 || vs > 1025))
		*sy *= (float)vs / 1024.0f;
	if ((vo_control >> 8) & 1)
		*sx *= 0.5f;
}

#define OSD_LINES 4
#define OSD_LINE_LEN 24

// P50: SPG's text ("FPS: 59.94\nSPEED: 100.00%") as the overlay's lines: each line's label, then its value.
static inline uint32_t osd_split(const char* text, char out[OSD_LINES][OSD_LINE_LEN])
{
	uint32_t n = 0;
	while (*text && n < OSD_LINES)
	{
		const char* nl = strchr(text, '\n');
		const size_t len = nl ? (size_t)(nl - text) : strlen(text);
		size_t sep = len;
		for (size_t i = 0; i + 1 < len && sep == len; i++)
			if (text[i] == ':' && text[i + 1] == ' ')
				sep = i;
		const size_t from[2] = { 0, sep + 2 }, to[2] = { sep, len };
		for (int k = 0; k < 2 && n < OSD_LINES; k++)
		{
			if (from[k] >= to[k])
				continue;
			const size_t c = to[k] - from[k] < OSD_LINE_LEN - 1 ? to[k] - from[k] : OSD_LINE_LEN - 1;
			memcpy(out[n], text + from[k], c);
			out[n++][c] = 0;
		}
		text += len + (nl ? 1 : 0);
	}
	return n;
}

// P51: one read-back row (A8R8G8B8 words; on the PS3 the bytes A, R, G, B) as the PNG's R, G, B bytes.
static inline void shot_row_rgb(const uint32_t* argb, uint32_t w, uint8_t* rgb)
{
	for (uint32_t x = 0; x < w; x++)
	{
		rgb[x * 3] = (uint8_t)(argb[x] >> 16);
		rgb[x * 3 + 1] = (uint8_t)(argb[x] >> 8);
		rgb[x * 3 + 2] = (uint8_t)argb[x];
	}
}
