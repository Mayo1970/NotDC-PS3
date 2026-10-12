// Plan section 7, bundle 11, PC check: the real ps3/ps3_rtt.h against the render to texture of before the bundle,
// against Flycast's code (written here in Flycast's own form) and against a model of the PVR's scaler and clip.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "ps3/ps3_rtt.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 12) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u32 g_seed = 12345;
static u32 rnd(u32 n)
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return (u32)(((unsigned long long)(g_seed >> 4) * n) >> 28);
}

// ---- the pixel packing -----------------------------------------------------------

// Flycast TexCache.cpp roundColor.
template<int bits>
static inline u8 roundColor(u8 in)
{
	u8 out = in >> (8 - bits);
	if (out != 0xffu >> (8 - bits))
		out += (in >> (8 - bits - 1)) & 1;
	return out;
}

// Flycast's FBLineWriter0555, 565, 4444 and 1555 with Round, for one pixel given as R, G, B, A bytes.
static u16 fly_pack(const u8* pixel, u32 fb_w_ctrl)
{
	enum { Red, Green, Blue, Alpha };
	const u32 fb_packmode = fb_w_ctrl & 7, fb_kval = (fb_w_ctrl >> 8) & 0xFF, fb_alpha_threshold = (fb_w_ctrl >> 16) & 0xFF;
	u8 red, green, blue, alpha;
	switch (fb_packmode)
	{
	case 0:
		red = roundColor<5>(pixel[Red]);
		green = roundColor<5>(pixel[Green]);
		blue = roundColor<5>(pixel[Blue]);
		return (u16)((red << 10) | (green << 5) | blue | ((fb_kval & 0x80) << 8));
	case 1:
		red = roundColor<5>(pixel[Red]);
		green = roundColor<6>(pixel[Green]);
		blue = roundColor<5>(pixel[Blue]);
		return (u16)((red << 11) | (green << 5) | blue);
	case 2:
		red = roundColor<4>(pixel[Red]);
		green = roundColor<4>(pixel[Green]);
		blue = roundColor<4>(pixel[Blue]);
		alpha = roundColor<4>(pixel[Alpha]);
		return (u16)((red << 8) | (green << 4) | blue | (alpha << 12));
	default:
		red = roundColor<5>(pixel[Red]);
		green = roundColor<5>(pixel[Green]);
		blue = roundColor<5>(pixel[Blue]);
		return (u16)((red << 10) | (green << 5) | blue | (pixel[Alpha] >= fb_alpha_threshold ? 0x8000 : 0));
	}
}

static void check_pack()
{
	for (u32 v = 0; v < 256; v++)
	{
		CHECK(rtt_round(v, 4) == roundColor<4>((u8)v) && rtt_round(v, 5) == roundColor<5>((u8)v) &&
		      rtt_round(v, 6) == roundColor<6>((u8)v), "round %u", v);
		// Nearest, and never past the top.
		CHECK(rtt_round(v, 5) == std::min(31u, (v + 4) >> 3), "round %u to 5 bits is %u", v, rtt_round(v, 5));
	}
	for (u32 n = 0; n < 4000000; n++)
	{
		const u32 argb = (rnd(65536) << 16) | rnd(65536);
		// Modes 0-3, every K and threshold bit, and the dither and reserved bits, which must not act.
		const u32 ctrl = ((rnd(65536) << 16) | rnd(65536)) & ~4u;
		const u8 px[4] = { (u8)(argb >> 16), (u8)(argb >> 8), (u8)argb, (u8)(argb >> 24) };
		CHECK(rtt_pack16_fly(argb, ctrl) == fly_pack(px, ctrl), "pack %08X ctrl %08X: %04X, Flycast %04X",
		      argb, ctrl, rtt_pack16_fly(argb, ctrl), fly_pack(px, ctrl));
	}
	// The alpha bit of 1555 at the threshold's edges.
	CHECK(rtt_pack16_fly(0x80000000u, 0x00800003) == 0x8000 && rtt_pack16_fly(0x7F000000u, 0x00800003) == 0,
	      "1555 threshold 0x80");
	CHECK(rtt_pack16_fly(0x00000000u, 0x00000003) == 0x8000 && rtt_pack16_fly(0xFE000000u, 0x00FF0003) == 0,
	      "1555 thresholds 0 and 0xFF");
}

