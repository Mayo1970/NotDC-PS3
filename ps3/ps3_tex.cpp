// PVR texture decode (twiddled, planar, stride, VQ, palette, YUV422, mipmap top level) to RGBA8,
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



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
extern "C" {
#include "ps3gl.h"
}
#pragma GCC diagnostic pop

extern "C" int get_tex_slot_hash_preset();
extern "C" int get_yuv_block_preset();
extern "C" int get_bump_map_preset();
extern "C" uint32_t* ps3gl_tex_direct_begin(GLsizei width, GLsizei height, uint32_t* pitch);
extern "C" void ps3gl_tex_direct_end(void);
extern "C" uint8_t* ps3gl_tex_direct_level0(GLsizei width, GLsizei height, uint32_t* pitch, int* swizzled);
extern "C" void ps3gl_tex_convert_check(void* dst, GLsizei width, GLsizei height, const void* rgba);

#define CACHE_SLOTS (1u << TEX_SLOT_BITS)
#define CACHE_PROBE 8
// Textures unused this many frames give back their RSX memory.
#define CACHE_MAX_AGE 180

struct TexEntry
{
	u32 key0, key1; // TCW, TSP size bits | IgnoreTexA | palette format | mirror | 3j bump
	u32 hash;
	GLuint name;
	u32 last_frame;
	u32 checked_frame;
	u16 w, h;
};

static TexEntry s_cache[CACHE_SLOTS];
static u32 s_frame = 1;
static u32 s_sweep;
static u32 s_decodes;
static u32 s_black_tex, s_alpha0_tex;
static u32 s_live;
static int s_slot_hash = -1;
static u32 s_evict, s_evict_hot;

// 5.12 #3 SPE Tex Hash mode of this frame's request (OFF with the SPU down).
static int s_spetex;

static YuvTables s_yuvt;
static int s_yuv_block = -1;
// Plan 3j Bump Maps, latched per frame; it is in the cache key of bump textures, so a switch decodes them again.
static int s_bump = -1;
// [texfmt]: P42 block-path decodes this window, and the last YUV texture's size, kind and pitch.
static u32 s_yuv_blk;
static u32 s_yuv_last_w, s_yuv_last_h, s_yuv_last_pitch;
static char s_yuv_last_kind = '-';

// Temporary G7 texture-mix counters, reset with the other per-window stats.
static u32 s_fmt_count[8];
static u32 s_flag_vq, s_flag_mip, s_flag_planar, s_flag_stride;
static u32 s_flag_nonsq, s_flag_mirror, s_flag_clamp;
// Counted per bind, not per cache fill: a mirrored texture drawn every frame
// fills once, so the fill counter cannot size the linear-layout cost.
static u32 s_bind_total, s_bind_mirror;

// 5.10 split of ps3tex_bind (timebase ticks): VRAM hash, decode + upload, ps3gl bind + params.
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
};

