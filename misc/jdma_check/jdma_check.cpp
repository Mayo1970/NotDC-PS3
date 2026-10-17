// DMA code drop PC check: the code map of dc/sh4/rec_v2/driver.cpp (its jdma_check region) against a plain model, on
// fixed cases and on random compiles, DMAs and vblanks. Arg: the count of random rounds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include "types.h"
#include "dc/sh4/rec_v2/jit_dma.h"

#define RAM_SIZE (16u << 20)
#define RAM_MASK (RAM_SIZE - 1)

// -- What the region names -------------------------------------------------------
static bool IsOnRam(u32 addr) { return ((addr >> 26) & 7) == 3 && ((addr >> 29) & 7) != 7; }
static bool g_jit_mmu;
static int g_sbp = 1;
static int get_jit_sbp_preset() { return g_sbp; }
static u32 spg_VblankCountTotal;
static u32 g_clears;
static std::string g_log;
static int log_printf(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g_log += buf;
	return n;
}
void recSh4_ClearCache();
#define printf log_printf
#include "region.inc"
#undef printf
// driver.cpp's clear: the code map goes with the blocks.
void recSh4_ClearCache()
{
	g_clears++;
	jdma_cleared();
}

// -- The model: a bit per 32-byte line of code, the open rule per 1 KB page -------
#define LINES (RAM_SIZE / 32)
#define PAGES (RAM_SIZE / 1024)
static u32 m_code[LINES / 32];
static u8 m_open[PAGES];
static u32 m_hit[PAGES], m_clears, m_seen[PAGES], m_stamp;

static bool m_bit(u32 l) { return (m_code[l / 32] >> (l % 32)) & 1; }
static void m_reset()
{
	memset(m_code, 0, sizeof(m_code));
	memset(m_open, 0, sizeof(m_open));
	memset(m_hit, 0, sizeof(m_hit));
}
// Each 32-byte line that [ram, ram + size) touches, once; RAM's 16 MB wrap, as the mirrors do.
template <typename F> static void m_lines(u32 ram, u32 size, F f)
{
	if (size > RAM_SIZE - 32)
	{
		for (u32 l = 0; l < LINES; l++)
			f(l);
		return;
	}
	for (u64 a = ram; a < (u64)ram + size; a = (a | 31) + 1)
		f((u32)((a >> 5) % LINES));
}
static bool m_open_any(u32 ram, u32 size)
{
	bool any = false;
	for (u64 a = ram; a < (u64)ram + size; a = (a | 1023) + 1)
		any = any || m_open[(a >> 10) % PAGES];
	return any;
}
// rdv_CompileBlock's rule for a block of size bytes at pc: DoCheck at JIT SMC CHECK 1 or 2, then the map.
static void compile(u32 pc, u32 size)
{
	const u32 sz = size ? size : 2;
	bool guard = false;
	if (!g_jit_mmu && g_sbp && IsOnRam(pc))
		guard = g_sbp >= 2 || jdma_open(pc & RAM_MASK, sz);
	if (!guard && !g_jit_mmu && IsOnRam(pc))
		jdma_mark(pc & RAM_MASK, sz);

	const bool m_guard = !g_jit_mmu && g_sbp && IsOnRam(pc) && (g_sbp >= 2 || m_open_any(pc & RAM_MASK, sz));
	if (!m_guard && !g_jit_mmu && IsOnRam(pc))
		m_lines(pc & RAM_MASK, sz, [](u32 l) { m_code[l / 32] |= 1u << (l % 32); });
}
static void dma(u32 addr, u32 size, JitDma who = JDMA_GDROM)
{
	rdv_DmaWrite(addr, size, who);
	if (!size || !IsOnRam(addr) || g_jit_mmu || !g_sbp)
		return;
	const u32 now = spg_VblankCountTotal + 1;
	bool hit = false;
	m_stamp++;
	m_lines(addr & RAM_MASK, size, [&](u32 l) {
		if (!m_bit(l))
			return;
		hit = true;
		const u32 p = l / 32;
		if (m_seen[p] == m_stamp)
			return;
		m_seen[p] = m_stamp;
		if (m_hit[p] && now - m_hit[p] < 60)
			m_open[p] = 1;
		m_hit[p] = now;
	});
	if (hit)
	{
		m_clears++;
		memset(m_code, 0, sizeof(m_code));
	}
}
static void ext_clear()
{
	recSh4_ClearCache();
	m_clears++;
	memset(m_code, 0, sizeof(m_code));
}
static void reset_all()
{
	jdma_reset();
	m_reset();
	recSh4_ClearCache();
	m_clears++;
}

