// Plan section 7, bundle 12 (P36): the disc's cheat file, read at boot and applied at each vblank.
#include "types.h"
#include "ps3_cheats.h"
#include "ps3_presets.h"
#include "dc/mem/sh4_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CHEAT_DIR      "/dev_hdd0/data/NotDC/cheats/"
// Flycast refuses a cheat file of 1 MB or more.
#define CHEAT_FILE_MAX (1024 * 1024)

static Cheat s_cheats[CHEAT_MAX];
static u32 s_count;
static u32 s_frames, s_writes;

// Flycast readRam and writeRam: through the memory map, so the guest's byte order is the map's work.
static uint32_t ram_read(uint32_t addr, uint32_t bits)
{
	switch (bits)
	{
	case 16: return ReadMem16_nommu(0x8C000000 + addr);
	case 32: return ReadMem32_nommu(0x8C000000 + addr);
	default: return ReadMem8_nommu(0x8C000000 + addr);
	}
}

static void ram_write(uint32_t addr, uint32_t value, uint32_t bits)
{
	switch (bits)
	{
	case 16: WriteMem16_nommu(0x8C000000 + addr, (u16)value); break;
	case 32: WriteMem32_nommu(0x8C000000 + addr, value); break;
	default: WriteMem8_nommu(0x8C000000 + addr, (u8)value); break;
	}
}

static char* read_text(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = (n > 0 && n < CHEAT_FILE_MAX) ? (char*)malloc(n + 1) : NULL;
	if (buf && fread(buf, 1, n, f) != (size_t)n)
	{
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf)
		buf[n] = 0;
	else
		printf("[cheat] %s: empty, over 1 MB or not read\n", path);
	return buf;
}

// disc: the image's path. The file is cheats/<image name with no extension>.cht, or .txt (Flycast's two names).
void ps3_cheats_load(const char* disc)
{
	s_count = 0;
	if (!g_cheats_preset || !disc || !disc[0])
		return;
	mkdir(CHEAT_DIR, 0777);
	const char* base = strrchr(disc, '/');
	base = base ? base + 1 : disc;
	const char* dot = strrchr(base, '.');
	const int len = dot ? (int)(dot - base) : (int)strlen(base);
	static const char* const ext[2] = { ".cht", ".txt" };
	char path[640];
	char* text = NULL;
	for (int k = 0; k < 2 && !text; k++)
	{
		snprintf(path, sizeof(path), CHEAT_DIR "%.*s%s", len, base, ext[k]);
		text = read_text(path);
	}
	if (!text)
	{
		printf("[cheat] no file " CHEAT_DIR "%.*s.cht\n", len, base);
		return;
	}
	s_count = cheats_parse(text, RAM_SIZE, s_cheats);
	free(text);
	u32 on = 0;
	for (u32 i = 0; i < s_count; i++)
		on += s_cheats[i].enabled;
	printf("[cheat] %s: %u cheat(s), %u enabled\n", path, s_count, on);
	for (u32 i = 0; i < s_count && i < 16; i++)
		printf("[cheat] %u: type %u, %u bits, address %06X, value %08X, repeat %u, %s\n", i, s_cheats[i].type,
		       s_cheats[i].bits, s_cheats[i].address, s_cheats[i].value, s_cheats[i].repeat,
		       s_cheats[i].enabled ? "enabled" : "not enabled");
}

void ps3_cheats_vblank(void)
{
	if (!s_count)
		return;
	s_writes += cheats_apply(s_cheats, s_count, ram_read, ram_write);
	if (++s_frames < 300)
		return;
	printf("[cheat] %u write(s) in %u frames\n", s_writes, s_frames);
	s_frames = s_writes = 0;
}
