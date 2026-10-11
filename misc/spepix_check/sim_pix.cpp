// 5.12 #4/#5 (WSL): the real spe_pix image in misc/spu_sim: SPU cycles per texture and per framebuffer, a profile, and
// every word against decode() + convert_pixels() and fb_convert_ppu(). Usage: sim_pix elf [seed]
#include "spusim.h"
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <vector>
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"
#include "spe_bell.h"
#include "spe_pix.h"

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

// ---- what the extracted PPU code needs (as spepix_check.cpp) ----
#define VRAM_SIZE 0x800000u
#define VRAM_MASK 0x7FFFFFu
static struct { u8* vram; } PVRPARAMS;
static u32* g_pal;
#define PALETTE_RAM g_pal
static struct { u32 pal_ram_ctrl, text_control; } g_rr;
static int s_vqfmt;  // P25 VQ Formats OFF: the texture forms the SPU knows
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

// ---- main memory, big-endian as on the PS3 ----
enum : uint32_t { EA_CTL = 0x10000000, EA_VRAM = 0x20000000, EA_PAL = 0x30000000, EA_DST = 0x40000000 };
#define DST_BYTES (4u << 20)
struct Region { uint64_t ea; uint32_t size; uint8_t* p; };
static Region g_reg[4];

static uint8_t* ea_fn(void*, uint64_t ea, uint32_t size)
{
	for (auto& r : g_reg)
		if (r.p && ea >= r.ea && ea + size <= r.ea + r.size)
			return r.p + (ea - r.ea);
	return 0;
}
static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
enum { BELL = 0, ECHO = 0x80, SETUP = 0x100 };
#define ECHOF(f) get32(g_reg[0].p + ECHO + offsetof(SpePixEcho, f))
#define CANARY 0xC0FFEE11u

static uint64_t g_rs = 0x9E3779B97F4A7C15ull;
static u32 rnd32()
{
	g_rs ^= g_rs << 13;
	g_rs ^= g_rs >> 7;
	g_rs ^= g_rs << 17;
	return (u32)(g_rs >> 16);
}
static u32 rnd(u32 n) { return rnd32() % n; }

static std::vector<u32> g_vram;
static SpuSim* g_sim;
static u32 g_seq;

static void sync_vram()
{
	for (u32 i = 0; i < VRAM_SIZE / 4; i++)
		put32(g_reg[1].p + 4 * i, g_vram[i]);
}

static uint64_t ring(uint32_t field_off)
{
	put32(g_reg[0].p + BELL + field_off, ++g_seq);
	spusim_llr_lost(g_sim);
	const uint64_t c0 = g_sim->cycle;
	const int rc = spusim_run(g_sim, 4000000000ull);
	if (rc != SPU_RUN_BLOCKED)
		die("SPU stopped: rc %d: %s", rc, g_sim->msg);
	return g_sim->cycle - c0;
}

struct Tex { const char* name; u32 fmt, u, v; bool vq, mip, planar, stride, mirrored; u32 stride_tc; };

