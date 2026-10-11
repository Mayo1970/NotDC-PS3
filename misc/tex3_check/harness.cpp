// Bundle 3 PC check (WSL): the real ps3_tex.cpp, ps3gl_textures.c and SPU decode (misc/spepix_check's shims). trace:
// every option 0 against the frozen files (TEX_REF). model: each bind against Flycast's rules, texel by texel.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <map>
#include <vector>
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"
#include "plugs/drkPvr/drkPvr.h"
#include "plugs/drkPvr/regs.h"
#include "dc/pvr/pvr_if.h"
#include "ps3_rend_regs.h"
#include "ps3_spetex.h"
#include "ps3_spepix.h"
extern "C" {
#include "ps3gl.h"
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
	fflush(stdout);
	exit(1);
}

// ---- the code under test ------------------------------------------------------

void ps3tex_frame_begin(const DcParam* params, u32 nparam);
#ifdef TEX_REF
bool ps3tex_bind(TSP tsp, TCW tcw, u32 pidx);
#else
bool ps3tex_bind(TSP tsp, TCW tcw, u32 pidx, bool punch_through);
#endif
void ps3tex_reset();
void ps3tex_stats(u32* decodes, u32* live);

PvrParamsStub PVRPARAMS;
u32* g_pal;
RenderRegs g_rr;
extern "C" {
volatile unsigned g_rend_phase, g_rend_phase_seq, g_sh4_phase, g_sh4_phase_seq;
int yuv_conv_generation(u32, u32*) { return 0; }
}

static int o_mips, o_aniso, o_filter, o_vqfmt, o_slot = 1, o_yuvblk, o_bump = 1;
extern "C" {
int get_tex_slot_hash_preset() { return o_slot; }
int get_yuv_block_preset() { return o_yuvblk; }
int get_bump_map_preset() { return o_bump; }
int get_mipmap_preset() { return o_mips; }
int get_aniso_preset() { return o_aniso; }
int get_tex_filter_preset() { return o_filter; }
int get_vq_formats_preset() { return o_vqfmt; }
}

// ---- random numbers and the trace -----------------------------------------------

static u64 g_rs = 0x9E3779B97F4A7C15ull;
static u32 rnd32()
{
	g_rs ^= g_rs << 13;
	g_rs ^= g_rs >> 7;
	g_rs ^= g_rs << 17;
	return (u32)(g_rs >> 16);
}
static u32 rnd(u32 n) { return rnd32() % n; }

static u64 g_fold = 1469598103934665603ull;
static void fold(u64 v) { g_fold = (g_fold ^ v) * 1099511628211ull; }

// ---- RSX memory: an arena under 4 GB (the code keeps its addresses in 32 bits), guards around each block -------

#define ARENA_BYTES (1u << 30)
#define GUARD 128u
#define FRESH 0xCDCDCDCDu

struct Blk { u32 off, size; bool live; };
static u8* g_arena;
static u32 g_arena_top;
static std::vector<Blk> g_blk;
static std::map<u32, u32> g_blk_at;                  // payload offset -> g_blk index
static std::map<u32, std::vector<u32>> g_blk_free;   // payload size -> free g_blk indices

static void blk_guards(const Blk& b, const char* when)
{
	const u8* p = g_arena + b.off;
	for (u32 i = 0; i < GUARD; i++)
		if (p[-(int)GUARD + (int)i] != 0xA5 || p[b.size + i] != 0xA5)
			die("%s: a write outside RSX block at %08X (%u bytes), guard byte %u", when, b.off, b.size, i);
}

static const Blk& blk_of(const void* ptr)
{
	const auto it = g_blk_at.find((u32)((const u8*)ptr - g_arena));
	if (it == g_blk_at.end())
		die("pointer %p is not an RSX block", ptr);
	return g_blk[it->second];
}

static void all_guards(const char* when)
{
	for (const Blk& b : g_blk)
		if (b.live)
			blk_guards(b, when);
}

extern "C" void* rsxMemalign(uint32_t alignment, uint32_t size)
{
	const u32 sz = (size + 127) & ~127u;
	u32 idx;
	auto f = g_blk_free.find(sz);
	if (f != g_blk_free.end() && !f->second.empty())
	{
		idx = f->second.back();
		f->second.pop_back();
	}
	else
	{
		if ((u64)g_arena_top + sz + 2 * GUARD > ARENA_BYTES)
			die("RSX arena full (%u bytes asked)", size);
		idx = (u32)g_blk.size();
		g_blk.push_back({ g_arena_top + GUARD, sz, false });
		g_blk_at[g_arena_top + GUARD] = idx;
		g_arena_top += sz + 2 * GUARD;
	}
	Blk& b = g_blk[idx];
	b.live = true;
	u8* p = g_arena + b.off;
	memset(p - GUARD, 0xA5, GUARD);
	memset(p + sz, 0xA5, GUARD);
	for (u32 i = 0; i < sz; i += 4)
		*(u32*)(p + i) = FRESH;
	if ((uintptr_t)p % alignment)
		die("rsxMemalign alignment %u", alignment);
	fold(0xA110C000u + size);
	return p;
}

extern "C" void rsxFree(void* ptr)
{
	Blk& b = const_cast<Blk&>(blk_of(ptr));
	if (!b.live)
		die("rsxFree of a free block at %08X", b.off);
	blk_guards(b, "rsxFree");
	b.live = false;
	g_blk_free[b.size].push_back((u32)(&b - g_blk.data()));
	fold(0xF4EE0000u + b.size);
}

extern "C" int32_t rsxAddressToOffset(const void* ptr, uint32_t* offset)
{
	*offset = (u32)((const u8*)ptr - g_arena);
	return 0;
}

// ---- ps3gl around ps3gl_textures.c --------------------------------------------

