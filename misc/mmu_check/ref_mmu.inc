// mmu_check: dc/mem/mmu.cpp of commit 27f2a65 (before bundle 13), the lines that the "mmu" section replaces.
// Store Queue fast remap table.
// Maps 1MB virtual SQ regions (0xE0000000–0xE3FFFFFF) to physical page bases.
// Index = bits [25:20] of the virtual address (max 64 regions * 1MB = 64MB).
u32 sq_remap[64];

// Unified TLB: 64 entries (data + instructions)
TLB_Entry UTLB[64];

// Instruction TLB: 4 dedicated entries for instruction fetches
TLB_Entry ITLB[4];


// Invalidate the whole TLB (MMUCR.TI). Clears the V bit on every ITLB/UTLB
// entry AND drops the derived sq_remap fast-path table — the remap slots are a
// cache of UTLB contents, so leaving them populated would keep translations
// alive that the guest believes it just flushed.
void mmu_InvalidateTLB()
{
	for (u32 i = 0; i < 4; i++)
		ITLB[i].Data.V = 0;

	for (u32 i = 0; i < 64; i++)
		UTLB[i].Data.V = 0;

	memset(sq_remap, 0, sizeof(sq_remap));
}

// Sync a UTLB entry into the emulator's fast lookup structures.
// For SQ addresses (0xE0xx_xxxx), updates the sq_remap fast-path table.
// For normal addresses, this is where you would invalidate JIT blocks or
// update software page tables (not yet implemented).
void UTLB_Sync(u32 entry)
{
	if (entry >= 64)
	{
		printf("UTLB_Sync: entry %u out of range\n", entry);
		return;
	}

	// Check if the VPN falls in the Store Queue region (0xE0000000–0xE3FFFFFF).
	// The SQ region occupies the top 4 bits = 0xE, and bit 25 may vary per sub-region.
	// We mask off the top 6 bits (0xFC000000>>10 in VPN space) and compare to 0xE0.
	if ((UTLB[entry].Address.VPN & (0xFC000000 >> 10)) == (0xE0000000 >> 10))
	{
		// Extract the 1MB slot index from bits [25:20] of the virtual address.
		// VPN is the virtual address right-shifted by 10, so bits [15:10] of VPN
		// correspond to address bits [25:20]. Mask to 6 bits for 64 slots.
		u32 sq_index = (UTLB[entry].Address.VPN >> 10) & 0x3F;
		sq_remap[sq_index] = UTLB[entry].Data.PPN << 10;

		printf("SQ remap [%u] slot=%u : virt=0x%08X -> phys=0x%08X\n",
		       entry, sq_index,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10);
	}
	else
	{
		// Normal virtual memory mapping.
		// TODO: invalidate any JIT-compiled blocks covering this virtual range.
		printf("MEM remap [%u] : virt=0x%08X -> phys=0x%08X\n",
		       entry,
		       UTLB[entry].Address.VPN << 10,
		       UTLB[entry].Data.PPN << 10);
	}
}