static void describe(TSP tsp, TCW tcw, TexDesc* d)
{
	d->fmt = tcw.NO_PAL.PixelFmt;
	d->w = 8 << tsp.TexU;
	d->h = 8 << tsp.TexV;
	d->addr = (tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
	d->pal4 = d->fmt == 5;
	d->pal8 = d->fmt == 6;
	d->vq = tcw.NO_PAL.VQ_Comp && !d->pal4 && !d->pal8;
	d->twiddled = d->pal4 || d->pal8 || d->vq || !tcw.NO_PAL.ScanOrder;
	d->pal_base = d->pal4 ? tcw.PAL.PalSelect << 4 : (tcw.PAL.PalSelect >> 4) << 8;
	d->pitch = d->w;

	bool mip = tcw.NO_PAL.MipMapped && d->twiddled;
	if (mip)
		d->h = d->w; // mipmapped textures are square

	u32 lvl = tsp.TexU + 3;
	if (d->vq)
	{
		// nullDC-v1 twidle_tex: the codebook stays at the texture address and
		// only the index map moves by the mip offset, then by the 2 KB book.
		d->vq_cb = d->addr;
		if (mip)
			d->addr += vq_mip_bytes[lvl];
		d->addr += 2048;
		d->src_bytes = d->w * d->h / 4;
	}
	else if (d->pal4 || d->pal8)
	{
		u32 bits = d->pal4 ? 4 : 8;
		if (mip)
			d->addr += mip_texels[lvl] * bits / 8;
		d->src_bytes = d->w * d->h * bits / 8;
	}
	else
	{
		if (mip)
			d->addr += mip_texels[lvl] * 2;
		// Stride textures: rows are TEXT_CONTROL[4:0] * 32 texels apart.
		if (!d->twiddled && tcw.NO_PAL.StrideSel)
		{
			u32 stride = (g_rr.text_control & 31) * 32;
			if (stride)
				d->pitch = stride;
		}
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
	if (s_spetex == SPETEX_ON && ps3spetex_hash(pidx, &h))
		return h;
	h = hash_texture(d);
	u32 spu;
	if (s_spetex == SPETEX_CHECK && ps3spetex_hash(pidx, &spu))
		ps3spetex_check(pidx, tsp.full, tcw.full, spu, h);
	return h;
}

// spepix_check: begin decode
static void decode(const TexDesc& d, bool ignore_alpha)
{
	u32 w = d.w, h = d.h;
	if (d.twiddled)
		build_twiddle(w, h);

	if (d.vq)
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

// 5.12 #4: the SPU decodes d into the bound texture's RSX memory; false leaves it to the PPU paths.
static bool upload_spu(const TexDesc& d, bool ignore_alpha)
{
	if (!spu_fits(d))
	{
		ps3spepix_tex_skip();
		return false;
	}
	u32 pitch;
	int swz;
	u8* const dst = ps3gl_tex_direct_level0(d.w, d.h, &pitch, &swz);
	if (!dst)
		return false;
	SpePixTex j;
	spu_job(d, ignore_alpha, (u32)(uintptr_t)dst, pitch, swz != 0, g_rr.pal_ram_ctrl, &j);
	u32 res;
	// A late SPU is down now; the PPU path rewrites the whole level.
	if (!ps3spepix_tex(&j, &res))
		return false;
	ps3gl_tex_direct_end();
	if (!(res & SPEPIX_LIT))
		s_black_tex++;
	if (!(res & SPEPIX_OPAQUE))
		s_alpha0_tex++;
	s_spu_dec++;
	return true;
}

// 5.12 #4 CHECK: the SPU decodes into a copy in the bound texture's layout, decode() runs as before, and its words as
// glTexImage2D stores them must equal the copy, with the same black and alpha-0 counts.
static void check_spu(const TexDesc& d, bool ignore_alpha)
{
	u32* const spu = ps3spepix_tex_check_buf(0);
	u32* const ppu = ps3spepix_tex_check_buf(1);
	u32 pitch = 0, res = 0;
	int swz = 0;
	bool ran = false;
	SpePixTex j;
	if (!spu_fits(d))
		ps3spepix_tex_skip();
	else if (spu && ps3gl_tex_direct_level0(d.w, d.h, &pitch, &swz))
	{
		spu_job(d, ignore_alpha, (u32)(uintptr_t)spu, pitch, swz != 0, g_rr.pal_ram_ctrl, &j);
		ran = ps3spepix_tex(&j, &res);
	}
	const u32 black = s_black_tex, alpha0 = s_alpha0_tex;
	decode(d, ignore_alpha);
	if (!ran)
		return;
	ps3gl_tex_convert_check(ppu, d.w, d.h, s_out);
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
	volatile u32* dst = ps3gl_tex_direct_begin(d.w, d.h, &pitch);
	if (!dst)
		return false;
	if (d.twiddled)
		build_twiddle(d.w, d.h);
	const u32 lit = yuvd_decode(&s_yuvt, (const u32*)PVRPARAMS.vram, d.w, d.h, d.addr, d.pitch, d.twiddled, d.vq,
	                            d.vq_cb, s_tx, s_ty, dst, pitch / 4);
	ps3gl_tex_direct_end();
	// YUV texels are opaque, so only the black count applies (decode()'s check).
	if (!(lit & 0xFFFFFF))
		s_black_tex++;
	s_yuv_blk++;
	return true;
}

// Decodes d into the bound texture.
static void upload(const TexDesc& d, bool ignore_alpha)
{
	// 3j: a bump texel is raw 4444 (S in A:R, R in G:B), and IgnoreTexA does not apply (Flycast texBMP_TW).
	if (d.fmt == 4 && s_bump > 0)
	{
		TexDesc b = d;
		b.fmt = 2;
		upload(b, false);
		return;
	}
	if (d.fmt == 3)
	{
		s_yuv_last_w = d.w;
		s_yuv_last_h = d.h;
		s_yuv_last_pitch = d.pitch;
		s_yuv_last_kind = d.vq ? 'v' : d.twiddled ? 't' : 'p';
	}
	const int pix = ps3spepix_tex_mode();
	if (pix == SPEPIX_ON && upload_spu(d, ignore_alpha))
		return;
	if (pix == SPEPIX_CHECK)
		check_spu(d, ignore_alpha);
	else if (d.fmt == 3 && upload_yuv_block(d))
		return;
	else
		decode(d, ignore_alpha);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d.w, d.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_out);
}

static void free_entry(TexEntry& e)
{
	if (e.name)
	{
		glDeleteTextures(1, &e.name);
		s_live--;
	}
	memset(&e, 0, sizeof(e));
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

	s_frame++;
	// Age out a slice per frame so the sweep cost stays flat.
	for (u32 i = 0; i < CACHE_SLOTS / 64; i++)
	{
		TexEntry& e = s_cache[s_sweep];
		if (e.name && s_frame - e.last_frame > CACHE_MAX_AGE)
			free_entry(e);
		s_sweep = (s_sweep + 1) % CACHE_SLOTS;
	}
}

// ps3gl picks the RSX layout at the first glTexImage2D, so set these before it:
// mirrored wrap needs a linear layout and stalls the RSX on a swizzled one.
static void set_tex_params(TSP tsp, bool mirror_u, bool mirror_v)
{
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
	                tsp.ClampU ? GL_CLAMP_TO_EDGE : (mirror_u ? GL_MIRRORED_REPEAT : GL_REPEAT));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
	                tsp.ClampV ? GL_CLAMP_TO_EDGE : (mirror_v ? GL_MIRRORED_REPEAT : GL_REPEAT));
	GLint filter = tsp.FilterMode ? GL_LINEAR : GL_NEAREST;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

bool ps3tex_bind(TSP tsp, TCW tcw, u32 pidx)
{
	// nullDC-v1 SetTexMode order: clamp wins, then mirror, then wrap.
	bool mirror_u = !tsp.ClampU && tsp.FlipU;
	bool mirror_v = !tsp.ClampV && tsp.FlipV;
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
			glBindTexture(GL_TEXTURE_2D, hit->name);
			upload(d, tsp.IgnoreTexA);
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
			return false;
		if (victim->name)
		{
			s_evict++;
			// Drawn in the last 4 frames: its probe window holds more textures than it fits (P16).
			if (s_frame - victim->last_frame <= 4)
				s_evict_hot++;
		}
		free_entry(*victim);
		hit = victim;
		hit->key0 = key0;
		hit->key1 = key1;
		hit->w = d.w;
		hit->h = d.h;
		const u64 t_h = PERF_TICKS();
		hit->hash = tex_hash(d, tsp, tcw, pidx);
		s_t_hash += PERF_TICKS() - t_h;
		s_hashes++;
		hit->checked_frame = s_frame;
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
		REND_PHASE(PH_TEX);
		const u64 t_d = PERF_TICKS();
		glGenTextures(1, &hit->name);
		glBindTexture(GL_TEXTURE_2D, hit->name);
		set_tex_params(tsp, mirror_u, mirror_v);
		// FMV is re-uploaded whole every frame, so swizzling only adds a staging copy (and P42 needs linear).
		// Must be set before the first upload: the layout is fixed at allocation.
		if (d.fmt == 3)
			ps3gl_texture_hint_no_swizzle();
		upload(d, tsp.IgnoreTexA);
		s_t_decode += PERF_TICKS() - t_d;
		s_live++;
		s_decodes++;
		REND_PHASE(PH_DRAW);
	}

	hit->last_frame = s_frame;
	const u64 t_g = PERF_TICKS();
	glBindTexture(GL_TEXTURE_2D, hit->name);
	set_tex_params(tsp, mirror_u, mirror_v);
	s_t_gl += PERF_TICKS() - t_g;
	return true;
}

