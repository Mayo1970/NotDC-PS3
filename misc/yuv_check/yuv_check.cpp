// Host-only check for P42 (OPTIMIZATION_POTENTIAL.md): ps3/ps3_tex_yuv.h against a copy of the old path, decode()'s
// YUV branches + ps3gl convert_pixels() as the big-endian PPE runs them, compared on the final RSX words.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
#define VRAM_SIZE (8 * 1024 * 1024)
#define VRAM_MASK (VRAM_SIZE - 1)

// mutate.py swaps in an edited copy.
#ifndef YUV_SOURCE
#define YUV_SOURCE "ps3/ps3_tex_yuv.h"
#endif
#include YUV_SOURCE

static u8* g_vram;
static u32 s_out[1024 * 1024];
static u32 s_tx[1024], s_ty[1024];

// ---- Copied from ps3/ps3_tex.cpp (fa6880a): twiddle, VRAM reads, yuv_pair, describe() and decode() for fmt 3 ----

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

static inline u32 vram32(u32 a)
{
	return *(u32*)&g_vram[a & VRAM_MASK & ~3u];
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
static inline u32 clamp8(s32 v) { return v < 0 ? 0 : (v > 255 ? 255 : (u32)v); }

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

static const u32 mip_texels[11] = {
	0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558
};
static const u32 vq_mip_bytes[11] = {
	0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556
};

struct TexDesc
{
	u32 fmt, w, h, addr, pitch;
	bool vq, twiddled, pal4, pal8;
	u32 pal_base;
	u32 vq_cb;
	u32 src_bytes;
};

// The TCW/TSP fields describe() reads, for a YUV texture.
struct TexFields { u32 tex_u, tex_v, tex_addr, text_control; bool vq_comp, scan_order, stride_sel, mipmapped; };

static void describe(const TexFields& f, TexDesc* d)
{
	d->fmt = 3;
	d->w = 8 << f.tex_u;
	d->h = 8 << f.tex_v;
	d->addr = (f.tex_addr << 3) & VRAM_MASK;
	d->pal4 = d->fmt == 5;
	d->pal8 = d->fmt == 6;
	d->vq = f.vq_comp && !d->pal4 && !d->pal8;
	d->twiddled = d->pal4 || d->pal8 || d->vq || !f.scan_order;
	d->pal_base = 0;
	d->pitch = d->w;

	bool mip = f.mipmapped && d->twiddled;
	if (mip)
		d->h = d->w;

	u32 lvl = f.tex_u + 3;
	if (d->vq)
	{
		d->vq_cb = d->addr;
		if (mip)
			d->addr += vq_mip_bytes[lvl];
		d->addr += 2048;
		d->src_bytes = d->w * d->h / 4;
	}
	else
	{
		d->vq_cb = 0;
		if (mip)
			d->addr += mip_texels[lvl] * 2;
		if (!d->twiddled && f.stride_sel)
		{
			u32 stride = (f.text_control & 31) * 32;
			if (stride)
				d->pitch = stride;
		}
		d->src_bytes = d->pitch * d->h * 2;
	}
}

// decode() with fmt 3; returns its black-texture flag (no texel with an RGB bit).
static bool old_decode(const TexDesc& d, bool* alpha0)
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
				u32 s0 = vram16(e), s1 = vram16(e + 2), s2 = vram16(e + 4), s3 = vram16(e + 6);
				u32* o0 = &s_out[y * w + x];
				u32* o1 = o0 + w;
				yuv_pair(s0, s2, o0);
				yuv_pair(s1, s3, o1);
			}
	}
	else if (d.twiddled)
	{
		for (u32 y = 0; y < h; y++)
			for (u32 x = 0; x < w; x += 2)
				yuv_pair(vram16(d.addr + (s_tx[x] | s_ty[y]) * 2),
				         vram16(d.addr + (s_tx[x + 1] | s_ty[y]) * 2), &s_out[y * w + x]);
	}
	else
	{
		u32 cols = d.pitch < w ? d.pitch : w;
		for (u32 y = 0; y < h; y++)
		{
			u32 row = d.addr + y * d.pitch * 2;
			for (u32 x = 0; x < cols; x += 2)
				yuv_pair(vram16(row + x * 2), vram16(row + x * 2 + 2), &s_out[y * w + x]);
			for (u32 x = cols; x < w; x++)
				s_out[y * w + x] = 0;
		}
	}

	u32 lit = 0, opaque = 0;
	for (u32 i = 0; i < w * h; i++)
	{
		if (s_out[i] & 0xFFFFFF00u) lit = 1;
		if (s_out[i] & 0xFFu) opaque = 1;
		if (lit && opaque) break;
	}
	*alpha0 = !opaque;
	return !lit;
}

