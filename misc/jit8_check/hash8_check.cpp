// Host-only check for plan section 7, bundle 8, way (B) JIT IDLE HASH: idle_hash.h and the decoder's dec_idle_hash()
// against hashlib's SHA-1 and Reicast's hash() as cut.py writes them. With args (address, opcode words): the entry.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8;
typedef uint32_t u32;

#include "dc/sh4/rec_v2/idle_hash.h"

// The decoder's side on mocks: guest memory as bytes, the log line kept for the checks.
static struct { u32 start, opcodes; } block;
static u8 g_mem[256];
static u32 g_mem_base, g_dec_hash_blocks, g_lines;
static char g_line[160];
static u8 ReadMem8(u32 addr) { return g_mem[(addr - g_mem_base) & 255]; }
#define printf(...) (g_lines++, snprintf(g_line, sizeof(g_line), __VA_ARGS__))
#include "hash_dec.inc"
#undef printf

static int g_fail, g_checks;
static void check(bool ok, const char* what, u32 a, u32 b)
{
	g_checks++;
	if (!ok)
	{
		if (g_fail < 12)
			printf("FAIL %s: %08X %08X\n", what, a, b);
		g_fail++;
	}
}

// hashlib's SHA-1 of the first n bytes of cut.py's stream, n = 0..128.
static const u32 k_sha_ref[129][5] = {
#include "sha_ref.inc"
};

static void test_sha1()
{
	u8 stream[128];
	for (u32 i = 0; i < 128; i++)
		stream[i] = (u8)(i * 167 + 13);
	for (u32 n = 0; n <= 128; n++)
	{
		u32 h[5];
		idle_sha1(stream, n, h);
		check(memcmp(h, k_sha_ref[n], sizeof(h)) == 0, "sha1", n, h[0]);
	}
}

// Blocks from the HW logs: the words, both forms of Reicast's hash() (python), and the list that holds each.
struct Known { const char* name; u32 addr, n; unsigned short w[16]; const char* brief; const char* full; int list; };
static const Known k_known[] = {
#include "hash_known.inc"
};
#define KNOWN_N (sizeof(k_known) / sizeof(k_known[0]))

static u32 load(const Known& k, u8* code)
{
	for (u32 i = 0; i < k.n; i++)
	{
		code[i * 2] = (u8)k.w[i];
		code[i * 2 + 1] = (u8)(k.w[i] >> 8);
	}
	return k.n * 2;
}

static void test_known()
{
	u32 own = 0, reicast = 0;
	for (u32 i = 0; i < KNOWN_N; i++)
	{
		const Known& k = k_known[i];
		u8 code[32];
		load(k, code);
		char full[IDLE_HASH_STR], want[IDLE_HASH_STR];
		const int list = idle_hash_listed(k.addr, k.n, code, full);
		check(strcmp(full, k.full) == 0, "full form", k.addr, i);
		check(list == k.list, "list", k.addr, (u32)list);
		own += list == IDLE_LIST_OWN;
		reicast += list == IDLE_LIST_REICAST;

		// The brief form is the full one without the address: the same words through idle_hash_words.
		u32 h[5];
		idle_hash_words(code, k.n, h);
		snprintf(want, sizeof(want), ">:1:%02X:%08X:%08X:%08X:%08X:%08X", k.n, h[0], h[1], h[2], h[3], h[4]);
		check(strcmp(want, k.brief) == 0, "brief form", k.addr, i);

		// One of ours is bound to its address; one of Reicast's is not. Its second half does not count.
		char other[IDLE_HASH_STR];
		const int moved = idle_hash_listed(k.addr + 0x100, k.n, code, other);
		check(moved == (k.list == IDLE_LIST_REICAST ? IDLE_LIST_REICAST : IDLE_LIST_NONE), "other address", k.addr, (u32)moved);
		code[k.n * 2 - 1] ^= 0x40;
		check(idle_hash_listed(k.addr, k.n, code, other) == k.list, "second half", k.addr, i);
		code[k.n * 2 - 1] ^= 0x40;
		for (u32 b = 0; b < k.n; b++)
		{
			code[b] ^= 1;
			check(idle_hash_listed(k.addr, k.n, code, other) == IDLE_LIST_NONE, "first half", k.addr, b);
			code[b] ^= 1;
		}
		check(idle_hash_listed(k.addr, k.n + 1, code, other) == IDLE_LIST_NONE, "one more opcode", k.addr, i);
	}
	check(own >= 1 && reicast >= 6, "known blocks cover both lists", own, reicast);

	// No block and a block over the limit: not listed, no text.
	u8 big[IDLE_HASH_MAX_OPS * 2 + 2] = {};
	char full[IDLE_HASH_STR] = "x";
	check(idle_hash_listed(0x8C010000u, 0, big, full) == IDLE_LIST_NONE && full[0] == 0, "no opcodes", 0, 0);
	full[0] = 'x';
	check(idle_hash_listed(0x8C010000u, IDLE_HASH_MAX_OPS + 1, big, full) == IDLE_LIST_NONE && full[0] == 0, "over the limit", 0, 0);
	check(idle_hash_listed(0x8C010000u, IDLE_HASH_MAX_OPS, big, full) == IDLE_LIST_NONE && strlen(full) == 60, "at the limit", 0, 0);

	// Reicast's last entry has no ':' after '>': the same text with it must not be in the list.
	check(strstr(idle_hash_reicast, ">1:08:AF4AC687") && !strstr(idle_hash_reicast, ">:1:08:AF4AC687"), "dead entry", 0, 0);
}

