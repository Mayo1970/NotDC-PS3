#pragma once
// flash_check: the few types.h names that dc/flash_fix.cpp and the cut sections use, on a PC.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
template<class T> class Array
{
public:
	T* data;
	u32 Size;
	T& operator[](u32 i) { return data[i]; }
};
// The checked code's log lines go to a buffer that the tests read.
int check_printf(const char* fmt, ...);
#define printf check_printf
