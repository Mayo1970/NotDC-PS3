#pragma once
// maple_check: the names of the root types.h and of the plugin headers that dc/maple/*.cpp use, on a PC.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef char wchar;
#define FASTCALL
// The checked code's log lines go to a buffer that the tests read.
int check_printf(const char* fmt, ...);
#define printf check_printf
// A failed verify hangs the PS3 (types.h dbgbreak). Here it throws, so a test can see it.
struct VerifyFail { const char* what; };
#define verify(x) if((x)==false){ throw VerifyFail{ #x }; }
extern int g_check_clock_mhz;
#define SH4_CLOCK_EFF ((u32)(g_check_clock_mhz * 1000000))
// A little-endian host: the XOR is the identity (plugins/plugin_header.h).
template<typename T> static inline T* host_ptr_xor(T* x) { return x; }
// run.sh checks the values against plugins/plugin_header.h.
enum HollyInterruptType { holly_nrm = 0x0000, holly_ext = 0x0100, holly_err = 0x0200 };
enum HollyInterruptID { holly_MAPLE_DMA = holly_nrm | 12, holly_MAPLE_OVERRUN = holly_err | 0x09 };
