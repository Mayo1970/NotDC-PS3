#pragma once
// OPTIMIZATION_PLAN 5.13 A1b PC test: SPU simulator for the instruction forms the DSP recompiler emits.
// LS is a set of host regions; each holds little-endian elements of 1, 2 or 4 bytes, shown big-endian.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint32_t w[4];
} SimQ;

typedef struct
{
	uint32_t ls, size;
	uint8_t* host;
	int esz;
} SimRegion;

typedef uint32_t (*SimHostFn)(uint32_t a, uint32_t b);

typedef struct
{
	SimQ r[128];
	uint32_t pc;
	SimRegion reg[64];
	int nreg;
	int8_t qmap[0x40000 / 16];   // region of each LS quadword: -1 none, -2 mixed
	uint32_t hc_addr[8];
	SimHostFn hc_fn[8];
	int nhc;
	uint64_t insns;               // executed over all calls
	uint32_t max_steps;
	int fault;
	char msg[200];
} SpuSim;

void spusim_init(SpuSim* s);
void spusim_map(SpuSim* s, uint32_t ls, void* host, uint32_t size, int esz);
// A brasl to ls runs fn($3, $4) on the host, puts the result in $3 and poisons $2, $4..$79.
void spusim_hostcall(SpuSim* s, uint32_t ls, SimHostFn fn);
// Calls entry with $0 = a return marker and $1 = sp; checks $1 and $80..$127 on return. 0 = ok.
int spusim_call(SpuSim* s, uint32_t entry, uint32_t sp);

#ifdef __cplusplus
}
#endif
