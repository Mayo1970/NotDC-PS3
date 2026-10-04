#pragma once
// OPTIMIZATION_PLAN 5.15: shared by the PPU (ps3/ps3_arm_spe.cpp) and the ARM7 SPU (spe_arm7.cpp). The PPU queues
// MediumUpdates and SH4 register accesses; the SPU runs the ARM7, nullAICA's registers and the caique control twin.
#include <stdint.h>

#define ARM_SPE_STATE_RUNNING 1
#define ARM_SPE_STATE_EXITED  2

#define ARM_CMDS 4096   // command ring entries, a power of two

enum { ARM_CMD_MU = 1, ARM_CMD_WR, ARM_CMD_RD, ARM_CMD_CDDA, ARM_CMD_RESET, ARM_CMD_FLUSH };

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
	uint32_t pad[8];
} __attribute__((aligned(128))) ArmEcho;

typedef struct
{
	uint32_t ring_ea;        // ArmCmd[ARM_CMDS]
	uint32_t aram_ea;        // PS3 aica_ram: DC byte a at host byte a ^ 3
	uint32_t aica_bell_ea, aica_echo_ea, aica_log_ea;   // the sound SPU's AicaBell, AicaEcho, AicaShared::log
	uint32_t pad[27];
} __attribute__((aligned(128))) ArmSetup;
