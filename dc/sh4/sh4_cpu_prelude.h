#pragma once
#include "dc/mem/mmu.h"
#include "dc/pvr/pvr_if.h"
// SH4 interpreter opcode prelude: decode + memory macros shared by the plain
// handlers (sh4_cpu.cpp) and the cached engine (sh4_cached.cpp).

// OPCODE DECODE MACROS
#define GetN(str)       ((str >> 8) & 0xF)
#define GetM(str)       ((str >> 4) & 0xF)
#define GetImm4(str)    ((str >> 0) & 0xF)
#define GetImm8(str)    ((str >> 0) & 0xFF)
#define GetSImm8(str)   ((s8)((str >> 0) & 0xFF))
#define GetImm12(str)   ((str >> 0) & 0xFFF)
#define GetSImm12(str)  (((s16)((GetImm12(str)) << 4)) >> 4)

#define iNimp  cpu_iNimp
#define iWarn  cpu_iWarn

// MEMORY ACCESS MACROS
// Read operations with sign/zero extension
#define ReadMemU32(to, addr)            to = ReadMem32(addr)
#define ReadMemS32(to, addr)            to = (s32)ReadMem32(addr)
#define ReadMemS16(to, addr)            to = (u32)(s32)(s16)ReadMem16(addr)
#define ReadMemS8(to, addr)             to = (u32)(s32)(s8)ReadMem8(addr)

// Base + offset read operations
#define ReadMemBOU32(to, addr, offset)  ReadMemU32(to, addr + offset)
#define ReadMemBOS16(to, addr, offset)  ReadMemS16(to, addr + offset)
#define ReadMemBOS8(to, addr, offset)   ReadMemS8(to, addr + offset)

// Write operations
#define WriteMemU32(addr, data)         WriteMem32(addr, (u32)data)
#define WriteMemU16(addr, data)         WriteMem16(addr, (u16)data)
#define WriteMemU8(addr, data)          WriteMem8(addr, (u8)data)

// Base + offset write operations
#define WriteMemBOU32(addr, offset, data) WriteMemU32(addr + offset, data)
#define WriteMemBOU16(addr, offset, data) WriteMemU16(addr + offset, data)
#define WriteMemBOU8(addr, offset, data)  WriteMemU8(addr + offset, data)


void cpu_iNimp(u32 op, const char* info);
void cpu_iWarn(u32 op, const char* info);

// Branch displacement helpers (were file-scope in sh4_cpu_branch.h; moved here so
// that header holds opcode bodies only and can be re-included as label bodies).
static INLINE u32 branch_target_s8(u32 op)
{
	return GetSImm8(op) * 2 + next_pc + 2;
}

static INLINE u32 branch_target_s12(u32 op)
{
	return GetSImm12(op) * 2 + next_pc + 2;
}

// Store-queue flush helper. Lives here, not with the opcode bodies, because
// sh4_cached.cpp includes those inside a function and a template cannot be
// declared at block scope.
// STORE QUEUE OPERATIONS
// Store Queue Write helper - template for MMU on/off
template<bool mmu_on>
INLINE void FASTCALL do_sqw(u32 Dest)
{
	CCALL(CC_SQW);
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
		WriteMemBlock_nommu_ptr(Address, sq, 32);  // 8 * 4 bytes
	}
}

extern bool sh4_sleeping;
void FASTCALL do_sqw_mmu(u32 dst);
void FASTCALL do_sqw_nommu(u32 dst);