// ---- the geometry ----------------------------------------------------------------

struct Ref
{
	bool ok;
	u32 tw, th;             // the target
	int x0, x1, y0, y1;     // texels written, ends not included
	double ax, bx, ay, by;  // texel (u, v) shows the TA point (ax + bx (u + 0.5), ay + by (v + 0.5))
	u32 stride;
	bool scissor;
};

// RTT Size TILE CLIP: Flycast getPvrFramebufferSize, getScaledFramebufferSize, CalcMatrices and writeTexture.
static Ref ref_tiles(const RttIn& in)
{
	Ref r;
	memset(&r, 0, sizeof(r));
	int width = in.tx1, height = in.ty1;   // tileClip.bottomRight() + 1
	const u32 vscalefactor = in.scaler_ctl & 0xFFFF, hscale = (in.scaler_ctl >> 16) & 1;
	const int W = width, H = height;
	if (in.scaler)
	{
		if ((vscalefactor < 1024 || vscalefactor > 1025) && vscalefactor != 0)
			height = height * 1024 / vscalefactor;
		if (hscale == 1)
			width /= 2;
	}
	if (in.tx1 <= in.tx0 || in.ty1 <= in.ty0 || width <= 0 || height <= 0 || width > (int)in.max_w || height > (int)in.max_h)
		return r;
	if (in.fx1 < in.fx0 || in.fy1 < in.fy0)
		return r;
	u32 linestride = in.stride;
	if (linestride == 0)
		linestride = width * 2;
	const int h2 = std::min<int>(height, in.fy1 + 1);
	const int xmin = in.fx0;
	int xmax = std::min<int>(in.fx1 + 1, width);
	if (in.fly)
		xmax = std::min<int>(xmax, linestride / 2);
	r.tw = width;
	r.th = height;
	r.x0 = xmin;
	r.x1 = xmax;
	r.y0 = in.fy0;
	r.y1 = h2;
	// The matrix puts the TA area 0..W, 0..H over the whole target.
	r.ax = r.ay = 0.0;
	r.bx = (double)W / width;
	r.by = (double)H / height;
	r.stride = linestride;
	r.scissor = in.tx0 != 0 || in.ty0 != 0;
	r.ok = r.x1 > r.x0 && r.y1 > r.y0;
	return r;
}

// RTT Size FB CLIP: the scaler puts TA pixel (X, Y) at framebuffer pixel (X / xs, Y / ys), and the FB clip picks
// the pixels that are written. RTT Pack puts pixel p at texel p; before, the clip's first pixel was texel (0, 0).
static Ref ref_fbclip(const RttIn& in)
{
	Ref r;
	memset(&r, 0, sizeof(r));
	if (in.fx1 < in.fx0 || in.fy1 < in.fy0)
		return r;
	const u32 vs = in.scaler_ctl & 0xFFFF;
	const double xs = (in.scaler && ((in.scaler_ctl >> 16) & 1)) ? 2.0 : 1.0;
	const double ys = (in.scaler && vs != 0 && vs != 1024 && vs != 1025) ? vs / 1024.0 : 1.0;
	const u32 fw = in.fx1 - in.fx0 + 1, fh = in.fy1 - in.fy0 + 1;
	if (fw > in.max_w || fh > in.max_h)
		return r;
	r.tw = fw;
	r.th = fh;
	const int offx = in.fly ? 0 : (int)in.fx0, offy = in.fly ? 0 : (int)in.fy0;   // texel = pixel - off
	r.x0 = (int)in.fx0 - offx;
	r.x1 = (int)in.fx1 + 1 - offx;
	r.y0 = (int)in.fy0 - offy;
	r.y1 = (int)in.fy1 + 1 - offy;
	r.stride = in.stride ? in.stride : (in.fly ? (in.fx1 + 1) * 2 : fw * 2);
	if (in.fly)
		r.x1 = std::min<int>(r.x1, r.stride / 2);
	r.ax = offx * xs;
	r.bx = xs;
	r.ay = offy * ys;
	r.by = ys;
	r.ok = r.x1 > r.x0 && r.y1 > r.y0;
	return r;
}

