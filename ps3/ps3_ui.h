#pragma once
#include <ppu-types.h>

// Immediate-mode text/quad UI on ps3gl, used by the front end and the in-game menu.
// Colors are 0xAARRGGBB. Coordinates are pixels, origin top-left; text y is the baseline.

// Rodin Sans from /dev_flash; the 8x8 font if that fails.
enum { PS3UI_FONT_BODY, PS3UI_FONT_TITLE, PS3UI_FONT_COUNT };

// Face-button icons from assets/buttons/.
enum { PS3UI_ICON_CROSS, PS3UI_ICON_CIRCLE, PS3UI_ICON_TRIANGLE, PS3UI_ICON_SQUARE, PS3UI_ICON_COUNT };

int  ps3ui_init(void);
void ps3ui_term(void);
void ps3ui_screen(float* w, float* h);

// Returns 0 when no back buffer is free; skip the frame and try again.
// Draws the assets/0.png background, or clears to bg_argb if it did not load.
int  ps3ui_begin(u32 bg_argb);
void ps3ui_end(void);

void ps3ui_rect(float x, float y, float w, float h, u32 argb);
// Draws the icon in a size x size box, top-left at (x, y). Returns 0 if it did not load.
int  ps3ui_icon(int icon, float x, float y, float size);
void ps3ui_text(float x, float y, int font, u32 argb, const char* s);
void ps3ui_textf(float x, float y, int font, u32 argb, const char* fmt, ...)
	__attribute__((format(printf, 5, 6)));
float ps3ui_text_w(int font, const char* s);
// Height of a capital letter, to centre text in a box.
float ps3ui_cap_h(int font);
