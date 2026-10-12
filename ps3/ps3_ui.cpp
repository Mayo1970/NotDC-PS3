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

#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// assets/0.png and the button icons, linked in by ps3_ui_assets.S.
extern "C" const u8 ui_bg_png[];
extern "C" const u8 ui_bg_png_end[];
extern "C" const u8 ui_ico_cross_png[], ui_ico_cross_png_end[];
extern "C" const u8 ui_ico_circle_png[], ui_ico_circle_png_end[];
extern "C" const u8 ui_ico_triangle_png[], ui_ico_triangle_png_end[];
extern "C" const u8 ui_ico_square_png[], ui_ico_square_png_end[];

static const struct { const u8* png; const u8* end; } k_icon_png[PS3UI_ICON_COUNT] = {
	{ ui_ico_cross_png,    ui_ico_cross_png_end },
	{ ui_ico_circle_png,   ui_ico_circle_png_end },
	{ ui_ico_triangle_png, ui_ico_triangle_png_end },
	{ ui_ico_square_png,   ui_ico_square_png_end },
};

// The XMB's own Rodin files; nothing ships with the emulator. Sizes are for 720p.
#define FONT_DIR "/dev_flash/data/font/"
static const char* const k_font_file[PS3UI_FONT_COUNT] = {
	FONT_DIR "SCE-PS3-RD-R-LATIN.TTF",
	FONT_DIR "SCE-PS3-RD-B-LATIN.TTF",
};
static const int k_font_px[PS3UI_FONT_COUNT] = { 20, 32 };

#define FIRST_CHAR 32
#define NUM_CHARS  95   // ' ' to '~'
#define ATLAS_W    512
#define ATLAS_H    512
#define BLUR       2    // shadow box-blur radius
#define PAD        3    // free texels around each glyph; >= BLUR keeps neighbour blurs apart
#define SHADOW_RGB 0x081838u
#define SHADOW_DX  1
#define SHADOW_DY  2

struct Glyph
{
	u16 u, v, w, h;  // bitmap rect in the atlas
	s16 left, top;   // bitmap offset from the pen; top counts rows above the baseline
	s16 adv;
};

struct Font
{
	Glyph g[NUM_CHARS];
	s8    kern[NUM_CHARS][NUM_CHARS];
	float cap;
	bool  ttf;
};

struct Packer
{
	int x, y, row_h;
};

static Font   s_font[PS3UI_FONT_COUNT];
static GLuint s_font_tex, s_glow_tex, s_bg_tex;
static GLuint s_icon_tex[PS3UI_ICON_COUNT];
static float  s_w = 1280, s_h = 720;
static int    s_ready;

// -- Atlas -------------------------------------------------------------------

static bool pack(Packer* p, int w, int h, u16* u, u16* v)
{
	int cw = w + 2 * PAD, ch = h + 2 * PAD;
	if (p->x + cw > ATLAS_W)
	{
		p->x = 0;
		p->y += p->row_h;
		p->row_h = 0;
	}
	if (cw > ATLAS_W || p->y + ch > ATLAS_H)
		return false;
	*u = (u16)(p->x + PAD);
	*v = (u16)(p->y + PAD);
	p->x += cw;
	if (ch > p->row_h)
		p->row_h = ch;
	return true;
}

static u8* read_file(const char* path, size_t* size)
{
	*size = 0;
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	u8* buf = (n > 0) ? (u8*)malloc(n) : NULL;
	if (buf && fread(buf, 1, n, f) != (size_t)n)
	{
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf)
		*size = (size_t)n;
	return buf;
}

