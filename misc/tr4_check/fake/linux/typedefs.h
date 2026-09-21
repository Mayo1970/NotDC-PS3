// Host-only stand-in so plugin_header.h's OS_LINUX branch resolves. Test scaffolding.
#pragma once
#include <stdint.h>
#include <cstddef>
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define FASTCALL
#define fastcall
inline void __debugbreak() {}
#define ALIGN(x) __attribute__((aligned(x)))
