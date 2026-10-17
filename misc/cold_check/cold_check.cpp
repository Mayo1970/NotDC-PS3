// Host-only check for row JIT COLD SPLIT's cache allocation (driver.cpp's `cold_check:` cuts) under random blocks: no
// byte written twice between clears, no hot byte in a chunk, and ngen_Compile's room check keeps the cold room.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32;

static int g_verify;
#define verify(x) do { if (!(x)) { g_verify++; throw 1; } } while (0)

#define CODE_SIZE (24 * 1024 * 1024)
static u8 CodeCache[CODE_SIZE];
static u32 LastAddr, LastAddr_min;
static u32* emit_ptr;
static u32 s_code_size = CODE_SIZE;
static u64 s_compiled_bytes;

#include "/tmp/cold_check/cut.inc"

// As ps3_driver.cpp: the start check, the island, the cold bounds.
#define COLD_FRAG_MAX 512
#define COLD_EXIT_MAX 128

static u64 g_seed = 0x853C49E6748FEA9Bull;
static u64 rnd() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17; return g_seed; }
static u32 rn(u32 n) { return (u32)(rnd() % n); }

static std::vector<u8> g_owner;   // 0 free, 1 hot, 2 cold, per byte since the last clear
static int g_fail;
static u32 g_blocks, g_clears, g_fallbacks, g_chunks_seen;

static void fail(const char* what)
{
	if (g_fail < 10)
		printf("FAIL %s (block %u)\n", what, g_blocks);
	g_fail++;
}

static void own(u32 at, u32 n, u8 kind)
{
	for (u32 i = 0; i < n; i++)
	{
		if (g_owner[at + i])
		{
			fail("a byte written twice between clears");
			return;
		}
		g_owner[at + i] = kind;
	}
}

static void clear()
{
	LastAddr = LastAddr_min;
	cold_reset();
	std::fill(g_owner.begin(), g_owner.end(), 0);
	g_clears++;
}

static void write_hot(u32 bytes)
{
	const u32 at = LastAddr;
	for (u32 i = 0; i < bytes / 4; i++)
		emit_Write32(0x60000000u);
	own(at, LastAddr - at, 1);
}

static void write_cold(u32* p, u32 bytes)
{
	emit_ptr = p;
	for (u32 i = 0; i < bytes / 4; i++)
		emit_Write32(0x60000000u);
	const u32 at = (u32)((u8*)p - CodeCache);
	own(at, bytes, 2);
	emit_ColdEnd(emit_ptr);
	emit_ptr = 0;
}

int main(int argc, char** argv)
{
	const u32 rounds = argc > 1 ? (u32)atoi(argv[1]) : 40;
	g_owner.assign(CODE_SIZE, 0);
	for (u32 r = 0; r < rounds; r++)
	{
		s_code_size = r % 3 == 0 ? (6u << 20) : r % 3 == 1 ? (12u << 20) : CODE_SIZE;
		LastAddr_min = 4096 + rn(65536) * 4;
		clear();
		for (u32 b = 0; b < 30000; b++)
		{
			g_blocks++;
			const u32 frags = rn(5) == 0 ? rn(200) : rn(12);
			// Past the 16 KB a block is assumed to take: oversized (the cold code may fall back to the hot stream)
			// and huge (the hot code may hit a chunk, which a verify must stop).
			const u32 kind = (rn(400) == 0 || (emit_FreeSpace() < 200000 && rn(4) == 0)) ? 2 : rn(100) == 0 ? 1 : 0;
			const u32 hot = kind == 2 ? 70000 + (rn(20000) & ~3u) : kind == 1 ? 16384 + (rn(32768) & ~3u)
			              : 64 + rn(4) * 4 + (rn(50) == 0 ? rn(16000) & ~3u : rn(1500) & ~3u);
			const u32 bound = COLD_EXIT_MAX + frags * COLD_FRAG_MAX;
			const u32 cold = rn(4) == 0 ? bound : rn(frags * 300 + COLD_EXIT_MAX) & ~3u;
			try
			{
				// ngen_Compile's start checks; a failed compile clears the cache and compiles again.
				if (emit_FreeSpace() < 16384 || emit_FreeSpace() < 65536 + 2 * COLD_EXIT_MAX + frags * COLD_FRAG_MAX)
				{
					clear();
					continue;
				}
				write_hot(hot);
				if (rn(4) == 0)   // + EXITS: one dynamic exit's miss path
				{
					u32* p = emit_ColdBegin(COLD_EXIT_MAX);
					if (p)
						write_cold(p, 4 * (8 + rn(20)));
					write_hot(4);
				}
				write_hot((frags + 1) * 4);   // the island
				u32* p = emit_ColdBegin(bound);
				if (!p)
				{
					g_fallbacks += kind == 0;
					write_hot(cold);
				}
				else
				{
					if ((u32)((u8*)p - CodeCache) < LastAddr)
						fail("a chunk below the hot code");
					write_cold(p, cold);
				}
				if (emit_FreeSpace() > s_code_size)
					fail("free space wrapped");
			}
			catch (int)
			{
				if (kind != 2)
					fail("a verify fired (hot code ran into a chunk, or a cold end out of its chunk)");
				emit_ptr = 0;
				clear();
			}
		}
		g_chunks_seen += (s_code_size - cold_lo_now()) / 1024;
	}
	printf("blocks %u, clears %u, fallbacks %u, verifies %d\n", g_blocks, g_clears, g_fallbacks, g_verify);
	if (g_fallbacks)
		fail("the start check left a block without cold room");
	if (g_clears < rounds * 2)
		fail("too few clears: the test did not fill the cache");
	if (g_verify < (int)rounds)
		fail("too few huge blocks ran into a chunk: the hot code's bound is not tested");
	if (g_fail)
	{
		printf("FAIL: %d\n", g_fail);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
