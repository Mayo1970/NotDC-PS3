#pragma once
// OPTIMIZATION_PLAN 5.15: shared by the PPU (ps3/ps3_arm_spe.cpp) and the ARM7 SPU (spe_arm7.cpp). The PPU queues
// MediumUpdates and SH4 register accesses; the SPU runs the ARM7, nullAICA's registers and the caique control twin.
#include <stdint.h>

#define ARM_SPE_STATE_RUNNING 1
#define ARM_SPE_STATE_EXITED  2

#define ARM_CMDS 4096   // command ring entries, a power of two

// Plan 6.6 SYNC: b = the wave RAM byte the SH4 is about to write, c = 1 for a G2 DMA. CFG: b = ARM_CFG_* (live).
enum { ARM_CMD_MU = 1, ARM_CMD_WR, ARM_CMD_RD, ARM_CMD_CDDA, ARM_CMD_RESET, ARM_CMD_FLUSH, ARM_CMD_SYNC, ARM_CMD_CFG };
#define ARM_CFG_SYNC  1u   // "SPE AICA Sync": the hazard map marks the slots' play points
#define ARM_CFG_BLOCK 2u   // + BLOCK SYNC: the first hit of a 64-sample block syncs with no test
#define ARM_CFG_ALL   4u   // + SAMPLE SYNC: each sample with a hit syncs with no test

// ArmEcho.hz_res after a SYNC: nothing to wait for; the sound SPU caught up; over the block's limit; sync is off
enum { ARM_HZ_NONE, ARM_HZ_SYNCED, ARM_HZ_MISS, ARM_HZ_DEAD };

// MU flags: Arm7Batch in bits 0-4, the arm7_speed stage in 5-6, Idle Skip SAFE in 7
#define ARM_MU_BATCH(f) ((f) & 31u)
#define ARM_MU_STAGE(f) (((f) >> 5) & 3u)
#define ARM_MU_SAFE(f)  (((f) >> 7) & 1u)

// 16 bytes. dlo..dhi: the ARAM lines (128 bytes) the SH4 wrote before this command; dhi < dlo = none.
typedef struct
{
	uint8_t op, flags;
	uint16_t a;    // MU: AICA samples; WR / RD: register offset
	uint32_t b;    // MU: SH4 cycles; WR: data; RD: sequence; CDDA: AicaShared cdda slot or AICA_CDDA_SILENT
	uint16_t dlo, dhi;
	uint32_t c;    // WR / RD: size
} ArmCmd;

// The PPU writes it; the SPU holds a reservation on it, so a PPU store wakes the SPU.
typedef struct
{
	volatile uint32_t head;   // commands written
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) ArmBell;

// The SPU puts it after each batch of commands; the PPU only reads it.
typedef struct
{
	volatile uint32_t tail;     // commands done
	volatile uint32_t state;
	volatile uint32_t irq;      // the Holly SPU interrupt level after the last command
	volatile uint32_t rd_seq, rd_val;   // the last register read
	volatile uint32_t samples;  // AICA samples run
	volatile uint32_t runs, ticks, fiqs;   // ARM7 slices with the core enabled, ticks given to them, FIQs taken
	volatile uint32_t decodes, evicts;     // block map
	volatile uint32_t misses, puts;        // ARAM line fetches, ARAM write puts
	volatile uint32_t busy;     // decrementer ticks spent on commands
	volatile uint32_t wakes, timeouts;
	volatile uint32_t cmds, recs;          // commands done, log records written
	volatile uint32_t log_waits, log_drops, cdda_under, mon_reads, checks;
	volatile uint32_t ls_end;   // end of the SPU image in LS
	// [aicafix]: slices extended, SH4 interrupts raised under AICA Race Fix; ARM7 resets and stale latches under
	// ARM7 Reset Fix; CA reads and clamps under CA Clamp
	volatile uint32_t races, raises, resets, stale, ca_reads, ca_clamped;
	volatile uint32_t hz_res;   // plan 6.6: ARM_HZ_* of the last SYNC command
	uint32_t pad[1];
} __attribute__((aligned(128))) ArmEcho;

// ArmSetup flags, set before boot: the Looney Tunes fixes (OPTIMIZATION_PLAN 6.5, P44, P31), then bundle 5
#define ARM_SETUP_RACE_FIX  1u
#define ARM_SETUP_RESET_FIX 2u
#define ARM_SETUP_CA_CLAMP  4u
#define ARM_SETUP_LFORE     8u    // P31: LFORE stays set after a write to the LFO register
#define ARM_SETUP_RING      16u   // P20 G2: the sound SPU's DSP ring (up to 32K words) mirrors wave RAM
#define ARM_SETUP_RING64    32u   // P20 G3: so does a 64K-word ring, through its line cache
#define ARM_SETUP_DSPRD     64u   // P20 G4: a read of TEMP, MEMS, MIXS or EFREG asks the sound SPU

typedef struct
{
	uint32_t ring_ea;        // ArmCmd[ARM_CMDS]
	uint32_t aram_ea;        // PS3 aica_ram: DC byte a at host byte a ^ 3
	uint32_t aica_bell_ea, aica_echo_ea, aica_log_ea;   // the sound SPU's AicaBell, AicaEcho, AicaShared::log
	uint32_t flags;          // ARM_SETUP_*
	uint32_t hz_ea;          // AicaHz (spe_aica.h): the hazard map and counts this SPU puts
	uint32_t pad[25];
} __attribute__((aligned(128))) ArmSetup;
