// PC check of the RSX stall log: the real ps3/ps3_rsxwatch.c on a fake gcm and clock. When it dumps, what the dump
// holds (queue state, the words at get, the last 16 frames), and that a flip ends the stall.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static char g_log[1 << 16];
static size_t g_log_n;
int check_printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int n = vsnprintf(g_log + g_log_n, sizeof(g_log) - g_log_n, fmt, ap);
	va_end(ap);
	if (n > 0)
		g_log_n += (size_t)n < sizeof(g_log) - g_log_n ? (size_t)n : sizeof(g_log) - g_log_n - 1;
	return n;
}
#define printf check_printf

#include "rsx/gcm_sys.h"
uint64_t ps3_timebase_hz = 79800000;
static uint64_t g_now = 1000;
uint64_t gettime(void) { return g_now; }
static gcmControlRegister g_ctrl;
gcmControlRegister* gcmGetControlRegister(void) { return &g_ctrl; }
static u64 g_vbl;
u64 gcmGetVBlankCount(void) { return g_vbl; }
u32 gcmGetFlipStatus(void) { return 7; }
// IO offsets: the command buffer at 0x100000, another IO region at 0x400000, nothing else.
static u32 g_cb[1024], g_other[64];
int gcmIoOffsetToAddress(u32 off, void** a)
{
	if (off >= 0x100000 && off < 0x100000 + sizeof(g_cb))
		*a = (char*)g_cb + (off - 0x100000);
	else if (off >= 0x400000 && off < 0x400000 + sizeof(g_other))
		*a = (char*)g_other + (off - 0x400000);
	else
		return -1;
	return 0;
}
uint32_t ps3gl_frame_flush_count, ps3gl_frame_flush_vertex_count, ps3gl_frame_tex_binds;
uint32_t ps3gl_frame_tex_alloc_count, ps3gl_frame_subimage_count;

#include "ps3/ps3_rsxwatch.c"

static int g_fail, g_checks;
#define EXPECT(c, ...) do { g_checks++; if (!(c)) { g_fail++; fprintf(stdout, "FAIL: " __VA_ARGS__); fputc('\n', stdout); } } while (0)

static int count(const char* s)
{
	int n = 0;
	for (const char* p = g_log; (p = strstr(p, s)) != NULL; p += strlen(s))
		n++;
	return n;
}
static void log_clear(void)
{
	g_log_n = 0;
	g_log[0] = 0;
}
static gcmContextData g_ctx;
// Frames skipped at 60 a second for s seconds.
static void skip_for(double s)
{
	for (int i = 0; i < (int)(s * 60.0 + 0.5); i++)
	{
		g_now += ps3_timebase_hz / 60;
		ps3_rsxwatch_skip(&g_ctx, 10, 8);
	}
}
// The text of the line that holds key, without its line end.
static const char* line_of(const char* key, char* out, size_t len)
{
	const char* p = strstr(g_log, key);
	out[0] = 0;
	if (!p)
		return out;
	while (p > g_log && p[-1] != '\n')
		p--;
	size_t n = strcspn(p, "\n");
	if (n >= len)
		n = len - 1;
	memcpy(out, p, n);
	out[n] = 0;
	return out;
}
// The words line for words from..to of buf, get at word g (printed in brackets).
static void words_text(char* out, size_t len, const u32* buf, int from, int to, int g)
{
	int n = 0;
	for (int i = from; i < to; i++)
		n += snprintf(out + n, len - n, i == g ? " [%08X]" : " %08X", (unsigned)buf[i]);
}

