// PVR texture decode (twiddled, planar, stride, VQ, palette, YUV422, mip chain) to RGBA8,
// cached per TCW/TSP key and revalidated once per frame by a hash of the source VRAM.
#include "ps3_tex.h"
#include "plugs/drkPvr/Renderer_if.h"
#include "ps3_phase.h"
#include "ps3_rend_regs.h"
#include "plugs/drkPvr/drkPvr.h"
#include "plugs/drkPvr/regs.h"
#include "dc/pvr/pvr_if.h"
#include "ps3_tex_slot.h"
#include "ps3_tex_yuv.h"
#include "ps3_spetex.h"
#include "ps3_spepix.h"
#include "ps3_rsxd.h"

extern "C" int get_tex_slot_hash_preset();
extern "C" int get_tex_recycle_preset();
extern "C" int get_yuv_block_preset();
extern "C" int get_bump_map_preset();
// Plan section 7, bundle 3 (the TEXTURES page).
extern "C" int get_mipmap_preset();
extern "C" int get_aniso_preset();
extern "C" int get_tex_filter_preset();
extern "C" int get_vq_formats_preset();

#define CACHE_SLOTS (1u << TEX_SLOT_BITS)
#define CACHE_PROBE 8
// Textures unused this many frames give back their RSX memory.
#define CACHE_MAX_AGE 180

struct TexEntry
{
	u32 key0, key1; // TCW, TSP size bits | IgnoreTexA | palette format | mirror | 3j bump
	u32 hash;
	u32 name;
	u32 last_frame;
	u32 checked_frame;
	u16 w, h;
	bool chain; // P45: its RSX memory holds the mip levels
	u8 kind;    // its layout, fixed at allocation: mirror | YUV << 1 | chain asked << 2 (Tex Recycle)
};

static TexEntry s_cache[CACHE_SLOTS];
static u32 s_frame = 1;
static u32 s_sweep;
static u32 s_decodes;
static u32 s_black_tex, s_alpha0_tex;
static u32 s_live;
static int s_slot_hash = -1;
static u32 s_evict, s_evict_hot;

// Bundle 14 (P73's counter): the key and content hash of the last texture freed for its age, per tex_slot() hash.
// [texlife]: textures freed so, cache misses whose key one of them had, and those with the same content.
struct TexGhost
{
	u32 key0, key1, hash;
	bool used;
};
static TexGhost s_ghost[1u << TEX_SLOT_BITS];
static u32 s_aged, s_back, s_back_same;

// 5.12 #3 SPE Tex Hash mode of this frame's request (OFF with the SPU down).
static int s_spetex;

static YuvTables s_yuvt;
static int s_yuv_block = -1;
// Plan 3j Bump Maps, latched per frame; it is in the cache key of bump textures, so a switch decodes them again.
static int s_bump = -1;
// Bundle 3, latched per frame. Mipmaps (P45) and VQ Formats (P25) change what a texture holds: a switch clears
// the cache. Anisotropic (index of k_aniso) and Texture Filter (P52) are sampler state only.
static int s_mips = -1, s_vqfmt = -1, s_aniso, s_filter;
static const int k_aniso[5] = { 1, 2, 4, 8, 16 };
// [texfmt]: textures given a chain and levels written past level 0, this window.
static u32 s_mip_tex, s_mip_levels;
// [texfmt]: P42 block-path decodes this window, and the last YUV texture's size, kind and pitch.
static u32 s_yuv_blk;
static u32 s_yuv_last_w, s_yuv_last_h, s_yuv_last_pitch;
static char s_yuv_last_kind = '-';

// Temporary G7 texture-mix counters, reset with the other per-window stats.
static u32 s_fmt_count[8];
static u32 s_flag_vq, s_flag_mip, s_flag_planar, s_flag_stride;
static u32 s_flag_nonsq, s_flag_mirror, s_flag_clamp;
// By the TCW bits, with VQ Formats ON or OFF: palette textures with VQ_Comp, VQ textures in scan order.
static u32 s_flag_palvq, s_flag_plvq;
// Counted per bind, not per cache fill: a mirrored texture drawn every frame
// fills once, so the fill counter cannot size the linear-layout cost.
static u32 s_bind_total, s_bind_mirror;

// 5.10 split of the bind (timebase ticks): VRAM hash, decode + upload, the words.
static u64 s_t_hash, s_t_decode, s_t_gl;
static u32 s_hashes;
static u64 s_hash_bytes;
// 5.12 #4: decodes the SPU did this window.
static u32 s_spu_dec;

// misc/spepix_check compiles the "spepix_check" regions of this file as the reference for the SPU decode.
// spepix_check: begin twiddle
static u32 s_out[1024 * 1024];
static u32 s_tx[1024], s_ty[1024];

// Texel offset bit layout of the PVR twiddle: y and x bits interleave while both sizes last.
static u32 twop(u32 x, u32 y, u32 xsz, u32 ysz)
{
	u32 rv = 0, sh = 0;
	xsz >>= 1;
	ysz >>= 1;
	while (xsz || ysz)
	{
		if (ysz) { rv |= (y & 1) << sh; ysz >>= 1; y >>= 1; sh++; }
		if (xsz) { rv |= (x & 1) << sh; xsz >>= 1; x >>= 1; sh++; }
	}
	return rv;
}

static void build_twiddle(u32 w, u32 h)
{
	for (u32 x = 0; x < w; x++) s_tx[x] = twop(x, 0, w, h);
	for (u32 y = 0; y < h; y++) s_ty[y] = twop(0, y, w, h);
}
// spepix_check: end twiddle

// misc/spetex_check compiles the code from here to "spetex_check: end" as the reference for the SPU hash.
// VRAM holds DC little-endian u32 words as host values, so sub-word reads index into the word.
static inline u32 vram32(u32 a)
{
	return *(u32*)&PVRPARAMS.vram[a & VRAM_MASK & ~3u];
}
static inline u32 vram16(u32 a)
{
	u32 w = vram32(a);
	return (a & 2) ? (w >> 16) : (w & 0xFFFF);
}
static inline u32 vram8(u32 a)
{
	return (vram32(a) >> ((a & 3) * 8)) & 0xFF;
}

static inline u32 rgba(u32 r, u32 g, u32 b, u32 a)
{
	return (r << 24) | (g << 16) | (b << 8) | a;
}
static inline u32 x5(u32 v) { return (v << 3) | (v >> 2); }
static inline u32 x6(u32 v) { return (v << 2) | (v >> 4); }
static inline u32 x4(u32 v) { return (v << 4) | v; }

