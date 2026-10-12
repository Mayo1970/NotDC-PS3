#pragma once
// Plan section 7, bundle 11: the geometry and the pixel packing of a render to texture, and the geometry of a 2D
// framebuffer read (Flycast transform_matrix.cpp, TexCache.cpp). No RSX and no registers here: misc/rtt_check runs it.
#include <stdint.h>

struct RttIn
{
	uint32_t fx0, fx1, fy0, fy1;    // FB_X_CLIP and FB_Y_CLIP, min and max (both included)
	uint32_t tx0, ty0, tx1, ty1;    // the tile clip in pixels (ends not included): the region array's tiles in TA_GLOB_TILE_CLIP
	uint32_t scaler_ctl, stride;    // SCALER_CTL; FB_W_LINESTRIDE in bytes, 0 = not set
	uint32_t max_w, max_h;          // the largest target
	bool by_tiles, scaler, fly;     // the rows RTT Size, RTT Scaler, RTT Pack
};

struct RttGeom
{
	float sx0, sy0, sw, sh;         // the TA area that fills the target
	uint32_t tw, th;                // the target, in pixels
	uint32_t wx0, wy0, wx1, wy1;    // the target pixels copied to VRAM, ends not included
	uint32_t ox, oy;                // the texel that target pixel (0, 0) is
	uint32_t stride;                // bytes from one texel row to the next
	bool scissor;                   // draw only inside cx0..cy1 (TA pixels, ends not included)
	float cx0, cy0, cx1, cy1;
};

// False: nothing to draw (an empty clip, or a target over the limit).
static inline bool rtt_geom(const RttIn* in, RttGeom* g)
{
	if (in->fx1 < in->fx0 || in->fy1 < in->fy0)
		return false;
	const uint32_t vs = in->scaler_ctl & 0xFFFF;
	const bool xh = in->scaler && ((in->scaler_ctl >> 16) & 1);
	const bool yv = in->scaler && vs != 0 && (vs < 1024 || vs > 1025);
	const uint32_t fw = in->fx1 - in->fx0 + 1, fh = in->fy1 - in->fy0 + 1;
	g->scissor = false;
	g->cx0 = g->cy0 = g->cx1 = g->cy1 = 0.0f;
	if (!in->by_tiles)
	{
		// The FB clip is the target. The scaler shrinks the TA area into it, so the area is the clip scaled back.
		const float xs = xh ? 2.0f : 1.0f, ys = yv ? (float)vs / 1024.0f : 1.0f;
		g->tw = fw;
		g->th = fh;
		g->sx0 = (float)in->fx0 * xs;
		g->sy0 = (float)in->fy0 * ys;
		g->sw = (float)fw * xs;
		g->sh = (float)fh * ys;
		g->wx0 = g->wy0 = 0;
		g->wx1 = fw;
		g->wy1 = fh;
		g->ox = in->fly ? in->fx0 : 0;
		g->oy = in->fly ? in->fy0 : 0;
	}
	else
	{
		// Flycast: the tiles from the TA origin are the scene, scaled into the target; the FB clip cuts the copy.
		if (in->tx1 <= in->tx0 || in->ty1 <= in->ty0)
			return false;
		g->tw = xh ? in->tx1 / 2 : in->tx1;
		g->th = yv ? in->ty1 * 1024 / vs : in->ty1;
		g->sx0 = g->sy0 = 0.0f;
		g->sw = (float)in->tx1;
		g->sh = (float)in->ty1;
		g->wx0 = in->fx0;
		g->wy0 = in->fy0;
		g->wx1 = in->fx1 + 1 < g->tw ? in->fx1 + 1 : g->tw;
		g->wy1 = in->fy1 + 1 < g->th ? in->fy1 + 1 : g->th;
		g->ox = g->oy = 0;
		g->scissor = in->tx0 != 0 || in->ty0 != 0;
		g->cx0 = (float)in->tx0;
		g->cy0 = (float)in->ty0;
		g->cx1 = (float)in->tx1;
		g->cy1 = (float)in->ty1;
	}
	if (g->tw == 0 || g->th == 0 || g->tw > in->max_w || g->th > in->max_h)
		return false;
	// With no FB_W_LINESTRIDE a row is as wide as the copy reaches.
	g->stride = in->stride ? in->stride : (g->tw + g->ox) * 2;
	// Flycast b77843b92 (Virtua Tennis): a row holds stride / 2 texels, and the copy stops there.
	if (in->fly)
	{
		const uint32_t lim = g->stride / 2;
		if (g->ox >= lim)
			g->wx1 = g->wx0;
		else if (g->wx1 + g->ox > lim)
			g->wx1 = lim - g->ox;
	}
	return g->wx1 > g->wx0 && g->wy1 > g->wy0;
}

