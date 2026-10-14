#pragma once
// jmmu_check (jmmu_rt.cpp): the names of the root types.h that dc/sh4/sh4_mmu_jit.cpp and its headers use, on the PC.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using std::vector;
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
#define OS_WINDOWS 1
#define OS_LINUX 2
#define OS_PS3 3
#define HOST_OS OS_PS3
#define INLINE inline
#define FASTCALL
#define fastcall
#define ALIGN(x) __attribute__((aligned(x)))
struct VerifyFail { const char* what; };
#define verify(x) if((x)==false){ throw VerifyFail{ #x }; }
#define die(reason) { throw VerifyFail{ reason }; }
#define RAM_SIZE (16u * 1024 * 1024)
#define RAM_MASK (RAM_SIZE - 1)
struct VArray2
{
	u8* data;
	u32 size;
	u8& operator[](const u32 i) { return data[i]; }
};
struct __settings
{
	struct { bool Enable, CPpass, UnderclockFpu, safemode; } dynarec;
};
extern __settings settings;