void ps3tex_reset()
{
	for (u32 i = 0; i < CACHE_SLOTS; i++)
		free_entry(s_cache[i]);
	s_live = 0;
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
	       " | yuvblk=%u yuvlast=%ux%u%c%u spudec=%u\n",
	       s_fmt_count[0], s_fmt_count[1], s_fmt_count[2], s_fmt_count[3],
	       s_fmt_count[4], s_fmt_count[5], s_fmt_count[6],
	       s_flag_vq, s_flag_mip, s_flag_planar, s_flag_stride,
	       s_flag_nonsq, s_flag_mirror, s_flag_clamp,
	       s_bind_total, s_bind_mirror, s_black_tex, s_alpha0_tex, s_evict, s_evict_hot,
	       s_yuv_blk, s_yuv_last_w, s_yuv_last_h, s_yuv_last_kind, s_yuv_last_pitch, s_spu_dec);
	memset(s_fmt_count, 0, sizeof(s_fmt_count));
	s_flag_vq = s_flag_mip = s_flag_planar = s_flag_stride = 0;
	s_flag_nonsq = s_flag_mirror = s_flag_clamp = 0;
	s_bind_total = s_bind_mirror = 0;
	s_black_tex = s_alpha0_tex = 0;
	s_evict = s_evict_hot = 0;
	s_yuv_blk = 0;
	s_spu_dec = 0;
}