static inline u32 cvt1555(u32 p) { return rgba(x5((p >> 10) & 31), x5((p >> 5) & 31), x5(p & 31), (p & 0x8000) ? 255 : 0); }
static inline u32 cvt565(u32 p)  { return rgba(x5(p >> 11), x6((p >> 5) & 63), x5(p & 31), 255); }
static inline u32 cvt4444(u32 p) { return rgba(x4((p >> 8) & 15), x4((p >> 4) & 15), x4(p & 15), x4(p >> 12)); }
static inline u32 cvt8888(u32 c) { return rgba((c >> 16) & 255, (c >> 8) & 255, c & 255, c >> 24); }

static inline u32 clamp8(s32 v) { return v < 0 ? 0 : (v > 255 ? 255 : (u32)v); }

// Two horizontal texels sharing U (in p0) and V (in p1); luma is the high byte.
// Chroma depends only on U/V, so it is computed once per pair (FMV decodes this every frame).
static inline void yuv_pair(u32 p0, u32 p1, u32* o)
{
	s32 u = (s32)(p0 & 255) - 128;
	s32 v = (s32)(p1 & 255) - 128;
	s32 dr = v * 11 / 8;
	s32 dg = (u * 11 + v * 22) / 32;
	s32 db = u * 110 / 64;
	s32 y0 = (s32)(p0 >> 8), y1 = (s32)(p1 >> 8);
	o[0] = rgba(clamp8(y0 + dr), clamp8(y0 - dg), clamp8(y0 + db), 255);
	o[1] = rgba(clamp8(y1 + dr), clamp8(y1 - dg), clamp8(y1 + db), 255);
}

static inline u32 cvt16(u32 fmt, u32 p)
{
	switch (fmt)
	{
	case 1: return cvt565(p);
	case 2: return cvt4444(p);
	case 4: return 0xFFFFFFFF; // bump map: not emulated
	default: return cvt1555(p); // 0, and 7 (reserved) as in Flycast
	}
}

static inline u32 cvt_pal(u32 index)
{
	u32 c = PALETTE_RAM[index & 1023];
	switch (g_rr.pal_ram_ctrl & 3)
	{
	case 0: return cvt1555(c & 0xFFFF);
	case 1: return cvt565(c & 0xFFFF);
	case 2: return cvt4444(c & 0xFFFF);
	default: return cvt8888(c);
	}
}

// Full-size level offset in texels for mipmapped textures (levels are stored small to large).
static const u32 mip_texels[11] = {
	0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558
};
// Same for VQ: bytes of index map after the 2 KB codebook.
static const u32 vq_mip_bytes[11] = {
	0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556
};

struct TexDesc
{
	u32 fmt, w, h, addr, pitch;
	bool vq, twiddled, pal4, pal8;
	u32 pal_base;
	u32 vq_cb;     // VQ codebook, at the texture address before any mip offset
	u32 src_bytes; // source span hashed for change detection
	u32 base, lvl; // the texture address; log2 of the width
	bool mip;      // the source holds a mip chain; addr is its top level
};

// P25 VQ Formats: the two forms it adds. The SPUs describe a texture the old way, so the PPU hashes and decodes these.
static inline bool vq_odd(const TexDesc& d)
{
	return d.vq && (d.pal4 || d.pal8 || !d.twiddled);
}

