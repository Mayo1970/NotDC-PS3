// OPTIMIZATION_PLAN 5.13 A1b PC test: the A1 SPU stand-in with the DSP recompiler checked on every sample.
// Built with -Dcaique=caique_spu, so it links beside the plain model of io_a1b.cpp.
#include "aica_model.h"
#include "spe_aica.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_swz[2u << 20];   // guest wave RAM in the PS3 layout (DC byte a at a ^ 3)
static void aram_fetch(uint8_t* dst, uint32_t line) { memcpy(dst, g_swz + line, 128); }

#include "spe_aica_ram.h"
#include "spe_dsp_jit.h"
#include "pc_glue.h"
#include "spe_dsp_check.h"
#include "pc_glue_post.h"

static caique::AicaModel* S;

static void to(uint32_t smp)
{
	while ((int32_t)(smp - (uint32_t)S->samples) > 0)
		S->step();
}

extern "C" {
void sim_init()
{
	S = new caique::AicaModel();
	pc_setup(S, 2);
}
void sim_set(uint32_t smp, uint32_t mdec, uint32_t lfsr, uint32_t K, uint32_t par)
{
	to(smp);
	S->MDEC_CT = mdec;
	S->lfsr = lfsr;
	S->eg_K = K;
	S->eg_par = par;
}
void sim_ram_w32(uint32_t off, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		g_swz[(off + i) ^ 3] = (uint8_t)(v >> (8 * i));
}
void sim_write(uint32_t smp, uint32_t off, uint32_t val)
{
	to(smp);
	S->write(off, val);
	dj_reg_written(off);
}
uint32_t sim_check(uint32_t smp, void* snap)
{
	to(smp);
	S->snap(*(caique::CtlSnap*)snap);
	aram_invalidate();
	return caique::AicaModel::snap_hash(*(caique::CtlSnap*)snap);
}
uint32_t sim_misses() { return aram_misses; }
// Last program: words, live steps, slow-path accesses
void sim_dsp_summary(FILE* f)
{
	fprintf(f, "A1b dsp checks %u bad %u compiles %u too-big %u odd %u faults %llu insns %llu words %u live %u slow %u\n",
	        dj_checks, dj_bad, dj_compiles, dj_fails, dj_odd, (unsigned long long)g_faults,
	        (unsigned long long)g_sim.insns, dj_info.words, dj_info.live, dj_info.slow);
}
void sim_dsp_replog(FILE* f) { g_replog = f; }
}
