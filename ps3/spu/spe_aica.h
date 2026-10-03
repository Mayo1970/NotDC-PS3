#pragma once
// OPTIMIZATION_PLAN 5.13 A1/A2: shared by the PPU (ps3/ps3_aica_spe.cpp) and the SPU (spe_aica.c, spe_aica_model.cpp).
// The PPU runs the caique control pass and logs every AICA register write; the SPU runs the full model from the log.
#include <stdint.h>

#define AICA_STATE_RUNNING 1
#define AICA_STATE_EXITED  2

#define AICA_LOG        16384  // log records, a power of two
#define AICA_BLOCK      64     // samples per CHECK
#define AICA_SNAPS      64     // PPU snapshots kept for mismatch reports, a power of two
#define AICA_BAD_MAX    4      // SPU snapshots of the first mismatches
#define AICA_SNAP_BYTES 1296   // sizeof(caique::CtlSnap), asserted on the PPU

// 5.13 A2 CD-DA: sectors in flight (a power of two) and s16 per 2352-byte sector, L/R pairs in host order
#define AICA_CDDA_SLOTS  8
#define AICA_CDDA_WORDS  1176
#define AICA_CDDA_SILENT 0xFFFFFFFFu

enum { AICA_OP_WRITE, AICA_OP_CHECK, AICA_OP_RESET, AICA_OP_CDDA };

// WRITE: off, val = 16-bit register value. CHECK: val = check number, hash = PPU snapshot hash. CDDA: val = cdda slot
// or AICA_CDDA_SILENT. sample = PPU samples run when the record was made; the SPU steps to it before applying.
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

// 5.13 A1b DSP recompiler. Modes: 0 interpreter, 1 recompiler, 2 recompiler + interpreter check.
#define AICA_DSP_BAD_MAX 2    // mismatch reports
#define AICA_DSP_LOG     128  // ring accesses kept per checked sample
// dsp_flags bits; the self-test sample count is in bits 16..31
#define AICA_DSPF_MODE    3u
#define AICA_DSPF_TESTED  0x100u
#define AICA_DSPF_FAILED  0x200u   // self-test mismatch: recompiler off
#define AICA_DSPF_ISA     0x400u   // ISA probe mismatch: recompiler off
// AicaDspBad.flags: the parts that differ
#define AICA_DSPD_TEMP   0x001u
#define AICA_DSPD_MEMS   0x002u
#define AICA_DSPD_EFREG  0x004u
#define AICA_DSPD_ACC    0x008u
#define AICA_DSPD_FRC    0x010u
#define AICA_DSPD_Y      0x020u
#define AICA_DSPD_ADRS   0x040u
#define AICA_DSPD_MEMVAL 0x080u
#define AICA_DSPD_PIPE   0x100u   // nofl_pipe, mem_q, mrd_even_masa
#define AICA_DSPD_RING   0x200u
#define AICA_DSPD_FAULT  0x400u   // PC test only: the simulator faulted
#define AICA_DSPD_SELF   0x80000000u   // from the boot self-test

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
	volatile uint32_t dsp_flags;
	volatile uint32_t dsp_compiles, dsp_fails, dsp_odd;   // odd: samples the interpreter ran for an unusual state
	volatile uint32_t dsp_words, dsp_live, dsp_slow;      // the current program
	volatile uint32_t dsp_checks, dsp_bad, dsp_bad_n;
	volatile uint32_t dsp_ticks;  // decrementer ticks in generated code
	volatile uint32_t ls_end;     // end of the SPU image in LS
	volatile uint32_t phase;      // LS copy only, for a stall dump: 1 generated code, 2 interpreter, 3 compile, 4 self-test
	volatile uint32_t snd_frames, snd_drops;   // 5.13 A2: 48 kHz frames written to the ring; dropped, the ring was full
	volatile uint32_t cdda;       // CD-DA records applied
	uint32_t pad[31];
} __attribute__((aligned(128))) AicaEcho;

typedef struct
{
	uint32_t aram_ea;  // PS3 aica_ram (DC byte a at host byte a ^ 3)
	uint32_t dsp_mode;
	uint32_t snd;      // 5.13 A2: the SPU makes the sound, without the DSP, into ps3_audio.cpp's ring
	uint32_t ring_ea, ring_frames, wr_ea, rd_ea, w0;   // ring (2 floats a frame), ring_wr, ring_rd, ring_wr at boot
	uint32_t pad[24];
} __attribute__((aligned(128))) AicaSetup;

// One CD-DA sector, 128-byte aligned for the SPU's DMA
typedef struct
{
	int16_t s[AICA_CDDA_WORDS];
	uint8_t pad[80];
} __attribute__((aligned(128))) AicaCddaSlot;

typedef struct
{
	uint32_t check, ppu_hash, spu_hash, pad;
	uint8_t snap[AICA_SNAP_BYTES];  // the SPU's caique::CtlSnap at that check
} __attribute__((aligned(16))) AicaBad;

// The DSP state one sample changes (caique::AicaModel fields)
typedef struct
{
	int32_t temp[128], mems[32], efreg[16];
	int32_t acc, frc, y;
	uint32_t adrs, memval, nofl;   // nofl = nofl_pipe[0] | nofl_pipe[1] << 8
	int32_t mem_qn, mrd_even_masa;
	uint32_t land_lo, land_hi, qval, pad;   // mem_q[0] when mem_qn = 1
} AicaDspState;

typedef struct
{
	uint32_t a;
	uint16_t v, jv;   // the interpreter's value; jv = the ring after the recompiled code
} AicaDspRingRec;

// A checked sample that differs: enough to replay it on the PC (start state, inputs, program)
typedef struct
{
	uint32_t sample_lo, mdec, rbpl, flags;
	uint32_t prog_id, nw, nr, pad;
	AicaDspState start, interp, jit;
	uint16_t mpro[512];               // registers 0x3400-0x3BFF
	uint16_t coef[128], madrs[64];
	int32_t mixs[16], exts[2], pad2[2];
	uint8_t coll_claim[64];
	uint16_t coll_word[64];
	AicaDspRingRec wr[AICA_DSP_LOG];   // the interpreter's ring writes, in order
	AicaDspRingRec rd[AICA_DSP_LOG];   // and its ring reads (jv unused)
} __attribute__((aligned(16))) AicaDspBad;

typedef struct
{
	uint32_t form;
	int32_t imm;
	uint32_t pad[2];
	uint32_t in[12], exp[4], got[4];
} __attribute__((aligned(16))) AicaDspIsaBad;

// One block, passed in arg2: the SPU main only gets 3 thread arguments.
typedef struct
{
	AicaSetup setup;
	AicaRec log[AICA_LOG];
	AicaBad bad[AICA_BAD_MAX];
	AicaDspBad dbad[AICA_DSP_BAD_MAX];
	AicaDspIsaBad isabad;
	AicaCddaSlot cdda[AICA_CDDA_SLOTS];
} __attribute__((aligned(128))) AicaShared;
