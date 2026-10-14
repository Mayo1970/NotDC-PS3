#pragma once
#include "dc/sh4/sh4_sqw.h"	// do_sqw; not with the opcode bodies, which sh4_cached.cpp includes at block scope
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

// Interp Fixes (plan section 7, bundle 20): 1 = mac.w and mac.l with S=1 saturate through shil.cpp's copy.
extern int g_interp_fix_preset;
extern "C" void sh4_mac_l(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs);
extern "C" void sh4_mac_w(u32 a, u32 b, u32 oldl, u32 oldh, u32 srs);

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

extern bool sh4_sleeping;