// dec_idle_hash(): level 1 acts on our list and logs Reicast's; level 2 acts on both; 24 log lines per list.
static void test_dec()
{
	for (int level = 1; level <= 2; level++)
		for (u32 i = 0; i < KNOWN_N; i++)
		{
			const Known& k = k_known[i];
			memset(g_mem, 0xEE, sizeof(g_mem));
			g_mem_base = k.addr;
			load(k, g_mem);
			block.start = k.addr;
			block.opcodes = k.n;
			const u32 n0 = g_dec_hash_blocks, l0 = g_lines;
			g_line[0] = 0;
			const bool act = dec_idle_hash(level);
			const bool want = k.list == IDLE_LIST_OWN || (k.list == IDLE_LIST_REICAST && level == 2);
			check(act == want, "dec act", k.addr, (u32)level);
			check(g_dec_hash_blocks - n0 == (u32)want, "dec count", k.addr, (u32)level);
			check(g_lines - l0 == (u32)(k.list != IDLE_LIST_NONE), "dec log", k.addr, (u32)level);
			if (k.list != IDLE_LIST_NONE)
				check(strstr(g_line, k.full) && strstr(g_line, want ? "ends the slice" : "log only") &&
				      strstr(g_line, k.list == IDLE_LIST_OWN ? "our list" : "Reicast's list"), "dec log text", k.addr, (u32)level);
		}
	block.opcodes = 0;
	check(!dec_idle_hash(2), "dec no opcodes", 0, 0);
	block.opcodes = IDLE_HASH_MAX_OPS + 1;
	check(!dec_idle_hash(2), "dec over the limit", 0, 0);

	// 24 log lines per list: 100 more hits of Reicast's list leave ours its share (2 lines per block so far).
	u32 own_lines = 0;
	for (u32 i = 0; i < KNOWN_N; i++)
		own_lines += k_known[i].list == IDLE_LIST_OWN ? 2 : 0;
	for (u32 pass = 0; pass < 2; pass++)
		for (u32 i = 0; i < KNOWN_N; i++)
		{
			const Known& k = k_known[i];
			if (k.list != (pass ? IDLE_LIST_OWN : IDLE_LIST_REICAST))
				continue;
			g_mem_base = k.addr;
			load(k, g_mem);
			block.start = k.addr;
			block.opcodes = k.n;
			const u32 n0 = g_dec_hash_blocks;
			for (u32 r = 0; r < 100; r++)
				dec_idle_hash(2);
			check(g_lines == (pass ? 48u : 24u + own_lines) && g_dec_hash_blocks - n0 == 100, "dec log limit", g_lines, pass);
			break;
		}
}

int main(int argc, char** argv)
{
	if (argc > 2)
	{
		u8 code[IDLE_HASH_MAX_OPS * 2];
		const u32 addr = (u32)strtoul(argv[1], 0, 16), n = (u32)argc - 2;
		if (n > IDLE_HASH_MAX_OPS)
			return 2;
		for (u32 i = 0; i < n; i++)
		{
			const u32 w = (u32)strtoul(argv[i + 2], 0, 16);
			code[i * 2] = (u8)w;
			code[i * 2 + 1] = (u8)(w >> 8);
		}
		char full[IDLE_HASH_STR];
		const int list = idle_hash_listed(addr, n, code, full);
		printf("%s  (%s)\n", full, list == IDLE_LIST_OWN ? "in our list" : list ? "in Reicast's list" : "in no list");
		return 0;
	}
	test_sha1();
	test_known();
	test_dec();
	printf("%s: idle hash: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