static void run_tex(const Tex& t, u32 reps)
{
	// TexAddr in 8-byte units: up to 4 MB in, odd ones 8 bytes off a quadword.
	const u32 tcw = (t.fmt << 27) | (0x200 + rnd(0x80000)) | (t.vq ? 1u << 30 : 0) | (t.mip ? 1u << 31 : 0) |
	                (t.planar ? 1u << 26 : 0) | (t.stride ? 1u << 25 : 0) | ((t.fmt == 5 || t.fmt == 6) ? rnd(64) << 21 : 0);
	TSP tsp;
	TCW tc;
	tsp.full = (t.u << 3) | t.v;
	tc.full = tcw;
	g_rr.text_control = t.stride_tc;
	g_rr.pal_ram_ctrl = rnd(4);
	TexDesc d;
	describe(tsp, tc, &d);
	if (!spu_fits(d))
		die("%s does not fit", t.name);
	const bool swz = d.fmt != 3 && !t.mirrored;
	const u32 pitch = tex_linear_pitch(d.w), bytes = swz ? d.w * d.h * 4 : pitch * d.h;
	const u32 black = s_black_tex, alpha0 = s_alpha0_tex;
	decode(d, false);
	const u32 want_res = (s_black_tex == black ? SPEPIX_LIT : 0) | (s_alpha0_tex == alpha0 ? SPEPIX_OPAQUE : 0);
	std::vector<u8> be(d.w * d.h * 4);
	for (u32 i = 0; i < d.w * d.h; i++)
		put32(&be[4 * i], s_out[i]);
	std::vector<u32> want(bytes / 4, CANARY);
	if (swz)
		swz_ensure_tables(d.w, d.h);
	convert_pixels((uint8_t*)want.data(), pitch, be.data(), d.w, d.h, 4, GL_RGBA, swz);

	SpePixTex j;
	memset(&j, 0, sizeof(j));
	spu_job(d, false, EA_DST, pitch, swz, g_rr.pal_ram_ctrl, &j);
	uint64_t cyc = 0;
	for (u32 r = 0; r < reps; r++)
	{
		for (u32 i = 0; i < bytes / 4; i++)
			put32(g_reg[3].p + 4 * i, CANARY);
		uint8_t* b = g_reg[0].p + BELL;
		const u32 f[] = { j.fmt, j.w, j.h, j.addr, j.pitch, j.flags, j.pal_base, j.vq_cb, j.pal_ctrl, j.dst_ea, j.dst_pitch };
		for (u32 k = 0; k < 11; k++)
			put32(b + 4 + 4 * k, f[k]);
		cyc += ring(offsetof(SpePixBell, tex.seq));
		if (ECHOF(tex_seq) != g_seq)
			die("%s: echo seq %u, want %u", t.name, ECHOF(tex_seq), g_seq);
		if (ECHOF(tex_res) != want_res)
			die("%s: result bits %u, want %u", t.name, ECHOF(tex_res), want_res);
		for (u32 i = 0; i < bytes / 4; i++)
			if (get32(g_reg[3].p + 4 * i) != want[i])
				die("%s: word %u spu %08X want %08X", t.name, i, get32(g_reg[3].p + 4 * i), want[i]);
	}
	const double c = (double)cyc / reps;
	printf("%-34s %4ux%-4u %s: %8.0f cycles = %6.3f ms at 3.2 GHz, %5.2f cycles/texel, out %4u KB (%.3f ms at 3.3 GB/s)\n",
	       t.name, d.w, d.h, swz ? "swz" : "lin", c, c / 3.2e6, c / (d.w * d.h), bytes / 1024,
	       bytes / 3.3e9 * 1e3);
}

static void run_fb(const char* name, u32 depth, u32 x_size, u32 y_size, u32 sof1)
{
	s_snap.fb_r_size = x_size | (y_size << 10) | (1u << 20);
	s_snap.fb_r_depth = depth;
	s_snap.fb_r_sof1 = sof1;
	FbGeom fg;
	if (!fb_geom(&fg))
		die("%s: not handled", name);
	fb_convert_ppu(fg);
	for (u32 i = 0; i < FB2D_W * FB2D_H; i++)
		put32(g_reg[3].p + 4 * i, CANARY);
	SpePixFb j;
	memset(&j, 0, sizeof(j));
	fb_job(fg, EA_DST, &j);
	uint8_t* b = g_reg[0].p + BELL + offsetof(SpePixBell, fb);
	const u32 f[] = { j.sof1, j.depth, j.w, j.h, j.line_bytes, j.dst_ea, j.dst_pitch };
	for (u32 k = 0; k < 7; k++)
		put32(b + 4 + 4 * k, f[k]);
	const uint64_t cyc = ring(offsetof(SpePixBell, fb.seq));
	if (ECHOF(fb_seq) != g_seq)
		die("%s: echo seq %u, want %u", name, ECHOF(fb_seq), g_seq);
	for (u32 y = 0; y < fg.cpy_h; y++)
		for (u32 x = 0; x < fg.cpy_w; x++)
		{
			const u32 p = s_fb_pix[y * fg.cpy_w + x], want = (p >> 8) | (p << 24);
			const u32 got = get32(g_reg[3].p + 4 * (y * FB2D_W + x));
			if (got != want)
				die("%s: pixel %u of row %u spu %08X want %08X", name, x, y, got, want);
		}
	printf("%-34s %4ux%-4u    : %8.0f cycles = %6.3f ms at 3.2 GHz, %5.2f cycles/pixel, out %4u KB (%.3f ms at 3.3 GB/s)\n",
	       name, fg.cpy_w, fg.cpy_h, (double)cyc, cyc / 3.2e6, (double)cyc / (fg.cpy_w * fg.cpy_h),
	       fg.cpy_w * fg.cpy_h * 4 / 1024, fg.cpy_w * fg.cpy_h * 4 / 3.3e9 * 1e3);
}

