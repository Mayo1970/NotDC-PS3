// 5.12 #2 (WSL): the real spe_vb image in misc/spu_sim on Looney Tunes-like scenes: SPU cycles per vertex, a profile,
// and the output against ps3_rend.cpp's merge, bit for bit. Usage: sim_vb elf [op_strips tr_strips seed builds]
#include "spusim.h"
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <random>
#include <vector>
#include <algorithm>
#include "ps3/ps3_rend_regs.h"
#include "spe_vb.h"

#ifndef SPEVB_VTX_STRIDE
#define SPEVB_VTX_STRIDE SPEVB_VTX_BYTES
#endif

// ---- what the extracted PPU code needs (as spevb_check.cpp) ----
#pragma pack(push, 1)
typedef struct
{
	float x, y, z, w;
	float u0, v0;
	float u1, v1;
	uint32_t color;
} ps3gl_vertex_t;
#pragma pack(pop)
typedef unsigned GLenum;
#define GL_TRIANGLE_STRIP 5
static const u32 ListType_Opaque = 0, ListType_Translucent = 2, ListType_Punch_Through = 4;
#define MAX_STRIPS SPE_MAX_STRIPS
#include "structs.inc"

static DrawLists s_rl;
static u32 s_stat_strips[5];
enum { DT_PARAM, DT_TEX, DT_GLEND, DT_SORT, DT_MODVOL, DT_COUNT };
static u64 s_dt[DT_COUNT];
static inline u64 PERF_TICKS() { return 0; }

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
static void emit_vertex(const DcVertex*, u32) { die("emit_vertex"); }
static void glBegin(GLenum) { die("glBegin"); }
static void glEnd() { die("glEnd"); }
static u32 apply_param(const DcParam&, u32) { die("apply_param in the DRY pass"); }
static void ps3gl_draw_vb(GLenum, u32, int, int) { die("draw in the DRY pass"); }

#include "region.inc"

// ---- main memory, big-endian as on the PS3 ----
enum : uint32_t { EA_CTL = 0x10000000, EA_OUT = 0x20000000, EA_RUNS = 0x30000000, EA_VB = 0x40000000 };
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
static void put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint8_t* ctl(uint32_t off) { return g_reg[0].p + off; }
enum { BELL = 0, ECHO = 0x80, SETUP = 0x100, DIAG = 0x180 };
#define ECHOF(f) get32(ctl(ECHO + offsetof(SpeVbEcho, f)))
#define DIAGF(f) get32(ctl(DIAG + offsetof(SpeVbDiag, f)))

static u32 fb(float f) { u32 u; memcpy(&u, &f, 4); return u; }

// ---- a Looney Tunes race frame: opaque strips in long same-state runs, a few punch-through, translucent ----
static std::mt19937 g_rng;
static uint32_t rnd(uint32_t n) { return (uint32_t)(g_rng() % n); }
static float rndf(float lo, float hi) { return lo + (hi - lo) * (float)(g_rng() / 4294967296.0); }

static std::vector<SpeVtx> g_vtx;
static std::vector<SpeStrip> g_strip;
static std::vector<SpeParam> g_param;