static struct
{
	gcmTexture tex;
	u32 enable, minlod, maxlod, aniso, bias, min, mag, ws, wt, loads, invalidates;
} g_samp;

extern "C" {
static ps3gl_state_t g_state;
ps3gl_state_t* ps3gl_ptr = &g_state;
uint32_t ps3gl_frame_tex_alloc_count, ps3gl_frame_subimage_count, ps3gl_frame_subimage_pixels;
uint32_t ps3gl_frame_defer_overflow_count, ps3gl_frame_mip_uploads, ps3gl_frame_mipped_binds, ps3gl_frame_tex_binds;
static gcmContextData g_ctx;
gcmContextData* ps3gl_get_ctx(void) { return &g_ctx; }
void ps3gl_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	die("ps3gl logged a fault");
}
void rsxInvalidateTextureCache(gcmContextData*, uint32_t) { g_samp.invalidates++; }
void rsxLoadTexture(gcmContextData*, uint8_t index, const gcmTexture* t)
{
	if (index == 0)
	{
		g_samp.tex = *t;
		g_samp.loads++;
	}
}
void rsxTextureControl(gcmContextData*, uint8_t index, uint32_t enable, uint16_t minlod, uint16_t maxlod, uint8_t aniso)
{
	if (index == 0)
	{
		g_samp.enable = enable;
		g_samp.minlod = minlod;
		g_samp.maxlod = maxlod;
		g_samp.aniso = aniso;
	}
}
void rsxTextureFilter(gcmContextData*, uint8_t index, uint16_t bias, uint8_t min, uint8_t mag, uint8_t)
{
	if (index == 0)
	{
		g_samp.bias = bias;
		g_samp.min = min;
		g_samp.mag = mag;
	}
}
void rsxTextureWrapMode(gcmContextData*, uint8_t index, uint8_t ws, uint8_t wt, uint8_t, uint8_t, uint8_t, uint8_t)
{
	if (index == 0)
	{
		g_samp.ws = ws;
		g_samp.wt = wt;
	}
}
}

// The PPE stores decode()'s RGBA words as the bytes R, G, B, A, which convert_pixels() reads; x86 does not.
static std::vector<u32> g_be;
static const void* be_words(const void* words, u32 n)
{
	g_be.resize(n);
	for (u32 i = 0; i < n; i++)
		g_be[i] = __builtin_bswap32(((const u32*)words)[i]);
	return g_be.data();
}

static struct { u64 img[2], spu_jobs, spu_lower, spu_fail, skips, hash_spu, hash_checks, pix_checks; } g_n;

extern "C" {
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void ps3gl_tex_convert_check(void* dst, GLsizei width, GLsizei height, const void* rgba);
// ps3_tex.cpp is built with glTexImage2D and ps3gl_tex_convert_check renamed to these.
void tex3_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum format,
                       GLenum type, const void* pixels)
{
	g_n.img[level != 0]++;
	fold(0x1A6E0000u + ((u64)level << 32) + ((u64)w << 16) + h);
	glTexImage2D(target, level, ifmt, w, h, border, format, type, be_words(pixels, (u32)(w * h)));
}
void tex3_convert_check(void* dst, GLsizei w, GLsizei h, const void* rgba)
{
	ps3gl_tex_convert_check(dst, w, h, be_words(rgba, (u32)(w * h)));
}
}

// ---- the SPUs ------------------------------------------------------------------

extern "C" {
extern uint64_t g_seed, g_cmds, g_elems;
extern int g_lazy;
void spu_regions_reset(void);
void spu_region(uint64_t ea, uint32_t size, uint8_t* p, int writable);
void spu_setup(uint32_t vram_ea, uint32_t pal_ea);
uint32_t spu_tex(const SpePixTex* b);
}

enum : u32 { EA_VRAM = 0x90000000u, EA_PAL = 0xA0000000u };
static u8* g_vram;
static u8* g_palbuf;
static u32 g_pal_pad;
static int g_spetex_mode, g_spepix_mode;
static u32 g_fail_in;       // the SPU decode job that comes late, counted down; 0 = none
static u32* g_chk[2];

// The SPU hash is spe_tex_hash.h, which misc/spetex_check proves equal to the frozen file's describe() + hash_texture().
namespace spuref {
static u64 s_hash_bytes;
#include "ref_hash.inc"
}

static const DcParam* g_req;
static u32 g_req_n;

int ps3spetex_mode() { return g_spetex_mode; }
void ps3spetex_request(const void* params, u32 nparam, u32, u32, u32, u32 text_control, u32 pal_ram_ctrl)
{
	g_req = (const DcParam*)params;
	g_req_n = nparam;
	if (text_control != g_rr.text_control || pal_ram_ctrl != g_rr.pal_ram_ctrl)
		die("the hash request does not carry the frame's registers");
}
bool ps3spetex_hash(u32 idx, u32* h)
{
	if (idx >= g_req_n)
		return false;
	const DcParam& p = g_req[idx];
	if (!p.pcw.Texture || p.tcw.NO_PAL.PixelFmt == 3)
		return false;
	spuref::TexDesc d;
	spuref::describe(p.tsp, p.tcw, &d);
	*h = spuref::hash_texture(d);
	g_n.hash_spu++;
	return true;
}
void ps3spetex_check(u32 idx, u32 tsp, u32 tcw, u32 spu, u32 ppu)
{
	if (spu != ppu)
		die("SPE Tex Hash CHECK: slot %u tsp %08X tcw %08X spu %08X ppu %08X", idx, tsp, tcw, spu, ppu);
	g_n.hash_checks++;
}

