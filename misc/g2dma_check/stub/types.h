// PC stand-in for the root types.h: what dc/aica/aica_if.cpp needs.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
#define FASTCALL
#define fastcall

typedef u32 RegReadFP();
typedef void RegWriteFP(u32 data);
enum RegStructFlags { REG_8BIT_READWRITE = 1, REG_16BIT_READWRITE = 2, REG_32BIT_READWRITE = 4, REG_READ_DATA = 8,
                      REG_WRITE_DATA = 16, REG_CONST = 128, REG_NOT_IMPL = 256 };
struct RegisterStruct
{
	union { u32* data32; u16* data16; u8* data8; };
	RegReadFP* readFunction;
	RegWriteFP* writeFunction;
	u32 flags;
};

struct VArray2 { u8 data[16]; void Zero() { memset(data, 0, sizeof(data)); } };
struct __settings { struct { u32 cable, RTC; } dreamcast; };
extern __settings settings;
void SaveSettings();

extern "C" int get_sh4_clock_preset();
#define SH4_CLOCK_EFF ((u32)(get_sh4_clock_preset() * 1000000))
#define MBX_OK 0
#define MBX_ICONERROR 0x10
int msgboxf(const char* text, unsigned int type, ...);
