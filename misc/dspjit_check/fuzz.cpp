// OPTIMIZATION_PLAN 5.13 A1b PC test: ISA probe on the simulator, random programs against the interpreter, binaries
// for the spu-objdump check. Usage: dj_fuzz <programs> <samples> [--isa-bin f.bin f.txt] [--prog-bin f.bin]
#include "aica_model.h"
#include "spe_aica.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t g_swz[2u << 20];   // aica_ram in the PS3 layout, for the cache path
static void aram_fetch(uint8_t* dst, uint32_t line) { memcpy(dst, g_swz + line, 128); }

#include "spe_aica_ram.h"
#include "spe_dsp_jit.h"
#include "pc_glue.h"
#include "spe_dsp_check.h"
#include "pc_glue_post.h"

static void put_be(FILE* f, uint32_t w)
{
	const uint8_t b[4] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w};
	fwrite(b, 1, 4, f);
}

// Every probe snippet, and the text spu-objdump must print for it
static void emit_isa(const char* bin, const char* txt)
{
	FILE* fb = fopen(bin, "wb");
	FILE* ft = fopen(txt, "w");
	const uint32_t in = 0x2000, out = 0x2030;
	for (int f = 0; f < DJF_COUNT; f++)
	{
		const int lo = dspjit_form_imm[f][0], hi = dspjit_form_imm[f][1];
		const int imm = lo + (hi - lo) / 3;
		uint32_t w[16];
		const int n = dspjit_probe(w, 16, f, in, out, imm);
		for (int i = 0; i < n; i++)
			put_be(fb, w[i]);
		const char* nm = dspjit_form_names[f];
		fprintf(ft, "lqa $10,0x%x\nlqa $11,0x%x\nlqa $12,0x%x\n", in, in + 16, in + 32);
		if (f <= DJF_MPY)
			fprintf(ft, "%s $13,$10,$11\n", nm);
		else if (f <= DJF_XSHW)
			fprintf(ft, "%s $13,$10\n", nm);
		else if (f <= DJF_SHUFB)
			fprintf(ft, "%s $13,$10,$11,$12\n", nm);
		else if (f == DJF_ROTMI || f == DJF_ROTMAI)
			fprintf(ft, "%s $13,$10,%d\n", nm, -imm);
		else if (f == DJF_CWD)
			fprintf(ft, "%s $13,%d($10)\n", nm, imm);
		else if (f <= DJF_CGTI)
			fprintf(ft, "%s $13,$10,%d\n", nm, imm);
		else if (f == DJF_IOHL)
			fprintf(ft, "ori $13,$10,0\niohl $13,%d\n", imm);
		else
			fprintf(ft, "%s $13,%d\n", nm, imm);
		fprintf(ft, "stqa $13,0x%x\nbi $0\n", out);
	}
	fclose(fb);
	fclose(ft);
}

// Three compiled random programs back to back; every word must disassemble
static void emit_prog(const char* bin)
{
	FILE* fb = fopen(bin, "wb");
	for (int p = 0; p < 3; p++)
	{
		dj_rand_program();
		dj_compile();
		for (uint32_t i = 0; i < dj_info.words && i < DJ_CODE_WORDS; i++)
			put_be(fb, dj_code[i]);
	}
	fclose(fb);
}

int main(int argc, char** argv)
{
	const uint32_t programs = argc > 1 ? (uint32_t)atoi(argv[1]) : 200;
	const uint32_t samples = argc > 2 ? (uint32_t)atoi(argv[2]) : 32;
	for (uint32_t i = 0; i < sizeof(g_swz); i++)
		g_swz[i] = (uint8_t)((i * 2654435761u) >> 13);
	caique::AicaModel* m = new caique::AicaModel();
	pc_setup(m, 2);
	for (int a = 3; a < argc; a++)
		if (!strcmp(argv[a], "--isa-bin") && a + 2 < argc)
		{
			emit_isa(argv[a + 1], argv[a + 2]);
			a += 2;
		}
		else if (!strcmp(argv[a], "--prog-bin") && a + 1 < argc)
			emit_prog(argv[++a]);

	const int isa = dj_isa_probe(512);
	printf("isa probe: %s\n", isa ? dspjit_form_names[isa - 1] : "ok");

	uint32_t ran = 0;
	const uint64_t i0 = g_sim.insns;
	const uint32_t c0 = dj_compiles, f0 = dj_fails;
	const uint32_t bad = dj_selftest(programs, samples, &ran);
	printf("self-test: programs %u x %u samples, checked %u, bad %u, compiles %u, too big %u, sim faults %llu\n", programs,
	       samples, ran, bad, dj_compiles - c0, dj_fails - f0, (unsigned long long)g_faults);
	if (ran)
		printf("simulated insns per checked sample: %.0f (random programs, p = 1/4 per flag)\n",
		       (double)(g_sim.insns - i0) / ran);
	return (isa || bad || g_faults) ? 1 : 0;
}