static bool load_ttf(FT_Library lib, int id, int px, Packer* pk, u8* atlas)
{
	size_t size;
	u8* file = read_file(k_font_file[id], &size);
	if (!file)
	{
		printf("[ui] cannot read %s\n", k_font_file[id]);
		return false;
	}
	FT_Face face;
	FT_Error err = FT_New_Memory_Face(lib, file, (FT_Long)size, 0, &face);
	if (err)
	{
		printf("[ui] FT_New_Memory_Face(%s) = %d\n", k_font_file[id], (int)err);
		free(file);
		return false;
	}

	Font* f = &s_font[id];
	memset(f, 0, sizeof(*f));
	FT_UInt idx[NUM_CHARS];
	bool ok = FT_Set_Pixel_Sizes(face, 0, px) == 0;
	for (int i = 0; ok && i < NUM_CHARS; i++)
	{
		idx[i] = FT_Get_Char_Index(face, FIRST_CHAR + i);
		// A glyph that fails to load keeps w = 0 and adv = 0: it draws nothing.
		if (FT_Load_Glyph(face, idx[i], FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT))
			continue;
		FT_GlyphSlot gs = face->glyph;
		const FT_Bitmap& bm = gs->bitmap;
		Glyph* g = &f->g[i];
		g->adv = (s16)((gs->advance.x + 32) >> 6);
		g->left = (s16)gs->bitmap_left;
		g->top = (s16)gs->bitmap_top;
		int bw = (int)bm.width, bh = (int)bm.rows;
		if (bw <= 0 || bh <= 0 || bm.pitch < bw || bm.pixel_mode != FT_PIXEL_MODE_GRAY)
			continue;
		if (!pack(pk, bw, bh, &g->u, &g->v))
		{
			printf("[ui] font atlas full at %s %dpx\n", k_font_file[id], px);
			ok = false;
			break;
		}
		g->w = (u16)bw;
		g->h = (u16)bh;
		for (int r = 0; r < bh; r++)
			memcpy(atlas + (g->v + r) * ATLAS_W + g->u, bm.buffer + r * bm.pitch, bw);
	}
	if (ok && FT_HAS_KERNING(face))
	{
		for (int a = 0; a < NUM_CHARS; a++)
			for (int b = 0; b < NUM_CHARS; b++)
			{
				FT_Vector d;
				if (idx[a] && idx[b] && FT_Get_Kerning(face, idx[a], idx[b], FT_KERNING_DEFAULT, &d) == 0)
				{
					long k = (d.x + 32) >> 6;
					f->kern[a][b] = (s8)(k < -127 ? -127 : (k > 127 ? 127 : k));
				}
			}
	}
	f->cap = (float)f->g['H' - FIRST_CHAR].top;
	f->ttf = ok;
	FT_Done_Face(face);
	free(file);
	return ok;
}

// Fallback when the Rodin files cannot be read: the 8x8 font at an integer scale.
static bool load_8x8(int id, int px, Packer* pk, u8* atlas)
{
	int s = px / 8 > 0 ? px / 8 : 1;
	Font* f = &s_font[id];
	memset(f, 0, sizeof(*f));
	for (int i = 0; i < NUM_CHARS; i++)
	{
		Glyph* g = &f->g[i];
		g->adv = (s16)(8 * s);
		g->top = (s16)(7 * s);
		if (!pack(pk, 8 * s, 8 * s, &g->u, &g->v))
			return false;
		g->w = g->h = (u16)(8 * s);
		const unsigned char* rows = ps3_font8x8[i];
		for (int y = 0; y < 8 * s; y++)
			for (int x = 0; x < 8 * s; x++)
				atlas[(g->v + y) * ATLAS_W + g->u + x] = (rows[y / s] & (1 << (x / s))) ? 255 : 0;
	}
	f->cap = 7.0f * s;
	return true;
}

// Separable box blur: the soft shadow under every glyph.
static void box_blur(const u8* src, u8* dst, u8* tmp)
{
	const int n = 2 * BLUR + 1;
	for (int y = 0; y < ATLAS_H; y++)
		for (int x = 0; x < ATLAS_W; x++)
		{
			int sum = 0;
			for (int k = -BLUR; k <= BLUR; k++)
				if (x + k >= 0 && x + k < ATLAS_W)
					sum += src[y * ATLAS_W + x + k];
			tmp[y * ATLAS_W + x] = (u8)(sum / n);
		}
	for (int y = 0; y < ATLAS_H; y++)
		for (int x = 0; x < ATLAS_W; x++)
		{
			int sum = 0;
			for (int k = -BLUR; k <= BLUR; k++)
				if (y + k >= 0 && y + k < ATLAS_H)
					sum += tmp[(y + k) * ATLAS_W + x];
			dst[y * ATLAS_W + x] = (u8)(sum / n);
		}
}

