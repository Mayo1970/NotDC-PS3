#pragma once
// OPTIMIZATION_PLAN 5.12 S0: shared by the PPU (ps3/ps3_spe.cpp) and the SPU (spe_bell.c).
// Each struct fills one 128-byte lock line, so a store to the bell never touches the echo.
#include <stdint.h>

#define SPE_STATE_RUNNING 1
#define SPE_STATE_EXITED  2

// PPU writes, SPU holds a reservation on it. A PPU store kills the reservation and wakes the SPU.
typedef struct
{
	volatile uint32_t seq;
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) SpeBell;

// SPU writes it with mfc_put; the PPU only reads it.
typedef struct
{
	volatile uint32_t seq;       // last bell seq the SPU saw
	volatile uint32_t handled;   // wakes that found a new seq
	volatile uint32_t wakes;     // reservation-lost events
	volatile uint32_t timeouts;  // 1 ms decrementer events
	volatile uint32_t rescued;   // new seq found by a timeout alone: a missed reservation event
	volatile uint32_t state;
	uint32_t pad[26];
} __attribute__((aligned(128))) SpeEcho;
