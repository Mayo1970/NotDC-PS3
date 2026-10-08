#pragma once
// vram_check: the few types.h names dc/pvr/pvr_vram.h and the pvr_if.cpp section use, on a PC.
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
#define VRAM_SIZE (8 * 1024 * 1024)
#define VRAM_MASK (VRAM_SIZE - 1)
