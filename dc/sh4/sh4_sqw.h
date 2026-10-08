#pragma once
// Store-queue flush, shared by the interpreters (sh4_cpu_prelude.h) and the JIT's PREF targets. A header
// of its own, so ps3_rend.cpp can inline it without the interpreter's register macros.
#include "types.h"
#include "dc/mem/mmu.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "dc/pvr/pvr_if.h"
#include "dc/pvr/pvr_vram.h"
#include "dc/sh4/rec_v2/ccall_census.h"
#include "ps3/ps3_tasplit.h"

// The flush itself: target address from the MMU or QACR, then the TA or a memory block write.
template<bool mmu_on>
INLINE void FASTCALL do_sqw_target(u32 Dest)
{
	u32* sq = (u32*)&sq_both[Dest & 0x20];
	u32 Address;

	if (mmu_on)
	{
		Address = mmu_TranslateSQW(Dest & 0xFFFFFFE0);
	}
	else
	{
		const u32 QACR = ((Dest & 0x20) == 0) ? CCN_QACR0.Area : CCN_QACR1.Area;
		Address = (Dest & 0x03FFFFE0) | (QACR << 26);
	}

	if (((Address >> 26) & 0x7) == 4)  // Area 4 - Tile Accelerator
	{
		TAWriteSQ(Address, sq);
	}
	else
	{
		g_vram_split.sq_mem++;
		WriteMemBlock_nommu_ptr(Address, sq, 32);  // 8 * 4 bytes
	}
}

template<bool mmu_on>
INLINE void FASTCALL do_sqw(u32 Dest)
{
	CCALL(CC_SQW);
	const u64 t_entry = ta_split_enter();
	do_sqw_target<mmu_on>(Dest);
	if (t_entry)
		ta_split_leave(t_entry);
}

// The JIT's PREF targets. do_sqw_nommu lives in ps3/ps3_rend.cpp (plan 5.17 TA SQ Direct).
void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);
void FASTCALL do_sqw_nommu_frame(u32 dst);   // plan 5.36 TA Peak Frame
