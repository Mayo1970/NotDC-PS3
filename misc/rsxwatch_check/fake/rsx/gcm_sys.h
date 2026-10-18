#pragma once
// rsxwatch_check: the PSL1GHT gcm names that ps3_rsxwatch.c uses, on a PC; the harness gives the values.
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u32* begin; u32* end; u32* current; void* callback; } gcmContextData;
typedef struct { volatile u32 put, get, ref; } gcmControlRegister;
gcmControlRegister* gcmGetControlRegister(void);
u64 gcmGetVBlankCount(void);
u32 gcmGetFlipStatus(void);
int gcmIoOffsetToAddress(u32 offset, void** address);
