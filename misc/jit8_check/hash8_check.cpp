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
	u32 own = 0, reicast = 0, task = 0;
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
		task += list == IDLE_LIST_TASK;

		// The brief form is the full one without the address: the same words through idle_hash_words.
		u32 h[5];
		idle_hash_words(code, k.n, h);
		snprintf(want, sizeof(want), ">:1:%02X:%08X:%08X:%08X:%08X:%08X", k.n, h[0], h[1], h[2], h[3], h[4]);
		check(strcmp(want, k.brief) == 0, "brief form", k.addr, i);

		// Ours and the task list's are bound to their address; one of Reicast's is not. Its second half does not count.
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
	check(own >= 1 && reicast >= 6 && task == 2, "known blocks cover the lists", own, reicast);

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

// dec_idle_hash(): the timeslices a block drops. Level 1 acts on our list and logs Reicast's, level 2 acts on both;
// JIT TASK IDLE acts on the task list (IDLE_TASK_SLICES). 24 log lines per list.
static void test_dec()
{
	u32 lines[4] = {};
	for (int level = 0; level <= 2; level++)
		for (int task = 0; task <= 1; task++)
			for (u32 i = 0; i < KNOWN_N; i++)
			{
				const Known& k = k_known[i];
				// Reicast's list is long: its log share is kept for the limit test below.
				if (k.list == IDLE_LIST_REICAST && (level == 0 || task))
					continue;
				memset(g_mem, 0xEE, sizeof(g_mem));
				g_mem_base = k.addr;
				load(k, g_mem);
				block.start = k.addr;
				block.opcodes = k.n;
				const u32 n0 = g_dec_hash_blocks, l0 = g_lines;
				g_line[0] = 0;
				const u32 slices = dec_idle_hash(level, task != 0);
				const u32 want = k.list == IDLE_LIST_OWN ? (level >= 1 ? 1 : 0)
				               : k.list == IDLE_LIST_REICAST ? (level == 2 ? 1 : 0)
				               : k.list == IDLE_LIST_TASK ? (task ? 3 : 0) : 0;
				const u32 key = (u32)(level * 2 + task);
				check(slices == want, "dec slices", k.addr, key);
				check(g_dec_hash_blocks - n0 == (u32)(want != 0), "dec count", k.addr, key);
				check(g_lines - l0 == (u32)(k.list != IDLE_LIST_NONE), "dec log", k.addr, key);
				lines[k.list] += k.list != IDLE_LIST_NONE;
				const char* name = k.list == IDLE_LIST_OWN ? "our list" : k.list == IDLE_LIST_TASK ? "the task list" : "Reicast's list";
				const char* act = !want ? "log only" : want == 1 ? "ends the slice" : "skips the slices";
				if (k.list != IDLE_LIST_NONE)
					check(strstr(g_line, k.full) && strstr(g_line, act) && strstr(g_line, name), "dec log text", k.addr, key);
			}
	check(IDLE_TASK_SLICES == 3, "three task slices", IDLE_TASK_SLICES, 0);
	block.opcodes = 0;
	check(!dec_idle_hash(2, true), "dec no opcodes", 0, 0);
	block.opcodes = IDLE_HASH_MAX_OPS + 1;
	check(!dec_idle_hash(2, true), "dec over the limit", 0, 0);

	// 24 log lines per list: 100 more hits of one list leave the others their lines so far.
	check(lines[IDLE_LIST_REICAST] < 24 && lines[IDLE_LIST_OWN] < 24 && lines[IDLE_LIST_TASK] < 24, "log lines so far",
	      lines[IDLE_LIST_REICAST], lines[IDLE_LIST_OWN]);
	static const int k_order[3] = { IDLE_LIST_REICAST, IDLE_LIST_OWN, IDLE_LIST_TASK };
	for (u32 pass = 0; pass < 3; pass++)
		for (u32 i = 0; i < KNOWN_N; i++)
		{
			const Known& k = k_known[i];
			if (k.list != k_order[pass])
				continue;
			g_mem_base = k.addr;
			load(k, g_mem);
			block.start = k.addr;
			block.opcodes = k.n;
			const u32 n0 = g_dec_hash_blocks;
			for (u32 r = 0; r < 100; r++)
				dec_idle_hash(2, true);
			lines[k.list] = 24;
			check(g_lines == lines[1] + lines[2] + lines[3] && g_dec_hash_blocks - n0 == 100, "dec log limit", g_lines, pass);
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
		printf("%s  (%s)\n", full, list == IDLE_LIST_OWN ? "in our list" : list == IDLE_LIST_TASK ? "in the task list"
		       : list ? "in Reicast's list" : "in no list");
		return 0;
	}
	test_sha1();
	test_known();
	test_dec();
	printf("%s: idle hash: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
