#pragma once
// PC stand-in for the repo's types.h: only what dc/gdrom/gdromv3.cpp and ps3/ps3_gdasync.cpp use.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#define FASTCALL
#define ENDIAN_LITTLE 1
#define ENDIAN_BIG 2
#define HOST_ENDIAN ENDIAN_LITTLE
#define HOST_TO_LE16(x) (x)
#define HOST_TO_LE32(x) (x)
#define MBX_ICONERROR 0
int msgboxf(const char* text, unsigned int type, ...);
void check_fail(const char* what, const char* file, int line);
// The repo's verify is an if with braces, used once without a semicolon.
#define verify(x) if (!(x)) { check_fail("verify " #x, __FILE__, __LINE__); }
#define die(reason) { check_fail(reason, __FILE__, __LINE__); }
template<class T> static inline T min(T a, T b) { return a < b ? a : b; }
