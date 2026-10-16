// Host-only check for plan section 7, bundle 8, way (B) JIT IDLE HASH: idle_hash.h and the decoder's dec_idle_hash()
// against hashlib's SHA-1, Reicast's hash() and P89's Kamui loops as cut.py writes them. Args (address, words): the entry.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#include "dc/sh4/rec_v2/idle_hash.h"

// The decoder's side on mocks: guest memory as bytes, the log line kept for the checks.
static struct { u32 start, opcodes; } block;
static u8 g_mem[256];
static u32 g_mem_base, g_dec_hash_blocks, g_lines;
static char g_line[160];
static u8 ReadMem8(u32 addr) { return g_mem[(addr - g_mem_base) & 255]; }
// The Kamui rule's reads keep the lowest address, so a test sees what was read before a block.
static u32 g_read_min;
static u16 ReadMem16(u32 addr)
{
	if (addr < g_read_min)
		g_read_min = addr;
	return (u16)(ReadMem8(addr) | ReadMem8(addr + 1) << 8);
}
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

		// Ours is bound to its address; one of Reicast's is not. Its second half does not count.
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
	// P94: no task list now; the task rule (test_task) takes those blocks.
	check(own >= 1 && reicast >= 6 && task == 0, "known blocks cover the lists", own, reicast);

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
// JIT TASK IDLE acts on the task rule (test_task_dec: ON 3 slices, LONG 16). 24 log lines per list.
static void test_dec()
{
	u32 lines[4] = {};
	for (int level = 0; level <= 2; level++)
		for (int task = 0; task <= 2; task++)
			for (u32 i = 0; i < KNOWN_N; i++)
			{
				const Known& k = k_known[i];
				// Reicast's list is long: its log share is kept for the limit test below. LONG: test_task_dec.
				if ((k.list == IDLE_LIST_REICAST && (level == 0 || task)) || (task == 2 && k.list != IDLE_LIST_TASK))
					continue;
				memset(g_mem, 0xEE, sizeof(g_mem));
				g_mem_base = k.addr;
				load(k, g_mem);
				block.start = k.addr;
				block.opcodes = k.n;
				const u32 n0 = g_dec_hash_blocks, l0 = g_lines;
				g_line[0] = 0;
				const u32 slices = dec_idle_hash(level, task);
				const u32 want = k.list == IDLE_LIST_OWN ? (level >= 1 ? 1 : 0)
				               : k.list == IDLE_LIST_REICAST ? (level == 2 ? 1 : 0)
				               : k.list == IDLE_LIST_TASK ? (task == 2 ? 16 : task ? 3 : 0) : 0;
				const u32 key = (u32)(level * 3 + task);
				check(slices == want, "dec slices", k.addr, key);
				check(g_dec_hash_blocks - n0 == (u32)(want != 0), "dec count", k.addr, key);
				check(g_lines - l0 == (u32)(k.list != IDLE_LIST_NONE), "dec log", k.addr, key);
				lines[k.list] += k.list != IDLE_LIST_NONE;
				const char* name = k.list == IDLE_LIST_OWN ? "our list" : k.list == IDLE_LIST_TASK ? "the task rule" : "Reicast's list";
				const char* act = !want ? "log only" : want == 1 ? "ends the slice" : "skips the slices";
				if (k.list != IDLE_LIST_NONE)
					check(strstr(g_line, k.full) && strstr(g_line, act) && strstr(g_line, name), "dec log text", k.addr, key);
			}
	check(IDLE_TASK_SLICES == 3, "three task slices", IDLE_TASK_SLICES, 0);
	check(IDLE_TASK_LONG == 16, "sixteen LONG task slices", IDLE_TASK_LONG, 0);
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

// P89: Kamui's vsync wait as the HW logs show it, the tail block's address and the form.
struct KamuiLoop { const char* name; u32 tail, form, n; unsigned short w[IDLE_KAMUI_MAX]; };
static const KamuiLoop k_kamui[] = {
#include "kamui_known.inc"
};
#define KAMUI_N (sizeof(k_kamui) / sizeof(k_kamui[0]))

// Guest memory 0xEE with the loop placed so that its tail block starts at `tail`.
static void put_loop(const KamuiLoop& k, u32 tail)
{
	memset(g_mem, 0xEE, sizeof(g_mem));
	g_mem_base = tail - 128;
	const u32 head = tail + 8 - k.n * 2;
	for (u32 i = 0; i < k.n; i++)
	{
		g_mem[(head + i * 2 - g_mem_base) & 255] = (u8)k.w[i];
		g_mem[(head + i * 2 + 1 - g_mem_base) & 255] = (u8)(k.w[i] >> 8);
	}
}

// idle_kamui_form(): the table against SH4 code and the logs; each loop at any address; no match with one bit
// changed or another block length; nothing read before a block that is not a tail.
static void test_kamui()
{
	// jsr @Rn with `mov #7,r4` in its slot, an exit bf to just past the loop, and the last bf back to the jsr.
	for (u32 f = 0; f < IDLE_KAMUI_FORMS; f++)
	{
		const u16* w = &idle_kamui[f][1];
		const u32 n = idle_kamui[f][0];
		check(n >= 8 && n <= IDLE_KAMUI_MAX, "kamui length", f, n);
		check((w[0] & 0xF0FF) == 0x400B && w[1] == 0xE407, "kamui jsr and hook 7", f, w[0]);
		check(w[n - 5] == 0x8B03, "kamui exit bf", f, w[n - 5]);
		const int disp = (int)(signed char)(w[n - 1] & 0xFF);
		check((w[n - 1] & 0xFF00) == 0x8B00 && 6 + 4 + disp * 2 == 8 - (int)n * 2, "kamui loop bf", f, w[n - 1]);
	}
	u32 forms[IDLE_KAMUI_FORMS] = {};
	for (u32 i = 0; i < KAMUI_N; i++)
	{
		const KamuiLoop& k = k_kamui[i];
		check(k.form < IDLE_KAMUI_FORMS && k.n == idle_kamui[k.form][0] &&
		      memcmp(k.w, &idle_kamui[k.form][1], k.n * 2) == 0, "kamui table holds the log", k.tail, i);
		forms[k.form < IDLE_KAMUI_FORMS ? k.form : 0]++;
		static const u32 k_moves[3] = { 0, 0x100, 0x00100000 };
		for (u32 m = 0; m < 3; m++)
		{
			const u32 tail = k.tail + k_moves[m];
			put_loop(k, tail);
			g_read_min = 0xFFFFFFFFu;
			check(idle_kamui_form(tail, 4, ReadMem16) == (int)k.form, "kamui form", tail, i);
			check(g_read_min == tail + 8 - k.n * 2, "kamui reads back to the jsr", g_read_min, i);
		}
		put_loop(k, k.tail);
		for (u32 op = 0; op <= 6; op++)
			if (op != 4)
				check(idle_kamui_form(k.tail, op, ReadMem16) == -1, "kamui opcodes", k.tail, op);
		const u32 head = k.tail + 8 - k.n * 2;
		for (u32 b = 0; b < k.n * 16; b++)
		{
			u8& byte = g_mem[(head + b / 8 - g_mem_base) & 255];
			byte ^= (u8)(1 << (b & 7));
			check(idle_kamui_form(k.tail, 4, ReadMem16) == -1, "kamui one bit", k.tail, b);
			byte ^= (u8)(1 << (b & 7));
		}
		for (u32 a = head - 8; a <= k.tail + 8; a += 2)
		{
			if (a == k.tail)
				continue;
			g_read_min = 0xFFFFFFFFu;
			check(idle_kamui_form(a, 4, ReadMem16) == -1, "kamui other block", a, i);
			check(g_read_min >= a, "kamui read before a block", a, g_read_min);
		}
	}
	check(forms[0] >= 1 && forms[1] >= 1 && forms[2] >= 1, "kamui logs cover the forms", forms[0], forms[1]);

	// A block whose last `last` words are a tail's: the code before it is read only when all 4 are.
	static const u16 k_tail[4] = { 0x61F2, 0x6312, 0x2338, 0x8BF1 };
	for (u32 last = 0; last <= 4; last++)
	{
		memset(g_mem, 0xEE, sizeof(g_mem));
		g_mem_base = 0x8C010000u;
		const u32 a = 0x8C010080u;
		for (u32 j = 4 - last; j < 4; j++)
		{
			g_mem[a - g_mem_base + j * 2] = (u8)k_tail[j];
			g_mem[a - g_mem_base + j * 2 + 1] = (u8)(k_tail[j] >> 8);
		}
		g_read_min = 0xFFFFFFFFu;
		check(idle_kamui_form(a, 4, ReadMem16) == -1, "kamui tail alone", a, last);
		check((g_read_min < a) == (last == 4), "kamui tail alone reads", g_read_min, last);
	}
}

// dec_idle_hash() and the Kamui rule: one slice from level 1 at any task level, its own log text and log share.
static void test_kamui_dec()
{
	const u32 base = g_lines;
	u32 calls = 0;
	for (u32 i = 0; i < KAMUI_N + 9; i++)
	{
		const KamuiLoop& k = k_kamui[i < KAMUI_N ? i : 0];
		const u32 key = i < KAMUI_N ? i % 9 : i - KAMUI_N;
		const int level = (int)(key % 3), task = (int)(key / 3);
		put_loop(k, k.tail);
		block.start = k.tail;
		block.opcodes = 4;
		const u32 n0 = g_dec_hash_blocks;
		g_line[0] = 0;
		const u32 slices = dec_idle_hash(level, task);
		calls++;
		check(slices == (level >= 1 ? 1u : 0u), "kamui dec slices", k.tail, key);
		check(g_dec_hash_blocks - n0 == (u32)(level >= 1), "kamui dec count", k.tail, key);
		char at[24];
		snprintf(at, sizeof(at), ">:1:%08X:04:", k.tail);
		check(g_lines - base == calls && strstr(g_line, at) && strstr(g_line, "the Kamui rule") &&
		      strstr(g_line, level >= 1 ? "ends the slice" : "log only"), "kamui dec log", k.tail, key);
		// The same code as a 3- or 5-opcode block: no slice, no line.
		for (u32 op = 3; op <= 5; op += 2)
		{
			block.opcodes = op;
			check(dec_idle_hash(2, 2) == 0 && g_lines - base == calls, "kamui dec opcodes", k.tail, op);
		}
	}
	// 24 lines in its own share: 100 more hits add only the rest of it.
	const KamuiLoop& k = k_kamui[KAMUI_N - 1];
	put_loop(k, k.tail);
	block.start = k.tail;
	block.opcodes = 4;
	const u32 l0 = g_lines, n0 = g_dec_hash_blocks;
	for (u32 r = 0; r < 100; r++)
		dec_idle_hash(1, 0);
	check(calls < 24 && g_lines - l0 == 24 - calls && g_dec_hash_blocks - n0 == 100, "kamui dec log limit", g_lines - l0,
	      calls);
}

// P94: the Shinobi task dispatch as the HW logs show it: the test block's address and the loop from its bf target.
struct TaskLoop { const char* name; u32 test; unsigned short w[IDLE_TASK_WORDS]; };
static const TaskLoop k_task[] = {
#include "task_known.inc"
};
#define TASK_N (sizeof(k_task) / sizeof(k_task[0]))

// Guest memory 0xEE with the loop placed so that its test block starts at `test`.
static void put_task(const TaskLoop& k, u32 test)
{
	memset(g_mem, 0xEE, sizeof(g_mem));
	g_mem_base = test - 128;
	const u32 head = test + 8 - IDLE_TASK_WORDS * 2;
	for (u32 i = 0; i < IDLE_TASK_WORDS; i++)
	{
		g_mem[(head + i * 2 - g_mem_base) & 255] = (u8)k.w[i];
		g_mem[(head + i * 2 + 1 - g_mem_base) & 255] = (u8)(k.w[i] >> 8);
	}
}

// idle_task_rule(): the rule against SH4 code and the logs; each loop at any address and with any table; no match with
// one bit changed outside the table or another block length; nothing read before a block that is not the test.
static void test_task()
{
	// The table's literal is what `mov.l @(16,PC),r3` (word 3) loads; the test's bf goes back to word 0, the skip's bra
	// (word 9) to the i++ (word 23), and the call's bf (word 8) to word 14.
	const u16* w = idle_task_loop;
	check(w[3] == 0xD304 && (((3 * 2 + 4) & ~3u) + 4 * 4) / 2 == IDLE_TASK_TABLE, "task table literal", w[3], 0);
	check((int)(signed char)(w[29] & 0xFF) * 2 + 29 * 2 + 4 == 0, "task loop bf", w[29], 0);
	check(9 * 2 + 4 + (w[9] & 0xFFF) * 2 == 23 * 2 && w[9] >> 12 == 0xA, "task skip bra", w[9], 0);
	check(8 * 2 + 4 + (w[8] & 0xFF) * 2 == 14 * 2 && w[8] >> 8 == 0x8B, "task call bf", w[8], 0);
	for (u32 i = 0; i < TASK_N; i++)
	{
		const TaskLoop& k = k_task[i];
		bool same = true;
		for (u32 j = 0; j < IDLE_TASK_WORDS; j++)
			same &= j == IDLE_TASK_TABLE || j == IDLE_TASK_TABLE + 1 || k.w[j] == w[j];
		check(same, "task rule holds the log", k.test, i);
		static const u32 k_moves[3] = { 0, 0x100, 0x00100000 };
		for (u32 m = 0; m < 3; m++)
		{
			const u32 test = k.test + k_moves[m];
			put_task(k, test);
			g_read_min = 0xFFFFFFFFu;
			check(idle_task_rule(test, 4, ReadMem16), "task rule", test, i);
			check(g_read_min == test + 8 - IDLE_TASK_WORDS * 2, "task rule reads back to the loop head", g_read_min, i);
		}
		put_task(k, k.test);
		for (u32 op = 0; op <= 6; op++)
			if (op != 4)
				check(!idle_task_rule(k.test, op, ReadMem16), "task opcodes", k.test, op);
		const u32 head = k.test + 8 - IDLE_TASK_WORDS * 2;
		for (u32 b = 0; b < IDLE_TASK_WORDS * 16; b++)
		{
			const u32 word = b / 16;
			u8& byte = g_mem[(head + b / 8 - g_mem_base) & 255];
			byte ^= (u8)(1 << (b & 7));
			const bool table = word == IDLE_TASK_TABLE || word == IDLE_TASK_TABLE + 1;
			check(idle_task_rule(k.test, 4, ReadMem16) == table, "task one bit", k.test, b);
			byte ^= (u8)(1 << (b & 7));
		}
		for (u32 a = head - 8; a <= k.test + 8; a += 2)
		{
			if (a == k.test)
				continue;
			g_read_min = 0xFFFFFFFFu;
			check(!idle_task_rule(a, 4, ReadMem16), "task other block", a, i);
			check(g_read_min >= a, "task read before a block", a, g_read_min);
		}
	}

	// A block whose last `last` words are the test's: the code before it is read only when all 4 are.
	for (u32 last = 0; last <= 4; last++)
	{
		memset(g_mem, 0xEE, sizeof(g_mem));
		g_mem_base = 0x8C010000u;
		const u32 a = 0x8C010080u;
		for (u32 j = 4 - last; j < 4; j++)
		{
			g_mem[a - g_mem_base + j * 2] = (u8)w[26 + j];
			g_mem[a - g_mem_base + j * 2 + 1] = (u8)(w[26 + j] >> 8);
		}
		g_read_min = 0xFFFFFFFFu;
		check(!idle_task_rule(a, 4, ReadMem16), "task test alone", a, last);
		check((g_read_min < a) == (last == 4), "task test alone reads", g_read_min, last);
	}
}

// dec_idle_hash() and the task rule: JIT TASK IDLE's slices at any JIT IDLE HASH level, its log text and log share.
static void test_task_dec()
{
	const u32 base = g_lines;
	u32 calls = 0;
	for (u32 i = 0; i < TASK_N * 3; i++)
	{
		const TaskLoop& k = k_task[i % TASK_N];
		const int level = (int)(i % 3), task = (int)(i / TASK_N);
		put_task(k, k.test);
		block.start = k.test;
		block.opcodes = 4;
		const u32 n0 = g_dec_hash_blocks;
		g_line[0] = 0;
		const u32 slices = dec_idle_hash(level, task);
		calls++;
		const u32 want = task >= 2 ? IDLE_TASK_LONG : task ? IDLE_TASK_SLICES : 0;
		check(slices == want, "task dec slices", k.test, (u32)(level * 3 + task));
		check(g_dec_hash_blocks - n0 == (u32)(want != 0), "task dec count", k.test, (u32)task);
		char at[24];
		snprintf(at, sizeof(at), ">:1:%08X:04:", k.test);
		check(g_lines - base == calls && strstr(g_line, at) && strstr(g_line, "the task rule") &&
		      strstr(g_line, !want ? "log only" : "skips the slices"), "task dec log", k.test, (u32)task);
		block.opcodes = 5;
		check(dec_idle_hash(2, 2) == 0 && g_lines - base == calls, "task dec opcodes", k.test, 5);
	}
	// 24 lines in its own share: 100 more hits add only the rest of it.
	const TaskLoop& k = k_task[0];
	put_task(k, k.test);
	block.start = k.test;
	block.opcodes = 4;
	const u32 l0 = g_lines, n0 = g_dec_hash_blocks;
	for (u32 r = 0; r < 100; r++)
		dec_idle_hash(0, 2);
	check(calls < 24 && g_lines - l0 == 24 - calls && g_dec_hash_blocks - n0 == 100, "task dec log limit", g_lines - l0,
	      calls);
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
	test_kamui();
	test_kamui_dec();
	test_task();
	test_task_dec();
	printf("%s: idle hash: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
	return g_fail != 0;
}
