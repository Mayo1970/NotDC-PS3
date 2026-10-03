#pragma once
// OPTIMIZATION_PLAN 5.13 A1b PC test: the DJ_* hooks of spe_dsp_check.h, with generated code run on spu_sim.
// Include before spe_dsp_check.h; include pc_glue_post.h after it.
#include "spu_sim.h"
#include <stdio.h>
#include <stdlib.h>

#define PC_HELPER_RD 0x00100u
#define PC_HELPER_WR 0x00110u
#define PC_SP 0x3FE00u

static SpuSim g_sim;
static struct { const uint8_t* host; uint32_t size, ls; } g_pcmap[32];
static int g_npcmap;
static uint64_t g_faults, g_reports;
static FILE* g_replog;   // mismatch details; stderr when null

static void pc_map(const void* host, uint32_t size, uint32_t ls, int esz)
{
	for (int i = 0; i < g_npcmap; i++)
		if (ls < g_pcmap[i].ls + g_pcmap[i].size && g_pcmap[i].ls < ls + size)
		{
			fprintf(stderr, "pc_map: LS %05X+%X overlaps %05X+%X\n", ls, size, g_pcmap[i].ls, g_pcmap[i].size);
			exit(4);
		}
	spusim_map(&g_sim, ls, (void*)host, size, esz);
	g_pcmap[g_npcmap].host = (const uint8_t*)host;
	g_pcmap[g_npcmap].size = size;
	g_pcmap[g_npcmap].ls = ls;
	g_npcmap++;
}

static uint32_t pc_ls(const void* p)
{
	const uint8_t* b = (const uint8_t*)p;
	for (int i = 0; i < g_npcmap; i++)
		if (b >= g_pcmap[i].host && b < g_pcmap[i].host + g_pcmap[i].size)
			return g_pcmap[i].ls + (uint32_t)(b - g_pcmap[i].host);
	fprintf(stderr, "pc_ls: %p is not mapped\n", p);
	exit(4);
}

static int pc_call(uint32_t ls)
{
	if (!spusim_call(&g_sim, ls, PC_SP))
		return 0;
	if (g_faults++ < 5)
		fprintf(stderr, "simulator fault: %s\n", g_sim.msg);
	return 1;
}

static void pc_report(const AicaDspBad* r, uint32_t n);
static void pc_isa_report(const AicaDspIsaBad* r);

#define DJ_LS(p) pc_ls((const void*)(p))
#define DJ_CALL(ls) pc_call(ls)
#define DJ_SYNC() ((void)0)
#define DJ_TICKS() 0u
#define DJ_REPORT(r, n) pc_report(r, n)
#define DJ_ISA_REPORT(r) pc_isa_report(r)