// -- Checks ----------------------------------------------------------------------
static int g_fail, g_checks;
#define EXPECT(c, ...) do { g_checks++; if (!(c)) { g_fail++; fprintf(stdout, "FAIL: " __VA_ARGS__); fputc('\n', stdout); } } while (0)

static bool code_bit(u32 ram) { return (s_jdma.code[ram >> 10] >> ((ram >> 5) & 31)) & 1; }
static bool same_state()
{
	return g_clears == m_clears && !memcmp(s_jdma.code, m_code, sizeof(m_code)) && !memcmp(s_jdma.open, m_open, PAGES) &&
	       !memcmp(s_jdma.hit, m_hit, sizeof(m_hit));
}
static void start()
{
	g_jit_mmu = false;
	g_sbp = 1;
	spg_VblankCountTotal = 0;
	g_clears = m_clears = 0;
	g_log.clear();
	jdma_reset();
	m_reset();
}
static u32 lines(const char* text)
{
	u32 n = 0;
	for (size_t at = g_log.find(text); at != std::string::npos; at = g_log.find(text, at + 1))
		n++;
	return n;
}

static void test_fixed()
{
	// A block's own bytes: the first DMA over them clears, the next one finds no code.
	start();
	compile(0x8C010000, 32);
	dma(0x0C010000, 4);
	EXPECT(g_clears == 1, "a DMA over a block: %u clears", g_clears);
	dma(0x0C010000, 4);
	EXPECT(g_clears == 1, "a second DMA after the clear: %u clears", g_clears);
	EXPECT(lines("[jdma] GD-ROM DMA to 0C010000, 4 bytes, over compiled code (1 lines of 32 bytes from 0C010000,"
	             " 0 pages opened): code cache clear #1, 0 pages open\n") == 1, "the clear's log line: %s", g_log.c_str());
	compile(0x8C010000, 32);
	spg_VblankCountTotal = 1000;
	dma(0x8C00FC00, 0x800, JDMA_MAPLE);
	EXPECT(lines("[jdma] Maple DMA to 8C00FC00, 2048 bytes, over compiled code (1 lines of 32 bytes from 0C010000,"
	             " 0 pages opened): code cache clear #2, 0 pages open\n") == 1, "the log line of a Maple DMA: %s",
	       g_log.c_str());

	// Only the block's own lines hold code: the rest of its page, the lines and page next to it hold none.
	start();
	compile(0x8C010000, 32);
	dma(0x0C010020, 0x3E0);
	dma(0x0C010400, 1024);
	dma(0x0C00FFFC, 4);
	EXPECT(g_clears == 0, "a DMA next to the block's line cleared");
	dma(0x0C01001C, 4);
	EXPECT(g_clears == 1, "a DMA over the block's last word did not clear");
	start();
	compile(0x8C0103F0, 32);   // 16 bytes on page 0x40, 16 on 0x41
	dma(0x0C010400, 4);
	EXPECT(g_clears == 1, "a block's second line is not in the map");
	start();
	compile(0x8C010010, 4);
	dma(0x0C010000, 16);
	EXPECT(g_clears == 1, "a DMA over the start of a block's line did not clear");

	// Every RAM mirror is the same line; other areas are not RAM.
	start();
	compile(0xAC123400, 2);
	dma(0x0E123400, 2);
	EXPECT(g_clears == 1, "a DMA through another mirror did not clear");
	start();
	compile(0x8C800000, 64);
	dma(0x00800000, 64);
	dma(0x04800000, 64);
	dma(0xFC800000, 64);
	EXPECT(g_clears == 0, "a DMA to wave RAM, VRAM or P4 cleared");
	start();
	compile(0x80800000, 64);
	dma(0x0C800000, 64);
	EXPECT(g_clears == 0, "a block outside RAM is in the map");

	// The end of RAM wraps to its start.
	start();
	compile(0x8CFFFFFE, 4);
	dma(0x0C000000, 4);
	EXPECT(g_clears == 1, "a block over the end of RAM does not wrap");
	start();
	compile(0x8C000100, 4);
	dma(0x0CFFFF00, 0x400);
	EXPECT(g_clears == 1, "a DMA over the end of RAM does not wrap");

	// Lengths: 0 writes nothing; 32 MB and more touch every line.
	start();
	compile(0x8C000000, 4);
	compile(0x8C7FFC00, 4);
	dma(0x0C000000, 0);
	dma(0x0C000400, 0);
	EXPECT(g_clears == 0, "a DMA of no bytes cleared");
	dma(0x0C400000, 0x02000000);
	EXPECT(g_clears == 1, "a 32 MB DMA did not clear");
	// Nearly all of RAM from the middle of a page: that page is met once, so it does not open.
	start();
	compile(0x8C001400, 4);
	compile(0x8C0017E0, 4);
	dma(0x0C001600, RAM_SIZE - 32);
	EXPECT(g_clears == 1 && !s_jdma.open[5] && s_jdma.hit[5] == 1, "a DMA of RAM less a line: clears %u open %u",
	       g_clears, s_jdma.open[5]);

	// No map with the MMU JIT or with JIT SMC CHECK off.
	start();
	compile(0x8C010000, 32);
	g_jit_mmu = true;
	dma(0x0C010000, 32);
	EXPECT(g_clears == 0, "a DMA cleared with the MMU JIT");
	g_jit_mmu = false;
	g_sbp = 0;
	dma(0x0C010000, 32);
	EXPECT(g_clears == 0, "a DMA cleared with the check off");

	// A page hit again within 60 vblanks opens; its blocks then guard themselves and leave the map.
	start();
	compile(0x8C020000, 32);
	spg_VblankCountTotal = 100;
	dma(0x0C020000, 64);
	compile(0x8C020000, 32);
	spg_VblankCountTotal = 159;
	dma(0x0C020000, 64);
	EXPECT(g_clears == 2 && s_jdma.open[0x80] && s_jdma.opened == 1, "a second hit at 59 vblanks: clears %u open %u",
	       g_clears, s_jdma.open[0x80]);
	EXPECT(lines("(1 lines of 32 bytes from 0C020000, 1 pages opened): code cache clear #2, 1 pages open\n") == 1,
	       "the log line of a clear that opens a page: %s", g_log.c_str());
	compile(0x8C020000, 32);
	compile(0x8C0203F0, 32);   // ends on page 0x81: guarded too
	EXPECT(!code_bit(0x20000) && !code_bit(0x203E0) && !code_bit(0x20400), "an open page's blocks are in the map");
	dma(0x0C020000, 64);
	dma(0x0C0203E0, 64);
	EXPECT(g_clears == 2, "a DMA over an open page cleared");
	// A second page opens: the line counts the pages this clear opened, then all the open ones.
	compile(0x8C024000, 32);
	spg_VblankCountTotal = 300;
	dma(0x0C024000, 4);
	compile(0x8C024000, 32);
	dma(0x0C024000, 4);
	EXPECT(lines("(1 lines of 32 bytes from 0C024000, 1 pages opened): code cache clear #4, 2 pages open\n") == 1,
	       "the log line of a second open page: %s", g_log.c_str());
	start();
	compile(0x8C020000, 32);
	spg_VblankCountTotal = 100;
	dma(0x0C020000, 4);
	compile(0x8C020000, 32);
	spg_VblankCountTotal = 160;
	dma(0x0C020000, 4);
	EXPECT(g_clears == 2 && !s_jdma.open[0x80], "a second hit at 60 vblanks opened the page");
	// At vblank 0 as well.
	start();
	compile(0x8C020000, 32);
	dma(0x0C020000, 4);
	compile(0x8C020000, 32);
	dma(0x0C020000, 4);
	EXPECT(s_jdma.open[0x80], "two hits at vblank 0 did not open the page");

	// The cache's other clears keep the open pages; a reset drops them.
	ext_clear();
	EXPECT(s_jdma.open[0x80], "a cache clear closed a page");
	reset_all();
	EXPECT(!s_jdma.open[0x80] && !s_jdma.hit[0x80] && !s_jdma.clears && !s_jdma.opened, "a reset kept the page state");
	compile(0x8C020000, 32);
	EXPECT(code_bit(0x20000), "after a reset a block is not in the map");
}

