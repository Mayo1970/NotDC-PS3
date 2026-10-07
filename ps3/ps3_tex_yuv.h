#pragma once
// P42 YUV422 block decoder: the final A8R8G8B8 words straight into the RSX rows, bit-exact with decode() + ps3gl
// convert_pixels(). Tables replace yuv_pair()'s multiplies and clamps; misc/yuv_check tests it against the old path.

struct YuvTables
{
	u16 dr[256], db[256]; // yuv_pair()'s red and blue terms + 256, so y + term indexes clamp[]
	u16 dg[768];          // 256 - its green term (u * 11 + v * 22) / 32 = 11 * (u + 2v) / 32, by u + 2v
	u8 clamp[1024];       // clamp8(i - 256)
};

static void yuvd_init(YuvTables* t)
{
	for (s32 i = 0; i < 256; i++)
	{
		const s32 c = i - 128;
		t->dr[i] = (u16)(c * 11 / 8 + 256);
		t->db[i] = (u16)(c * 110 / 64 + 256);
	}
	// Index u8 + 2 * v8 is u + 2v + 384 for the centred u, v.
	for (s32 k = 0; k < 768; k++)
		t->dg[k] = (u16)(256 - 11 * (k - 384) / 32);
	for (s32 i = 0; i < 1024; i++)
		t->clamp[i] = (u8)(i < 256 ? 0 : (i > 511 ? 255 : i - 256));
}

// One texel pair from the DC word U | Y0 << 8 | V << 16 | Y1 << 24.
static inline void yuvd_pair(const YuvTables* t, u32 w, u32* o0, u32* o1)
{
	const u32 u = w & 255, v = (w >> 16) & 255;
	const u8* r = t->clamp + t->dr[v];
	const u8* g = t->clamp + t->dg[u + 2 * v];
	const u8* b = t->clamp + t->db[u];
	const u32 y0 = (w >> 8) & 255, y1 = w >> 24;
	*o0 = 0xFF000000u | (u32)r[y0] << 16 | (u32)g[y0] << 8 | b[y0];
	*o1 = 0xFF000000u | (u32)r[y1] << 16 | (u32)g[y1] << 8 | b[y1];
}

// The decoder reads VRAM without decode()'s wrap mask, so the source must end inside VRAM.
static inline bool yuvd_fits(u32 w, u32 addr, u32 src_bytes, u32 pitch, bool twiddled, bool vq, u32 cb)
{
	if (addr + src_bytes > VRAM_SIZE || (w & 3))
		return false;
	if (vq)
		return cb + 2048 <= VRAM_SIZE;
	return (addr & 3) == 0 && (twiddled || ((pitch < w ? pitch : w) & 3) == 0);
}

// w x h texels into rows dpw words apart; returns the OR of every word (decode()'s black-texture check).
// tx/ty: build_twiddle() tables (twiddled and VQ). dst is volatile: RSX memory is cache-inhibited, so word stores only.
static u32 yuvd_decode(const YuvTables* t, const u32* vram, u32 w, u32 h, u32 addr, u32 pitch, bool twiddled, bool vq,
                       u32 cb, const u32* tx, const u32* ty, volatile u32* dst, u32 dpw)
{
	u32 lit = 0;
	if (!twiddled)
	{
		// Only `pitch` texels per row exist (stride textures); decode() leaves the rest of the row 0.
		const u32 cols = pitch < w ? pitch : w;
		for (u32 y = 0; y < h; y++, dst += dpw)
		{
			const u32* src = vram + ((addr + y * pitch * 2) >> 2);
			const u32* const end = src + cols / 2;
			volatile u32* d = dst;
			for (; src != end; src += 2, d += 4)
			{
				u32 o0, o1, o2, o3;
				yuvd_pair(t, src[0], &o0, &o1);
				yuvd_pair(t, src[1], &o2, &o3);
				d[0] = o0;
				d[1] = o1;
				d[2] = o2;
				d[3] = o3;
				lit |= o0 | o1 | o2 | o3;
			}
			for (volatile u32* const row_end = dst + w; d != row_end; d++)
				*d = 0;
		}
		return lit;
	}

	for (u32 y = 0; y < h; y++, dst += dpw)
	{
		// Pair (x, x+1) sits in the 8-byte 2x2 block at texel tx[x] | ty[y & ~1]; row y+1 is the high word halves.
		const u32 sh = (y & 1) * 16;
		const u32 tyb = ty[y & ~1u];
		volatile u32* d = dst;
		for (u32 x = 0; x < w; x += 2, d += 2)
		{
			u32 blk;
			if (vq)
			{
				const u32 ia = addr + ((tx[x] | tyb) >> 2);
				blk = (cb >> 2) + ((vram[ia >> 2] >> ((ia & 3) * 8)) & 255) * 2;
			}
			else
				blk = (addr >> 2) + (tx[x] | tyb) / 2;
			u32 o0, o1;
			yuvd_pair(t, ((vram[blk] >> sh) & 0xFFFF) | (vram[blk + 1] >> sh) << 16, &o0, &o1);
			d[0] = o0;
			d[1] = o1;
			lit |= o0 | o1;
		}
	}
	return lit;
}
