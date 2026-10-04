#pragma once
// OPTIMIZATION_PLAN 5.13 A2 high voices: SPU simulator for the PC, written from the SPU ISA. Runs a real SPU image
// (LS, MFC DMA, events, decrementer) with an in-order dual-issue timing model, so SPU cost can be measured off HW.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t w[4]; } SpuQ;

enum { SPU_RUN_STOP = 1, SPU_RUN_BLOCKED, SPU_RUN_FAULT, SPU_RUN_LIMIT };

// Main memory as the harness sees it: EA -> host bytes (big-endian data, as on the PS3)
typedef uint8_t* (*SpuEaFn)(void* ctx, uint64_t ea, uint32_t size);

typedef struct
{
	uint8_t ls[0x40000];
	SpuQ r[128];
	uint32_t pc;

	// timing: cycle of the last issue, operand ready cycles, earliest next issue
	uint64_t cycle, ready[128], next_min;
	uint32_t prev_pc;
	int prev_pipe, prev_rt, pair_ok;
	uint32_t hint_br, hint_tgt;
	uint64_t hint_cycle;
	int hint_on;

	// channels
	uint32_t lsa, eah, eal, size, tagid, tag_mask, tag_upd;
	uint64_t tag_done[32];
	uint32_t ev_mask, ev_pend;
	uint32_t dec_val, dec_armed;
	uint64_t dec_cycle;
	uint32_t atomic_stat;
	uint64_t llr_ea;
	int llr_on;
	uint32_t out_mbox, stop_code;
	SpuEaFn ea;
	void* ctx;
	uint32_t dma_lat;   // cycles for one DMA command of up to 128 bytes

	// stats
	uint64_t insns, dual, br_miss, dma_wait, dmas, dma_bytes;
	uint64_t* prof;     // cycles per instruction word (0x10000 entries), or null
	uint32_t* brmiss;   // mispredicts per instruction word, or null
	uint64_t pen;       // mispredict penalty still to charge to pen_pc
	uint32_t pen_pc;
	int fault;
	char msg[256];
} SpuSim;

void spusim_init(SpuSim* s, SpuEaFn ea, void* ctx);
// Loads an SPU ELF image into LS; returns the entry point or 0 on failure. syms gets STT_FUNC symbols when the ELF has them.
uint32_t spusim_load_elf(SpuSim* s, const char* path);
// Runs until stop, a blocking event read with nothing pending, a fault or max_insns.
int spusim_run(SpuSim* s, uint64_t max_insns);
// The harness changed the reserved line (a PPU store): raise the lock-line-lost event.
void spusim_llr_lost(SpuSim* s);
const char* spusim_sym(uint32_t pc);   // function name of pc (from the loaded ELF), or "?"
void spusim_print_profile(const SpuSim* s, uint64_t total, int top);
// The hottest instruction words of one function (name substring): cycles, mispredicts, address
void spusim_print_hot(const SpuSim* s, const char* fn, int top);

#ifdef __cplusplus
}
#endif
