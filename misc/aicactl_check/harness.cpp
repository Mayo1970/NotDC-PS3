// OPTIMIZATION_PLAN 5.15 B1: runs out/ac_test.elf (build_spu.sh) in misc/spu_sim for one seed and prints its report.
// Usage: harness elf seed ops
#include "spusim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t g_rep[256];
static const uint64_t EA_REP = 0x10000000;

static uint8_t* ea_fn(void*, uint64_t ea, uint32_t size)
{
	return ea >= EA_REP && ea + size <= EA_REP + sizeof(g_rep) ? g_rep + (ea - EA_REP) : 0;
}

static uint32_t get32(int i) { const uint8_t* p = g_rep + 4 * i; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		fprintf(stderr, "usage: harness elf seed ops\n");
		return 2;
	}
	SpuSim* sim = (SpuSim*)calloc(1, sizeof(SpuSim));
	spusim_init(sim, ea_fn, 0);
	const uint32_t entry = spusim_load_elf(sim, argv[1]);
	if (!entry)
	{
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}
	sim->pc = entry;
	sim->r[3].w[1] = (uint32_t)strtoul(argv[2], 0, 0);
	sim->r[4].w[1] = (uint32_t)strtoul(argv[3], 0, 0);
	sim->r[5].w[1] = (uint32_t)EA_REP;
	const int rc = spusim_run(sim, 200000000000ull);
	if (rc != SPU_RUN_STOP)
	{
		printf("seed %s: SPU did not stop: rc %d %s\n", argv[2], rc, sim->msg);
		return 1;
	}
	static const char* const cov[] = { "rd", "wr", "arm rd", "arm wr", "samples", "fiq up", "-", "sh4 irq up",
	                                   "sh4 irq down", "timer ovf", "kyonex", "lfore", "mon", "cdda", "accept", "race",
	                                   "ca clamp", "latch stale" };
	printf("seed %s: %s (stop %u, %llu insns)", argv[2], get32(0) == 1 && sim->stop_code == 1 ? "PASS" : "FAIL",
	       sim->stop_code, (unsigned long long)sim->insns);
	for (int i = 0; i < 18; i++)
		if (i != 6)
			printf(" | %s %u", cov[i], get32(8 + i));
	printf(" | verify %u\n", get32(7));
	if (get32(0) != 1)
	{
		const uint32_t w = get32(2);
		printf("  op %u: '%c' %08X %08X %08X %08X\n", get32(1), (char)w, get32(3), get32(4), get32(5), get32(6));
		for (int i = 0; i < 8; i++)
			if (get32(32 + 4 * i))
				printf("    %c addr %08X size %u data %08X\n", (char)get32(32 + 4 * i), get32(33 + 4 * i), get32(34 + 4 * i),
				       get32(35 + 4 * i));
		return 1;
	}
	return 0;
}