static RttIn rnd_in()
{
	static const u32 scalers[] = { 0x00400, 0x00401, 0x10400, 0x00800, 0x10800, 0x00200, 0x00600, 0x10555, 0x00000, 0x103FF, 0x00402 };
	RttIn in;
	memset(&in, 0, sizeof(in));
	const u32 big = rnd(4) == 0 ? 1100 : 700;
	in.fx0 = rnd(3) ? 0 : rnd(200);
	in.fy0 = rnd(3) ? 0 : rnd(200);
	in.fx1 = rnd(12) == 0 ? rnd(40) : in.fx0 + rnd(big);
	in.fy1 = rnd(12) == 0 ? rnd(40) : in.fy0 + rnd(big);
	in.tx0 = rnd(4) ? 0 : rnd(8) * 32;
	in.ty0 = rnd(4) ? 0 : rnd(8) * 32;
	in.tx1 = rnd(16) == 0 ? rnd(4) * 32 : in.tx0 + (1 + rnd(40)) * 32;
	in.ty1 = rnd(16) == 0 ? rnd(4) * 32 : in.ty0 + (1 + rnd(30)) * 32;
	in.scaler_ctl = rnd(5) == 0 ? rnd(0x20000) : scalers[rnd(sizeof(scalers) / sizeof(scalers[0]))];
	in.stride = rnd(3) == 0 ? 0 : (1 + rnd(400)) * 8;
	in.max_w = 1024;
	in.max_h = rnd(2) ? 720 : 1024;
	in.by_tiles = rnd(2) != 0;
	in.scaler = rnd(2) != 0;
	in.fly = rnd(2) != 0;
	return in;
}