// Flycast roundColor: an 8-bit channel as a bits-bit one, rounded to nearest, not past the top.
static inline uint32_t rtt_round(uint32_t in, uint32_t bits)
{
	uint32_t out = in >> (8 - bits);
	if (out != (0xFFu >> (8 - bits)))
		out += (in >> (8 - bits - 1)) & 1;
	return out;
}

// One A8R8G8B8 pixel in FB_W_CTRL (ctrl) pack mode 0-3, as Flycast's line writers with rounding: 0555 with the K
// bit, 565, 4444, and 1555 whose alpha bit is alpha >= fb_alpha_threshold.
static inline uint16_t rtt_pack16_fly(uint32_t argb, uint32_t ctrl)
{
	const uint32_t a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	const uint32_t c555 = (rtt_round(r, 5) << 10) | (rtt_round(g, 5) << 5) | rtt_round(b, 5);
	switch (ctrl & 7)
	{
	case 1:  return (uint16_t)((rtt_round(r, 5) << 11) | (rtt_round(g, 6) << 5) | rtt_round(b, 5));
	case 2:  return (uint16_t)((rtt_round(a, 4) << 12) | (rtt_round(r, 4) << 8) | (rtt_round(g, 4) << 4) | rtt_round(b, 4));
	case 3:  return (uint16_t)((a >= ((ctrl >> 16) & 0xFF) ? 0x8000u : 0u) | c555);
	default: return (uint16_t)((ctrl & 0x8000u) | c555);
	}
}

// A 24-bit framebuffer pixel (B, G, R bytes) that starts at byte a & 3 of the VRAM word w0 and runs on into the
// next word w1; a native word holds its 4 bytes from the low end. R is in bits 16-23, B in bits 0-7.
static inline uint32_t fb_px888(uint32_t w0, uint32_t w1, uint32_t a)
{
	const uint32_t sh = (a & 3) * 8;
	uint32_t c = w0 >> sh;
	if (sh > 8)
		c |= w1 << (32 - sh);
	return c & 0xFFFFFFu;
}

struct FbReadIn
{
	uint32_t size, depth;           // FB_R_SIZE; FB_R_CTRL.fb_depth: 0 = 0555, 1 = 565, 2 = 888, 3 = 0888
	uint32_t sof1, sof2;            // FB_R_SOF1 and FB_R_SOF2
	bool interlace, field2, vclk;   // SPG_CONTROL.interlace, SPG_STATUS.fieldnum, FB_R_CTRL.vclk_div
	uint32_t max_w, max_h;
};

struct FbReadGeom
{
	uint32_t sof, w, h, line_bytes; // first pixel, pixels and lines to copy, bytes from one line start to the next
};

// P26, Flycast ReadFramebuffer: two fields that interleave line by line are one image of twice the lines; else an
// interlaced read takes the shown field; a 15 kHz progressive mode has 240 lines; 888 pixels are 3 bytes.
static inline bool fb_read_geom(const FbReadIn* in, FbReadGeom* g)
{
	const uint32_t xs = in->size & 0x3FF, ys = (in->size >> 10) & 0x3FF, md = (in->size >> 20) & 0x3FF;
	if (xs == 0 || ys == 0)
		return false;
	// In 16-bit words, as the register counts them.
	uint32_t w = (xs + 1) * 2, mod = (md > 0 ? md - 1 : 0) * 2, bpp = 2;
	if (in->depth == 2)
	{
		bpp = 3;
		w = w * 2 / 3;
		mod = mod * 2 / 3;
	}
	else if (in->depth == 3)
	{
		bpp = 4;
		w /= 2;
		mod /= 2;
	}
	uint32_t h = ys + 1;
	g->sof = in->sof1;
	if (in->interlace)
	{
		if (w == mod && (in->sof2 & 0x00FFFFFCu) == ((in->sof1 & 0x00FFFFFCu) + w * bpp))
		{
			mod = 0;
			h *= 2;
		}
		else if (in->field2)
			g->sof = in->sof2;
	}
	else if (!in->vclk && h > 240)
		h = 240;
	g->w = w > in->max_w ? in->max_w : w;
	g->h = h > in->max_h ? in->max_h : h;
	g->line_bytes = (w + mod) * bpp;
	return true;
}