int main(int argc, char** argv)
{
	if (argc < 2)
		die("usage: sim_pix elf [seed]");
	g_rs += argc > 2 ? (u32)atoi(argv[2]) : 1;
	g_vram.resize(VRAM_SIZE / 4);
	std::vector<u32> pal(1024 + 8);
	for (auto& w : g_vram)
		w = rnd32();
	for (auto& w : pal)
		w = rnd32();
	PVRPARAMS.vram = (u8*)g_vram.data();
	g_pal = pal.data() + 1;  // PALETTE_RAM 4 bytes into its quadword

	g_reg[0] = { EA_CTL, 0x1000, (uint8_t*)calloc(1, 0x1000) };
	g_reg[1] = { EA_VRAM, VRAM_SIZE, (uint8_t*)malloc(VRAM_SIZE) };
	g_reg[2] = { EA_PAL, 4096 + 32, (uint8_t*)calloc(1, 4096 + 32) };
	g_reg[3] = { EA_DST, DST_BYTES, (uint8_t*)malloc(DST_BYTES) };
	sync_vram();
	for (u32 i = 0; i < 1024 + 8 && 4 * i + 4 <= 4096 + 32; i++)
		put32(g_reg[2].p + 4 * i, pal[i]);
	put32(g_reg[0].p + SETUP + offsetof(SpePixSetup, vram_ea), EA_VRAM);
	put32(g_reg[0].p + SETUP + offsetof(SpePixSetup, pal_ea), EA_PAL + 4);

	g_sim = (SpuSim*)calloc(1, sizeof(SpuSim));
	spusim_init(g_sim, ea_fn, 0);
	const uint32_t entry = spusim_load_elf(g_sim, argv[1]);
	if (!entry)
		die("cannot load %s", argv[1]);
	g_sim->pc = entry;
	g_sim->r[3].w[1] = EA_CTL + BELL;
	g_sim->r[4].w[1] = EA_CTL + ECHO;
	g_sim->r[5].w[1] = EA_CTL + SETUP;
	g_sim->prof = (uint64_t*)calloc(0x10000, sizeof(uint64_t));
	g_sim->brmiss = (uint32_t*)calloc(0x10000, sizeof(uint32_t));
	int rc = spusim_run(g_sim, 100000000);
	if (rc != SPU_RUN_BLOCKED || ECHOF(state) != SPE_STATE_RUNNING)
		die("SPU boot: rc %d state %u: %s", rc, ECHOF(state), g_sim->msg);
	memset(g_sim->prof, 0, 0x10000 * sizeof(uint64_t));
	const uint64_t c0 = g_sim->cycle;

	// The Looney Tunes track load (VQ 565/4444, mipmapped), common 16-bit and palette textures, the FMV textures.
	static const Tex tex[] = {
		{ "vq 565 mip", 1, 5, 5, true, true, false, false, false, 0 },
		{ "vq 4444 mip", 2, 5, 5, true, true, false, false, false, 0 },
		{ "vq 565", 1, 6, 6, true, false, false, false, false, 0 },
		{ "twiddled 565", 1, 5, 5, false, false, false, false, false, 0 },
		{ "twiddled 1555 mip", 0, 6, 6, false, true, false, false, false, 0 },
		{ "twiddled 4444 mirrored", 2, 4, 4, false, false, false, false, true, 0 },
		{ "pal8", 6, 5, 5, false, false, false, false, false, 0 },
		{ "pal4 mip", 5, 4, 4, false, true, false, false, false, 0 },
		{ "planar 565", 1, 6, 5, false, false, true, false, false, 0 },
		{ "planar 565 mirrored", 1, 6, 5, false, false, true, false, true, 0 },
		{ "yuv planar (Looney Tunes FMV)", 3, 7, 6, false, false, true, true, false, 20 },
		{ "yuv planar (Sonic Adventure FMV)", 3, 6, 6, false, false, true, true, false, 10 },
		{ "yuv twiddled (Soul Calibur tiles)", 3, 3, 3, false, false, false, false, false, 0 },
		{ "vq yuv", 3, 5, 5, true, false, false, false, false, 0 },
		{ "small 565 8x8", 1, 0, 0, false, false, false, false, false, 0 },
	};
	for (const Tex& t : tex)
		run_tex(t, 2);
	run_fb("fb 565 640x480", 1, 319, 479, 0x200000);
	run_fb("fb 0555 640x480 odd start", 0, 319, 479, 0x200002);
	run_fb("fb 0888 640x480", 3, 639, 479, 0x100000);
	run_fb("fb 565 across the bank edge", 1, 319, 479, 0x3F0000);
	const uint64_t busy = g_sim->cycle - c0;
	printf("same as the PPU code for every word | dmas %llu, %.1f%% of cycles waiting on DMA\n",
	       (unsigned long long)g_sim->dmas, g_sim->dma_wait * 100.0 / (double)busy);
	spusim_print_profile(g_sim, busy, 14);
	if (getenv("HOT"))
		spusim_print_hot(g_sim, getenv("HOT"), getenv("HOTN") ? atoi(getenv("HOTN")) : 60);
	return 0;
}