static void check_geom()
{
	u32 drawn = 0;
	for (u32 n = 0; n < 3000000; n++)
	{
		const RttIn in = rnd_in();
		RttGeom g;
		memset(&g, 0xEE, sizeof(g));
		const bool ok = rtt_geom(&in, &g);
		const Ref r = in.by_tiles ? ref_tiles(in) : ref_fbclip(in);
		char what[200];
		snprintf(what, sizeof(what), "#%u fb %u..%u,%u..%u tiles %u..%u,%u..%u scaler %05X stride %u rows %d%d%d", n,
		         in.fx0, in.fx1, in.fy0, in.fy1, in.tx0, in.tx1, in.ty0, in.ty1, in.scaler_ctl, in.stride,
		         (int)in.by_tiles, (int)in.scaler, (int)in.fly);
		CHECK(ok == r.ok, "%s: drawn %d, want %d", what, (int)ok, (int)r.ok);
		if (!ok || !r.ok)
			continue;
		drawn++;
		CHECK(g.tw == r.tw && g.th == r.th, "%s: target %ux%u, want %ux%u", what, g.tw, g.th, r.tw, r.th);
		CHECK((int)(g.ox + g.wx0) == r.x0 && (int)(g.ox + g.wx1) == r.x1 && (int)(g.oy + g.wy0) == r.y0 &&
		      (int)(g.oy + g.wy1) == r.y1, "%s: texels %u..%u,%u..%u, want %d..%d,%d..%d", what,
		      g.ox + g.wx0, g.ox + g.wx1, g.oy + g.wy0, g.oy + g.wy1, r.x0, r.x1, r.y0, r.y1);
		CHECK(g.wx1 <= g.tw && g.wy1 <= g.th, "%s: the copy leaves the target", what);
		CHECK(g.stride == r.stride, "%s: stride %u, want %u", what, g.stride, r.stride);
		// The TA point at the centre of the first and of the last texel written.
		for (int k = 0; k < 2; k++)
		{
			const u32 i = k ? g.wx1 - 1 : g.wx0, j = k ? g.wy1 - 1 : g.wy0;
			const double mx = g.sx0 + (i + 0.5) * g.sw / g.tw, my = g.sy0 + (j + 0.5) * g.sh / g.th;
			const double wx = r.ax + r.bx * (g.ox + i + 0.5), wy = r.ay + r.by * (g.oy + j + 0.5);
			CHECK(fabs(mx - wx) < 2e-3 && fabs(my - wy) < 2e-3, "%s: texel %u,%u shows %g,%g, want %g,%g", what,
			      g.ox + i, g.oy + j, mx, my, wx, wy);
		}
		CHECK(g.scissor == r.scissor, "%s: scissor %d, want %d", what, (int)g.scissor, (int)r.scissor);
		if (g.scissor)
			CHECK(g.cx0 == in.tx0 && g.cy0 == in.ty0 && g.cx1 == in.tx1 && g.cy1 == in.ty1, "%s: the scissor is not the tiles", what);
		if (in.fly)
			CHECK((g.ox + g.wx1) * 2 <= g.stride, "%s: a texel past the row", what);

		// Every row off: the values of rtt_render() before bundle 11.
		if (!in.by_tiles && !in.scaler && !in.fly)
		{
			const u32 w = in.fx1 - in.fx0 + 1, h = in.fy1 - in.fy0 + 1;
			CHECK(g.tw == w && g.th == h && g.sx0 == (float)in.fx0 && g.sy0 == (float)in.fy0 && g.sw == (float)w &&
			      g.sh == (float)h && g.wx0 == 0 && g.wy0 == 0 && g.wx1 == w && g.wy1 == h && g.ox == 0 && g.oy == 0 &&
			      !g.scissor && g.stride == (in.stride ? in.stride : w * 2), "%s: not the old geometry", what);
		}
	}
	CHECK(drawn > 500000, "only %u cases drawn", drawn);
	printf("geometry: 3000000 cases, %u drawn\n", drawn);

	// The Crazy Taxi kind of case: a 640x480 scene scaled to a 320x240 texture.
	RttIn in;
	memset(&in, 0, sizeof(in));
	in.fx1 = 319;
	in.fy1 = 239;
	in.tx1 = 640;
	in.ty1 = 480;
	in.scaler_ctl = 0x10800;
	in.stride = 640;
	in.max_w = 1024;
	in.max_h = 720;
	RttGeom g;
	for (int tiles = 0; tiles < 2; tiles++)
	{
		in.by_tiles = tiles != 0;
		in.scaler = true;
		CHECK(rtt_geom(&in, &g) && g.tw == 320 && g.th == 240 && g.sx0 == 0 && g.sy0 == 0 && g.sw == 640 && g.sh == 480 &&
		      g.wx1 == 320 && g.wy1 == 240, "half-size scene, tiles %d", tiles);
		in.scaler = false;
		// With no scaler the old path shows the scene's top left quarter; the tiles give the whole scene, cut by the clip.
		CHECK(rtt_geom(&in, &g) && g.sw == (tiles ? 640 : 320) && g.tw == (tiles ? 640u : 320u) && g.wx1 == 320 && g.wy1 == 240,
		      "no scaler, tiles %d", tiles);
	}
}

// ---- the framebuffer read --------------------------------------------------------

