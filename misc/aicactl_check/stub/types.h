// 5.15 B1 test stand-in for the root types.h: what nullAICA and arm_mem.cpp need, built for the SPU (big-endian).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

#define FASTCALL
#define fastcall
#define naked
#define INLINE inline   // not forced: the reference must fit the LS
#define SINLINE inline
#define OS_WII 5
#define OS_PS3 8
#define HOST_OS OS_PS3

// The real verify and dbgbreak hang; the test counts them, and the port goes on as nullAICA would after them.
extern int g_verify_fail;
#define verify(x) if (!(x)) g_verify_fail++;
#define dbgbreak { g_verify_fail++; }

enum PluginType { Plugin_ARM = 1 };
struct emu_info;
enum HollyInterruptID { holly_SPU_IRQ = 0x0101 };
typedef void FASTCALL HollyRaiseInterruptFP(HollyInterruptID intr);
typedef void FASTCALL HollyCancelInterruptFP(HollyInterruptID intr);
struct aica_init_params
{
	HollyRaiseInterruptFP* RaiseInterrupt;
	u8* aica_ram;
	u32* SB_ISTEXT;
	HollyCancelInterruptFP* CancelInterrupt;
};
struct arm_init_params
{
	u8* aica_ram;
	u32 (*ReadMem_aica_reg)(u32 addr, u32 size);
	void (*WriteMem_aica_reg)(u32 addr, u32 data, u32 size);
};
enum { rv_ok = 0 };
#define ARAM_SIZE (2*1024*1024)
#define ARAM_MASK (ARAM_SIZE-1)