int ps3spepix_tex_mode() { return g_spepix_mode; }
bool ps3spepix_tex(const SpePixTex* j, u32* res)
{
	// A late job: the real caller turns the SPU off.
	if (g_fail_in && --g_fail_in == 0)
	{
		g_spepix_mode = SPEPIX_OFF;
		g_n.spu_fail++;
		fold(0xFA110000u);
		return false;
	}
	if (j->w < 8 || j->h < 8)
		die("SPU decode job of %ux%u", j->w, j->h);
	spu_regions_reset();
	spu_region(EA_VRAM, VRAM_SIZE, g_vram, 0);
	spu_region(EA_PAL, 4096 + 32, g_palbuf, 0);
	spu_region((u32)(uintptr_t)g_arena, ARENA_BYTES, g_arena, 1);
	spu_setup(EA_VRAM, EA_PAL + g_pal_pad);
	*res = spu_tex(j);
	g_n.spu_jobs++;
	// A lower level written straight into the texture (not CHECK's copy): ps3gl counts it as a mip upload.
	if (g_spepix_mode == SPEPIX_ON && ps3gl.tmu[0].bound && j->w < ps3gl.tmu[0].bound->width)
		g_n.spu_lower++;
	fold(j->fmt);
	fold(((u64)j->w << 32) | j->h);
	fold(((u64)j->addr << 32) | j->pitch);
	fold(((u64)j->flags << 32) | j->pal_base);
	if (j->flags & SPEPIX_VQ)
		fold(j->vq_cb);
	fold(((u64)j->pal_ctrl << 32) | j->dst_pitch);
	fold(j->dst_ea - (u32)(uintptr_t)g_arena);
	fold(*res);
	return true;
}
void ps3spepix_tex_skip()
{
	g_n.skips++;
	fold(0x5C1F0000u);
}
u32* ps3spepix_tex_check_buf(int ppu) { return g_chk[ppu ? 1 : 0]; }
void ps3spepix_tex_check(bool ok, const char* what)
{
	if (!ok)
		die("SPE Tex Decode CHECK: %s", what);
	g_n.pix_checks++;
}

// ---- the model: Flycast's rules (core/rend/TexCache.cpp, texconv.cpp), one texel at a time ---------------------

// VQMipPoint less the codebook, and OtherMipPoint.
static const u32 M_VQ[11] = { 0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556 };
static const u32 M_OTHER[11] = { 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558 };

static u32 m8(u32 a) { return g_vram[a & VRAM_MASK]; }
static u32 m16(u32 a) { return m8(a) | (m8(a + 1) << 8); }

// Offset of (x, y) in a w x h Morton image: the bits interleave while both sizes last. The DC puts y first, the RSX x.
static u32 morton(u32 x, u32 y, u32 w, u32 h, bool yfirst)
{
	u32 r = 0, s = 0;
	for (u32 b = 0; (1u << b) < w || (1u << b) < h; b++)
	{
		const bool hx = (1u << b) < w, hy = (1u << b) < h;
		if (yfirst && hy)
			r |= ((y >> b) & 1) << s++;
		if (hx)
			r |= ((x >> b) & 1) << s++;
		if (!yfirst && hy)
			r |= ((y >> b) & 1) << s++;
	}
	return r;
}

struct MTex
{
	u32 fmt, w, h, base, lvl, stride, pal_base, bits;
	bool vq, planar, pal, mip;
};