// Flycast TexCache.cpp ReadFramebuffer, the part before the pixel loops.
static void fly_fb(u32 fb_x_size, u32 fb_y_size, u32 fb_modulus, u32 fb_depth, u32 fb_r_sof1, u32 fb_r_sof2, bool interlace,
                   u32 fieldnum, u32 vclk_div, int* o_width, int* o_height, int* o_modulus, int* o_bpp, u32* o_addr)
{
	int width = (fb_x_size + 1) * 2;     // in 16-bit words
	int height = fb_y_size + 1;
	int modulus = (fb_modulus - 1) * 2;

	int bpp;
	switch (fb_depth)
	{
		case 0:
		case 1:
			bpp = 2;
			break;
		case 2:
			bpp = 3;
			width = (width * 2) / 3;		// in pixels
			modulus = (modulus * 2) / 3;	// in pixels
			break;
		default:
			bpp = 4;
			width /= 2;             // in pixels
			modulus /= 2;           // in pixels
			break;
	}

	u32 addr = fb_r_sof1;
	if (interlace)
	{
		if (width == modulus && fb_r_sof2 == fb_r_sof1 + width * bpp)
		{
			// Typical case alternating even and odd lines -> take the whole buffer at once
			modulus = 0;
			height *= 2;
		}
		else
		{
			addr = fieldnum ? fb_r_sof2 : fb_r_sof1;
		}
	}
	else if (vclk_div == 0) {
		// 240p if PAL or NTSC
		height = std::min(height, 240);
	}
	*o_width = width;
	*o_height = height;
	*o_modulus = modulus;
	*o_bpp = bpp;
	*o_addr = addr;
}

