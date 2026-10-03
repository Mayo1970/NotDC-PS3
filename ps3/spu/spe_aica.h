#pragma once
// OPTIMIZATION_PLAN 5.13 A1: shared by the PPU (ps3/ps3_aica_spe.cpp) and the SPU (spe_aica.c, spe_aica_model.cpp).
// The PPU runs the caique control pass and logs every AICA register write; the SPU runs the full model from the log.
#include <stdint.h>

#define AICA_STATE_RUNNING 1
#define AICA_STATE_EXITED  2

#define AICA_LOG        16384  // log records, a power of two
#define AICA_BLOCK      64     // samples per CHECK
#define AICA_SNAPS      64     // PPU snapshots kept for mismatch reports, a power of two
#define AICA_BAD_MAX    4      // SPU snapshots of the first mismatches
#define AICA_SNAP_BYTES 1296   // sizeof(caique::CtlSnap), asserted on the PPU

enum { AICA_OP_WRITE, AICA_OP_CHECK, AICA_OP_RESET };

// WRITE: off, val = 16-bit register value. CHECK: val = check number, hash = PPU snapshot hash.
// sample = PPU samples run when the record was made; the SPU steps to it before applying.
typedef struct
{
	uint32_t sample;
	uint8_t op, pad;
	uint16_t off;
	uint32_t val, hash;
} AicaRec;

// PPU writes, SPU holds a reservation on it: a PPU store wakes the SPU.
typedef struct
{
	volatile uint32_t head;  // records written to the log
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) AicaBell;

// SPU writes it with mfc_put; the PPU only reads it.
typedef struct
{
	volatile uint32_t tail;      // records done; the PPU reuses log slots below it
	volatile uint32_t state;
	volatile uint32_t checks, bad, bad_n;  // bad_n: reports written to AicaShared::bad
	volatile uint32_t samples;   // model steps run
	volatile uint32_t busy;      // decrementer ticks spent on records
	volatile uint32_t wakes, timeouts;
	volatile uint32_t misses;    // wave RAM cache line fetches
	volatile uint32_t ls_addr;
	volatile uint32_t prof[6];   // decrementer ticks: slots, envelopes, preview + buses, DSP, mix; slots rendered
	uint32_t pad[15];
} __attribute__((aligned(128))) AicaEcho;

typedef struct
{
	uint32_t aram_ea;  // PS3 aica_ram (DC byte a at host byte a ^ 3)
	uint32_t pad[31];
} __attribute__((aligned(128))) AicaSetup;

typedef struct
{
	uint32_t check, ppu_hash, spu_hash, pad;
	uint8_t snap[AICA_SNAP_BYTES];  // the SPU's caique::CtlSnap at that check
} __attribute__((aligned(16))) AicaBad;

// One block, passed in arg2: the SPU main only gets 3 thread arguments.
typedef struct
{
	AicaSetup setup;
	AicaRec log[AICA_LOG];
	AicaBad bad[AICA_BAD_MAX];
} __attribute__((aligned(128))) AicaShared;