static u64 g_seed = 1;
static u32 rnd()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 7;
	g_seed ^= g_seed << 17;
	return (u32)g_seed;
}
// A guest address: mostly RAM in its four mirrors, near a few hot regions so the hits come often.
static u32 addr_rnd()
{
	static const u32 hi[] = { 0x8C000000, 0x0C000000, 0xAC000000, 0x0D000000, 0x0F000000, 0x00000000, 0x04000000,
	                          0xA0000000, 0xFC000000 };
	const u32 h = hi[rnd() % 16 < 13 ? rnd() % 5 : 5 + rnd() % 4];
	const u32 base = (rnd() % 4) << 22;
	return h + ((base + (rnd() % 0x8000) * 2) & 0x00FFFFFE);
}
static u32 g_rnd_clears, g_rnd_opens;
static void test_random(int rounds)
{
	int bad = 0;
	for (int r = 0; r < rounds && bad < 3; r++)
	{
		g_seed = 0x9E3779B97F4A7C15ull * (u64)(r + 1);
		start();
		for (int op = 0; op < 2000; op++)
		{
			const u32 k = rnd() % 100;
			if (k < 45)
				compile(addr_rnd(), rnd() % 8 ? 2 + (rnd() % 64) * 2 : rnd() % 1200);
			else if (k < 80)
			{
				const u32 s = rnd() % 10;
				dma(addr_rnd() & ~3u, s == 0 ? 0 : s == 1 ? 0x01000000 + rnd() % 0x01800000 : (rnd() % 0x800) * 4);
			}
			else if (k < 95)
				spg_VblankCountTotal += rnd() % 4 ? rnd() % 8 : rnd() % 200;
			else if (k < 97)
				ext_clear();
			else if (k < 98)
				g_jit_mmu = rnd() % 4 == 0;
			else if (k < 99)
				g_sbp = rnd() % 4 ? 1 : rnd() % 3;
			else if (rnd() % 4 == 0)
			{
				g_rnd_clears += s_jdma.clears;
				g_rnd_opens += s_jdma.opened;
				reset_all();
			}
			if (!same_state())
			{
				bad++;
				fprintf(stdout, "FAIL: round %d op %d: clears %u model %u\n", r, op, g_clears, m_clears);
				break;
			}
		}
		g_rnd_clears += s_jdma.clears;
		g_rnd_opens += s_jdma.opened;
	}
	EXPECT(bad == 0, "random: %d rounds differ from the model", bad);
	// A run that never clears or opens tests nothing.
	EXPECT(g_rnd_clears > (u32)rounds * 20 && g_rnd_opens > (u32)rounds, "random: only %u DMA clears, %u pages opened",
	       g_rnd_clears, g_rnd_opens);
}

int main(int argc, char** argv)
{
	const int rounds = argc > 1 ? atoi(argv[1]) : 60;
	test_fixed();
	test_random(rounds);
	printf("%s: jdma_check, %d checks, %d random rounds (%u DMA clears, %u pages opened)\n", g_fail ? "FAIL" : "PASS",
	       g_checks, rounds, g_rnd_clears, g_rnd_opens);
	return g_fail ? 1 : 0;
}
