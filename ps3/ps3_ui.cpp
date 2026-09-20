// Text and quad drawing for the PS3 front end, on top of ps3gl.
#include "types.h"
#include "ps3_ui.h"
#include "ps3_video.h"
#include "ps3_font.h"
// ps3gl.h silences C-only warnings, which g++ reports as unknown pragmas.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
extern "C" {
#include "ps3gl.h"
}
#pragma GCC diagnostic pop

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// 16x8 grid of 8x8 glyphs covering ASCII 0..127; 0..31 stay blank.
#define FONT_TEX_W 128
#define FONT_TEX_H 64

static GLuint s_font_tex;
static float s_w = 1280, s_h = 720;
static int s_ready;

int ps3ui_init(void)
{
	if (s_ready)
		return 1;
	if (!ps3_video_init())
		return 0;

	u32 w, h;
	ps3_video_size(&w, &h);
	s_w = (float)w;
	s_h = (float)h;

	static u32 pixels[FONT_TEX_W * FONT_TEX_H];
	memset(pixels, 0, sizeof(pixels));
	for (int c = 32; c < 128; c++)
	{
		const unsigned char* g = ps3_font8x8[c - 32];
		int ox = (c & 15) * PS3UI_GLYPH_W;
		int oy = (c >> 4) * PS3UI_GLYPH_H;
		for (int row = 0; row < 8; row++)
			for (int col = 0; col < 8; col++)
				// White with the glyph bit as alpha, so glColor tints it.
				pixels[(oy + row) * FONT_TEX_W + ox + col] =
					(g[row] & (1 << col)) ? 0xFFFFFFFFu : 0x00FFFFFFu;
	}

	glGenTextures(1, &s_font_tex);
	glBindTexture(GL_TEXTURE_2D, s_font_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX_W, FONT_TEX_H, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	s_ready = 1;
	printf("[ui] ready, %ux%u\n", (unsigned)w, (unsigned)h);
	return 1;
}

void ps3ui_term(void)
{
	if (!s_ready)
		return;
	glDeleteTextures(1, &s_font_tex);
	s_font_tex = 0;
	s_ready = 0;
}

void ps3ui_screen(float* w, float* h)
{
	*w = s_w;
	*h = s_h;
}

int ps3ui_begin(u32 bg_argb)
{
	if (!s_ready || !ps3_video_begin_frame())
		return 0;

	glViewport(0, 0, (GLsizei)s_w, (GLsizei)s_h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, s_w, s_h, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glClearColor(((bg_argb >> 16) & 0xFF) / 255.0f, ((bg_argb >> 8) & 0xFF) / 255.0f,
	             (bg_argb & 0xFF) / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return 1;
}

void ps3ui_end(void)
{
	glDepthMask(GL_TRUE);
	ps3_video_end_frame();
}

static inline void set_color(u32 argb)
{
	glColor4ub((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24);
}

void ps3ui_rect(float x, float y, float w, float h, u32 argb)
{
	if (!s_ready)
		return;
	glDisable(GL_TEXTURE_2D);
	set_color(argb);
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();
}

float ps3ui_text_w(float scale, const char* s)
{
	return (float)strlen(s) * PS3UI_GLYPH_W * scale;
}

void ps3ui_text(float x, float y, float scale, u32 argb, const char* s)
{
	if (!s_ready || !s || !*s)
		return;

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_font_tex);
	set_color(argb);

	const float cw = PS3UI_GLYPH_W * scale;
	const float ch = PS3UI_GLYPH_H * scale;
	const float du = (float)PS3UI_GLYPH_W / FONT_TEX_W;
	const float dv = (float)PS3UI_GLYPH_H / FONT_TEX_H;

	glBegin(GL_QUADS);
	for (const unsigned char* p = (const unsigned char*)s; *p; p++, x += cw)
	{
		unsigned char c = *p;
		if (c < 32 || c > 127)
			c = '?';
		float u0 = (c & 15) * du;
		float v0 = (c >> 4) * dv;
		glTexCoord2f(u0, v0);           glVertex2f(x, y);
		glTexCoord2f(u0 + du, v0);      glVertex2f(x + cw, y);
		glTexCoord2f(u0 + du, v0 + dv); glVertex2f(x + cw, y + ch);
		glTexCoord2f(u0, v0 + dv);      glVertex2f(x, y + ch);
	}
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void ps3ui_textf(float x, float y, float scale, u32 argb, const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ps3ui_text(x, y, scale, argb, buf);
}