// -- PNG ---------------------------------------------------------------------

// libpng and libc's setjmp use the AltiVec 512-byte jmp_buf; -mno-altivec makes ours 256,
// so png_jmpbuf() returned NULL and setjmp wrote to address 0, killing the menu thread.
#define PNG_JMPBUF_SIZE 512

struct PngReader
{
	const u8* p;
	size_t    left;
};

static void png_read_mem(png_structp png, png_bytep out, png_size_t n)
{
	PngReader* r = (PngReader*)png_get_io_ptr(png);
	if (n > r->left)
		png_error(png, "truncated");
	memcpy(out, r->p, n);
	r->p += n;
	r->left -= n;
}

// Any PNG layout comes out as 8-bit RGBA, rows top to bottom.
static u8* decode_png(const u8* data, size_t size, u32* out_w, u32* out_h)
{
	if (size < 8 || png_sig_cmp((png_bytep)data, 0, 8))
		return NULL;
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png)
		return NULL;
	png_infop info = png_create_info_struct(png);
	if (!info)
	{
		png_destroy_read_struct(&png, NULL, NULL);
		return NULL;
	}

	PngReader rd = { data, size };
	u8* volatile pixels = NULL;
	png_bytep* volatile rows = NULL;
	jmp_buf* jb = png_set_longjmp_fn(png, longjmp, PNG_JMPBUF_SIZE);
	if (!jb)
	{
		printf("[ui] libpng rejected jmp_buf size %d\n", PNG_JMPBUF_SIZE);
		png_destroy_read_struct(&png, &info, NULL);
		return NULL;
	}
	if (setjmp(*jb))
	{
		png_destroy_read_struct(&png, &info, NULL);
		free(rows);
		free(pixels);
		return NULL;
	}
	png_set_read_fn(png, &rd, png_read_mem);
	png_read_info(png, info);

	png_uint_32 w, h;
	int depth, type;
	png_get_IHDR(png, info, &w, &h, &depth, &type, NULL, NULL, NULL);
	bool trns = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
	if (depth == 16)
		png_set_strip_16(png);
	if (type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (trns)
		png_set_tRNS_to_alpha(png);
	if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (!(type & PNG_COLOR_MASK_ALPHA) && !trns)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	png_set_interlace_handling(png);
	png_read_update_info(png, info);

	if (w == 0 || h == 0 || w > 4096 || h > 4096 || png_get_rowbytes(png, info) != w * 4)
		png_error(png, "unsupported size or layout");
	pixels = (u8*)malloc((size_t)w * h * 4);
	rows = (png_bytep*)malloc(sizeof(png_bytep) * h);
	if (!pixels || !rows)
		png_error(png, "out of memory");
	for (png_uint_32 y = 0; y < h; y++)
		rows[y] = pixels + (size_t)y * w * 4;
	png_read_image(png, rows);
	png_read_end(png, NULL);
	png_destroy_read_struct(&png, &info, NULL);
	free(rows);
	*out_w = w;
	*out_h = h;
	return pixels;
}

// -- Setup -------------------------------------------------------------------

static GLuint upload(GLenum fmt, int w, int h, const void* px)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	// ps3gl fixes the layout at the first upload, so the wrap mode goes first.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
	return t;
}

static void load_fonts()
{
	u8* atlas = (u8*)calloc(ATLAS_W * ATLAS_H, 3);
	if (!atlas)
	{
		printf("[ui] font atlas alloc failed\n");
		return;
	}
	u8* glow = atlas + ATLAS_W * ATLAS_H;
	u8* tmp = glow + ATLAS_W * ATLAS_H;

	FT_Library lib = NULL;
	FT_Error err = FT_Init_FreeType(&lib);
	if (err)
	{
		printf("[ui] FT_Init_FreeType = %d\n", (int)err);
		lib = NULL;
	}
	Packer pk = { 0, 0, 0 };
	for (int i = 0; i < PS3UI_FONT_COUNT; i++)
	{
		int px = (int)(k_font_px[i] * s_h / 720.0f + 0.5f);
		if (px < 12)
			px = 12;
		if (!(lib && load_ttf(lib, i, px, &pk, atlas)) && !load_8x8(i, px, &pk, atlas))
			printf("[ui] font %d: atlas full\n", i);
		printf("[ui] font %d: %s %dpx, cap %.0f\n", i,
		       s_font[i].ttf ? k_font_file[i] : "8x8 fallback", px, s_font[i].cap);
	}
	if (lib)
		FT_Done_FreeType(lib);

	box_blur(atlas, glow, tmp);
	s_font_tex = upload(GL_ALPHA, ATLAS_W, ATLAS_H, atlas);
	s_glow_tex = upload(GL_ALPHA, ATLAS_W, ATLAS_H, glow);
	free(atlas);
}