static void gen_list(uint32_t list, uint32_t n, uint32_t run_len)
{
	SpeParam p = { 0, 0, 0, 0 };
	uint32_t left = 0;
	for (uint32_t i = 0; i < n; i++)
	{
		if (!left)
		{
			p.pcw = 0x00000008u | (rnd(2) << 1) | (list << 24);
			p.isp = (rnd(8) << 29) | (rnd(4) << 27);
			p.tsp = rnd(0x7FFFFFFF) | 0x00100000u;
			p.tcw = rnd(64) << 8;
			g_param.push_back(p);
			left = 1 + rnd(2 * run_len);
		}
		left--;
		const uint32_t count = rnd(50) ? 4 + rnd(6) : 10 + rnd(40);
		SpeStrip s;
		s.first = (uint32_t)g_vtx.size();
		s.count = (uint16_t)count;
		s.list = (uint8_t)list;
		s.pad = 0;
		s.param = (uint16_t)(g_param.size() - 1);
		s.pad2 = 0;
		g_strip.push_back(s);
		for (uint32_t k = 0; k < count; k++)
		{
			SpeVtx v;
			v.x = fb(rndf(-20.f, 660.f));
			v.y = fb(rndf(-20.f, 500.f));
			v.w = rnd(400) ? fb(expf(rndf(0.f, 11.f))) : 0;
			v.col = (uint32_t)g_rng();
			v.u = fb(rndf(0.f, 1.f));
			v.v = fb(rndf(0.f, 1.f));
			g_vtx.push_back(v);
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
		die("usage: sim_vb elf [op_strips tr_strips seed builds]");
	const uint32_t n_op = argc > 2 ? (uint32_t)atoi(argv[2]) : 4620, n_tr = argc > 3 ? (uint32_t)atoi(argv[3]) : 889;
	g_rng.seed(argc > 4 ? (uint32_t)atoi(argv[4]) : 1);
	const uint32_t builds = argc > 5 ? (uint32_t)atoi(argv[5]) : 3;
	gen_list(0, n_op, 12);
	gen_list(4, 25, 25);
	gen_list(2, n_tr, 3);
	const uint32_t nvtx = (uint32_t)g_vtx.size(), nstrip = (uint32_t)g_strip.size(), nparam = (uint32_t)g_param.size();
	if (nvtx > SPE_MAX_VTX || nstrip > SPE_MAX_STRIPS || nparam > SPE_MAX_PARAMS)
		die("scene too large: %u vertices %u strips %u params", nvtx, nstrip, nparam);
	const uint32_t cap = 3 * nvtx + 4 * nstrip + 16;

	// The reference: ps3_rend.cpp's merge as the CHECK dry pass runs it.
	std::vector<u32> ref_vb((size_t)cap * 9 + 9);
	std::vector<SpeRun> ref_runs(SPEVB_MAX_RUNS);
	s_rl.vtx = (const DcVertex*)g_vtx.data();
	s_rl.strip = (const DcStrip*)g_strip.data();
	s_rl.param = (const DcParam*)g_param.data();
	s_rl.nvtx = nvtx;
	s_rl.nstrip = nstrip;
	s_rl.nparam = nparam;
	s_merge = true;
	s_vb = (ps3gl_vertex_t*)ref_vb.data();
	s_vb_cap = cap;
	s_dry_runs = ref_runs.data();
	draw_list<true>(ListType_Opaque);
	draw_list<true>(ListType_Punch_Through);
	draw_tr_sorted<true>();
	const uint32_t ref_n = s_dry_nruns, ref_v = s_vb_used;

	// Main memory for the SPU.
	const uint32_t out_size = (uint32_t)sizeof(SpeOut) * SPE_CTX;
	g_reg[0] = { EA_CTL, 0x1000, (uint8_t*)calloc(1, 0x1000) };
	g_reg[1] = { EA_OUT, out_size, (uint8_t*)calloc(1, out_size) };
	g_reg[2] = { EA_RUNS, (uint32_t)sizeof(SpeRun) * SPEVB_MAX_RUNS, (uint8_t*)calloc(SPEVB_MAX_RUNS, sizeof(SpeRun)) };
	g_reg[3] = { EA_VB, (cap + 8) * SPEVB_VTX_STRIDE, (uint8_t*)calloc(cap + 8, SPEVB_VTX_STRIDE) };
	uint8_t* o = g_reg[1].p;   // context 0
	for (uint32_t i = 0; i < nvtx; i++)
	{
		const uint32_t* w = (const uint32_t*)&g_vtx[i];
		for (int k = 0; k < 6; k++)
			put32(o + offsetof(SpeOut, vtx) + i * 24 + 4 * k, w[k]);
	}
	for (uint32_t i = 0; i < nstrip; i++)
	{
		uint8_t* d = o + offsetof(SpeOut, strip) + i * 12;
		put32(d, g_strip[i].first);
		put16(d + 4, g_strip[i].count);
		d[6] = g_strip[i].list;
		d[7] = 0;
		put16(d + 8, g_strip[i].param);
		put16(d + 10, 0);
	}
	for (uint32_t i = 0; i < nparam; i++)
	{
		const uint32_t* w = (const uint32_t*)&g_param[i];
		for (int k = 0; k < 4; k++)
			put32(o + offsetof(SpeOut, param) + i * 16 + 4 * k, w[k]);
	}
	for (uint32_t i = 0; i < SPE_CTX; i++)
		put32(ctl(SETUP + 4 * i), EA_OUT + i * (uint32_t)sizeof(SpeOut));
	put32(ctl(SETUP + offsetof(SpeVbSetup, runs_ea)), EA_RUNS);
	put32(ctl(SETUP + offsetof(SpeVbSetup, diag_ea)), EA_CTL + DIAG);

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
	for (uint32_t b = 1; b <= builds; b++)
	{
		memset(g_reg[3].p, 0xCD, g_reg[3].size);
		put32(ctl(BELL + 8), 0);
		put32(ctl(BELL + 12), nvtx);
		put32(ctl(BELL + 16), nstrip);
		put32(ctl(BELL + 20), nparam);
		put32(ctl(BELL + 24), EA_VB);
		put32(ctl(BELL + 28), cap);
		put32(ctl(BELL + 32), SPEVB_AUTOSORT);
		put32(ctl(BELL + 0), b);
		spusim_llr_lost(sim);
		const uint64_t c0 = sim->cycle;
		rc = spusim_run(sim, 4000000000ull);
		if (rc != SPU_RUN_BLOCKED)
			die("SPU stopped: rc %d: %s", rc, sim->msg);
		busy += sim->cycle - c0;
		if (ECHOF(seq) != b || ECHOF(flags) != (SPEVB_OP_DONE | SPEVB_PT_DONE | SPEVB_TR_DONE))
			die("build %u: echo seq %u flags %X", b, ECHOF(seq), ECHOF(flags));
		if (ECHOF(runs) != ref_n || ECHOF(vb_used) != ref_v)
			die("build %u: runs %u vb_used %u, ref %u %u", b, ECHOF(runs), ECHOF(vb_used), ref_n, ref_v);
		for (uint32_t i = 0; i < ref_n; i++)
		{
			const uint8_t* r = g_reg[2].p + i * sizeof(SpeRun);
			if (get32(r) != ref_runs[i].first || get32(r + 4) != ref_runs[i].count || get32(r + 8) != ref_runs[i].param)
				die("run %u: spu %u+%u p%u ref %u+%u p%u", i, get32(r), get32(r + 4), get32(r + 8), ref_runs[i].first,
				    ref_runs[i].count, ref_runs[i].param);
		}
		for (uint32_t i = 0; i < ref_v; i++)
			for (int k = 0; k < 9; k++)
				if (get32(g_reg[3].p + (size_t)i * SPEVB_VTX_STRIDE + 4 * k) != ref_vb[(size_t)i * 9 + k])
					die("vertex %u word %d: spu %08X ref %08X", i, k, get32(g_reg[3].p + (size_t)i * SPEVB_VTX_STRIDE + 4 * k),
					    ref_vb[(size_t)i * 9 + k]);
	}
	const double per = (double)busy / builds;
	printf("scene: %u vertices, %u strips (op %u pt 25 tr %u), %u params | %u runs, %u vertices out | same as the PPU merge\n",
	       nvtx, nstrip, n_op, n_tr, nparam, ref_n, ref_v);
	printf("SPU: %.0f cycles per build = %.2f ms at 3.2 GHz | %.0f cycles per vertex in, %.0f per vertex out | ipc %.2f dual "
	       "%.1f%% br_miss %.0f/build dma_wait %.1f%%\n",
	       per, per / 3.2e6, per / nvtx, per / ref_v, (double)(sim->insns - i0) / busy,
	       (sim->dual - d0) * 100.0 / (double)(sim->insns - i0), (double)(sim->br_miss - m0) / builds,
	       sim->dma_wait * 100.0 / (double)busy);
	printf("diag per build: key pass %.0f sort %.0f tr pass %.0f us | waits slot %.0f pub %.0f slide %.0f in %.0f qfull %.0f us\n",
	       DIAGF(t_key) / 79.8 / builds, DIAGF(t_sort) / 79.8 / builds, DIAGF(t_tr) / 79.8 / builds,
	       DIAGF(t_slot) / 79.8 / builds, DIAGF(t_pub) / 79.8 / builds, DIAGF(t_slide) / 79.8 / builds,
	       DIAGF(t_in) / 79.8 / builds, DIAGF(t_qfull) / 79.8 / builds);
	spusim_print_profile(sim, busy, 20);
	if (getenv("HOT"))
		spusim_print_hot(sim, getenv("HOT"), getenv("HOTN") ? atoi(getenv("HOTN")) : 60);
	return 0;
}