static void describe(TSP tsp, TCW tcw, TexDesc* d)
{
	d->fmt = tcw.NO_PAL.PixelFmt;
	d->w = 8 << tsp.TexU;
	d->h = 8 << tsp.TexV;
	d->addr = (tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
	d->pal4 = d->fmt == 5;
	d->pal8 = d->fmt == 6;
	const bool pal = d->pal4 || d->pal8;
	// VQ Formats (Flycast): VQ acts on palette textures too, and a VQ texture not mipmapped keeps its scan order.
	d->vq = tcw.NO_PAL.VQ_Comp && (s_vqfmt > 0 || !pal);
	d->twiddled = pal || !tcw.NO_PAL.ScanOrder || (d->vq && (s_vqfmt <= 0 || tcw.NO_PAL.MipMapped));
	d->pal_base = d->pal4 ? tcw.PAL.PalSelect << 4 : (tcw.PAL.PalSelect >> 4) << 8;
	d->pitch = d->w;
	d->base = d->addr;
	d->vq_cb = d->addr;
	d->lvl = tsp.TexU + 3;

	d->mip = tcw.NO_PAL.MipMapped && d->twiddled;
	if (d->mip)
		d->h = d->w; // mipmapped textures are square
	// Stride textures: rows are TEXT_CONTROL[4:0] * 32 texels apart.
	if (!d->twiddled && tcw.NO_PAL.StrideSel)
	{
		u32 stride = (g_rr.text_control & 31) * 32;
		if (stride)
			d->pitch = stride;
	}

	if (d->vq)
	{
		// nullDC-v1 twidle_tex: the codebook stays at the texture address and
		// only the index map moves by the mip offset, then by the 2 KB book.
		if (d->mip)
			d->addr += vq_mip_bytes[d->lvl];
		d->addr += 2048;
		// Scan order: one index byte per 4 texels of a row (Flycast texture_PLVQ).
		d->src_bytes = d->twiddled ? d->w * d->h / 4 : d->pitch * d->h / 4;
	}
	else if (pal)
	{
		u32 bits = d->pal4 ? 4 : 8;
		if (d->mip)
			d->addr += mip_texels[d->lvl] * bits / 8;
		d->src_bytes = d->w * d->h * bits / 8;
	}
	else
	{
		if (d->mip)
			d->addr += mip_texels[d->lvl] * 2;
		d->src_bytes = d->pitch * d->h * 2;
	}
}

// Jenkins one-at-a-time step on 64-bit words: a bijection in h and in w, so any one changed
// word changes the lane. No multiply: the serial FNV mullw chain cost ~46 cycles per word.
static inline u64 oaat(u64 h, u64 w)
{
	h += w;
	h += h << 10;
	h ^= h >> 6;
	return h;
}

static u32 hash_range(u32 addr, u32 bytes, u32 seed)
{
	addr &= ~3u;
	bytes = (bytes + 3) & ~3u;
	s_hash_bytes += bytes;
	u64 a = seed, b = seed ^ 0x9E3779B97F4A7C15ull, c = ~(u64)seed, d = seed ^ 0xC2B2AE3D27D4EB4Full;

	// A span past the end of VRAM wraps (garbage TCW): word loop with the mask.
	if (addr + bytes > VRAM_SIZE)
	{
		for (u32 i = 0; i < bytes; i += 4)
			a = oaat(a, vram32(addr + i));
		bytes = 0;
	}
	// Mip offsets can leave the start 4-byte aligned only.
	if (bytes != 0 && (addr & 4))
	{
		a = oaat(a, vram32(addr));
		addr += 4;
		bytes -= 4;
	}
	const u64* p = (const u64*)&PVRPARAMS.vram[addr];
	u32 n = bytes >> 3;
	// Four independent lanes keep the in-order PPE busy; the prefetch runs 256 bytes ahead.
	while (n >= 16)
	{
		__builtin_prefetch(p + 32);
		a = oaat(a, p[0]);  b = oaat(b, p[1]);  c = oaat(c, p[2]);  d = oaat(d, p[3]);
		a = oaat(a, p[4]);  b = oaat(b, p[5]);  c = oaat(c, p[6]);  d = oaat(d, p[7]);
		a = oaat(a, p[8]);  b = oaat(b, p[9]);  c = oaat(c, p[10]); d = oaat(d, p[11]);
		a = oaat(a, p[12]); b = oaat(b, p[13]); c = oaat(c, p[14]); d = oaat(d, p[15]);
		p += 16;
		n -= 16;
	}
	while (n--)
		a = oaat(a, *p++);
	if (bytes & 4)
		d = oaat(d, *(const u32*)p);

	// oaat chains the lanes bijectively; fmix64 spreads all 64 bits before the fold to 32.
	u64 h = oaat(oaat(oaat(a, b), c), d);
	h ^= h >> 33;
	h *= 0xFF51AFD7ED558CCDull;
	h ^= h >> 33;
	h *= 0xC4CEB9FE1A85EC53ull;
	h ^= h >> 33;
	return (u32)(h ^ (h >> 32));
}

static u32 hash_texture(const TexDesc& d)
{
	// FMV: the YUV converter's generation counter replaces a full src_bytes scan per video frame.
	if (d.fmt == 3)
	{
		u32 gen;
		if (yuv_conv_generation(d.addr, &gen))
			return gen;
	}

	u32 h = hash_range(d.addr, d.src_bytes, 2166136261u);
	// The codebook is not contiguous with the index map once mipmapped.
	if (d.vq)
		h = hash_range(d.vq_cb, 2048, h);
	if (d.pal4 || d.pal8)
	{
		u32 n = d.pal4 ? 16 : 256;
		for (u32 i = 0; i < n; i++)
			h = (h ^ PALETTE_RAM[(d.pal_base + i) & 1023]) * 16777619u;
		h ^= g_rr.pal_ram_ctrl & 3;
	}
	return h;
}
// spetex_check: end

// 5.12 #3: the SPU's hash of this scene's param pidx when it has one; CHECK hashes on the PPU and compares.
static u32 tex_hash(const TexDesc& d, TSP tsp, TCW tcw, u32 pidx)
{
	u32 h;
	const bool spu_ok = !vq_odd(d);
	if (s_spetex == SPETEX_ON && spu_ok && ps3spetex_hash(pidx, &h))
		return h;
	h = hash_texture(d);
	u32 spu;
	if (s_spetex == SPETEX_CHECK && spu_ok && ps3spetex_hash(pidx, &spu))
		ps3spetex_check(pidx, tsp.full, tcw.full, spu, h);
	return h;
}

// P45: the level of side 1 << i of d's chain, by Flycast's rules (TexCache.cpp Update()). *take11: a VQ 1x1 level
// is texel (1,1) of the 2x2 block m describes.
static void level_desc(const TexDesc& d, u32 i, TexDesc* m, bool* take11)
{
	*m = d;
	*take11 = false;
	m->lvl = i;
	m->w = m->h = m->pitch = 1u << i;
	if (d.vq)
	{
		m->addr = d.vq_cb + 2048 + vq_mip_bytes[i];
		if (i == 0)
		{
			// A YUV texture takes the block of its 2x2 level.
			if (d.fmt == 3)
				m->addr = d.vq_cb + 2048 + vq_mip_bytes[1];
			m->w = m->h = m->pitch = 2;
			*take11 = true;
		}
		m->src_bytes = (m->w * m->h + 3) / 4;
	}
	else if (d.pal4 || d.pal8)
	{
		const u32 bits = d.pal4 ? 4 : 8;
		m->addr = d.base + mip_texels[i] * bits / 8;
		m->src_bytes = (m->w * m->h * bits + 7) / 8;
	}
	else
	{
		m->addr = d.base + mip_texels[i] * 2;
		m->src_bytes = m->w * m->h * 2;
		// The 1x1 level of a YUV texture is one 565 texel.
		if (i == 0 && d.fmt == 3)
			m->fmt = 1;
	}
}

// spepix_check: begin decode
static void decode(const TexDesc& d, bool ignore_alpha)
{
	u32 w = d.w, h = d.h;
	if (d.twiddled)
		build_twiddle(w, h);

	if (d.vq && (d.pal4 || d.pal8))
	{
		// P25 (Flycast texPAL4_VQ, texPAL8_VQ): a codebook entry is the 8 index bytes of one twiddled block; a
		// is the index byte this texel has without VQ.
		for (u32 y = 0; y < h; y++)
			for (u32 x = 0; x < w; x++)
			{
				u32 t = s_tx[x] | s_ty[y];
				u32 a = d.pal4 ? t >> 1 : t;
				u32 b = vram8(d.vq_cb + vram8(d.addr + (a >> 3)) * 8 + (a & 7));
				s_out[y * w + x] = cvt_pal(d.pal_base + (d.pal4 ? (b >> ((t & 1) * 4)) & 15 : b));
			}
	}
	else if (d.vq && !d.twiddled)
	{
		// P25 (Flycast texture_PLVQ): a codebook entry is 4 texels of one row.
		u32 cols = d.pitch < w ? d.pitch : w;
		for (u32 y = 0; y < h; y++)
		{
			u32 row = d.addr + y * (d.pitch / 4);
			for (u32 x = 0; x < cols; x += 4)
			{
				u32 e = d.vq_cb + vram8(row + x / 4) * 8;
				u32* o = &s_out[y * w + x];
				if (d.fmt == 3)
				{
					yuv_pair(vram16(e), vram16(e + 2), o);
					yuv_pair(vram16(e + 4), vram16(e + 6), o + 2);
				}
				else
					for (u32 k = 0; k < 4; k++)
						o[k] = cvt16(d.fmt, vram16(e + k * 2));
			}
			for (u32 x = cols; x < w; x++)
				s_out[y * w + x] = 0;
		}
	}
	else if (d.vq)
	{
		u32 cb = d.vq_cb;
		u32 idx_base = d.addr;
		for (u32 y = 0; y < h; y += 2)
			for (u32 x = 0; x < w; x += 2)
			{
				u32 e = cb + vram8(idx_base + ((s_tx[x] | s_ty[y]) >> 2)) * 8;
				// Codebook entry order is twiddled: (0,0) (0,1) (1,0) (1,1).
				u32 s0 = vram16(e), s1 = vram16(e + 2), s2 = vram16(e + 4), s3 = vram16(e + 6);
				u32* o0 = &s_out[y * w + x];
				u32* o1 = o0 + w;
				if (d.fmt == 3)
				{
					yuv_pair(s0, s2, o0);
					yuv_pair(s1, s3, o1);
				}
				else
				{
					o0[0] = cvt16(d.fmt, s0); o0[1] = cvt16(d.fmt, s2);
					o1[0] = cvt16(d.fmt, s1); o1[1] = cvt16(d.fmt, s3);
				}
			}
	}
	else if (d.pal4 || d.pal8)
	{
		for (u32 y = 0; y < h; y++)
			for (u32 x = 0; x < w; x++)
			{
				u32 t = s_tx[x] | s_ty[y];
				u32 i = d.pal4 ? (vram8(d.addr + (t >> 1)) >> ((t & 1) * 4)) & 15 : vram8(d.addr + t);
				s_out[y * w + x] = cvt_pal(d.pal_base + i);
			}
	}
	else if (d.twiddled)
	{
		if (d.fmt == 3)
		{
			for (u32 y = 0; y < h; y++)
				for (u32 x = 0; x < w; x += 2)
					yuv_pair(vram16(d.addr + (s_tx[x] | s_ty[y]) * 2),
					         vram16(d.addr + (s_tx[x + 1] | s_ty[y]) * 2), &s_out[y * w + x]);
		}
		else
		{
			for (u32 y = 0; y < h; y++)
				for (u32 x = 0; x < w; x++)
					s_out[y * w + x] = cvt16(d.fmt, vram16(d.addr + (s_tx[x] | s_ty[y]) * 2));
		}
	}
	else
	{
		// Only `pitch` texels per row actually exist; reicast converts that many
		// into a w-wide buffer and leaves the tail alone (the UVs never reach it).
		u32 cols = d.pitch < w ? d.pitch : w;
		for (u32 y = 0; y < h; y++)
		{
			u32 row = d.addr + y * d.pitch * 2;
			if (d.fmt == 3)
				for (u32 x = 0; x < cols; x += 2)
					yuv_pair(vram16(row + x * 2), vram16(row + x * 2 + 2), &s_out[y * w + x]);
			else
				for (u32 x = 0; x < cols; x++)
					s_out[y * w + x] = cvt16(d.fmt, vram16(row + x * 2));
			for (u32 x = cols; x < w; x++)
				s_out[y * w + x] = 0;
		}
	}

	// yuv_pair() already stores alpha 255, so a YUV (FMV) surface skips this full-buffer pass.
	if (ignore_alpha && d.fmt != 3)
		for (u32 i = 0; i < w * h; i++)
			s_out[i] |= 0xFF;

	// Both inputs to the final pixel under shade mode 3 (modulate alpha):
	// all-black RGB, or all-zero alpha, each render the poly as nothing.
	u32 lit = 0, opaque = 0;
	for (u32 i = 0; i < w * h; i++)
	{
		if (s_out[i] & 0xFFFFFF00u) lit = 1;
		if (s_out[i] & 0xFFu) opaque = 1;
		if (lit && opaque) break;
	}
	if (!lit) s_black_tex++;
	if (!opaque) s_alpha0_tex++;
}

// 5.12 #4: the SPU reads whole quadwords and does not wrap at the end of VRAM as vram32() does.
static bool spu_fits(const TexDesc& d)
{
	u32 end = d.addr + d.src_bytes;
	if (d.vq && d.vq_cb + 2048 > VRAM_SIZE)
		return false;
	if (!d.twiddled)
		end = d.addr + ((d.h - 1) * d.pitch + (d.pitch < d.w ? d.pitch : d.w)) * 2;
	return d.addr < VRAM_SIZE && ((end + 15) & ~15u) <= VRAM_SIZE;
}

// The SPU job for d into dst, in the bound texture's layout.
static void spu_job(const TexDesc& d, bool ignore_alpha, u32 dst, u32 pitch, bool swizzled, u32 pal_ram_ctrl,
                    SpePixTex* j)
{
	j->fmt = d.fmt;
	j->w = d.w;
	j->h = d.h;
	j->addr = d.addr;
	j->pitch = d.pitch;
	j->flags = (d.twiddled ? SPEPIX_TW : 0) | (d.vq ? SPEPIX_VQ : 0) | (d.pal4 ? SPEPIX_PAL4 : 0) |
	           (d.pal8 ? SPEPIX_PAL8 : 0) | (ignore_alpha && d.fmt != 3 ? SPEPIX_IGNA : 0) | (swizzled ? SPEPIX_SWZ : 0);
	j->pal_base = d.pal_base;
	j->vq_cb = d.vq_cb;
	j->pal_ctrl = pal_ram_ctrl & 3;
	j->dst_ea = dst;
	j->dst_pitch = pitch;
}
// spepix_check: end decode

// The SPU's swizzled output has no twiddled YUV form: before P45 (a chain) no YUV texture was swizzled.
static inline bool spu_layout(const TexDesc& d, bool swizzled)
{
	return !(swizzled && d.fmt == 3 && d.twiddled && !d.vq);
}

// 5.12 #4: the SPU decodes d into level `level` of the bound texture's RSX memory; false leaves it to the PPU paths.
static bool upload_spu(const TexDesc& d, bool ignore_alpha, u32 level)
{
	if (!spu_fits(d))
	{
		ps3spepix_tex_skip();
		return false;
	}
	// P45: a chain level is swizzled, so it has no pitch.
	u32 pitch = 0;
	int swz = 1;
	u8* const dst = level ? rglTexDirectLevel(level) : rglTexDirectLevel0(d.w, d.h, &pitch, &swz);
	if (!dst || !spu_layout(d, swz != 0))
		return false;
	SpePixTex j;
	spu_job(d, ignore_alpha, (u32)(uintptr_t)dst, pitch, swz != 0, g_rr.pal_ram_ctrl, &j);
	u32 res;
	// A late SPU is down now; the PPU path rewrites the whole level.
	if (!ps3spepix_tex(&j, &res))
		return false;
	// The counts below are per texture: level 0 gives them.
	if (level)
	{
		rglTexDirectLevelEnd(level);
		return true;
	}
	rglTexDirectEnd();
	if (!(res & SPEPIX_LIT))
		s_black_tex++;
	if (!(res & SPEPIX_OPAQUE))
		s_alpha0_tex++;
	s_spu_dec++;
	return true;
}

// 5.12 #4 CHECK: the SPU decodes into a copy in the bound texture's layout, decode() runs as before, and its words as
// glTexImage2D stores them must equal the copy, with the same black and alpha-0 counts.
static void check_spu(const TexDesc& d, bool ignore_alpha, u32 level)
{
	u32* const spu = ps3spepix_tex_check_buf(0);
	u32* const ppu = ps3spepix_tex_check_buf(1);
	u32 pitch = 0, res = 0;
	int swz = level != 0;
	bool ran = false;
	SpePixTex j;
	if (!spu_fits(d))
		ps3spepix_tex_skip();
	else if (spu && (level ? rglTexDirectLevel(level) : rglTexDirectLevel0(d.w, d.h, &pitch, &swz)) &&
	         spu_layout(d, swz != 0))
	{
		spu_job(d, ignore_alpha, (u32)(uintptr_t)spu, pitch, swz != 0, g_rr.pal_ram_ctrl, &j);
		ran = ps3spepix_tex(&j, &res);
	}
	const u32 black = s_black_tex, alpha0 = s_alpha0_tex;
	decode(d, ignore_alpha);
	if (!ran)
		return;
	rglTexConvertCheck(ppu, d.w, d.h, s_out);
	const u32 rows = swz ? 1 : d.h, cols = swz ? d.w * d.h : d.w, stride = swz ? 0 : pitch / 4;
	for (u32 y = 0; y < rows; y++)
		for (u32 x = 0; x < cols; x++)
			if (spu[y * stride + x] != ppu[y * stride + x])
			{
				char what[160];
				snprintf(what, sizeof(what), "fmt %u %ux%u flags %02X pitch %u addr %06X: word %u of row %u spu/ppu %08X/%08X",
				         d.fmt, d.w, d.h, j.flags, d.pitch, d.addr, x, y, spu[y * stride + x], ppu[y * stride + x]);
				ps3spepix_tex_check(false, what);
				return;
			}
	const u32 want = (s_black_tex == black ? SPEPIX_LIT : 0) | (s_alpha0_tex == alpha0 ? SPEPIX_OPAQUE : 0);
	if (res != want)
	{
		char what[96];
		snprintf(what, sizeof(what), "fmt %u %ux%u: lit/opaque bits spu/ppu %u/%u", d.fmt, d.w, d.h, res, want);
		ps3spepix_tex_check(false, what);
		return;
	}
	ps3spepix_tex_check(true, 0);
}

// P42: a YUV texture straight into the bound texture's RSX rows; false leaves it to decode() + glTexImage2D.
static bool upload_yuv_block(const TexDesc& d)
{
	if (s_yuv_block <= 0 || !yuvd_fits(d.w, d.addr, d.src_bytes, d.pitch, d.twiddled, d.vq, d.vq_cb))
		return false;
	u32 pitch;
	volatile u32* dst = rglTexDirectBegin(d.w, d.h, &pitch);
	if (!dst)
		return false;
	if (d.twiddled)
		build_twiddle(d.w, d.h);
	const u32 lit = yuvd_decode(&s_yuvt, (const u32*)PVRPARAMS.vram, d.w, d.h, d.addr, d.pitch, d.twiddled, d.vq,
	                            d.vq_cb, s_tx, s_ty, dst, pitch / 4);
	rglTexDirectEnd();
	// YUV texels are opaque, so only the black count applies (decode()'s check).
	if (!(lit & 0xFFFFFF))
		s_black_tex++;
	s_yuv_blk++;
	return true;
}

// Decodes d into level `level` of the bound texture. take11 (P45): the level is texel (1,1) of d's 2x2 block.
static void upload(const TexDesc& d, bool ignore_alpha, u32 level = 0, bool take11 = false)
{
	// 3j: a bump texel is raw 4444 (S in A:R, R in G:B), and IgnoreTexA does not apply (Flycast texBMP_TW).
	if (d.fmt == 4 && s_bump > 0)
	{
		TexDesc b = d;
		b.fmt = 2;
		upload(b, false, level, take11);
		return;
	}
	if (d.fmt == 3 && level == 0)
	{
		s_yuv_last_w = d.w;
		s_yuv_last_h = d.h;
		s_yuv_last_pitch = d.pitch;
		s_yuv_last_kind = d.vq ? 'v' : d.twiddled ? 't' : 'p';
	}
	// The SPU's tiles are 8 texels a side or more (the small mip levels are not), and it has no P25 VQ forms; nor
	// has the P42 decoder.
	const bool fast = d.w >= 8 && d.h >= 8 && !take11 && !vq_odd(d);
	const int pix = fast ? ps3spepix_tex_mode() : SPEPIX_OFF;
	if (pix == SPEPIX_ON && upload_spu(d, ignore_alpha, level))
		return;
	if (pix == SPEPIX_CHECK)
		check_spu(d, ignore_alpha, level);
	else if (d.fmt == 3 && fast && level == 0 && upload_yuv_block(d))
		return;
	else
		decode(d, ignore_alpha);
	if (take11)
		s_out[0] = s_out[3];
	const int w = take11 ? 1 : (int)d.w, h = take11 ? 1 : (int)d.h;
	rglTexImage2D((int)level, w, h, s_out);
}

// P45: the levels under level 0, when the bound texture's memory holds them (RSXD_HINT_MIPMAPS).
static void upload_chain(const TexDesc& d, bool ignore_alpha)
{
	const u32 levels = (u32)rglTexChainLevels();
	if (!d.mip || levels < 2)
		return;
	// The black and alpha-0 counts are per texture: level 0 gave them.
	const u32 black = s_black_tex, alpha0 = s_alpha0_tex;
	for (u32 level = 1; level < levels && level <= d.lvl; level++)
	{
		TexDesc m;
		bool take11;
		level_desc(d, d.lvl - level, &m, &take11);
		upload(m, ignore_alpha, level, take11);
	}
	s_black_tex = black;
	s_alpha0_tex = alpha0;
	s_mip_tex++;
	s_mip_levels += levels - 1;
}

static void free_entry(TexEntry& e)
{
	if (e.name)
	{
		rglDeleteTexture(e.name);
		s_live--;
	}
	memset(&e, 0, sizeof(e));
}

// Garou (row Tex Recycle): an evicted texture waits as a spare until the RSX is past its last frame (the ring fences 2
// frames back), then a new texture of its size and layout takes it: no new table entry, no RSX allocation.
#ifndef SPARE_MAX
#define SPARE_MAX 256
#endif
#ifndef SPARE_BYTES
#define SPARE_BYTES (16u << 20)
#endif
#define SPARE_WAIT 2
#define SPARE_AGE 120

struct TexSpare
{
	u32 name;
	u32 frame;  // evicted in this frame
	u16 w, h;
	u8 kind;
};
static TexSpare s_spare[SPARE_MAX];  // oldest first
static u32 s_spare_n, s_spare_bytes;
static int s_recycle = -1;
// [texfmt]: new textures that took a spare, and evicted textures kept as one, this window.
static u32 s_reused, s_spared;

static inline u32 spare_bytes(const TexSpare& s)
{
	return (u32)s.w * s.h * 4;
}

static void spare_remove(u32 i, bool del)
{
	if (del)
		rglDeleteTexture(s_spare[i].name);
	s_spare_bytes -= spare_bytes(s_spare[i]);
	memmove(&s_spare[i], &s_spare[i + 1], (s_spare_n - i - 1) * sizeof(TexSpare));
	s_spare_n--;
}

static void spare_clear()
{
	while (s_spare_n)
		spare_remove(s_spare_n - 1, true);
}

// The entry's texture becomes a spare and the entry keeps none; a full list gives up its oldest first.
static void spare_put(TexEntry& e)
{
	const TexSpare s = { e.name, s_frame, e.w, e.h, e.kind };
	if (spare_bytes(s) > SPARE_BYTES)
		return;
	while (s_spare_n && (s_spare_n == SPARE_MAX || s_spare_bytes + spare_bytes(s) > SPARE_BYTES))
		spare_remove(0, true);
	s_spare[s_spare_n++] = s;
	s_spare_bytes += spare_bytes(s);
	e.name = 0;
	s_live--;
	s_spared++;
}

// The oldest spare of this size and kind that the RSX is done with, or 0. The list is in eviction order.
static u32 spare_take(u32 w, u32 h, u8 kind)
{
	for (u32 i = 0; i < s_spare_n && s_spare[i].frame + SPARE_WAIT <= s_frame; i++)
		if (s_spare[i].w == w && s_spare[i].h == h && s_spare[i].kind == kind)
		{
			const u32 name = s_spare[i].name;
			spare_remove(i, false);
			s_reused++;
			return name;
		}
	return 0;
}

void ps3tex_frame_begin(const DcParam* params, u32 nparam)
{
	// 5.12 #3: the SPU starts on this scene's textures before the rest of the frame.
	s_spetex = ps3spetex_mode();
	if (s_spetex != SPETEX_OFF)
		ps3spetex_request(params, nparam, g_rr.bg_param.pcw.full, g_rr.bg_param.tsp.full, g_rr.bg_param.tcw.full,
		                  g_rr.text_control, g_rr.pal_ram_ctrl);

	// Entries sit where the other slot function put them, so a switch starts from an empty cache.
	const int slot_hash = get_tex_slot_hash_preset() != 0;
	if (slot_hash != s_slot_hash)
	{
		ps3tex_reset();
		s_slot_hash = slot_hash;
		printf("[tex] slot hash %s, cache cleared\n", slot_hash ? "ON" : "OFF");
	}
	// Both YUV paths give the same texels, so a switch needs no cache clear.
	const int yuv_block = get_yuv_block_preset() != 0;
	if (yuv_block != s_yuv_block)
	{
		if (s_yuv_block < 0)
			yuvd_init(&s_yuvt);
		s_yuv_block = yuv_block;
		printf("[tex] yuv block decode %s\n", yuv_block ? "ON" : "OFF");
	}
	const int bump = get_bump_map_preset() != 0;
	if (bump != s_bump)
	{
		s_bump = bump;
		printf("[tex] bump maps %s\n", bump ? "ON" : "OFF");
	}
	// Bundle 3. A chain is fixed at allocation and VQ Formats changes the texels: both start from an empty cache.
	const int mips = get_mipmap_preset() != 0, vqfmt = get_vq_formats_preset() != 0;
	if (mips != s_mips || vqfmt != s_vqfmt)
	{
		ps3tex_reset();
		s_mips = mips;
		s_vqfmt = vqfmt;
		printf("[tex] mipmaps %s, vq formats %s, cache cleared\n", mips ? "ON" : "OFF", vqfmt ? "ON" : "OFF");
	}
	// Tex Recycle is live: OFF gives the spares' memory back.
	const int recycle = get_tex_recycle_preset() != 0;
	if (recycle != s_recycle)
	{
		spare_clear();
		s_recycle = recycle;
		printf("[tex] recycle %s\n", recycle ? "ON" : "OFF");
	}
	int aniso = get_aniso_preset(), filter = get_tex_filter_preset();
	aniso = aniso < 0 ? 0 : (aniso > 4 ? 4 : aniso);
	filter = filter < 0 || filter > 2 ? 0 : filter;
	if (aniso != s_aniso || filter != s_filter)
	{
		static const char* const k_filter[3] = { "GAME", "NEAREST", "LINEAR" };
		s_aniso = aniso;
		s_filter = filter;
		printf("[tex] anisotropic %dx, filter %s\n", (int)k_aniso[aniso], k_filter[filter]);
	}

	s_frame++;
	// Spares no new texture took give back their RSX memory.
	while (s_spare_n && s_frame - s_spare[0].frame > SPARE_AGE)
		spare_remove(0, true);
	// Age out a slice per frame so the sweep cost stays flat.
	for (u32 i = 0; i < CACHE_SLOTS / 64; i++)
	{
		TexEntry& e = s_cache[s_sweep];
		if (e.name && s_frame - e.last_frame > CACHE_MAX_AGE)
		{
			TexGhost& g = s_ghost[tex_slot(e.key0, e.key1, true)];
			g.key0 = e.key0;
			g.key1 = e.key1;
			g.hash = e.hash;
			g.used = true;
			s_aged++;
			free_entry(e);
		}
		s_sweep = (s_sweep + 1) % CACHE_SLOTS;
	}
}

// Set before the first glTexImage2D, which fixes the layout: a mirrored wrap stalls the RSX on a swizzled one.
// chain: the texture has its mip levels and the list may sample them.
static void set_tex_params(TSP tsp, bool mirror_u, bool mirror_v, bool chain)
{
	rglTexParameteri(GL_TEXTURE_WRAP_S, tsp.ClampU ? GL_CLAMP_TO_EDGE : (mirror_u ? GL_MIRRORED_REPEAT : GL_REPEAT));
	rglTexParameteri(GL_TEXTURE_WRAP_T, tsp.ClampV ? GL_CLAMP_TO_EDGE : (mirror_v ? GL_MIRRORED_REPEAT : GL_REPEAT));
	// P52 Texture Filter (Flycast TextureFiltering): 1 forces nearest, 2 linear.
	const bool nearest = s_filter == 1 || (s_filter == 0 && !tsp.FilterMode);
	// Flycast SetGPState: the chain filters bilinear textures only; anisotropic takes the trilinear form.
	int min = nearest ? GL_NEAREST : GL_LINEAR;
	if (chain && !nearest)
		min = s_aniso > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
	rglTexParameteri(GL_TEXTURE_MIN_FILTER, min);
	rglTexParameteri(GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
	rglTexParameteri(GL_TEXTURE_MAX_ANISOTROPY_EXT, nearest ? 1 : k_aniso[s_aniso]);
}

// The cache entry of a bind: a hit (hashed once a frame, decoded again when its VRAM changed) or a new texture.
static TexEntry* bind_entry(TSP tsp, TCW tcw, u32 pidx, bool mirror_u, bool mirror_v)
{
	bool mirror = mirror_u || mirror_v;

	s_bind_total++;
	if (mirror)
		s_bind_mirror++;

	u32 key0 = tcw.full;
	// Mirror is in the key because it decides the RSX layout at allocation time,
	// and that layout is fixed for the life of the texture.
	u32 key1 = (tsp.TexU << 3) | tsp.TexV | (tsp.IgnoreTexA << 6) | ((g_rr.pal_ram_ctrl & 3) << 7)
	         | (mirror << 9) | ((s_bump > 0 && tcw.NO_PAL.PixelFmt == 4) << 10);

	u32 slot = tex_slot(key0, key1, s_slot_hash > 0);
	TexEntry* hit = 0;
	TexEntry* victim = 0;
	for (u32 i = 0; i < CACHE_PROBE; i++)
	{
		TexEntry& e = s_cache[(slot + i) % CACHE_SLOTS];
		if (e.name && e.key0 == key0 && e.key1 == key1)
		{
			hit = &e;
			break;
		}
		if (!victim || !e.name || (victim->name && e.last_frame < victim->last_frame))
			victim = &e;
	}

	TexDesc d;
	if (!hit || hit->checked_frame != s_frame)
		describe(tsp, tcw, &d);

	if (hit && hit->checked_frame != s_frame)
	{
		const u64 t_h = PERF_TICKS();
		u32 hsh = tex_hash(d, tsp, tcw, pidx);
		s_t_hash += PERF_TICKS() - t_h;
		s_hashes++;
		if (hsh != hit->hash)
		{
			REND_PHASE(PH_TEX);
			const u64 t_d = PERF_TICKS();
			rglBindTexture(hit->name);
			upload(d, tsp.IgnoreTexA);
			if (hit->chain)
				upload_chain(d, tsp.IgnoreTexA);
			s_t_decode += PERF_TICKS() - t_d;
			hit->hash = hsh;
			s_decodes++;
			REND_PHASE(PH_DRAW);
		}
		hit->checked_frame = s_frame;
	}
	else if (!hit)
	{
		if (d.w > 1024 || d.h > 1024)
			return 0;
		// P45 Mipmaps: the chain needs the swizzled layout, so the first build gives a mirrored texture none.
		const bool chain = s_mips > 0 && d.mip && !mirror;
		const u8 kind = (u8)(mirror | ((d.fmt == 3) << 1) | (chain << 2));
		if (victim->name)
		{
			s_evict++;
			// Drawn in the last 4 frames: its probe window holds more textures than it fits (P16).
			if (s_frame - victim->last_frame <= 4)
				s_evict_hot++;
			// Tex Recycle: a texture with no mip chain waits as a spare.
			if (s_recycle > 0 && victim->kind < 4)
				spare_put(*victim);
		}
		free_entry(*victim);
		hit = victim;
		hit->key0 = key0;
		hit->key1 = key1;
		hit->w = d.w;
		hit->h = d.h;
		hit->kind = kind;
		const u64 t_h = PERF_TICKS();
		hit->hash = tex_hash(d, tsp, tcw, pidx);
		s_t_hash += PERF_TICKS() - t_h;
		s_hashes++;
		hit->checked_frame = s_frame;
		const TexGhost& g = s_ghost[tex_slot(key0, key1, true)];
		if (g.used && g.key0 == key0 && g.key1 == key1)
		{
			s_back++;
			s_back_same += g.hash == hit->hash;
		}
		// Temporary G7 diagnostic, counted not printed, so the sample follows what
		// is on screen instead of the first textures of the boot.
		s_fmt_count[d.fmt & 7]++;
		if (d.vq)                     s_flag_vq++;
		if (tcw.NO_PAL.MipMapped)     s_flag_mip++;
		if (!d.twiddled)              s_flag_planar++;
		if (tcw.NO_PAL.StrideSel)     s_flag_stride++;
		if (d.w != d.h)               s_flag_nonsq++;
		if (tsp.FlipU || tsp.FlipV)   s_flag_mirror++;
		if (tsp.ClampU || tsp.ClampV) s_flag_clamp++;
		const bool pal = tcw.NO_PAL.PixelFmt == 5 || tcw.NO_PAL.PixelFmt == 6;
		if (tcw.NO_PAL.VQ_Comp && pal) s_flag_palvq++;
		if (tcw.NO_PAL.VQ_Comp && !pal && tcw.NO_PAL.ScanOrder && !tcw.NO_PAL.MipMapped) s_flag_plvq++;
		REND_PHASE(PH_TEX);
		const u64 t_d = PERF_TICKS();
		hit->name = s_recycle > 0 && !chain ? spare_take(d.w, d.h, kind) : 0;
		const bool spare = hit->name != 0;
		if (!spare)
			rglGenTextures(&hit->name);
		rglBindTexture(hit->name);
		set_tex_params(tsp, mirror_u, mirror_v, false);
		// FMV is re-uploaded whole every frame, so swizzling only adds a staging copy (and P42 needs linear).
		// Both must be set before the first upload: the layout is fixed at allocation (a spare's is already).
		if (!spare && chain)
			rglTexHint(RSXD_HINT_MIPMAPS);
		else if (!spare && d.fmt == 3)
			rglTexHint(RSXD_HINT_LINEAR);
		upload(d, tsp.IgnoreTexA);
		if (chain)
			upload_chain(d, tsp.IgnoreTexA);
		hit->chain = chain && rglTexChainLevels() > 1;
		s_t_decode += PERF_TICKS() - t_d;
		s_live++;
		s_decodes++;
		REND_PHASE(PH_DRAW);
	}

	hit->last_frame = s_frame;
	return hit;
}

// RSX Direct (G17): the words are the texture's and the sampler's that set_tex_params would leave (rt_param). nullDC-v1
// SetTexMode order: clamp wins, then mirror, then wrap. Flycast: a punch-through polygon samples level 0 only.
bool ps3tex_bind_rsxd(u32 tsp_w, u32 tcw_w, u32 pidx, bool punch_through, RsxdTex* out)
{
	TSP tsp;
	TCW tcw;
	tsp.full = tsp_w;
	tcw.full = tcw_w;
	const bool mirror_u = !tsp.ClampU && tsp.FlipU, mirror_v = !tsp.ClampV && tsp.FlipV;
	TexEntry* hit = bind_entry(tsp, tcw, pidx, mirror_u, mirror_v);
	if (!hit)
		return false;
	const u64 t_g = PERF_TICKS();
	ps3rsxd_tex_name(hit->name, out);
	const bool chain = hit->chain && !punch_through;
	const bool nearest = s_filter == 1 || (s_filter == 0 && !tsp.FilterMode);
	u32 min = nearest ? 1 : 2;
	if (chain && !nearest)
		min = s_aniso > 0 ? 6 : 4;
	const u32 mag = nearest ? 1 : 2;
	const int a = nearest ? 1 : k_aniso[s_aniso];
	const u32 aniso = a >= 16 ? 7 : a >= 12 ? 6 : a >= 10 ? 5 : a >= 8 ? 4 : a >= 6 ? 3 : a >= 4 ? 2 : a >= 2 ? 1 : 0;
	out->ctrl |= aniso << 4;
	out->filter = (mag << 24) | (min << 16) | (1u << 13);
	out->wrap = (tsp.ClampU ? 3u : mirror_u ? 2u : 1u) | ((tsp.ClampV ? 3u : mirror_v ? 2u : 1u) << 8) | (3u << 16);
	s_t_gl += PERF_TICKS() - t_g;
	return true;
}

void ps3tex_reset()
{
	for (u32 i = 0; i < CACHE_SLOTS; i++)
		free_entry(s_cache[i]);
	s_live = 0;
	spare_clear();
}

void ps3tex_split(u64* t_hash, u64* t_decode, u64* t_gl, u32* hashes, u64* hash_bytes)
{
	*t_hash = s_t_hash;
	*t_decode = s_t_decode;
	*t_gl = s_t_gl;
	*hashes = s_hashes;
	*hash_bytes = s_hash_bytes;
	s_t_hash = s_t_decode = s_t_gl = 0;
	s_hashes = 0;
	s_hash_bytes = 0;
}

void ps3tex_stats(u32* decodes, u32* live)
{
	*decodes = s_decodes;
	*live = s_live;
	s_decodes = 0;

	printf("[texfmt] 1555=%u 565=%u 4444=%u yuv=%u bump=%u p4=%u p8=%u"
	       " | vq=%u mip=%u planar=%u stride=%u nonsquare=%u mirror=%u clamp=%u"
	       " | binds=%u mirrorbinds=%u blacktex=%u alpha0tex=%u | evict=%u hot=%u"
	       " | yuvblk=%u yuvlast=%ux%u%c%u spudec=%u"
	       " | chains=%u chainlv=%u palvq=%u plvq=%u | spares reused=%u put=%u held=%u\n",
	       s_fmt_count[0], s_fmt_count[1], s_fmt_count[2], s_fmt_count[3],
	       s_fmt_count[4], s_fmt_count[5], s_fmt_count[6],
	       s_flag_vq, s_flag_mip, s_flag_planar, s_flag_stride,
	       s_flag_nonsq, s_flag_mirror, s_flag_clamp,
	       s_bind_total, s_bind_mirror, s_black_tex, s_alpha0_tex, s_evict, s_evict_hot,
	       s_yuv_blk, s_yuv_last_w, s_yuv_last_h, s_yuv_last_kind, s_yuv_last_pitch, s_spu_dec,
	       s_mip_tex, s_mip_levels, s_flag_palvq, s_flag_plvq, s_reused, s_spared, s_spare_n);
	printf("[texlife] aged=%u back=%u same=%u\n", s_aged, s_back, s_back_same);
	s_reused = s_spared = 0;
	s_aged = s_back = s_back_same = 0;
	memset(s_fmt_count, 0, sizeof(s_fmt_count));
	s_flag_vq = s_flag_mip = s_flag_planar = s_flag_stride = 0;
	s_flag_nonsq = s_flag_mirror = s_flag_clamp = 0;
	s_flag_palvq = s_flag_plvq = 0;
	s_mip_tex = s_mip_levels = 0;
	s_bind_total = s_bind_mirror = 0;
	s_black_tex = s_alpha0_tex = 0;
	s_evict = s_evict_hot = 0;
	s_yuv_blk = 0;
	s_spu_dec = 0;
}