static MTex mdesc(TSP tsp, TCW tcw)
{
	MTex m;
	m.fmt = tcw.NO_PAL.PixelFmt;
	m.pal = m.fmt == 5 || m.fmt == 6;
	m.bits = m.fmt == 5 ? 4 : 8;
	bool scan = tcw.NO_PAL.ScanOrder;
	const bool vqbit = tcw.NO_PAL.VQ_Comp, mipbit = tcw.NO_PAL.MipMapped;
	if (o_vqfmt)
	{
		// Flycast: "Star Wars Demolition": a mipmapped VQ texture is never in scan order.
		m.vq = vqbit;
		if (vqbit && mipbit)
			scan = false;
		m.planar = scan && !m.pal;
	}
	else
	{
		// The old path: no VQ on a palette texture, and VQ is always twiddled.
		m.vq = vqbit && !m.pal;
		m.planar = scan && !m.pal && !m.vq;
	}
	m.mip = mipbit && !m.planar;
	m.w = 8u << tsp.TexU;
	m.h = m.mip ? m.w : 8u << tsp.TexV;
	m.lvl = tsp.TexU + 3;
	m.base = (tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
	m.stride = m.w;
	if (m.planar && tcw.NO_PAL.StrideSel && (g_rr.text_control & 31))
		m.stride = (g_rr.text_control & 31) * 32;
	m.pal_base = m.fmt == 5 ? tcw.PAL.PalSelect << 4 : (tcw.PAL.PalSelect >> 4) << 8;
	return m;
}

// The 16-bit word, or the palette index, of texel (x, y) of a w x h image whose data starts at a.
static u32 mraw(const MTex& m, u32 a, u32 w, u32 h, u32 x, u32 y)
{
	const u32 cb = m.base;
	if (m.planar)
	{
		// texture_PLVQ: one index byte per 4 texels of a row.
		if (m.vq)
			return m16(cb + m8(a + y * (m.stride / 4) + x / 4) * 8 + (x & 3) * 2);
		return m16(a + (y * m.stride + x) * 2);
	}
	if (m.vq && m.fmt == 5)
	{
		// ConvertTwiddlePal4 under texture_VQ: one index byte per 4x4 block, 16 nibbles in twiddled order.
		const u32 e = cb + m8(a + morton(x & ~3u, y & ~3u, w, h, true) / 16) * 8, n = morton(x & 3, y & 3, 4, 4, true);
		return (m8(e + n / 2) >> ((n & 1) * 4)) & 15;
	}
	if (m.vq && m.fmt == 6)
	{
		// ConvertTwiddlePal8: one index byte per 2x4 block.
		const u32 e = cb + m8(a + morton(x & ~1u, y & ~3u, w, h, true) / 8) * 8;
		return m8(e + morton(x & 1, y & 3, 2, 4, true));
	}
	if (m.vq)
		return m16(cb + m8(a + morton(x & ~1u, y & ~1u, w, h, true) / 4) * 8 + ((x & 1) * 2 + (y & 1)) * 2);
	const u32 t = morton(x, y, w, h, true);
	if (m.fmt == 5)
		return (m8(a + t / 2) >> ((t & 1) * 4)) & 15;
	if (m.fmt == 6)
		return m8(a + t);
	return m16(a + t * 2);
}

static u32 mrgba(u32 r, u32 g, u32 b, u32 a) { return (r << 24) | (g << 16) | (b << 8) | a; }
static u32 e5(u32 v) { return (v << 3) | (v >> 2); }
static u32 e6(u32 v) { return (v << 2) | (v >> 4); }
static u32 e4(u32 v) { return v * 17; }

static u32 mc16(u32 fmt, u32 p)
{
	switch (fmt)
	{
	case 1: return mrgba(e5(p >> 11), e6((p >> 5) & 63), e5(p & 31), 255);
	case 2: return mrgba(e4((p >> 8) & 15), e4((p >> 4) & 15), e4(p & 15), e4(p >> 12));
	case 4: return 0xFFFFFFFFu;   // a bump map with Bump Maps OFF
	default: return mrgba(e5((p >> 10) & 31), e5((p >> 5) & 31), e5(p & 31), (p & 0x8000) ? 255 : 0);
	}
}

static u32 mpal(u32 index)
{
	const u32 c = g_pal[index & 1023];
	switch (g_rr.pal_ram_ctrl & 3)
	{
	case 0: return mc16(0, c & 0xFFFF);
	case 1: return mc16(1, c & 0xFFFF);
	case 2: return mc16(2, c & 0xFFFF);
	default: return mrgba((c >> 16) & 255, (c >> 8) & 255, c & 255, c >> 24);
	}
}

static u32 mclamp(s32 v) { return v < 0 ? 0 : (v > 255 ? 255 : (u32)v); }

// ps3_tex.cpp yuv_pair(): p0 holds U, p1 holds V, luma in the high byte; second: the right texel of the pair.
static u32 myuv(u32 p0, u32 p1, bool second)
{
	const s32 u = (s32)(p0 & 255) - 128, v = (s32)(p1 & 255) - 128, y = (s32)((second ? p1 : p0) >> 8);
	return mrgba(mclamp(y + v * 11 / 8), mclamp(y - (u * 11 + v * 22) / 32), mclamp(y + u * 110 / 64), 255);
}

// Texel (x, y) of level `level` (0 = the full size) as decode() stores it, RGBA.
static u32 mtexel(const MTex& m, u32 level, u32 x, u32 y, bool igna)
{
	u32 fmt = m.fmt, w = m.w, h = m.h, a = m.base;
	if (m.mip)
	{
		const u32 i = m.lvl - level;
		w = h = 1u << i;
		if (m.vq)
		{
			a = m.base + 2048 + M_VQ[i];
			if (i == 0)
			{
				// Update(): the 1x1 level is texel (1,1) of a 2x2 block; YUV takes the block of the 2x2 level.
				if (fmt == 3)
					a = m.base + 2048 + M_VQ[1];
				w = h = 2;
				x = y = 1;
			}
		}
		else if (m.pal)
			a = m.base + M_OTHER[i] * m.bits / 8;
		else
		{
			a = m.base + M_OTHER[i] * 2;
			// Update(): the 1x1 level of a YUV texture goes through the 565 converter.
			if (i == 0 && fmt == 3)
				fmt = 1;
		}
	}
	else if (m.vq)
		a = m.base + 2048;
	// 3j: a bump texel is raw 4444 and keeps its alpha.
	if (fmt == 4 && o_bump)
	{
		fmt = 2;
		igna = false;
	}
	u32 c;
	if (m.planar && x >= (m.stride < w ? m.stride : w))
		c = 0;   // past the row's texels: decode() stores 0
	else if (m.pal)
		c = mpal(m.pal_base + mraw(m, a, w, h, x, y));
	else if (fmt == 3)
		c = myuv(mraw(m, a, w, h, x & ~1u, y), mraw(m, a, w, h, x | 1, y), x & 1);
	else
		c = mc16(fmt, mraw(m, a, w, h, x, y));
	if (igna && fmt != 3)
		c |= 0xFF;
	return c;
}

// ---- one bind against the model -------------------------------------------------

struct Def { TSP tsp; TCW tcw; };

static struct
{
	u64 binds, texels, chains, levels, lin, swz, palvq, plvq, pt_chain, mirror_mip, yuv_chain, vq_chain, pal_chain;
	u64 aniso, nearest, trilinear;
} g_v;

static const u32 k_gcm_aniso[5] = { GCM_TEXTURE_MAX_ANISO_1, GCM_TEXTURE_MAX_ANISO_2, GCM_TEXTURE_MAX_ANISO_4,
	                                GCM_TEXTURE_MAX_ANISO_8, GCM_TEXTURE_MAX_ANISO_16 };

static u32 log2u(u32 v)
{
	u32 l = 0;
	while ((1u << l) < v)
		l++;
	return l;
}

// What a level holds when *stale is its words from an earlier decode (the lower levels are not in the hash).
static void verify(const Def& d, bool pt, const std::vector<u32>* stale = 0, u32 stale_level = 0)
{
	ps3gl_texture_t* const t = ps3gl.tmu[0].bound;
	if (!t || !t->data)
		die("no texture bound after ps3tex_bind");
	const MTex m = mdesc(d.tsp, d.tcw);
	const bool mirror_u = !d.tsp.ClampU && d.tsp.FlipU, mirror_v = !d.tsp.ClampV && d.tsp.FlipV;
	const bool mirror = mirror_u || mirror_v;
	const bool chain = o_mips && m.mip && !mirror;
	// ps3gl: a mirrored wrap is linear; so is YUV (the no-swizzle hint) unless it has a chain.
	const bool swz = !mirror && !(m.fmt == 3 && !chain);
	const u32 levels = chain ? m.lvl + 1 : 1;
	const u32 pitch = (m.w * 4 + 63) & ~63u;
	char id[160];
	snprintf(id, sizeof(id), "tsp %08X tcw %08X (fmt %u %ux%u%s%s%s%s) mips %d vqfmt %d bump %d pix %d", d.tsp.full,
	         d.tcw.full, m.fmt, m.w, m.h, m.vq ? " vq" : "", m.planar ? " planar" : "", m.mip ? " mip" : "",
	         mirror ? " mirror" : "", o_mips, o_vqfmt, o_bump, g_spepix_mode);

	if (t->width != m.w || t->height != m.h)
		die("%s: size %ux%u", id, t->width, t->height);
	if ((t->swizzled != 0) != swz)
		die("%s: swizzled %u, want %u", id, t->swizzled, swz);
	if (t->num_levels != levels || (t->alloc_levels ? t->alloc_levels : 1u) != levels)
		die("%s: levels %u (memory for %u), want %u", id, t->num_levels, t->alloc_levels, levels);

	const Blk& b = blk_of(t->data);
	blk_guards(b, id);
	const u32* const mem = (const u32*)t->data;
	const bool igna = d.tsp.IgnoreTexA;
	u32 off = 0;   // words
	for (u32 l = 0; l < levels; l++)
	{
		const u32 lw = m.w >> l ? m.w >> l : 1, lh = m.h >> l ? m.h >> l : 1;
		for (u32 y = 0; y < lh; y++)
			for (u32 x = 0; x < lw; x++)
			{
				const u32 at = swz ? off + morton(x, y, lw, lh, false) : y * (pitch / 4) + x;
				const u32 c = mtexel(m, l, x, y, igna);
				u32 want = (c >> 8) | (c << 24);
				if (stale && l == stale_level)
					want = (*stale)[at - off];
				if (mem[at] != want)
					die("%s: level %u texel (%u,%u) holds %08X, want %08X", id, l, x, y, mem[at], want);
			}
		// A linear row's bytes past its texels stay as allocated.
		if (!swz)
			for (u32 y = 0; y < lh; y++)
				for (u32 x = lw; x < pitch / 4; x++)
					if (mem[y * (pitch / 4) + x] != FRESH)
						die("%s: row %u word %u written past the row", id, y, x);
		off += swz ? lw * lh : (pitch / 4) * lh;
		g_v.texels += lw * lh;
	}
	for (u32 i = off; i < b.size / 4; i++)
		if (mem[i] != FRESH)
			die("%s: word %u written past the texture's %u words", id, i, off);

	// The sampler state the next draw sends.
	ps3gl.tmu[0].enabled = 1;
	memset(&g_samp, 0xEE, sizeof(g_samp));
	ps3gl_apply_textures();
	const bool nearest = o_filter == 1 || (o_filter == 0 && d.tsp.FilterMode == 0);
	// Flycast SetGPState: the mip filter for bilinear, not for punch-through; anisotropic takes the trilinear one.
	u32 min = nearest ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	if (chain && !pt && !nearest)
		min = o_aniso > 0 ? GCM_TEXTURE_LINEAR_MIPMAP_LINEAR : GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
	const u32 mag = nearest ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	const u32 aniso = nearest ? GCM_TEXTURE_MAX_ANISO_1 : k_gcm_aniso[o_aniso];
	const u32 ws = d.tsp.ClampU ? GCM_TEXTURE_CLAMP_TO_EDGE : mirror_u ? GCM_TEXTURE_MIRRORED_REPEAT : GCM_TEXTURE_REPEAT;
	const u32 wt = d.tsp.ClampV ? GCM_TEXTURE_CLAMP_TO_EDGE : mirror_v ? GCM_TEXTURE_MIRRORED_REPEAT : GCM_TEXTURE_REPEAT;
	if (g_samp.enable != GCM_TRUE || g_samp.minlod != 0 || g_samp.maxlod != (levels - 1) << 8 || g_samp.bias != 0)
		die("%s: sampler enable %u lod %u..%u bias %u, want lod 0..%u", id, g_samp.enable, g_samp.minlod, g_samp.maxlod,
		    g_samp.bias, (levels - 1) << 8);
	if (g_samp.min != min || g_samp.mag != mag || g_samp.aniso != aniso)
		die("%s pt %d filter %d aniso %d: sampler min %u mag %u aniso %u, want %u %u %u", id, pt, o_filter, o_aniso,
		    g_samp.min, g_samp.mag, g_samp.aniso, min, mag, aniso);
	if (g_samp.ws != ws || g_samp.wt != wt)
		die("%s: wrap %u %u, want %u %u", id, g_samp.ws, g_samp.wt, ws, wt);
	const gcmTexture& g = g_samp.tex;
	if (g.format != (GCM_TEXTURE_FORMAT_A8R8G8B8 | (swz ? 0 : GCM_TEXTURE_FORMAT_LIN)) || g.mipmap != levels ||
	    g.width != m.w || g.height != m.h || g.pitch != pitch || g.offset != b.off)
		die("%s: gcm texture format %02X mipmap %u %ux%u pitch %u offset %08X", id, g.format, g.mipmap, g.width,
		    g.height, g.pitch, g.offset);

	g_v.binds++;
	g_v.chains += chain;
	g_v.levels += levels - 1;
	g_v.lin += !swz;
	g_v.swz += swz;
	g_v.palvq += m.vq && m.pal;
	g_v.plvq += m.vq && m.planar;
	g_v.pt_chain += chain && pt;
	g_v.mirror_mip += m.mip && mirror;
	g_v.yuv_chain += chain && m.fmt == 3;
	g_v.vq_chain += chain && m.vq;
	g_v.pal_chain += chain && m.pal;
	g_v.aniso += aniso != GCM_TEXTURE_MAX_ANISO_1;
	g_v.nearest += nearest;
	g_v.trilinear += min == GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
}

// trace: everything the bind left behind.
static void fold_state(bool ok)
{
	fold(ok);
	ps3gl_texture_t* const t = ps3gl.tmu[0].bound;
	if (!t || !t->data)
	{
		fold(0xDEAD);
		return;
	}
	ps3gl.tmu[0].enabled = 1;
	memset(&g_samp, 0xEE, sizeof(g_samp));
	ps3gl_apply_textures();
	fold(((u64)t->width << 32) | t->height);
	fold(((u64)t->swizzled << 16) | ((u64)t->num_levels << 8) | t->alloc_levels);
	fold(((u64)g_samp.tex.format << 48) | ((u64)g_samp.tex.mipmap << 32) | g_samp.tex.pitch);
	fold(((u64)g_samp.tex.width << 32) | g_samp.tex.height);
	fold(g_samp.tex.offset);
	fold(((u64)g_samp.minlod << 32) | g_samp.maxlod);
	fold(((u64)g_samp.min << 24) | ((u64)g_samp.mag << 16) | ((u64)g_samp.ws << 8) | g_samp.wt);
	fold(((u64)g_samp.aniso << 32) | g_samp.bias);
	const Blk& b = blk_of(t->data);
	const u32* const mem = (const u32*)t->data;
	for (u32 i = 0; i < b.size / 4; i++)
		fold(mem[i]);
}

// ---- the scenario -----------------------------------------------------------------

static void fill_words(u8* p, u32 bytes)
{
	for (u32 i = 0; i + 4 <= bytes; i += 4)
	{
		const u32 w = rnd32();
		memcpy(p + i, &w, 4);
	}
}

static Def random_def()
{
	static const u32 fmts[] = { 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 5, 5, 6, 6, 6, 7 };
	const u32 fmt = fmts[rnd(sizeof(fmts) / sizeof(fmts[0]))];
	const u32 r = rnd(100), top = r < 55 ? 4 : r < 85 ? 6 : r < 97 ? 7 : 8;
	const u32 u = rnd(top), v = rnd(3) ? u : rnd(top);
	Def d;
	d.tsp.full = rnd32();
	d.tsp.TexU = u;
	d.tsp.TexV = v;
	// Mirrored wraps on a quarter of the textures, as a game has them on few.
	if (rnd(4))
		d.tsp.FlipU = d.tsp.FlipV = 0;
	d.tcw.full = 0;
	d.tcw.NO_PAL.PixelFmt = fmt;
	d.tcw.NO_PAL.VQ_Comp = rnd(3) == 0;
	d.tcw.NO_PAL.MipMapped = rnd(2);
	if (fmt == 5 || fmt == 6)
		d.tcw.PAL.PalSelect = rnd(64);
	else
	{
		d.tcw.NO_PAL.ScanOrder = rnd(4) == 0;
		d.tcw.NO_PAL.StrideSel = rnd(3) == 0;
	}
	// One texture in eight is one of the two P25 forms for sure: palette VQ, or scan-order VQ with a row stride.
	if (rnd(8) == 0)
	{
		d.tcw.NO_PAL.VQ_Comp = 1;
		if (fmt != 5 && fmt != 6)
		{
			d.tcw.NO_PAL.ScanOrder = 1;
			d.tcw.NO_PAL.MipMapped = 0;
			d.tcw.NO_PAL.StrideSel = rnd(2);
		}
	}
	u32 addr8;
	switch (rnd(12))
	{
	case 0: addr8 = VRAM_SIZE / 8 - 1 - rnd(40000); break;   // the source ends near, at or past the end of VRAM
	case 1: addr8 = rnd(64); break;
	default: addr8 = rnd(VRAM_SIZE / 8);
	}
	d.tcw.NO_PAL.TexAddr = addr8;
	return d;
}

// The texture's whole source span, when it does not wrap: the codebook, every mip level, the top level.
static bool def_span(const Def& d, u32* lo, u32* hi, u32* top_lo)
{
	const MTex m = mdesc(d.tsp, d.tcw);
	u32 top = m.base, bytes;
	if (m.vq)
	{
		top = m.base + 2048 + (m.mip ? M_VQ[m.lvl] : 0);
		bytes = m.planar ? m.stride * m.h / 4 : m.w * m.h / 4;
	}
	else if (m.pal)
	{
		top = m.base + (m.mip ? M_OTHER[m.lvl] * m.bits / 8 : 0);
		bytes = m.w * m.h * m.bits / 8;
	}
	else
	{
		top = m.base + (m.mip ? M_OTHER[m.lvl] * 2 : 0);
		bytes = m.stride * m.h * 2;
	}
	*lo = m.base;
	*hi = top + bytes;
	*top_lo = top;
	return *hi <= VRAM_SIZE;
}

static std::vector<Def> g_pool;

// New words in one texture's top level, when no other texture of the pool reads that span (its lower levels would
// keep the old texels: they are not in the hash). tail: only its last 8 bytes, which a hash span too short misses.
static void touch_one(bool tail)
{
	const Def& d = g_pool[rnd((u32)g_pool.size())];
	u32 lo, hi, top;
	if (!def_span(d, &lo, &hi, &top))
		return;
	for (const Def& o : g_pool)
	{
		u32 olo, ohi, otop;
		if (&o == &d || (o.tsp.full == d.tsp.full && o.tcw.full == d.tcw.full))
			continue;
		if (!def_span(o, &olo, &ohi, &otop) || (olo < hi && top < ohi))
			return;
	}
	for (u32 a = tail && hi - top > 8 ? hi - 8 : top; a < hi; a++)
		g_vram[a] = (u8)rnd32();
}

// One texture's whole source as zeros, then as ones: black and transparent textures.
static void flatten_one(u32 value)
{
	const Def& d = g_pool[rnd((u32)g_pool.size())];
	u32 lo, hi, top;
	if (def_span(d, &lo, &hi, &top))
		memset(g_vram + lo, (int)value, hi - lo);
}

static void begin_frame(const std::vector<DcParam>& params)
{
	ps3gl_flush_deferred_texture_frees();
	ps3tex_frame_begin(params.data(), (u32)params.size());
}

static bool bind(const Def& d, u32 pidx, bool pt)
{
#ifdef TEX_REF
	return ps3tex_bind(d.tsp, d.tcw, pidx);
#else
	return ps3tex_bind(d.tsp, d.tcw, pidx, pt);
#endif
}

static void run(bool model, u32 epochs)
{
	std::vector<DcParam> params;
	for (u32 e = 0; e < epochs; e++)
	{
		if (model)
		{
			o_mips = rnd(4) != 0;
			o_vqfmt = rnd(3) != 0;
			o_aniso = rnd(3) ? 0 : rnd(5);
			o_filter = rnd(4) ? 0 : 1 + rnd(2);
		}
		o_bump = rnd(4) != 0;
		o_yuvblk = rnd(2);
		o_slot = rnd(8) != 0;
		g_spetex_mode = rnd(3);
		g_spepix_mode = rnd(3);
		g_fail_in = rnd(6) ? 0 : 1 + rnd(60);
		g_rr.text_control = rnd32();
		// Half the epochs: a stride of 32 to 128 texels, under the width of many textures (their rows end early).
		if (rnd(2))
			g_rr.text_control = (g_rr.text_control & ~31u) | (1 + rnd(4));
		g_rr.pal_ram_ctrl = rnd32();
		g_pal_pad = rnd(4) * 4;
		g_pal = (u32*)(g_palbuf + g_pal_pad);
		fill_words(g_vram, VRAM_SIZE);
		fill_words(g_palbuf, 4096 + 32);
		g_pool.clear();
		const u32 npool = 12 + rnd(28);
		for (u32 i = 0; i < npool; i++)
			g_pool.push_back(random_def());

		const u32 frames = 8 + rnd(16);
		u32 flats = 0;
		for (u32 f = 0; f < frames; f++)
		{
			// Between frames: all of VRAM, the palette, one texture, or an option from the SELECT menu.
			switch (rnd(8))
			{
			case 0: fill_words(g_vram, VRAM_SIZE); break;
			case 1: fill_words(g_palbuf, 4096 + 32); break;
			case 2:
				// Twice per epoch at most, with two values: a span flattened twice the same way would keep a top
				// level's words while the fill changes its lower levels.
				fill_words(g_vram, VRAM_SIZE);
				if (flats < 2)
					flatten_one(flats++ ? 0xFF : 0);
				break;
			case 3: touch_one(false); break;
			case 4: touch_one(true); break;
			case 5:
				if (model && rnd(3) == 0)
				{
					switch (rnd(4))
					{
					case 0: o_mips = !o_mips; break;
					case 1: o_vqfmt = !o_vqfmt; break;
					case 2: o_aniso = rnd(5); break;
					default: o_filter = rnd(3);
					}
				}
				break;
			default: break;
			}
			g_seed = (u64)e * 1000 + f + 1;
			g_lazy = (int)(f & 1);

			const u32 n = 1 + rnd(10);
			std::vector<u32> pick;
			params.clear();
			for (u32 i = 0; i < n; i++)
			{
				const u32 k = rnd((u32)g_pool.size());
				pick.push_back(k);
				DcParam p;
				memset(&p, 0, sizeof(p));
				p.pcw.Texture = 1;
				p.tsp = g_pool[k].tsp;
				p.tcw = g_pool[k].tcw;
				params.push_back(p);
			}
			begin_frame(params);
			for (u32 i = 0; i < n; i++)
			{
				const Def& d = g_pool[pick[i]];
				// A bind with no SPU hash slot now and then; a second bind of the same param in the frame.
				const u32 turns = rnd(5) ? 1 : 2;
				for (u32 k = 0; k < turns; k++)
				{
					const bool pt = rnd(4) == 0;
					const bool ok = bind(d, rnd(6) ? i : SPETEX_NONE, pt);
					if (model)
					{
						if (!ok)
							die("ps3tex_bind refused tsp %08X tcw %08X", d.tsp.full, d.tcw.full);
						verify(d, pt);
					}
					else
						fold_state(ok);
				}
			}
			all_guards("after a frame");
		}
		// Now and then the cache ages out: 200 frames with no texture.
		if (rnd(3) == 0)
		{
			params.clear();
			for (u32 i = 0; i < 200; i++)
				begin_frame(params);
			all_guards("after the age-out");
		}
	}
}

#ifndef TEX_REF
// The pick of 2026-10-10: the hash covers the top level. A write to a lower level alone keeps that level's old
// texels; the next change of the top level decodes the whole chain again.
static void lower_level_rule()
{
	o_mips = 1;
	o_vqfmt = o_aniso = o_filter = 0;
	o_bump = 1;
	u32 done = 0;
	for (u32 mode = 0; mode < 3; mode++)
	{
		g_spepix_mode = (int)mode;
		g_spetex_mode = (int)mode;
		g_fail_in = 0;
		Def d;
		d.tsp.full = 0;
		d.tsp.TexU = d.tsp.TexV = 3;   // 64x64
		d.tsp.FilterMode = 1;
		d.tcw.full = 0;
		d.tcw.NO_PAL.PixelFmt = 1;
		d.tcw.NO_PAL.MipMapped = 1;
		d.tcw.NO_PAL.TexAddr = (0x100000 + mode * 0x10000) / 8;
		fill_words(g_vram, VRAM_SIZE);
		std::vector<DcParam> params(1);
		memset(&params[0], 0, sizeof(DcParam));
		params[0].pcw.Texture = 1;
		params[0].tsp = d.tsp;
		params[0].tcw = d.tcw;
		begin_frame(params);
		bind(d, 0, false);
		verify(d, false);
		// Level 2 (16x16) of a 64x64 chain: after the 4096 + 1024 words of levels 0 and 1.
		const u32* const mem = (const u32*)ps3gl.tmu[0].bound->data;
		const std::vector<u32> old(mem + 4096 + 1024, mem + 4096 + 1024 + 256);
		const MTex m = mdesc(d.tsp, d.tcw);
		fill_words(g_vram + m.base + M_OTHER[4] * 2, 16 * 16 * 2);
		begin_frame(params);
		bind(d, 0, false);
		verify(d, false, &old, 2);
		fill_words(g_vram + m.base + M_OTHER[6] * 2, 64 * 64 * 2);
		begin_frame(params);
		bind(d, 0, false);
		verify(d, false);
		done++;
	}
	printf("lower-level rule: %u chains keep a level written alone and decode it with the next top level\n", done);
}
#endif

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		printf("usage: %s trace|model seed epochs\n", argv[0]);
		return 2;
	}
	const bool model = !strcmp(argv[1], "model");
	const u32 seed = (u32)atoi(argv[2]), epochs = (u32)atoi(argv[3]);
	g_rs ^= (u64)seed * 0x2545F4914F6CDD1Dull;
	for (u32 i = 0; i < 8; i++)
		rnd32();

	g_arena = (u8*)mmap(0, ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_NORESERVE,
	                    -1, 0);
	if (g_arena == MAP_FAILED)
		die("no 1 GB arena under 4 GB");
	g_vram = (u8*)aligned_alloc(65536, VRAM_SIZE);
	g_palbuf = (u8*)aligned_alloc(128, 4096 + 128);
	PVRPARAMS.vram = g_vram;
	g_pal = (u32*)g_palbuf;
	memset(&g_rr, 0, sizeof(g_rr));
	ps3gl_textures_init();
	g_chk[0] = (u32*)rsxMemalign(128, 4u << 20);
	g_chk[1] = (u32*)rsxMemalign(128, 4u << 20);

