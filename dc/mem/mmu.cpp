#include "mmu.h"
#include "dc/sh4/sh4_if.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/sh4_fullmmu.h"
#include "plugins/plugin_manager.h"

#include "_vmem.h"

// Row "SH4 MMU" (P38): Windows CE rewrites TLB entries all the time, so only the first remap lines are printed then.
static u32 s_remap_logs;

// mmu_check: begin mmu
// Store queue remap table: the physical base of each 1 MB region of 0xE0000000-0xE3FFFFFF, by address bits 25-20.
u32 sq_remap[64];

// Unified TLB: 64 entries (data + instructions)
TLB_Entry UTLB[64];

// Instruction TLB: 4 dedicated entries for instruction fetches
TLB_Entry ITLB[4];


// MMUCR.TI: every ITLB and UTLB entry loses its V bit. OLD also clears sq_remap; MMUCR Bits FIXED keeps it, as
// Flycast's mmu_flush_table: no TLB miss comes here to load a slot again (P58).
void mmu_InvalidateTLB()
{
	for (u32 i = 0; i < 4; i++)
		ITLB[i].Data.V = 0;

	for (u32 i = 0; i < 64; i++)
		UTLB[i].Data.V = 0;

	if (!g_mmu_bits_preset)
		memset(sq_remap, 0, sizeof(sq_remap));
}

// Syncs a UTLB entry into the lookup tables: an entry in the store queue area sets its sq_remap slot. Other
// addresses are only logged (no JIT block is dropped, no page table is kept).
void UTLB_Sync(u32 entry)
{
	if (entry >= 64)
	{
		printf("UTLB_Sync: entry %u out of range\n", entry);
		return;
	}

	// The store queue area: the top 6 address bits are 0xE0-0xE3 (VPN is the address >> 10).
	if ((UTLB[entry].Address.VPN & (0xFC000000 >> 10)) == (0xE0000000 >> 10))
	{
		// VPN bits 15-10 are address bits 25-20: the 1 MB slot.
		u32 sq_index = (UTLB[entry].Address.VPN >> 10) & 0x3F;

		// P58 (Flycast's UTLB_Sync): an entry that is not valid maps nothing; its slot keeps its address.
		if (g_mmu_bits_preset && !UTLB[entry].Data.V)
		{
			printf("SQ remap [%u] slot=%u : virt=0x%08X not valid, slot stays 0x%08X\n",
			       entry, sq_index, UTLB[entry].Address.VPN << 10, sq_remap[sq_index]);
			return;
		}
		sq_remap[sq_index] = UTLB[entry].Data.PPN << 10;

		printf("SQ remap [%u] slot=%u : virt=0x%08X -> phys=0x%08X V=%u\n",
		       entry, sq_index,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10, (u32)UTLB[entry].Data.V);
	}
	else if (!g_fullmmu_active || s_remap_logs++ < 16)
	{
		// Normal virtual memory mapping. TODO: invalidate any JIT-compiled blocks covering this virtual range.
		printf("MEM remap [%u] : virt=0x%08X -> phys=0x%08X\n",
		       entry,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10);
	}
}
// mmu_check: end mmu

// Sync an ITLB entry. Currently only logs; extend to flush instruction caches
// or invalidate JIT blocks covering the affected virtual range as needed.
void ITLB_Sync(u32 entry)
{
	if (entry >= 4)
	{
		printf("ITLB_Sync: entry %u out of range\n", entry);
		return;
	}

	if (g_fullmmu_active && s_remap_logs++ >= 16)
		return;
	printf("ITLB remap [%u] : virt=0x%08X -> phys=0x%08X\n",
	       entry,
	       ITLB[entry].Address.VPN << 10,
	       ITLB[entry].Data.PPN << 10);
}

void MMU_Init()
{
	// Zero all TLB entries and SQ remap table on startup.
	memset(UTLB,    0, sizeof(UTLB));
	memset(ITLB,    0, sizeof(ITLB));
	memset(sq_remap, 0, sizeof(sq_remap));
}

void MMU_Reset(bool Manual)
{
	// A reset clears all translation state: the SH4's TLBs are undefined after it, and software loads them again.
	memset(UTLB,    0, sizeof(UTLB));
	memset(ITLB,    0, sizeof(ITLB));
	memset(sq_remap, 0, sizeof(sq_remap));

	if (Manual)
		printf("MMU: manual reset\n");
	else
		printf("MMU: power-on reset\n");
}

void MMU_Term()
{
	// Nothing to free (all storage is statically allocated).
}
