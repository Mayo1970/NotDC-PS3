// 5.12 #3 (WSL): the real spe_tex image in misc/spu_sim: SPU cycles per request and per hashed byte, a profile, and
// every result slot against hash_texture() of ps3_tex.cpp. Usage: sim_tex elf [params textures kb seed requests]
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
#include "spe_tex.h"

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

// ---- what the extracted PPU code needs (as spetex_check.cpp) ----
#define VRAM_SIZE 0x800000u
#define VRAM_MASK 0x7FFFFFu
static struct { u8* vram; } PVRPARAMS;
static u32* g_pal;
#define PALETTE_RAM g_pal
static struct { u32 pal_ram_ctrl, text_control; } g_rr;
static u64 s_hash_bytes;
static int yuv_conv_generation(u32, u32*) { return 0; }
#define __builtin_prefetch(...) ((void)0)
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

// ---- main memory, big-endian as on the PS3 ----
enum : uint32_t { EA_CTL = 0x10000000, EA_VRAM = 0x20000000, EA_PAL = 0x30000000, EA_PRM = 0x40000000, EA_RES = 0x50000000 };
struct Region { uint64_t ea; uint32_t size; uint8_t* p; };
static Region g_reg[5];

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
#define ECHOF(f) get32(g_reg[0].p + ECHO + offsetof(SpeTexEcho, f))

static uint64_t g_rs = 0x9E3779B97F4A7C15ull;
static u32 rnd32()
{
	g_rs ^= g_rs << 13;
	g_rs ^= g_rs >> 7;
	g_rs ^= g_rs << 17;
	return (u32)(g_rs >> 16);
}
static u32 rnd(u32 n) { return rnd32() % n; }

struct Prm { u32 pcw, isp, tsp, tcw; };

// A texture of about kb KB: 16-bit twiddled, VQ or palette, mipmapped or not, at a random place.
static Prm make_tex(u32 kb)
{
	Prm p = { 0x08u | (rnd32() & ~0x0Fu), rnd32(), rnd32() & ~63u, 0 };
	u32 lg = 0;
	while ((1u << (lg + 1)) <= kb * 1024 / 2)
		lg++;  // texels of a 16-bit texture of kb KB
	if (lg < 6)
		lg = 6;
	const u32 u = (lg / 2 > 3 ? lg / 2 - 3 : 0), v = (lg - lg / 2 > 3 ? lg - lg / 2 - 3 : 0);
	p.tsp |= ((u > 7 ? 7 : u) << 3) | (v > 7 ? 7 : v);
	const u32 kind = rnd(10);
	const u32 fmt = kind < 6 ? 1 : kind < 8 ? 2 : (rnd(2) ? 5 : 6);
	u32 tcw = (fmt << 27) | (rnd(0x100000) & ~0xFFu);
	if (kind == 8 && fmt != 5 && fmt != 6)
		tcw |= 1u << 30;  // VQ
	if (rnd(2))
		tcw |= 1u << 31;  // mip
	if (fmt == 5 || fmt == 6)
		tcw |= rnd(64) << 21;
	p.tcw = tcw;
	return p;
}