#ifdef TEX_REF
	if (model)
		die("the frozen files have no options to model");
#endif
	run(model, epochs);
#ifndef TEX_REF
	if (model)
	{
		lower_level_rule();
		u32 decodes, live;
		ps3tex_stats(&decodes, &live);
		printf("model: %llu binds, %llu texels | chains %llu (+%llu levels; vq %llu, palette %llu, yuv %llu; %llu in a"
		       " punch-through list), mipmapped and mirrored %llu | palette vq %llu, scan-order vq %llu | swizzled %llu,"
		       " linear %llu | anisotropic %llu, nearest %llu, trilinear %llu\n",
		       (unsigned long long)g_v.binds, (unsigned long long)g_v.texels, (unsigned long long)g_v.chains,
		       (unsigned long long)g_v.levels, (unsigned long long)g_v.vq_chain, (unsigned long long)g_v.pal_chain,
		       (unsigned long long)g_v.yuv_chain, (unsigned long long)g_v.pt_chain, (unsigned long long)g_v.mirror_mip,
		       (unsigned long long)g_v.palvq, (unsigned long long)g_v.plvq, (unsigned long long)g_v.swz,
		       (unsigned long long)g_v.lin, (unsigned long long)g_v.aniso, (unsigned long long)g_v.nearest,
		       (unsigned long long)g_v.trilinear);
		printf("paths: glTexImage2D level 0 %llu, lower levels %llu | SPU jobs %llu (%llu lower levels written; late %llu,"
		       " left to the PPU %llu) |"
		       " SPU hashes %llu, hash CHECKs %llu, decode CHECKs %llu | mip uploads counted by ps3gl %u\n",
		       (unsigned long long)g_n.img[0], (unsigned long long)g_n.img[1], (unsigned long long)g_n.spu_jobs,
		       (unsigned long long)g_n.spu_lower, (unsigned long long)g_n.spu_fail, (unsigned long long)g_n.skips, (unsigned long long)g_n.hash_spu,
		       (unsigned long long)g_n.hash_checks, (unsigned long long)g_n.pix_checks, ps3gl_frame_mip_uploads);
		if (ps3gl_frame_mip_uploads != g_n.img[1] + g_n.spu_lower)
			die("ps3gl counted %u mip uploads for %llu glTexImage2D and %llu SPU lower levels", ps3gl_frame_mip_uploads,
			    (unsigned long long)g_n.img[1], (unsigned long long)g_n.spu_lower);
		printf("PASS\n");
		return 0;
	}
#endif
	printf("trace %016llx frames with %llu SPU jobs, %llu images\n", (unsigned long long)g_fold,
	       (unsigned long long)g_n.spu_jobs, (unsigned long long)(g_n.img[0] + g_n.img[1]));
	return 0;
}
