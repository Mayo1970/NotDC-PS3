#pragma once
// interp_check: the names of the root types.h that the SH4 interpreter's headers use, on a little-endian PC.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
typedef uintptr_t unat;
#define ENDIAN_LITTLE 1
#define ENDIAN_BIG 2
#define HOST_ENDIAN ENDIAN_LITTLE
// No host FPU branch in SetFloatStatusReg(): the PC keeps round to nearest.
#define HOST_ARCH 0
#define HOST_CPU 0
#define ARCH_X86 1
#define CPU_PPC_CELL 6
#define INLINE inline
#define FASTCALL
#define fastcall
#define ALIGN(x) __attribute__((aligned(x)))
// A failed verify hangs the PS3 (types.h dbgbreak). Here it throws, so the harness counts it.
struct VerifyFail { const char* what; };
#define verify(x) if((x)==false){ throw VerifyFail{ #x }; }
#define die(reason) { throw VerifyFail{ reason }; }
