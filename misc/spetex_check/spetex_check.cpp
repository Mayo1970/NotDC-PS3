// 5.12 #3 (WSL): spe_tex_hash.h on SPU shims against describe() and hash_texture() of ps3_tex.cpp (as run.sh extracts
// them), bit for bit: hash_range on its own, then every result slot of random scenes, also as each result line lands.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "types.h"
#include "plugs/drkPvr/ta_structs.h"
#include "spe_tex.h"

extern "C" {
extern uint64_t g_seed;
extern int g_lazy;
extern void (*g_land_hook)(uint64_t ea, uint32_t size);
void spu_regions_reset(void);
void spu_region(uint64_t ea, uint32_t size, uint8_t* p, int writable);
void spu_setup(uint32_t vram_ea, uint32_t pal_ea, uint32_t res_ea);
int spu_run(const SpeTexBell* b);
void spu_counts(uint32_t* params, uint32_t* hashed, uint32_t* dups, uint32_t* skips, uint32_t* bytes, uint32_t* wraps);
uint32_t t_hash_range(uint32_t addr, uint32_t bytes, uint32_t seed);
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
#define __builtin_prefetch(...) ((void)0)

// The PPE's u64 load from VRAM: VRAM holds host words, and the big-endian PPE puts the lower address's word on top.
struct BePtr
{
	const u8* p;
	explicit BePtr(const u8* q) : p(q) {}
	static u64 ld(const u8* q)
	{
		u32 hi, lo;
		memcpy(&hi, q, 4);
		memcpy(&lo, q + 4, 4);
		return (u64)hi << 32 | lo;
	}
	u64 operator[](u32 i) const { return ld(p + 8 * i); }
	u64 operator*() const { return ld(p); }
	BePtr& operator+=(u32 n)
	{
		p += 8 * n;
		return *this;
	}
	BePtr operator++(int)
	{
		BePtr t = *this;
		p += 8;
		return t;
	}
	operator const u32*() const { return (const u32*)p; }
};

#include "region.inc"

// ---- fake main memory -------------------------------------------------------

enum : uint32_t { EA_VRAM = 0x10000000, EA_PAL = 0x20000000, EA_PRM = 0x30000000, EA_RES = 0x40000000 };
#define PRM_BYTES (SPE_MAX_PARAMS * 16 + 32)
#define RES_BYTES (SPETEX_SLOTS * 8)

static u8* g_vram;
static u8* g_palbuf;
static u8* g_prm;
static SpeTexRes* g_res;
static u32 g_pal_pad, g_prm_off;

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

static void layout()
{
	spu_regions_reset();
	spu_region(EA_VRAM, VRAM_SIZE, g_vram, 0);
	spu_region(EA_PAL, 4096 + 32, g_palbuf, 0);
	spu_region(EA_PRM, PRM_BYTES, g_prm, 0);
	spu_region(EA_RES, RES_BYTES, (u8*)g_res, 1);
	spu_setup(EA_VRAM, EA_PAL + g_pal_pad, EA_RES);
	PVRPARAMS.vram = g_vram;
	g_pal = (u32*)(g_palbuf + g_pal_pad);
}

// ---- part 1: hash_range alone, every start alignment and chunk edge ---------

static u32 pick_bytes()
{
	const u32 C = 16256;
	switch (rnd(6))
	{
	case 0: return rnd(64);
	case 1: return rnd(512);
	case 2: return rnd(40000);
	case 3: return (1 + rnd(4)) * C + rnd(48) - 24;            // stream ends near a chunk edge
	case 4: return (1 + rnd(3)) * C + rnd(256);
	default: return rnd(2 * 1024 * 1024 + 1);
	}
}

static void check_ranges(u32 n)
{
	for (u32 i = 0; i < n; i++)
	{
		u32 bytes = pick_bytes();
		const u32 b4 = (bytes + 3) & ~3u;
		u32 addr;
		switch (rnd(7))
		{
		case 0: addr = VRAM_SIZE - bytes + rnd(64) - 32; break;  // around the end: the masked word loop
		case 1: addr = VRAM_SIZE + rnd(0x20000); break;          // past the end after a mip offset
		case 2: addr = rnd(64); break;
		case 3: addr = VRAM_SIZE - b4 + (rnd(2) ? 4 : 0); break; // ends at the end, or one word past it
		default: addr = rnd(VRAM_SIZE);
		}
		// Both sides mask the low two bits of the start.
		addr = (addr & ~3u) | rnd(4);
		const u32 seed = rnd32();
		const u32 spu = t_hash_range(addr, bytes, seed);
		const u32 ppu = hash_range(addr, bytes, seed);
		if (spu != ppu)
			die("hash_range(addr %08X, bytes %u, seed %08X) spu %08X ppu %08X", addr, bytes, seed, spu, ppu);
	}
	printf("hash_range: %u ranges ok\n", n);
}

// ---- part 2: requests --------------------------------------------------------

struct Prm { u32 pcw, isp, tsp, tcw; };

static u32 rand_tcw()
{
	u32 tcw = rnd32();
	// Pixel formats as games use them, YUV and the odd ones included.
	static const u32 fmts[] = { 0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7 };
	tcw = (tcw & ~(7u << 27)) | (fmts[rnd(sizeof(fmts) / sizeof(fmts[0]))] << 27);
	switch (rnd(6))
	{
	case 0: tcw = (tcw & ~0x1FFFFFu) | ((VRAM_SIZE >> 3) - 1 - rnd(0x8000)); break;  // near the end of VRAM
	case 1: tcw = (tcw & ~0x1FFFFFu) | rnd(0x400); break;
	default: break;
	}
	return tcw;
}

static u32 rand_tsp()
{
	u32 tsp = rnd32() & ~63u;
	// Mostly 8-256 texels; now and then up to 1024.
	const u32 u = rnd(10) ? rnd(6) : rnd(8), v = rnd(10) ? rnd(6) : rnd(8);
	return tsp | (u << 3) | v;
}

static std::vector<Prm> g_scene;
static std::vector<SpeTexRes> g_want;  // per slot: the hash and tag the SPU must write
static u32 g_tag;
static u64 g_landed_lines, g_landed_checked;

static SpeTexRes want_of(u32 pcw, u32 tsp, u32 tcw)
{
	SpeTexRes r = { 0, g_tag | SPETEX_SKIP };
	if (!(pcw & 8) || ((tcw >> 27) & 7) == 3)
		return r;
	TSP t;
	TCW c;
	t.full = tsp;
	c.full = tcw;
	TexDesc d;
	describe(t, c, &d);
	r.hash = hash_texture(d);
	r.tag = g_tag;
	return r;
}

// Every slot a put lands with this request's tag must already hold its final value; nothing outside the scene's lines.
static void land(uint64_t ea, uint32_t size)
{
	if (ea < EA_RES || ea + size > EA_RES + RES_BYTES)
		return;
	const u32 first = (u32)(ea - EA_RES) / 8, n = size / 8;
	const u32 lines = ((u32)g_scene.size() + 15) & ~15u;
	if (!(first + n <= lines || first == SPETEX_BG))
		die("result put to slots %u..%u, outside the scene (%u params)", first, first + n - 1, (u32)g_scene.size());
	g_landed_lines++;
	for (u32 i = first; i < first + n; i++)
	{
		const SpeTexRes& r = g_res[i];
		if ((r.tag & SPETEX_SEQ) != g_tag)
			continue;
		const u32 k = i == SPETEX_BG ? (u32)g_scene.size() : i;
		if (k > g_scene.size() || (i != SPETEX_BG && i >= g_scene.size()))
			die("slot %u past the scene holds this request's tag", i);
		const SpeTexRes& w = g_want[k];
		if (r.tag != w.tag || (!(w.tag & SPETEX_SKIP) && r.hash != w.hash))
			die("slot %u landed %08X/%08X, want %08X/%08X", i, r.hash, r.tag, w.hash, w.tag);
		g_landed_checked++;
	}
}

static u32 g_seq = 0x7FFFFFF0u;  // crosses the tag wrap after 16 scenes

// A game's next frame: the same textures, a few of them uploaded again, so a hash kept from the last request is wrong.
static void next_frame()
{
	for (u32 k = 0; k < 8 && !g_scene.empty(); k++)
	{
		const Prm& p = g_scene[rnd((u32)g_scene.size())];
		TSP t;
		TCW c;
		t.full = p.tsp;
		c.full = p.tcw;
		TexDesc d;
		describe(t, c, &d);
		const u32 at = d.addr & VRAM_MASK & ~3u;
		fill_words(g_vram + at, VRAM_SIZE - at < 64 ? VRAM_SIZE - at : 64);
	}
}

static void one_scene(u32 nparam, bool same)
{
	if (!same)
	{
		g_rr.text_control = rnd32();
		g_rr.pal_ram_ctrl = rnd32();
		fill_words(g_palbuf, 4096 + 32);
		g_pal_pad = rnd(4) * 4;
		g_prm_off = rnd(4) * 4;
	}
	layout();
	// New texels at a few places, so a texture seen before can change between requests.
	for (u32 k = rnd(8); k; k--)
	{
		const u32 at = rnd(VRAM_SIZE - 65536) & ~3u;
		fill_words(g_vram + at, rnd(65536) & ~3u);
	}
	// A scene draws most params with a pool of a few hundred textures, as games do.
	static std::vector<Prm> pool;
	if (same)
	{
		next_frame();
		nparam = (u32)g_scene.size();
	}
	else
	{
		g_scene.clear();
		pool.clear();
		for (u32 k = 1 + rnd(rnd(2) ? 64 : 400); k; k--)
			pool.push_back({ 0, 0, rand_tsp(), rand_tcw() });
	}
	for (u32 i = (u32)g_scene.size(); i < nparam; i++)
	{
		Prm p = { rnd32(), rnd32(), rand_tsp(), rand_tcw() };
		if (rnd(5))
		{
			const Prm& t = pool[rnd((u32)pool.size())];
			p.tsp = (p.tsp & ~63u) | (t.tsp & 63);
			p.tcw = t.tcw;
		}
		if (rnd(5) == 0)
			p.pcw &= ~8u;
		else
			p.pcw |= 8u;
		if (i && rnd(3) == 0)
		{
			// Dupes: the same texture with other TSP bits, or the same TCW at another size.
			const Prm& o = g_scene[rnd(i)];
			p.tcw = o.tcw;
			p.tsp = rnd(2) ? (p.tsp & ~63u) | (o.tsp & 63) : p.tsp;
		}
		g_scene.push_back(p);
	}
	static Prm bg;
	if (!same)
	{
		bg = { rnd32(), 0, rand_tsp(), rand_tcw() };
		if (rnd(2))
			bg.pcw |= 8u;
	}

	if ((++g_seq & SPETEX_SEQ) == 0)
		++g_seq;
	g_tag = g_seq & SPETEX_SEQ;
	g_want.clear();
	s_hash_bytes = 0;
	for (const Prm& p : g_scene)
		g_want.push_back(want_of(p.pcw, p.tsp, p.tcw));
	g_want.push_back(want_of(bg.pcw, bg.tsp, bg.tcw));

	memset(g_prm, 0xA5, PRM_BYTES);
	for (u32 i = 0; i < nparam; i++)
		memcpy(g_prm + g_prm_off + i * 16, &g_scene[i], 16);
	// Stale slots from older requests: any tag but this one.
	for (u32 i = 0; i < SPETEX_SLOTS; i++)
	{
		g_res[i].hash = rnd32();
		u32 t = rnd32();
		if ((t & SPETEX_SEQ) == g_tag)
			t ^= 1;
		g_res[i].tag = t;
	}

	SpeTexBell b;
	memset(&b, 0, sizeof(b));
	b.seq = g_seq;
	b.params_ea = EA_PRM + g_prm_off;
	b.nparam = nparam;
	b.bg_pcw = bg.pcw;
	b.bg_tsp = bg.tsp;
	b.bg_tcw = bg.tcw;
	b.text_control = g_rr.text_control;
	b.pal_ram_ctrl = g_rr.pal_ram_ctrl;
	u32 c0[6], c1[6];
	spu_counts(&c0[0], &c0[1], &c0[2], &c0[3], &c0[4], &c0[5]);
	spu_run(&b);
	spu_counts(&c1[0], &c1[1], &c1[2], &c1[3], &c1[4], &c1[5]);

	for (u32 i = 0; i <= nparam; i++)
	{
		const u32 slot = i == nparam ? SPETEX_BG : i;
		const SpeTexRes& r = g_res[slot];
		const SpeTexRes& w = g_want[i];
		if (r.tag != w.tag || r.hash != w.hash)
			die("scene seq %08X, %u params: slot %u spu %08X/%08X want %08X/%08X (pcw %08X tsp %08X tcw %08X)",
			    g_seq, nparam, slot, r.hash, r.tag, w.hash, w.tag, i == nparam ? bg.pcw : g_scene[i].pcw,
			    i == nparam ? bg.tsp : g_scene[i].tsp, i == nparam ? bg.tcw : g_scene[i].tcw);
	}
	const u32 params = c1[0] - c0[0], hashed = c1[1] - c0[1], dups = c1[2] - c0[2], skips = c1[3] - c0[3];
	if (params != nparam + 1 || hashed + dups + skips != nparam + 1)
		die("counts: params %u hashed %u dups %u skips %u for %u params", params, hashed, dups, skips, nparam);
}

int main(int argc, char** argv)
{
	const u32 scenes = argc > 1 ? (u32)atoi(argv[1]) : 600;
	g_vram = (u8*)aligned_alloc(65536, VRAM_SIZE);
	g_palbuf = (u8*)aligned_alloc(128, 4096 + 128);
	g_prm = (u8*)aligned_alloc(128, (PRM_BYTES + 255) & ~127u);
	g_res = (SpeTexRes*)aligned_alloc(128, RES_BYTES);
	fill_words(g_vram, VRAM_SIZE);
	fill_words(g_palbuf, 4096 + 32);
	layout();

	check_ranges(scenes * 20);

	g_land_hook = land;
	u64 total_params = 0;
	u32 frames = 0;
	for (u32 s = 0; s < scenes; s++)
	{
		u32 n;
		switch (rnd(8))
		{
		case 0: n = 0; break;
		case 1: n = 1 + rnd(16); break;
		case 2: n = SPE_MAX_PARAMS - rnd(3); break;
		default: n = rnd(1500);
		}
		g_seed = s + 1;
		g_lazy = s & 1;
		// Half the requests are the next frame of the last scene.
		const bool same = s && rnd(2);
		frames += same;
		one_scene(n, same);
		total_params += g_scene.size();
	}
	printf("requests: %u scenes (%u of them next frames), %llu params, %llu result puts checked as they landed (%llu slots)\n",
	       scenes, frames, (unsigned long long)total_params, (unsigned long long)g_landed_lines,
	       (unsigned long long)g_landed_checked);
	printf("PASS\n");
	return 0;
}
