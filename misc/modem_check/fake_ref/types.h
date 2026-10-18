#pragma once
// modem_check: what Flycast's modem.cpp (ref/) takes from its types.h, for the reference build on the PC.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define DEBUG_LOG(tag, ...) do {} while (0)
#define INFO_LOG(tag, ...) do {} while (0)
#define WARN_LOG(tag, ...) do {} while (0)
struct Serializer
{
	template<typename T> Serializer& operator<<(const T&) { return *this; }
};
struct Deserializer
{
	enum Version { V20 = 20, V31 = 31 };
	Version version() const { return V31; }
	template<typename T> Deserializer& operator>>(T&) { return *this; }
};