int main(int argc, char** argv)
{
	if (argc < 2)
		die("usage: sim_tex elf [params textures kb seed requests]");
	const u32 nparam = argc > 2 ? (u32)atoi(argv[2]) : 2000;
	const u32 ntex = argc > 3 ? (u32)atoi(argv[3]) : 150;
	const u32 kb = argc > 4 ? (u32)atoi(argv[4]) : 4;
	g_rs += argc > 5 ? (u32)atoi(argv[5]) : 1;
	const u32 requests = argc > 6 ? (u32)atoi(argv[6]) : 3;

	// Host-word VRAM for the reference, the same words big-endian for the SPU.
	std::vector<u32> vram(VRAM_SIZE / 4), pal(1024 + 8);
	for (auto& w : vram)
		w = rnd32();
	for (auto& w : pal)
		w = rnd32();
	PVRPARAMS.vram = (u8*)vram.data();
	g_pal = pal.data() + 1;  // PALETTE_RAM 4 bytes into its quadword
	g_rr.text_control = rnd32();
	g_rr.pal_ram_ctrl = rnd32();

	std::vector<Prm> tex, prm;
	for (u32 i = 0; i < ntex; i++)
		tex.push_back(make_tex(1 + rnd(2 * kb)));
	for (u32 i = 0; i < nparam; i++)
	{
		Prm p = tex[i < ntex ? i : rnd(ntex)];
		if (rnd(10) == 0)
			p.pcw &= ~8u;
		prm.push_back(p);
	}
	std::vector<SpeTexRes> want;
	u64 bytes = 0;
	for (u32 i = 0; i <= nparam; i++)
	{
		const Prm& p = i < nparam ? prm[i] : tex[0];
		SpeTexRes r = { 0, SPETEX_SKIP };
		if ((p.pcw & 8) && ((p.tcw >> 27) & 7) != 3)
		{
			TSP t;
			TCW c;
			t.full = p.tsp;
			c.full = p.tcw;
			TexDesc d;
			describe(t, c, &d);
			const u64 b0 = s_hash_bytes;
			r.hash = hash_texture(d);
			r.tag = 0;
			if (i < ntex)
				bytes += s_hash_bytes - b0;
		}
		want.push_back(r);
	}

	g_reg[0] = { EA_CTL, 0x1000, (uint8_t*)calloc(1, 0x1000) };
	g_reg[1] = { EA_VRAM, VRAM_SIZE, (uint8_t*)malloc(VRAM_SIZE) };
	g_reg[2] = { EA_PAL, 4096 + 32, (uint8_t*)calloc(1, 4096 + 32) };
	g_reg[3] = { EA_PRM, SPE_MAX_PARAMS * 16 + 32, (uint8_t*)calloc(1, SPE_MAX_PARAMS * 16 + 32) };
	g_reg[4] = { EA_RES, SPETEX_SLOTS * 8, (uint8_t*)calloc(SPETEX_SLOTS, 8) };
	for (u32 i = 0; i < VRAM_SIZE / 4; i++)
		put32(g_reg[1].p + 4 * i, vram[i]);
	for (u32 i = 0; i < 1024 + 8 && 4 * i + 4 <= 4096 + 32; i++)
		put32(g_reg[2].p + 4 * i, pal[i]);
	const u32 prm_off = 4;  // a TaContext's params sit 4 bytes off a quadword
	for (u32 i = 0; i < nparam; i++)
	{
		put32(g_reg[3].p + prm_off + i * 16, prm[i].pcw);
		put32(g_reg[3].p + prm_off + i * 16 + 4, prm[i].isp);
		put32(g_reg[3].p + prm_off + i * 16 + 8, prm[i].tsp);
		put32(g_reg[3].p + prm_off + i * 16 + 12, prm[i].tcw);
	}
	put32(g_reg[0].p + SETUP + offsetof(SpeTexSetup, vram_ea), EA_VRAM);
	put32(g_reg[0].p + SETUP + offsetof(SpeTexSetup, pal_ea), EA_PAL + 4);
	put32(g_reg[0].p + SETUP + offsetof(SpeTexSetup, res_ea), EA_RES);

	SpuSim* sim = (SpuSim*)calloc(1, sizeof(SpuSim));
	spusim_init(sim, ea_fn, 0);
	const uint32_t entry = spusim_load_elf(sim, argv[1]);
	if (!entry)
		die("cannot load %s", argv[1]);
	sim->pc = entry;
	sim->r[3].w[1] = EA_CTL + BELL;
	sim->r[4].w[1] = EA_CTL + ECHO;
	sim->r[5].w[1] = EA_CTL + SETUP;
	sim->prof = (uint64_t*)calloc(0x10000, sizeof(uint64_t));
	sim->brmiss = (uint32_t*)calloc(0x10000, sizeof(uint32_t));
	int rc = spusim_run(sim, 100000000);
	if (rc != SPU_RUN_BLOCKED || ECHOF(state) != SPE_STATE_RUNNING)
		die("SPU boot: rc %d state %u: %s", rc, ECHOF(state), sim->msg);
	memset(sim->prof, 0, 0x10000 * sizeof(uint64_t));
	memset(sim->brmiss, 0, 0x10000 * sizeof(uint32_t));

	uint64_t busy = 0;
	const uint64_t i0 = sim->insns, m0 = sim->br_miss, d0 = sim->dual;
	for (u32 q = 1; q <= requests; q++)
	{
		uint8_t* b = g_reg[0].p + BELL;
		put32(b + offsetof(SpeTexBell, params_ea), EA_PRM + prm_off);
		put32(b + offsetof(SpeTexBell, nparam), nparam);
		put32(b + offsetof(SpeTexBell, bg_pcw), tex[0].pcw);
		put32(b + offsetof(SpeTexBell, bg_tsp), tex[0].tsp);
		put32(b + offsetof(SpeTexBell, bg_tcw), tex[0].tcw);
		put32(b + offsetof(SpeTexBell, text_control), g_rr.text_control);
		put32(b + offsetof(SpeTexBell, pal_ram_ctrl), g_rr.pal_ram_ctrl);
		put32(b + offsetof(SpeTexBell, seq), q);
		spusim_llr_lost(sim);
		const uint64_t c0 = sim->cycle;
		rc = spusim_run(sim, 4000000000ull);
		if (rc != SPU_RUN_BLOCKED)
			die("SPU stopped: rc %d: %s", rc, sim->msg);
		busy += sim->cycle - c0;
		if (ECHOF(seq) != q)
			die("request %u: echo seq %u", q, ECHOF(seq));
		for (u32 i = 0; i <= nparam; i++)
		{
			const uint8_t* r = g_reg[4].p + (size_t)(i == nparam ? SPETEX_BG : i) * 8;
			const u32 tag = want[i].tag | q;
			if (get32(r + 4) != tag || get32(r) != want[i].hash)
				die("request %u slot %u: spu %08X/%08X want %08X/%08X", q, i, get32(r), get32(r + 4), want[i].hash, tag);
		}
	}
	const double per = (double)busy / requests;
	printf("scene: %u params, %u textures, %.0f KB hashed per request (first request; the table dedupes the rest)\n",
	       nparam, ntex, bytes / 1024.0);
	printf("SPU: %.0f cycles per request = %.3f ms at 3.2 GHz | %.2f cycles per hashed byte | ipc %.2f dual %.1f%% "
	       "br_miss %.0f/request dma_wait %.1f%% | echo hashed %u dups %u skips %u\n",
	       per, per / 3.2e6, per / (double)bytes, (double)(sim->insns - i0) / busy,
	       (sim->dual - d0) * 100.0 / (double)(sim->insns - i0), (double)(sim->br_miss - m0) / requests,
	       sim->dma_wait * 100.0 / (double)busy, ECHOF(hashed), ECHOF(dups), ECHOF(skips));
	printf("same as hash_texture() for all %u slots of %u requests\n", nparam + 1, requests);
	spusim_print_profile(sim, busy, 12);
	if (getenv("HOT"))
		spusim_print_hot(sim, getenv("HOT"), getenv("HOTN") ? atoi(getenv("HOTN")) : 60);
	return 0;
}
