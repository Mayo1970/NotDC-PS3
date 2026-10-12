// mmu_check: dc/mem/sh4_internal_reg.cpp of commit 27f2a65 (before bundle 13), the lines that the "p4" section replaces.
// P4 (0xE0000000-0xFFFFFFFF): store queues, cache/TLB arrays and area7 on-chip registers.
// TLB page-size masks by SZ field: 0=1KB, 1=4KB, 2=64KB, 3=1MB.
static const u32 mmu_mask_2[4] =
{
	((0xFFFFFFFF) >> 10) << 0,   // 1 KB page
	((0xFFFFFFFF) >> 12) << 2,   // 4 KB page
	((0xFFFFFFFF) >> 16) << 6,   // 64 KB page
	((0xFFFFFFFF) >> 20) << 10,  // 1 MB page
};

template <u32 sz, class T>
T FASTCALL ReadMem_P4(u32 addr)
{
	switch ((addr >> 24) & 0xFF)
	{
	// Store queues (0xE0-0xE3): fallback, normally handled by vmem block map
	case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		return 0;

	// Instruction cache address array (F0): not emulated
	case 0xF0:
		return 0;

	// Instruction cache data array (F1): not emulated
	case 0xF1:
		return 0;

	// ITLB address array (F2)
	case 0xF2:
		{
			u32 entry = (addr >> 8) & 3;
			return (T)(ITLB[entry].Address.reg_data | (ITLB[entry].Data.V << 8));
		}

	// ITLB data arrays 1 and 2 (F3)
	case 0xF3:
		{
			u32 entry = (addr >> 8) & 3;
			return (T)ITLB[entry].Data.reg_data;
		}

	// Operand cache address array (F4): not emulated
	case 0xF4:
		return 0;

	// Operand cache data array (F5): not emulated
	case 0xF5:
		return 0;

	// UTLB address array (F6)
	case 0xF6:
		{
			u32 entry = (addr >> 8) & 63;
			u32 rv = UTLB[entry].Address.reg_data;
			rv |= UTLB[entry].Data.D << 9;
			rv |= UTLB[entry].Data.V << 8;
			return (T)rv;
		}

	// UTLB data arrays 1 and 2 (F7)
	case 0xF7:
		{
			u32 entry = (addr >> 8) & 63;
			return (T)UTLB[entry].Data.reg_data;
		}

	// Area7 (FF): should be routed via area7 handler, not here
	case 0xFF:
		return 0;

	default:
		// Outside defined P4 sub-regions (e.g. 0xECxxxxxx).
		// Not a real SH4 address — likely a guest bug. Return 0 safely.
		return 0;
	}
}

template <u32 sz, class T>
void FASTCALL WriteMem_P4(u32 addr, T data)
{
	switch ((addr >> 24) & 0xFF)
	{
	// Store queues: fallback, normally handled by vmem block map
	case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		return;

	// Instruction cache address array (F0): ignore writes (no cache emulation)
	case 0xF0:
		return;

	// Instruction cache data array (F1): ignore writes
	case 0xF1:
		return;

	// ITLB address array (F2)
	case 0xF2:
		{
			u32 entry = (addr >> 8) & 3;
			ITLB[entry].Address.reg_data = data & 0xFFFFFEFF;
			ITLB[entry].Data.V = ((u32)data >> 8) & 1;
			ITLB_Sync(entry);
			return;
		}

	// ITLB data arrays (F3)
	case 0xF3:
		{
			u32 entry = (addr >> 8) & 3;
			ITLB[entry].Data.reg_data = data;
			ITLB_Sync(entry);
			return;
		}

	// Operand cache address array (F4): ignore writes
	case 0xF4:
		return;

	// Operand cache data array (F5): ignore writes
	case 0xF5:
		return;

	// UTLB address array (F6)
	case 0xF6:
		{
			if (addr & 0x80)
			{
				// Associative write: match VPN+ASID across all TLB entries
				CCN_PTEH_type t;
				t.reg_data = data;

				for (int i = 0; i < 64; i++)
				{
					u32 tsz  = UTLB[i].Data.SZ1 * 2 + UTLB[i].Data.SZ0;
					u32 mask = mmu_mask_2[tsz];
					u32 vpn  = t.VPN & mask;

					if (((UTLB[i].Address.VPN & mask) == vpn) &&
					    (UTLB[i].Address.ASID == CCN_PTEH.ASID))
					{
						UTLB[i].Data.V = ((u32)data >> 8) & 1;
						UTLB[i].Data.D = ((u32)data >> 9) & 1;
					}
				}

				for (int i = 0; i < 4; i++)
				{
					u32 tsz  = ITLB[i].Data.SZ1 * 2 + ITLB[i].Data.SZ0;
					u32 mask = mmu_mask_2[tsz];
					u32 vpn  = t.VPN & mask;

					if (((ITLB[i].Address.VPN & mask) == vpn) &&
					    (ITLB[i].Address.ASID == CCN_PTEH.ASID))
					{
						ITLB[i].Data.V = ((u32)data >> 8) & 1;
						ITLB[i].Data.D = ((u32)data >> 9) & 1;
					}
				}
			}
			else
			{
				u32 entry = (addr >> 8) & 63;
				UTLB[entry].Address.reg_data = data & 0xFFFFFCFF;
				UTLB[entry].Data.D = ((u32)data >> 9) & 1;
				UTLB[entry].Data.V = ((u32)data >> 8) & 1;
				UTLB_Sync(entry);
			}
			return;
		}

	// UTLB data arrays (F7)
	case 0xF7:
		{
			u32 entry = (addr >> 8) & 63;
			UTLB[entry].Data.reg_data = data;
			UTLB_Sync(entry);
			return;
		}

	// Area7 (FF): fallback
	case 0xFF:
		return;

	default:
		// Outside defined P4 sub-regions — discard silently
		return;
	}
}
