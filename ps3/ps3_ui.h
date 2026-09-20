#pragma once
#include <ppu-types.h>

// Immediate-mode text/quad UI on ps3gl, used by the front end before EmuMain.
// Colors are 0xAARRGGBB. Coordinates are pixels, origin top-left.

#define PS3UI_GLYPH_W 8
#define PS3UI_GLYPH_H 8

int  ps3ui_init(void);
void ps3ui_term(void);
void ps3ui_screen(float* w, float* h);

// Returns 0 when no back buffer is free; skip the frame and try again.
int  ps3ui_begin(u32 bg_argb);
void ps3ui_end(void);

void ps3ui_rect(float x, float y, float w, float h, u32 argb);
void ps3ui_text(float x, float y, float scale, u32 argb, const char* s);
void ps3ui_textf(float x, float y, float scale, u32 argb, const char* fmt, ...)
	__attribute__((format(printf, 5, 6)));
float ps3ui_text_w(float scale, const char* s);
