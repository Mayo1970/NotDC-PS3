#pragma once
// mmu_check: the few types.h names that ccn.h, mmu.h, dmac.h, sh4_internal_reg.h and the checked sections use, on a PC.
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
#define INLINE inline
#define FASTCALL
#define ALIGN(x)
#define ENDIAN_LITTLE 1
#define ENDIAN_BIG 2
#define HOST_ENDIAN ENDIAN_LITTLE
struct RegisterStruct { u32 flags; };
template<class T> class Array { public: T* data; u32 Size; };
