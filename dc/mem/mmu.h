#pragma once
#include "types.h"
#include "dc/sh4/ccn.h"

// TLB entry: pairs a virtual address (PTEH) with its physical mapping (PTEL)
struct TLB_Entry
{
	CCN_PTEH_type Address;     // Virtual Page Number + ASID
	CCN_PTEL_type Data;        // Physical Page Number + protection bits
	CCN_PTEA_type Assistance;  // data array 2: SA and TC (P78)
};

// SH4 Unified TLB: 64 entries for data + instruction translation
extern TLB_Entry UTLB[64];

// SH4 Instruction TLB: 4 entries, dedicated for instruction fetches
extern TLB_Entry ITLB[4];

// Store Queue fast remap table: caches physical addresses for 0xE0000000-0xE3FFFFFF
// Each entry covers a 1MB region (64 entries * 1MB = 64MB max SQ space)
extern u32 sq_remap[64];

// MMUCR.TI: every ITLB/UTLB entry loses its V bit; with MMUCR Bits OLD the sq_remap table is cleared too
void mmu_InvalidateTLB();

// Sync a single UTLB entry into the fast remap tables
// entry: index 0-63; call after writing a new UTLB entry
void UTLB_Sync(u32 entry);

// Sync a single ITLB entry (currently logs only)
// entry: index 0-3
void ITLB_Sync(u32 entry);

void MMU_Init();
void MMU_Reset(bool Manual);
void MMU_Term();

// The physical address of a store queue write to adr (0xE0000000-0xE3FFFFFF): the sq_remap slot of address bits
// 25-20, with the low 20 bits kept.
#define mmu_TranslateSQW(adr) (sq_remap[((adr) >> 20) & 0x3F] | ((adr) & 0xFFFFF))
