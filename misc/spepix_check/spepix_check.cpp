// 5.12 #4/#5 (WSL): spe_pix_job.h on SPU shims against the PPU code run.sh extracts: describe(), decode() and ps3gl's
// convert_pixels() word for word with decode()'s black/alpha-0 checks; fb_geom() and fb_convert_ppu().
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"
#include "spe_pix.h"

extern "C" {
extern uint64_t g_seed, g_cmds, g_elems;
extern int g_lazy;
void spu_regions_reset(void);
void spu_region(uint64_t ea, uint32_t size, uint8_t* p, int writable);
void spu_setup(uint32_t vram_ea, uint32_t pal_ea);
uint32_t spu_tex(const SpePixTex* b);
void spu_fb(const SpePixFb* b);
}

static void die(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FAIL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

// ---- what the extracted PPU code needs -----------------------------------

#define VRAM_SIZE 0x800000u
#define VRAM_MASK 0x7FFFFFu
static struct { u8* vram; } PVRPARAMS;
static u32* g_pal;
#define PALETTE_RAM g_pal
static struct { u32 pal_ram_ctrl, text_control; } g_rr;
static u64 s_hash_bytes;
static int yuv_conv_generation(u32, u32*) { return 0; }
static u32 s_black_tex, s_alpha0_tex;

typedef unsigned int GLenum;
#define GL_ALPHA 0x1906
#define GL_RGBA 0x1908
#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1
#define PS3GL_MAX_TEX_DIM 4096
static uint32_t g_swz_x[PS3GL_MAX_TEX_DIM], g_swz_y[PS3GL_MAX_TEX_DIM], g_swz_tbl_w, g_swz_tbl_h;
static int g_mipmap_enabled;

#define FB2D_W 768
#define FB2D_H 576
static u32 s_fb_pix[FB2D_W * FB2D_H];
static struct { u32 fb_r_size, fb_r_depth, fb_r_sof1; } s_snap;

#include "tex_twiddle.inc"
#include "tex_hash.inc"
#include "tex_decode.inc"
#include "gl_convert.inc"
#include "rend_vram.inc"
#include "rend_fb.inc"

// ---- fake main memory -------------------------------------------------------

enum : uint32_t { EA_VRAM = 0x10000000, EA_PAL = 0x20000000, EA_DST = 0x30000000 };
#define CANARY 0xC0FFEE11u
#define DST_WORDS (1024 * 1024)

static u8* g_vram;
static u8* g_palbuf;
static u32* g_dst;
static std::vector<u32> g_want;
static std::vector<u8> g_be;
static u32 g_pal_pad;

static uint64_t g_rs = 0x9E3779B97F4A7C15ull;
static u32 rnd32()
{
	g_rs ^= g_rs << 13;
	g_rs ^= g_rs >> 7;
	g_rs ^= g_rs << 17;
	return (u32)(g_rs >> 16);
}
static u32 rnd(u32 n) { return rnd32() % n; }

static void fill_words(u8* p, u32 bytes)
{
	for (u32 i = 0; i + 4 <= bytes; i += 4)
	{
		const u32 w = rnd32();
		memcpy(p + i, &w, 4);
	}
}

// Pure red texels in each 16-bit format (1555, 565, 4444 opaque and clear): lit only through the red channel.
static const u32 k_red[] = { 0xFC00FC00u, 0xF800F800u, 0xFF00FF00u, 0x0F000F00u, 0x7C007C00u };

// Words of one kind over [at, at + bytes) of VRAM: random, zero, all ones, one repeated word, or pure red.
static void paint(u32 at, u32 bytes)
{
	at &= ~3u;
	if (at >= VRAM_SIZE)
		return;
	if (at + bytes > VRAM_SIZE)
		bytes = VRAM_SIZE - at;
	const u32 kind = rnd(9), word = kind == 4 ? k_red[rnd(5)] : rnd32();
	for (u32 i = 0; i + 4 <= bytes; i += 4)
	{
		const u32 w = kind == 0 ? 0 : kind == 1 ? 0xFFFFFFFFu : kind == 2 || kind == 4 ? word : kind == 3 ? word & 0x7FFF7FFFu
		                                                                                 : rnd32();
		memcpy(g_vram + at + i, &w, 4);
	}
}

// Every palette entry pure red in one of the four PAL_RAM_CTRL formats.
static void paint_pal()
{
	static const u32 red[4] = { 0xFC00u, 0xF800u, 0x0F00u, 0x00FF0000u };
	const u32 ctrl = rnd(4);
	g_rr.pal_ram_ctrl = ctrl;
	for (u32 i = 0; i + 4 <= 4096 + 32; i += 4)
		memcpy(g_palbuf + i, &red[ctrl], 4);
}

static void layout(u32 dst_bytes)
{
	spu_regions_reset();
	spu_region(EA_VRAM, VRAM_SIZE, g_vram, 0);
	spu_region(EA_PAL, 4096 + 32, g_palbuf, 0);
	spu_region(EA_DST, dst_bytes, (u8*)g_dst, 1);
	spu_setup(EA_VRAM, EA_PAL + g_pal_pad);
	PVRPARAMS.vram = g_vram;
	g_pal = (u32*)(g_palbuf + g_pal_pad);
}

// ---- textures ---------------------------------------------------------------

struct Counts { u64 tex, texels, skip, swz, lin, black, alpha0, kind[8]; } g_n;

// One texture: the SPU's words in the layout ps3gl would give it against convert_pixels() of decode()'s result.
static void one_tex(u32 tsp_full, u32 tcw_full, bool igna, bool mirrored)
{
	TSP tsp;
	TCW tcw;
	tsp.full = tsp_full;
	tcw.full = tcw_full;
	TexDesc d;
	describe(tsp, tcw, &d);
	if (!spu_fits(d))
	{
		g_n.skip++;
		return;
	}
	// ps3gl's layout: YUV has the no-swizzle hint, a mirrored wrap is linear, the rest is swizzled (all DC sizes are
	// powers of two).
	const bool swz = d.fmt != 3 && !mirrored;
	const u32 pitch = tex_linear_pitch(d.w);
	const u32 bytes = swz ? d.w * d.h * 4 : pitch * d.h;
	if (bytes > DST_WORDS * 4)
		die("texture of %u bytes", bytes);

	const u32 black = s_black_tex, alpha0 = s_alpha0_tex;
	decode(d, igna);
	const u32 want_res = (s_black_tex == black ? SPEPIX_LIT : 0) | (s_alpha0_tex == alpha0 ? SPEPIX_OPAQUE : 0);
	// convert_pixels reads s_out's bytes as the big-endian PPE stores them.
	g_be.resize(d.w * d.h * 4);
	for (u32 i = 0; i < d.w * d.h; i++)
	{
		const u32 v = s_out[i];
		g_be[4 * i] = (u8)(v >> 24);
		g_be[4 * i + 1] = (u8)(v >> 16);
		g_be[4 * i + 2] = (u8)(v >> 8);
		g_be[4 * i + 3] = (u8)v;
	}
	g_want.assign(bytes / 4, CANARY);
	if (swz)
		swz_ensure_tables(d.w, d.h);
	convert_pixels((uint8_t*)g_want.data(), pitch, g_be.data(), d.w, d.h, 4, GL_RGBA, swz);

	for (u32 i = 0; i < bytes / 4; i++)
		g_dst[i] = CANARY;
	layout(bytes);
	SpePixTex j;
	memset(&j, 0, sizeof(j));
	spu_job(d, igna, EA_DST, pitch, swz, g_rr.pal_ram_ctrl, &j);
	const u32 res = spu_tex(&j);
	for (u32 i = 0; i < bytes / 4; i++)
		if (g_dst[i] != g_want[i])
			die("texture fmt %u %ux%u %s%s%s%s%s pitch %u addr %06X cb %06X igna %u pal %u/%u: word %u (row %u col %u)"
			    " spu %08X want %08X",
			    d.fmt, d.w, d.h, swz ? "swizzled" : "linear", d.twiddled ? " twiddled" : " planar", d.vq ? " vq" : "",
			    d.pal4 ? " pal4" : "", d.pal8 ? " pal8" : "", d.pitch, d.addr, d.vq_cb, igna, d.pal_base,
			    g_rr.pal_ram_ctrl & 3, i, swz ? 0 : i / (pitch / 4), swz ? i : i % (pitch / 4), g_dst[i], g_want[i]);
	if (res != want_res)
		die("texture fmt %u %ux%u: result bits spu %u want %u", d.fmt, d.w, d.h, res, want_res);
	g_n.tex++;
	g_n.texels += d.w * d.h;
	g_n.swz += swz;
	g_n.lin += !swz;
	g_n.black += !(want_res & SPEPIX_LIT);
	g_n.alpha0 += !(want_res & SPEPIX_OPAQUE);
	g_n.kind[d.pal4 ? 0 : d.pal8 ? 1 : d.vq ? 2 : !d.twiddled ? 3 : d.fmt == 3 ? 4 : 5]++;
}

// TCW bits: 31 MipMapped, 30 VQ_Comp, 27-29 PixelFmt, 26 ScanOrder, 25 StrideSel, 21-26 PalSelect (palette formats),
// 0-20 TexAddr. TSP bits: 3-5 TexU, 0-2 TexV.
static u32 make_tcw(u32 fmt, bool vq, bool mip, bool planar, bool stride, u32 addr8)
{
	u32 tcw = (fmt << 27) | (addr8 & 0x1FFFFF);
	if (fmt == 5 || fmt == 6)
		return tcw | (rnd(64) << 21) | (vq ? 1u << 30 : 0) | (mip ? 1u << 31 : 0);
	return tcw | (vq ? 1u << 30 : 0) | (mip ? 1u << 31 : 0) | (planar ? 1u << 26 : 0) | (stride ? 1u << 25 : 0);
}

static u32 pick_size()
{
	const u32 r = rnd(100);
	return r < 60 ? rnd(5) : r < 95 ? rnd(7) : rnd(8);
}

static u32 pick_addr8(u32 span)
{
	switch (rnd(8))
	{
	case 0: return (VRAM_SIZE - span) / 8 - rnd(4);           // the source ends at or near the end of VRAM
	case 1: return (VRAM_SIZE - span + rnd(64)) / 8;          // past the end: the PPU decodes it
	case 2: return rnd(64);
	default: return rnd(VRAM_SIZE / 8);
	}
}

static void random_tex()
{
	static const u32 fmts[] = { 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7 };
	u32 fmt = fmts[rnd(sizeof(fmts) / sizeof(fmts[0]))];
	u32 u = pick_size(), v = pick_size();
	bool planar = rnd(4) == 0, stride = rnd(3) == 0, vq = rnd(3) == 0, mip = rnd(5) < 2;
	g_rr.text_control = rnd32();
	g_rr.pal_ram_ctrl = rnd32();
	// An FMV texture: YUV rows, fewer texels per row than the width (stride), as the Sofdec ones are.
	if (rnd(8) == 0)
	{
		fmt = 3;
		planar = stride = true;
		vq = false;
		u = 3 + rnd(5);
		v = rnd(8);
		const u32 w = 8u << u, pitch = 32 * (1 + rnd(w / 32 - 1));
		g_rr.text_control = (g_rr.text_control & ~31u) | (pitch / 32);
	}
	const u32 span = (8u << u) * (8u << v) * 2 + 4096;
	const u32 tcw = make_tcw(fmt, vq, mip, planar, stride, pick_addr8(span));
	const u32 tsp = (rnd32() & ~63u) | (u << 3) | v;
	TSP t;
	TCW c;
	t.full = tsp;
	c.full = tcw;
	TexDesc d;
	describe(t, c, &d);
	// Now and then a source of one kind of word, so all-black and all-transparent textures come up.
	if (rnd(4) == 0)
	{
		paint(d.addr, d.twiddled ? d.src_bytes : (d.h * d.pitch * 2));
		if (d.vq)
			paint(d.vq_cb, 2048);
	}
	if (rnd(6) == 0)
		fill_words(g_palbuf, 4096 + 32);
	if (rnd(12) == 0)
		memset(g_palbuf, rnd(2) ? 0 : 0xFF, 4096 + 32);
	if (rnd(12) == 0)
		paint_pal();
	g_pal_pad = rnd(4) * 4;
	layout(4);
	one_tex(tsp, tcw, rnd(3) == 0, rnd(5) == 0);
}

// Every kind and layout at the small sizes and both source alignments, plus mip offsets.
static void sweep_tex()
{
	for (u32 fmt = 0; fmt < 8; fmt++)
		for (u32 u = 0; u < 4; u++)
			for (u32 v = 0; v < 4; v++)
				for (u32 k = 0; k < 16; k++)
				{
					const bool vq = k & 1, mip = (k >> 1) & 1, planar = (k >> 2) & 1, mirrored = (k >> 3) & 1;
					const u32 addr8 = rnd(VRAM_SIZE / 16) * 2 + rnd(2);   // 16-byte aligned, or 8 off
					g_rr.text_control = rnd32();
					g_rr.pal_ram_ctrl = rnd(4);
					g_pal_pad = rnd(4) * 4;
					layout(4);
					one_tex((u << 3) | v, make_tcw(fmt, vq, mip, planar, rnd(2), addr8), rnd(2), mirrored);
				}
}

// ---- framebuffer --------------------------------------------------------------

static u64 g_fb, g_fb_px, g_fb_skip, g_fb_split;

static void one_fb()
{
	// x_size counts 32-bit words per line less one; 319 is a 640-pixel 16-bit line.
	static const u32 xs[] = { 319, 639, 383, 255, 159 };
	static const u32 ys[] = { 479, 239, 575, 223 };
	const u32 x_size = rnd(3) ? xs[rnd(5)] : rnd(1024), y_size = rnd(3) ? ys[rnd(4)] : rnd(1024);
	const u32 modulus = rnd(3) ? 1 : rnd(1024);
	s_snap.fb_r_size = x_size | (y_size << 10) | (modulus << 20);
	s_snap.fb_r_depth = rnd(4);
	u32 sof1;
	switch (rnd(6))
	{
	case 0: sof1 = 0x400000u - rnd(8192); break;   // lines cross into bank 1
	case 1: sof1 = 0x800000u - rnd(8192); break;   // lines wrap to 0
	case 2: sof1 = rnd(64); break;
	default: sof1 = rnd32();
	}
	s_snap.fb_r_sof1 = sof1;
	FbGeom fg;
	if (!fb_geom(&fg))
	{
		g_fb_skip++;
		return;
	}
	fb_convert_ppu(fg);
	const u32 rows = FB2D_H, dst_bytes = FB2D_W * 4 * rows;
	for (u32 i = 0; i < dst_bytes / 4; i++)
		g_dst[i] = CANARY;
	layout(dst_bytes);
	SpePixFb j;
	memset(&j, 0, sizeof(j));
	fb_job(fg, EA_DST, &j);
	spu_fb(&j);
	const u32 wq = ((fg.cpy_w * 4 + 15) & ~15u) / 4;
	for (u32 y = 0; y < rows; y++)
		for (u32 x = 0; x < FB2D_W; x++)
		{
			const u32 got = g_dst[y * FB2D_W + x];
			if (y < fg.cpy_h && x < fg.cpy_w)
			{
				const u32 p = s_fb_pix[y * fg.cpy_w + x], want = (p >> 8) | (p << 24);
				if (got != want)
					die("framebuffer depth %u %ux%u sof1 %08X line %u: pixel %u of row %u spu %08X want %08X", fg.depth,
					    fg.cpy_w, fg.cpy_h, fg.sof1, fg.line_bytes, x, y, got, want);
			}
			else if ((y >= fg.cpy_h || x >= wq) && got != CANARY)
				die("framebuffer depth %u %ux%u: word %u of row %u written outside the frame", fg.depth, fg.cpy_w, fg.cpy_h,
				    x, y);
		}
	// A line that crosses a 4 MB bank edge comes in two runs.
	const u32 bpp = fg.depth == 3 ? 4 : 2;
	for (u32 y = 0; y < fg.cpy_h; y++)
	{
		const u32 a = (fg.sof1 + y * fg.line_bytes) & VRAM_MASK;
		if ((a & 0x3FFFFF) + fg.cpy_w * bpp > 0x400000)
		{
			g_fb_split++;
			break;
		}
	}
	g_fb++;
	g_fb_px += fg.cpy_w * fg.cpy_h;
}

int main(int argc, char** argv)
{
	const u32 ntex = argc > 1 ? (u32)atoi(argv[1]) : 3000, nfb = argc > 2 ? (u32)atoi(argv[2]) : 600;
	g_vram = (u8*)aligned_alloc(65536, VRAM_SIZE);
	g_palbuf = (u8*)aligned_alloc(128, 4096 + 128);
	g_dst = (u32*)aligned_alloc(128, DST_WORDS * 4);
	fill_words(g_vram, VRAM_SIZE);
	fill_words(g_palbuf, 4096 + 32);
	layout(4);

	for (u32 pass = 0; pass < 2; pass++)
	{
		g_seed = pass + 1;
		g_lazy = (int)pass;
		sweep_tex();
	}
	printf("sweep: %llu textures (%llu left to the PPU)\n", (unsigned long long)g_n.tex, (unsigned long long)g_n.skip);
	for (u32 i = 0; i < ntex; i++)
	{
		g_seed = i + 100;
		g_lazy = i & 1;
		if (rnd(50) == 0)
			fill_words(g_vram, VRAM_SIZE);
		random_tex();
	}
	printf("textures: %llu checked, %llu texels, %llu swizzled, %llu linear, %llu left to the PPU | pal4 %llu pal8 %llu"
	       " vq %llu planar %llu yuv-tw %llu tw16 %llu | black %llu alpha0 %llu\n",
	       (unsigned long long)g_n.tex, (unsigned long long)g_n.texels, (unsigned long long)g_n.swz,
	       (unsigned long long)g_n.lin, (unsigned long long)g_n.skip, (unsigned long long)g_n.kind[0],
	       (unsigned long long)g_n.kind[1], (unsigned long long)g_n.kind[2], (unsigned long long)g_n.kind[3],
	       (unsigned long long)g_n.kind[4], (unsigned long long)g_n.kind[5], (unsigned long long)g_n.black,
	       (unsigned long long)g_n.alpha0);
	for (u32 i = 0; i < nfb; i++)
	{
		g_seed = i + 7;
		g_lazy = i & 1;
		if (rnd(20) == 0)
			fill_words(g_vram, VRAM_SIZE);
		one_fb();
	}
	printf("framebuffers: %llu checked (%llu pixels, %llu with a line across a bank edge), %llu not handled\n",
	       (unsigned long long)g_fb, (unsigned long long)g_fb_px, (unsigned long long)g_fb_split,
	       (unsigned long long)g_fb_skip);
	printf("DMA: %llu commands, %llu list elements\n", (unsigned long long)g_cmds, (unsigned long long)g_elems);
	printf("PASS\n");
	return 0;
}
