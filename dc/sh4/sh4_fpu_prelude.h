#pragma once
// SH4 FPU prelude: macros and helpers shared by sh4_fpu.cpp and sh4_cached.cpp.

// Possible later: AltiVec for FIPR/FTRV, a cache-friendlier data layout.

// ---- Runtime accuracy selection ----

// The user's accuracy mode, defined in ps3_presets.cpp.
extern "C" int get_accuracy_preset();
// Interp Fixes (plan section 7, bundle 20): 1 = P32's fneg and mac fixes.
extern int g_interp_fix_preset;

// Helper macros to check current accuracy mode
#define FAST() (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

#include "types.h"
#include <math.h>
#include <float.h>

#include "sh4_interpreter.h"
#include "sh4_registers.h"
#include "dc/mem/sh4_mem.h"

// ---- Macros and helper functions ----

#define sh4op(str) void FASTCALL str(u32 op)
#define GetN(str) ((str >> 8) & 0xf)
#define GetM(str) ((str >> 4) & 0xf)
#define GetImm4(str) ((str >> 0) & 0xf)
#define GetImm8(str) ((str >> 0) & 0xff)
#define GetImm12(str) ((str >> 0) & 0xfff)

#define GetDN(opc) ((op & 0x0F00) >> 9)
#define GetDM(opc) ((op & 0x00F0) >> 5)

void iNimp(const char *str);

#define IS_DENORMAL(f) (((*(f)) & 0x7f800000) == 0)

#define ReadMemU64(to, addr) to = ReadMem64(addr)
#define ReadMemU32(to, addr) to = ReadMem32(addr)
#define ReadMemS32(to, addr) to = (s32)ReadMem32(addr)
#define ReadMemS16(to, addr) to = (u32)(s32)(s16)ReadMem16(addr)
#define ReadMemS8(to, addr) to = (u32)(s32)(s8)ReadMem8(addr)

// Base,offset format
#define ReadMemBOU32(to, addr, offset) ReadMemU32(to, addr + offset)
#define ReadMemBOS16(to, addr, offset) ReadMemS16(to, addr + offset)
#define ReadMemBOS8(to, addr, offset) ReadMemS8(to, addr + offset)

// Write Mem Macros
#define WriteMemU64(addr, data) WriteMem64(addr, (u64)data)
#define WriteMemU32(addr, data) WriteMem32(addr, (u32)data)
#define WriteMemU16(addr, data) WriteMem16(addr, (u16)data)
#define WriteMemU8(addr, data) WriteMem8(addr, (u8)data)

// Base,offset format
#define WriteMemBOU32(addr, offset, data) WriteMemU32(addr + offset, data)
#define WriteMemBOU16(addr, offset, data) WriteMemU16(addr + offset, data)
#define WriteMemBOU8(addr, offset, data) WriteMemU8(addr + offset, data)

// ---- NaN fixing, for better accuracy ----

// Fix NaN values according to SH4 spec
INLINE float fixNaN(float value)
{
  u32 *v = (u32 *)&value;
  // If NaN, return default NaN (0x7FFFFFFF for positive, 0xFFFFFFFF for negative)
  if ((*v & 0x7F800000) == 0x7F800000 && (*v & 0x007FFFFF) != 0)
  {
    *v = (*v & 0x80000000) | 0x7FFFFFFF;
  }
  return value;
}

INLINE double fixNaN64(double value)
{
  u64 *v = (u64 *)&value;
  // If NaN, return default NaN
  if ((*v & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (*v & 0x000FFFFFFFFFFFFFULL) != 0)
  {
    *v = (*v & 0x8000000000000000ULL) | 0x7FFFFFFFFFFFFFFFULL;
  }
  return value;
}

// ---- Denormal handling ----

INLINE void Denorm32(float &value)
{
  if (fpscr.DN)
  {
    u32 *v = (u32 *)&value;
    if (IS_DENORMAL(v) && (*v & 0x7fFFFFFF) != 0)
    {
      *v &= 0x80000000;
    }
  }
}

// Use fixNaN for better accuracy (improvement)
#define CHECK_FPU_32(v) v = fixNaN(v)
#define CHECK_FPU_64(v) v = fixNaN64(v)

// Placeholder macros for potential future rounding mode control
#define START64()
#define END64()
#define STARTMODE64()
#define ENDMODE64()

// ---- Helper functions for double precision ----

static INLINE double getDRn(u32 op)
{
  return GetDR((op >> 9) & 7);
}

static INLINE double getDRm(u32 op)
{
  return GetDR((op >> 5) & 7);
}

static INLINE void setDRn(u32 op, double d)
{
  SetDR((op >> 9) & 7, d);
}
