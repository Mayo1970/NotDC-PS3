#pragma once
// OPTIMIZATION_PLAN 5.12 S1: shared by the PPU (ps3/ps3_spe.cpp) and the SPU (spe_bell.c).
// Bell and echo each fill one 128-byte lock line, so a store to the bell never touches the echo.
#include <stdint.h>

#define SPE_STATE_RUNNING 1
#define SPE_STATE_EXITED  2

#define SPE_RING    4096  // command entries, a power of two
#define SPE_RESULTS 16    // fence result slots
#define SPE_CTX     4     // TA_CONTEXTS in ps3_rend.cpp

enum { SPE_OP_NOP, SPE_OP_DATA, SPE_OP_RESET, SPE_OP_CONT, SPE_OP_SOFT, SPE_OP_FENCE };
enum { SPE_STAGE_RUN, SPE_STAGE_RING, SPE_STAGE_DATA, SPE_STAGE_FENCE, SPE_STAGE_ECHO, SPE_STAGE_EVENT };

// DATA: ea = raw + from, a = bytes, b = to. CONT: b = mark. FENCE: a = fence id, b = raw end.
typedef struct
{
	uint8_t op, ctx;
	uint16_t pad;
	uint32_t ea, a, b;
} SpeCmd;

// PPU writes, SPU holds a reservation on it. A PPU store kills the reservation and wakes the SPU.
typedef struct
{
	volatile uint32_t head;  // commands written to the ring
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) SpeBell;

// SPU writes it with mfc_put; the PPU only reads it.
typedef struct
{
	volatile uint32_t head;       // last bell head the SPU saw
	volatile uint32_t tail;       // commands done; the PPU reuses ring slots below it
	volatile uint32_t fence;      // last fence id in the results
	volatile uint32_t handled;    // wakes that found new commands
	volatile uint32_t wakes;      // reservation-lost events
	volatile uint32_t timeouts;   // 1 ms decrementer events
	volatile uint32_t rescued;    // new commands found by a timeout with no lost event pending
	volatile uint32_t state;
	volatile uint32_t data_bytes; // raw bytes pulled
	volatile uint32_t gaps;       // DATA that did not start at its context's position
	volatile uint32_t cont_errs;  // CONT whose mark was not the context's position
	// Debug: the PPU reads these from LS on a stall (the copy in main memory is stale by then).
	volatile uint32_t ls_addr;    // LS address of the SPU's echo
	volatile uint32_t stage;      // SPE_STAGE_*
	volatile uint32_t cmd_op, cmd_ctx, cmd_ea, cmd_a, cmd_b;
	volatile uint32_t dma_ea, dma_size;
	uint32_t pad[12];
} __attribute__((aligned(128))) SpeEcho;

// Fletcher sums per word lane (word index mod 4): a += w, b += a. The SPU adds one quadword per step.
typedef struct
{
	uint32_t a[4], b[4];
} SpeHash;

// One per FENCE: the context's stream since its last RESET, as the SPU pulled it.
typedef struct
{
	uint32_t id, ctx, pos, ncont;
	uint32_t gaps, pad[3];
	SpeHash hash;
} __attribute__((aligned(64))) SpeFence;

// One block for the ring and the results: the SPU main only gets 3 thread arguments (arg3 arrives as 0).
typedef struct
{
	SpeCmd ring[SPE_RING];
	SpeFence results[SPE_RESULTS];
} __attribute__((aligned(128))) SpeShared;