// ---- Copied from ps3/ps3gl/ps3gl_textures.c: the linear level-0 path of glTexImage2D(GL_RGBA) ----

static uint32_t tex_linear_pitch(uint32_t w0)
{
	return ((w0 * 4) + 63u) & ~63u;
}

// convert_pixels(swizzled 0, fast): s_out goes in as bytes, which the PPE stores big-endian (r, g, b, a).
static void old_convert(u8* dst, uint32_t dst_stride, int width, int height)
{
	static std::vector<u8> be;
	be.resize((size_t)width * height * 4);
	for (size_t i = 0; i < (size_t)width * height; i++)
	{
		be[i * 4 + 0] = (u8)(s_out[i] >> 24);
		be[i * 4 + 1] = (u8)(s_out[i] >> 16);
		be[i * 4 + 2] = (u8)(s_out[i] >> 8);
		be[i * 4 + 3] = (u8)s_out[i];
	}
	const uint8_t* src = be.data();
	for (int row = 0; row < height; row++)
	{
		const uint8_t* srow = src + (size_t)row * width * 4;
		uint32_t* drow = (uint32_t*)(dst + (uint32_t)row * dst_stride);
		for (int col = 0; col < width; col++)
		{
			drow[col] =
				((uint32_t)srow[col * 4 + 3] << 24) |
				((uint32_t)srow[col * 4 + 0] << 16) |
				((uint32_t)srow[col * 4 + 1] << 8)  |
				 (uint32_t)srow[col * 4 + 2];
		}
	}
}

// ---- Test driver ----