static void check_fb()
{
	u32 whole = 0;
	for (u32 n = 0; n < 2000000; n++)
	{
		FbReadIn in;
		// An x size the weave's modulus (x size + 2) still fits its 10 bits with.
		const u32 xs = 1 + rnd(rnd(4) ? 400 : 1021), ys = 1 + rnd(rnd(4) ? 300 : 1023);
		u32 md = 1 + rnd(8);
		in.depth = rnd(4);
		in.interlace = rnd(2) != 0;
		in.field2 = rnd(2) != 0;
		in.vclk = rnd(2) != 0;
		// Half of the interlaced cases have the fields one line apart, as games set them.
		const bool weave = in.interlace && rnd(2);
		const u32 w16 = (xs + 1) * 2;
		const u32 wpx = in.depth == 2 ? w16 * 2 / 3 : in.depth == 3 ? w16 / 2 : w16;
		const u32 bpp = in.depth == 2 ? 3 : in.depth == 3 ? 4 : 2;
		if (weave)
			md = xs + 2;   // (md - 1) * 2 == (xs + 1) * 2
		const u32 sof1 = rnd(0x400000) & ~3u;
		const u32 sof2 = weave ? sof1 + wpx * bpp : rnd(0x400000) & ~3u;
		in.size = xs | (ys << 10) | (md << 20);
		// The registers keep the bits Flycast masks off at the write.
		const u32 junk1 = rnd(4) ? 0 : (rnd(256) << 24) | rnd(4), junk2 = rnd(4) ? 0 : (rnd(256) << 24) | rnd(4);
		in.sof1 = sof1 | junk1;
		in.sof2 = (sof2 & 0x00FFFFFCu) | junk2;
		in.max_w = 100000;
		in.max_h = 100000;
		FbReadGeom g;
		const bool ok = fb_read_geom(&in, &g);
		int fw, fh, fmod, fbpp;
		u32 faddr;
		fly_fb(xs, ys, md, in.depth, sof1, sof2 & 0x00FFFFFCu, in.interlace, in.field2, in.vclk, &fw, &fh, &fmod, &fbpp, &faddr);
		CHECK(ok, "#%u: not read", n);
		// The address: ours keeps the register's bits, so compare the address bits.
		CHECK(g.w == (u32)fw && g.h == (u32)fh && g.line_bytes == (u32)((fw + fmod) * fbpp) && (g.sof & 0x00FFFFFCu) == faddr,
		      "#%u size %08X depth %u il %d f2 %d vclk %d sof %06X %06X: %ux%u line %u at %06X, Flycast %dx%d line %d at %06X",
		      n, in.size, in.depth, (int)in.interlace, (int)in.field2, (int)in.vclk, sof1, sof2, g.w, g.h, g.line_bytes,
		      g.sof & 0x00FFFFFCu, fw, fh, (fw + fmod) * fbpp, faddr);
		whole += weave && g.h == 2 * (ys + 1);
		// The size cap of the convert buffers.
		in.max_w = 768;
		in.max_h = 576;
		FbReadGeom c;
		fb_read_geom(&in, &c);
		CHECK(c.w == std::min(g.w, 768u) && c.h == std::min(g.h, 576u) && c.line_bytes == g.line_bytes && c.sof == g.sof,
		      "#%u: the cap changes more than the size", n);
		// Where the rules agree, fb_geom() of ps3_rend.cpp gives the same: one field, no 240-line cap, no 24-bit.
		if (!in.interlace && (in.vclk || ys + 1 <= 240) && in.depth != 2)
		{
			const bool wide = in.depth == 3;
			const u32 src_w = wide ? (xs + 1) : ((xs + 1) << 1), src_h = ys + 1;
			const u32 mod_px = (md > 0 ? md - 1 : 0) << (wide ? 0 : 1);
			CHECK(c.w == std::min(src_w, 768u) && c.h == std::min(src_h, 576u) && c.line_bytes == (src_w + mod_px) * (wide ? 4 : 2) &&
			      c.sof == in.sof1, "#%u: not the old read", n);
		}
	}
	CHECK(whole > 100000, "only %u whole interlaced frames", whole);
	FbReadIn in;
	memset(&in, 0, sizeof(in));
	in.max_w = in.max_h = 4096;
	FbReadGeom g;
	in.size = 0 | (239 << 10) | (1 << 20);
	CHECK(!fb_read_geom(&in, &g), "an x size of 0 is read");
	in.size = 319 | (0 << 10) | (1 << 20);
	CHECK(!fb_read_geom(&in, &g), "a y size of 0 is read");
	// A modulus of 0 is a modulus of 1 (no pixel between lines), as in fb_geom().
	in.size = 319 | (239 << 10);
	in.vclk = true;
	CHECK(fb_read_geom(&in, &g) && g.w == 640 && g.h == 240 && g.line_bytes == 1280, "modulus 0");
	// 640x480 interlaced, the fields one line apart: one image of 480 lines.
	in.size = 319 | (239 << 10) | (321 << 20);
	in.interlace = true;
	in.sof1 = 0x200000;
	in.sof2 = 0x200000 + 1280;
	CHECK(fb_read_geom(&in, &g) && g.w == 640 && g.h == 480 && g.line_bytes == 1280 && g.sof == 0x200000, "480i whole frame");
	printf("framebuffer read: 2000000 cases, %u whole interlaced frames\n", whole);

	// 24-bit pixels: Flycast's loop over a line of words against fb_px888 at each pixel's byte.
	for (u32 n = 0; n < 20000; n++)
	{
		u32 words[64];
		for (u32 k = 0; k < 64; k++)
			words[k] = (rnd(65536) << 16) | rnd(65536);
		const int width = 1 + (int)rnd(80);
		u32 fly[84];
		u32* dst = fly;
		u32 addr = 0;
		#define RD() words[addr / 4]
		#define PACK(r, g, b) ((((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF))
		for (int i = 0; i < width; i += 4)
		{
			u32 src = RD();
			*dst++ = PACK(src >> 16, src >> 8, src);
			addr += 4;
			if (i + 1 >= width)
				break;
			u32 src2 = RD();
			*dst++ = PACK(src2 >> 8, src2, src >> 24);
			addr += 4;
			if (i + 2 >= width)
				break;
			u32 src3 = RD();
			*dst++ = PACK(src3, src2 >> 24, src2 >> 16);
			addr += 4;
			if (i + 3 >= width)
				break;
			*dst++ = PACK(src3 >> 24, src3 >> 16, src3 >> 8);
		}
		for (int i = 0; i < width; i++)
		{
			const u32 a = (u32)i * 3;
			const u32 c = fb_px888(words[a / 4], words[a / 4 + 1], a);
			CHECK(c == fly[i], "888 pixel %d: %06X, Flycast %06X", i, c, fly[i]);
		}
	}
}

int main()
{
	check_pack();
	check_geom();
	check_fb();
	if (g_fail)
	{
		printf("FAIL: %d checks\n", g_fail);
		return 1;
	}
	printf("PASS: pack, geometry, framebuffer read\n");
	return 0;
}