static void load_background()
{
	u32 w = 0, h = 0;
	u8* px = decode_png(ui_bg_png, (size_t)(ui_bg_png_end - ui_bg_png), &w, &h);
	if (!px)
	{
		printf("[ui] background PNG did not decode (%u bytes)\n", (unsigned)(ui_bg_png_end - ui_bg_png));
		return;
	}
	s_bg_tex = upload(GL_RGBA, (int)w, (int)h, px);
	free(px);
	printf("[ui] background %ux%u\n", (unsigned)w, (unsigned)h);
}

static void load_icons()
{
	for (int i = 0; i < PS3UI_ICON_COUNT; i++)
	{
		u32 w = 0, h = 0;
		size_t size = (size_t)(k_icon_png[i].end - k_icon_png[i].png);
		u8* px = decode_png(k_icon_png[i].png, size, &w, &h);
		if (!px)
		{
			printf("[ui] icon %d PNG did not decode (%u bytes)\n", i, (unsigned)size);
			continue;
		}
		// Clear texels hold white, which GL_LINEAR blends into a light rim when the icon
		// is scaled; black matches the icons' dark edge.
		for (u32 j = 0; j < w * h; j++)
			if (px[j * 4 + 3] == 0)
				px[j * 4] = px[j * 4 + 1] = px[j * 4 + 2] = 0;
		s_icon_tex[i] = upload(GL_RGBA, (int)w, (int)h, px);
		free(px);
		printf("[ui] icon %d: %ux%u\n", i, (unsigned)w, (unsigned)h);
	}
}

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

	load_fonts();
	load_background();
	load_icons();

	s_ready = 1;
	printf("[ui] ready, %ux%u\n", (unsigned)w, (unsigned)h);
	return 1;
}

void ps3ui_term(void)
{
	if (!s_ready)
		return;
	GLuint tex[3] = { s_font_tex, s_glow_tex, s_bg_tex };
	for (int i = 0; i < 3; i++)
		if (tex[i])
			glDeleteTextures(1, &tex[i]);
	s_font_tex = s_glow_tex = s_bg_tex = 0;
	for (int i = 0; i < PS3UI_ICON_COUNT; i++)
		if (s_icon_tex[i])
		{
			glDeleteTextures(1, &s_icon_tex[i]);
			s_icon_tex[i] = 0;
		}
	s_ready = 0;
}

void ps3ui_screen(float* w, float* h)
{
	*w = s_w;
	*h = s_h;
}

static inline void set_color(u32 argb)
{
	glColor4ub((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24);
}

// The GL state every UI draw needs: pixel coordinates over the whole display, alpha blend, no depth.
static void ui_state()
{
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
}

int ps3ui_begin(u32 bg_argb)
{
	if (!s_ready || !ps3_video_begin_frame())
		return 0;
	ui_state();

	glClearColor(((bg_argb >> 16) & 0xFF) / 255.0f, ((bg_argb >> 8) & 0xFF) / 255.0f,
	             (bg_argb & 0xFF) / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (s_bg_tex)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, s_bg_tex);
		set_color(0xFFFFFFFFu);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(s_w, 0);
		glTexCoord2f(1, 1); glVertex2f(s_w, s_h);
		glTexCoord2f(0, 1); glVertex2f(0, s_h);
		glEnd();
		glDisable(GL_TEXTURE_2D);
	}
	return 1;
}

