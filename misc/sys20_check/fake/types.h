#pragma once
// sys20_check: the types.h names that bundle 20's system sources use, on a little-endian PC.
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
typedef int64_t s64;
typedef float f32;
typedef double f64;
#define ENDIAN_LITTLE 1
#define ENDIAN_BIG 2
#define HOST_ENDIAN ENDIAN_LITTLE
#define INLINE inline
#define FASTCALL
#define fastcall
#define ALIGN(x) __attribute__((aligned(x)))
#define HOST_TO_LE16(x) (x)
#define HOST_TO_LE32(x) (x)
#define RAM_SIZE (16 * 1024 * 1024)
#define RAM_MASK (RAM_SIZE - 1)
// A failed verify hangs the PS3; here it throws, so the harness counts it.
struct VerifyFail { const char* what; };
#define verify(x) do { if (!(x)) throw VerifyFail{ #x }; } while (0)

typedef u32 RegReadFP();
typedef void RegWriteFP(u32 data);
enum RegStructFlags
{
	REG_8BIT_READWRITE = 1, REG_16BIT_READWRITE = 2, REG_32BIT_READWRITE = 4, REG_READ_DATA = 8, REG_WRITE_DATA = 16,
	REG_READ_PREDICT = 32, REG_WRITE_NOCHANGE = 64, REG_CONST = 128, REG_NOT_IMPL = 256
};
struct RegisterStruct
{
	union { u32* data32; u16* data16; u8* data8; };
	RegReadFP* readFunction;
	RegWriteFP* writeFunction;
	u32 flags;
};

// stdclass.h's Array as the sources use it: a zeroed table of Size entries.
template<class T> class Array
{
public:
	T* data;
	u32 Size;
	Array() : data(nullptr), Size(0) {}
	Array(u32 n) : data((T*)calloc(n, sizeof(T))), Size(n) {}
	Array(u32 n, bool) : data((T*)calloc(n, sizeof(T))), Size(n) {}
	T& operator[](u32 i) { return data[i]; }
	void Zero() { memset(data, 0, Size * sizeof(T)); }
	void Resize(u32 n, bool) { data = (T*)realloc(data, n * sizeof(T)); memset(data, 0, n * sizeof(T)); Size = n; }
	void Free() { free(data); data = nullptr; Size = 0; }
};
template<u32 sz, typename T> T host_to_le(T v) { return v; }

class VArray2
{
public:
	u8* data;
	u32 size;
	u8& operator[](u32 i) { return data[i]; }
	void Zero() { memset(data, 0, size); }
};