static u32 g_seed = 20261007;
static u32 rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed ^ (g_seed >> 15); }
static u32 rnd(u32 n) { return (u32)(((u64)rnd() * n) >> 32); }

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 10) { printf("FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static YuvTables g_t;
static const u32 CANARY = 0xA5C3E1F7u;

struct Stats { u32 runs, fast, fallback, black, kinds[3]; u64 texels; };

// One texture: old decode + convert against the new decoder, whole buffers (row padding included).
static void run_case(const TexFields& f, Stats* st, const char* tag)
{
	TexDesc d;
	describe(f, &d);
	const u32 pitch_b = tex_linear_pitch(d.w);
	const size_t words = (size_t)pitch_b / 4 * d.h;
	static std::vector<u32> a, b;
	a.assign(words, CANARY);
	b.assign(words, CANARY);

	bool alpha0 = false;
	const bool black = old_decode(d, &alpha0);
	old_convert((u8*)a.data(), pitch_b, d.w, d.h);
	CHECK(!alpha0, "%s: old path saw a YUV texture with no alpha", tag);

	const bool span_ok = d.addr + d.src_bytes <= VRAM_SIZE && (!d.vq || d.vq_cb + 2048 <= VRAM_SIZE);
	const bool fits = yuvd_fits(d.w, d.addr, d.src_bytes, d.pitch, d.twiddled, d.vq, d.vq_cb);
	CHECK(fits == span_ok, "%s: yuvd_fits=%d, span inside VRAM=%d (w=%u addr=%06X bytes=%u pitch=%u tw=%d vq=%d cb=%06X)",
	      tag, fits, span_ok, d.w, d.addr, d.src_bytes, d.pitch, d.twiddled, d.vq, d.vq_cb);
	st->runs++;
	st->kinds[d.vq ? 2 : d.twiddled ? 1 : 0]++;
	if (!fits)
	{
		st->fallback++;
		return;
	}
	st->fast++;
	st->texels += (u64)d.w * d.h;
	st->black += black;

	// Fresh tables from build_twiddle() for every texture, as the PS3 caller does.
	memset(s_tx, 0xEE, sizeof(s_tx));
	memset(s_ty, 0xEE, sizeof(s_ty));
	if (d.twiddled)
		build_twiddle(d.w, d.h);
	const u32 lit = yuvd_decode(&g_t, (const u32*)g_vram, d.w, d.h, d.addr, d.pitch, d.twiddled, d.vq, d.vq_cb,
	                            s_tx, s_ty, (volatile u32*)b.data(), pitch_b / 4);
	CHECK(black == !(lit & 0xFFFFFF), "%s: black flag old=%d new=%d", tag, black, !(lit & 0xFFFFFF));
	for (size_t i = 0; i < words; i++)
		if (a[i] != b[i])
		{
			const u32 y = (u32)(i / (pitch_b / 4)), x = (u32)(i % (pitch_b / 4));
			CHECK(false, "%s: %ux%u %s pitch=%u addr=%06X: texel (%u,%u) old %08X new %08X", tag, d.w, d.h,
			      d.vq ? "vq" : d.twiddled ? "twiddled" : "planar", d.pitch, d.addr, x, y, a[i], b[i]);
			break;
		}
}

static void fill_random(u32 from, u32 bytes)
{
	for (u32 i = 0; i < bytes; i += 4)
		*(u32*)&g_vram[(from + i) & VRAM_MASK & ~3u] = rnd();
}

static TexFields random_fields(int kind, bool near_end)
{
	TexFields f = {};
	// Mostly small textures (the Soul Calibur tiles), some FMV sizes up to 1024.
	const u32 big = rnd(8) == 0;
	f.tex_u = big ? 5 + rnd(3) : rnd(6);
	f.tex_v = big ? 5 + rnd(3) : rnd(6);
	f.scan_order = kind == 0;
	f.vq_comp = kind == 2;
	f.stride_sel = kind == 0 && rnd(2);
	f.mipmapped = kind != 0 && rnd(3) == 0;
	f.text_control = rnd(32) | (rnd() & ~31u);
	f.tex_addr = rnd(1u << 21);
	if (near_end)
	{
		// Starts within 2 MB of the end of VRAM (or of its mirror), so some sources cross it.
		const u32 back = rnd(2u << 20) & ~7u;
		f.tex_addr = (((rnd(2) ? 0x1000000u : 0x800000u) - back) >> 3) & 0x1FFFFF;
	}
	return f;
}

int main(int argc, char** argv)
{
	const u32 cases = argc > 1 ? (u32)atoi(argv[1]) : 3000;
	g_vram = (u8*)aligned_alloc(4096, VRAM_SIZE);
	fill_random(0, VRAM_SIZE);
	yuvd_init(&g_t);

	// 1. The pair kernel on every U, V and Y: o0 takes Y0 = y, o1 takes Y1 = 255 - y.
	u64 kernel = 0;
	for (u32 u = 0; u < 256; u++)
		for (u32 v = 0; v < 256; v++)
			for (u32 y = 0; y < 256; y++)
			{
				const u32 y1 = 255 - y;
				u32 o[2], n0, n1;
				yuv_pair(u | y << 8, v | y1 << 8, o);
				yuvd_pair(&g_t, u | y << 8 | v << 16 | y1 << 24, &n0, &n1);
				// convert_pixels on the PPE: RGBA bytes -> A8R8G8B8.
				const u32 e0 = (o[0] << 24) | (o[0] >> 8), e1 = (o[1] << 24) | (o[1] >> 8);
				kernel++;
				CHECK(n0 == e0 && n1 == e1, "kernel u=%u v=%u y0=%u y1=%u: old %08X %08X new %08X %08X",
				      u, v, y, y1, e0, e1, n0, n1);
			}
	printf("kernel: %llu texel pairs (every U, V, Y0 and Y1)\n", (unsigned long long)kernel);

	// 2. Fixed textures: black, white, the chroma extremes, odd texels lit only, a gradient, black and lit words
	// in turn; all three layouts, all sizes 8..1024.
	Stats fixed = {};
	const u32 patterns[] = { 0x00800080u, 0xFF80FF80u, 0x00000000u, 0xFFFFFFFFu, 0x00FF00FFu, 0xFF00FF00u, 0x10801080u,
	                         0x10800080u };
	const u32 npat = sizeof(patterns) / sizeof(patterns[0]);
	for (int kind = 0; kind < 3; kind++)
		for (u32 su = 0; su < 8; su++)
			for (u32 p = 0; p < npat + 2; p++)
			{
				TexFields f = {};
				f.tex_u = su;
				f.tex_v = (su * 5 + p) % 8;
				f.scan_order = kind == 0;
				f.vq_comp = kind == 2;
				f.stride_sel = kind == 0 && (p & 1);
				f.text_control = 20;
				f.tex_addr = (0x100000u + p * 0x10000u) >> 3;
				TexDesc d;
				describe(f, &d);
				const u32 lo = d.vq ? d.vq_cb : d.addr, bytes = d.src_bytes + (d.vq ? 2048 + 64 : 0);
				for (u32 i = 0; i < bytes + 8; i += 4)
				{
					const u32 at = (lo + i) & VRAM_MASK & ~3u;
					u32 wv = p < npat ? patterns[p] : p == npat ? 0x80008000u | (i * 0x01010101u >> 3)
					       : (i & 4) ? 0x10801080u : 0x00800080u;
					*(u32*)&g_vram[at] = wv;
				}
				char tag[64];
				snprintf(tag, sizeof(tag), "fixed k%d s%u p%u", kind, su, p);
				run_case(f, &fixed, tag);
			}
	printf("fixed: %u textures, %u fast, %u fallback, %u black\n", fixed.runs, fixed.fast, fixed.fallback, fixed.black);
	CHECK(fixed.black > 0 && fixed.black < fixed.fast, "fixed: the patterns must give black and non-black textures");
	fill_random(0, VRAM_SIZE);

	// 3. Sources that end 8 bytes before, at, and 8 bytes past the end of VRAM (ASan sees any read past it).
	Stats edge = {};
	for (int kind = 0; kind < 3; kind++)
		for (u32 su = 0; su < 8; su++)
			for (u32 mip = 0; mip < 2; mip++)
				for (int delta = -8; delta <= 8; delta += 8)
				{
					TexFields f = {};
					f.tex_u = su;
					f.tex_v = (su + 3 * mip + 1) % 8;
					f.scan_order = kind == 0;
					f.vq_comp = kind == 2;
					f.stride_sel = kind == 0 && mip;
					f.mipmapped = kind != 0 && mip;
					f.text_control = 7;
					// At TexAddr 0 the source ends at addr + src_bytes; move the texture so that lands on the end.
					TexDesc d;
					describe(f, &d);
					const u32 end = d.addr + d.src_bytes;
					f.tex_addr = ((u32)(VRAM_SIZE - end + delta) & ~7u) >> 3;
					char tag[64];
					snprintf(tag, sizeof(tag), "edge k%d s%u m%u d%d", kind, su, mip, delta);
					run_case(f, &edge, tag);
				}
	printf("edge: %u textures, %u fast, %u fallback\n", edge.runs, edge.fast, edge.fallback);
	CHECK(edge.fast > 0 && edge.fallback > 0, "edge: both sides of the end of VRAM must be tested");

	// 4. Random textures over random VRAM, some at the end of VRAM; VRAM changes between textures.
	Stats rs = {};
	for (u32 i = 0; i < cases; i++)
	{
		const int kind = (int)rnd(3);
		const bool near_end = rnd(4) == 0;
		TexFields f = random_fields(kind, near_end);
		if (i % 64 == 0)
			fill_random(rnd(VRAM_SIZE), 1u << 20);
		char tag[64];
		snprintf(tag, sizeof(tag), "random %u", i);
		run_case(f, &rs, tag);
	}
	printf("random: %u textures (planar %u, twiddled %u, vq %u), %u fast (%.1fM texels), %u fallback (source past "
	       "the end of VRAM), %u black\n", rs.runs, rs.kinds[0], rs.kinds[1], rs.kinds[2], rs.fast, rs.texels / 1e6,
	       rs.fallback, rs.black);
	CHECK(rs.fallback > 0 && rs.fallback < rs.runs / 4, "random: the end-of-VRAM cases must take the fallback");

	if (g_fail)
	{
		printf("FAIL: %d mismatches\n", g_fail);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