void ps3ui_end(void)
{
	glDepthMask(GL_TRUE);
	ps3_video_end_frame();
	ps3_video_forget_frame();
}

// P50: the UI state inside a frame the renderer began and will end: no frame start, no clear, no background.
int ps3ui_overlay_begin(void)
{
	if (!s_ready)
		return 0;
	ui_state();
	return 1;
}

void ps3ui_overlay_end(void)
{
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
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

int ps3ui_icon(int icon, float x, float y, float size)
{
	if (!s_ready || icon < 0 || icon >= PS3UI_ICON_COUNT || !s_icon_tex[icon])
		return 0;
	// Whole pixels, so every icon scales the same way.
	x = floorf(x + 0.5f);
	y = floorf(y + 0.5f);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_icon_tex[icon]);
	set_color(0xFFFFFFFFu);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(x, y);
	glTexCoord2f(1, 0); glVertex2f(x + size, y);
	glTexCoord2f(1, 1); glVertex2f(x + size, y + size);
	glTexCoord2f(0, 1); glVertex2f(x, y + size);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	return 1;
}

// -- Text --------------------------------------------------------------------

static inline int glyph_index(unsigned char c)
{
	return (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) ? '?' - FIRST_CHAR : c - FIRST_CHAR;
}

static inline bool font_ok(int font)
{
	return font >= 0 && font < PS3UI_FONT_COUNT;
}

float ps3ui_text_w(int font, const char* s)
{
	if (!font_ok(font) || !s)
		return 0;
	const Font* f = &s_font[font];
	int w = 0, prev = -1;
	for (const unsigned char* p = (const unsigned char*)s; *p; p++)
	{
		int i = glyph_index(*p);
		if (prev >= 0)
			w += f->kern[prev][i];
		w += f->g[i].adv;
		prev = i;
	}
	return (float)w;
}

float ps3ui_cap_h(int font)
{
	return font_ok(font) ? s_font[font].cap : 0;
}

// grow = 0 draws the glyphs; grow = BLUR draws the larger shadow cells around them.
static void text_pass(const Font* f, float x, float y, int grow, const char* s)
{
	const float iu = 1.0f / ATLAS_W, iv = 1.0f / ATLAS_H;
	int pen = 0, prev = -1;
	glBegin(GL_QUADS);
	for (const unsigned char* p = (const unsigned char*)s; *p; p++)
	{
		int i = glyph_index(*p);
		const Glyph* g = &f->g[i];
		if (prev >= 0)
			pen += f->kern[prev][i];
		if (g->w)
		{
			float x0 = x + pen + g->left - grow, y0 = y - g->top - grow;
			float x1 = x0 + g->w + 2 * grow, y1 = y0 + g->h + 2 * grow;
			float u0 = (g->u - grow) * iu, v0 = (g->v - grow) * iv;
			float u1 = (g->u + g->w + grow) * iu, v1 = (g->v + g->h + grow) * iv;
			glTexCoord2f(u0, v0); glVertex2f(x0, y0);
			glTexCoord2f(u1, v0); glVertex2f(x1, y0);
			glTexCoord2f(u1, v1); glVertex2f(x1, y1);
			glTexCoord2f(u0, v1); glVertex2f(x0, y1);
		}
		pen += g->adv;
		prev = i;
	}
	glEnd();
}

void ps3ui_text(float x, float y, int font, u32 argb, const char* s)
{
	if (!s_ready || !s || !*s || !font_ok(font) || !s_font_tex)
		return;
	const Font* f = &s_font[font];
	// Whole pixels, so each glyph lands 1:1 on its atlas texels.
	x = floorf(x + 0.5f);
	y = floorf(y + 0.5f);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_glow_tex);
	set_color(((argb >> 24) * 158 / 255) << 24 | SHADOW_RGB);
	text_pass(f, x + SHADOW_DX, y + SHADOW_DY, BLUR, s);
	glBindTexture(GL_TEXTURE_2D, s_font_tex);
	set_color(argb);
	text_pass(f, x, y, 0, s);
	glDisable(GL_TEXTURE_2D);
}

void ps3ui_textf(float x, float y, int font, u32 argb, const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ps3ui_text(x, y, font, argb, buf);
}
