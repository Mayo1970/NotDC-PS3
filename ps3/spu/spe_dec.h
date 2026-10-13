#pragma once
// OPTIMIZATION_PLAN 5.12 S2: the SPU TA decoder (spe_dec.c), driven by the command loop in spe_bell.c.
#include "spe_bell.h"

#define TAG_OUT0     8                      // two per output stream: 8..19
#define TAG_OUT_MASK (0xFFFu << TAG_OUT0)

extern SpeEcho echo;

void dec_init(const SpeSetup* setup);
// RESET: the replay's ListInit, plus empty lists for ctx; opt is the context's TAOPT_* from here on.
void dec_reset(uint32_t ctx, uint32_t opt);
// Records of one pulled chunk; n >= 1. 64-byte records may span two chunks.
void dec_records(uint32_t ctx, const uint32_t* rec, uint32_t n);
// Puts every list of ctx out, waits for the DMA and fills the counts of res.
void dec_fence(uint32_t ctx, SpeFence* res);