int main(void)
{
	char ln[512], want[512];
	for (u32 i = 0; i < 1024; i++)
		g_cb[i] = 0xC0DE0000u | i;
	for (u32 i = 0; i < 64; i++)
		g_other[i] = 0xAB000000u | i;
	g_ctx.begin = g_cb;
	g_ctx.end = g_cb + 1024;
	g_ctx.current = g_cb + 100;

	// 20 frames: the ring keeps the last 16, with the counts of their own frame call.
	for (u32 f = 0; f < 20; f++)
	{
		ps3gl_frame_flush_vertex_count = 1000 + f;
		ps3gl_frame_flush_count = 10 + f;
		ps3gl_frame_tex_binds = 20 + f;
		ps3gl_frame_tex_alloc_count = f;
		ps3gl_frame_subimage_count = 2 * f;
		if (f != 7)
			ps3_rsxwatch_frame(640, 480);
		g_ctrl.put = 0x100000 + 4 * f;
		g_vbl = 100 + f;
		ps3_rsxwatch_flip(f);
	}
	EXPECT(g_log_n == 0, "flips with no stall log: %s", g_log);

	// A stall: nothing under 1 s, then one dump.
	g_ctrl.get = 0x100000 + 4 * 50;
	g_ctrl.put = 0x100000 + 4 * 60;
	g_ctrl.ref = 0x1234;
	g_vbl = 777;
	skip_for(0.95);
	EXPECT(g_log_n == 0, "a dump before 1 s: %s", g_log);
	skip_for(0.1);
	EXPECT(count("[rsxw] no flip for") == 1, "no dump at 1 s");
	line_of("[rsxw] no flip for", ln, sizeof(ln));
	EXPECT(strstr(ln, "get=001000C8 put=001000F0 ref=00001234 | flips queued=10 done=8 status=7 | vblank 777") != NULL,
	       "the queue line: %s", ln);
	words_text(want, sizeof(want), g_cb, 42, 58, 50);
	line_of("[rsxw] words at", ln, sizeof(ln));
	EXPECT(strstr(ln, "(get at 8):") && strstr(ln, want), "the words around get: %s", ln);
	EXPECT(count("[rsxw] flip ") == 16, "%d frame lines, not 16", count("[rsxw] flip "));
	const char* first = strstr(g_log, "[rsxw] flip ");
	EXPECT(first && strncmp(first, "[rsxw] flip 4 vblank 104 put=00100010:", 38) == 0, "the oldest frame is not first");
	line_of("[rsxw] flip 19 ", ln, sizeof(ln));
	EXPECT(strstr(ln, "put=0010004C: 1019 verts, 29 flushes, 39 binds, 19 new tex, 38 sub, scene 640x480") != NULL,
	       "frame 19: %s", ln);
	line_of("[rsxw] flip 7 ", ln, sizeof(ln));
	EXPECT(strstr(ln, ": 0 verts, 0 flushes, 0 binds, 0 new tex, 0 sub, scene 0x0") != NULL,
	       "a flip with no frame call keeps old counts: %s", ln);
	line_of("[rsxw] flip 8 ", ln, sizeof(ln));
	EXPECT(strstr(ln, ": 1008 verts,") != NULL, "frame 8: %s", ln);

	// A second dump at 5 s, then none.
	skip_for(3.8);
	EXPECT(count("[rsxw] no flip for") == 1, "a second dump before 5 s");
	skip_for(0.2);
	EXPECT(count("[rsxw] no flip for") == 2, "no second dump at 5 s");
	skip_for(20.0);
	EXPECT(count("[rsxw] no flip for") == 2, "%d dumps, not 2", count("[rsxw] no flip for"));

	// A flip ends it: one line, with the skips; the next short stall logs nothing.
	log_clear();
	ps3_rsxwatch_flip(20);
	line_of("[rsxw] flips back", ln, sizeof(ln));
	EXPECT(strstr(ln, "[rsxw] flips back after 25.0 s, 1503 frames skipped") != NULL, "the back line: %s", g_log);
	log_clear();
	skip_for(0.8);
	ps3_rsxwatch_flip(21);
	skip_for(0.8);
	EXPECT(g_log_n == 0, "two short stalls logged: %s", g_log);
	ps3_rsxwatch_flip(22);

	// get at the buffer's start, near its end, in another IO region, at no IO address.
	log_clear();
	g_ctrl.get = 0x100000;
	skip_for(1.1);
	words_text(want, sizeof(want), g_cb, 0, 8, 0);
	line_of("[rsxw] words at", ln, sizeof(ln));
	EXPECT(strstr(ln, "(get at 0):") && strstr(ln, want), "get at the start: %s", ln);
	ps3_rsxwatch_flip(23);
	log_clear();
	g_ctrl.get = 0x100000 + 4 * 1020;
	skip_for(1.1);
	words_text(want, sizeof(want), g_cb, 1012, 1024, 1020);
	line_of("[rsxw] words at", ln, sizeof(ln));
	EXPECT(strstr(ln, "(get at 8):") && strstr(ln, want) && !strstr(ln, "AB000000"), "get near the end: %s", ln);
	ps3_rsxwatch_flip(24);
	log_clear();
	g_ctrl.get = 0x400000 + 8;
	skip_for(1.1);
	words_text(want, sizeof(want), g_other, 2, 10, 2);
	line_of("[rsxw] words at", ln, sizeof(ln));
	EXPECT(strstr(ln, "(get at 0):") && strstr(ln, want), "get in another region: %s", ln);
	ps3_rsxwatch_flip(25);
	log_clear();
	g_ctrl.get = 0x900000;
	skip_for(1.1);
	EXPECT(count("[rsxw] get 00900000 is no IO address") == 1 && count("[rsxw] words at") == 0, "no IO address: %s", g_log);

	fprintf(stdout, "%s: rsxwatch_check, %d checks\n", g_fail ? "FAIL" : "PASS", g_checks);
	return g_fail ? 1 : 0;
}
