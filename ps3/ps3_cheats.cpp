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

// G24 (Flycast cheats.cpp 7fee0d2d2, e894d4df5): Fur Fighters hangs on two equal TCNT0 reads (its logging on costs
// cycles); Dave Mirra BMX gets a fixed main loop time. Keyed by IP.BIN's 10-byte product number.
static const struct { char id[11]; u32 addr, value; } k_patches[] = {
	{ "T-8107N   ", 0x314248, 1 },            // Fur Fighters (US)
	{ "T-8113D-50", 0x314228, 1 },            // Fur Fighters (EU)
	{ "T-8120N   ", 0x30B8CC, 0x42040000 },   // Dave Mirra BMX (US): 33.0 ms
	{ "T8120D  50", 0x3011CC, 0x42200000 },   // Dave Mirra BMX (EU): 40.0 ms
};

int ps3_cheats_disc(const u8* id)
{
	for (u32 i = 0; i < sizeof(k_patches) / sizeof(k_patches[0]); i++)
	{
		if (memcmp(id, k_patches[i].id, 10) != 0)
			continue;
		// Already in the list: the cheat file has it, or an earlier boot of this run added it.
		for (u32 k = 0; k < s_count; k++)
			if (s_cheats[k].type == CHEAT_SET && s_cheats[k].address == k_patches[i].addr)
				return 1;
		if (s_count >= CHEAT_MAX)
			return 0;
		Cheat& c = s_cheats[s_count++];
		memset(&c, 0, sizeof(c));
		c.type = CHEAT_SET;
		c.bits = 32;
		c.address = k_patches[i].addr;
		c.value = k_patches[i].value;
		c.repeat = 1;
		c.enabled = c.has_address = true;
		return 1;
	}
	return 0;
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

// A disabled example that shows users the file form. Written only when absent, so a user's edit stays.
void ps3_cheats_example(void)
{
	static const char k_text[] =
		"; NotDC cheat file example. Its cheat is not enabled, and its name matches no disc.\n"
		"; To use a cheat file: give it the name of the disc image file with the extension .cht,\n"
		"; for example \"Crazy Taxi (USA).gdi\" -> \"Crazy Taxi (USA).cht\", in this folder.\n"
		"; Then set SCREEN > Cheats ON before you boot the game. Lines that start with ; are notes.\n"
		";\n"
		"; cheats                     the number of cheats in the file\n"
		"; cheatN_desc                a name for cheat N (N starts at 0)\n"
		"; cheatN_enable              true = apply the cheat at each frame, false = do not\n"
		"; cheatN_address             RAM offset 0x0 to 0xFFFFFF (0x8C123456 or 0x0C123456 = 0x123456)\n"
		"; cheatN_value               the value to write or compare\n"
		"; cheatN_memory_search_size  3 = 8 bits, 4 = 16 bits, 5 = 32 bits (always set it)\n"
		"; cheatN_cheat_type          1 = set, 2 = add, 3 = subtract,\n"
		";                            4 / 5 / 6 / 7 = apply the next cheat only if the RAM value is\n"
		";                            equal / not equal / greater / less than the value,\n"
		";                            8 = copy from cheatN_address to cheatN_dest_address\n"
		"; cheatN_repeat_count        optional: write N values; each next one moves by\n"
		";                            cheatN_repeat_add_to_address values and adds cheatN_repeat_add_to_value\n"
		";\n"
		"; This example writes 1 (32 bits) at RAM offset 0x314248: the Fur Fighters (US) freeze fix.\n"
		"cheats = 1\n"
		"cheat0_desc = \"Example: Fur Fighters (US) freeze fix\"\n"
		"cheat0_enable = false\n"
		"cheat0_address = 0x314248\n"
		"cheat0_value = 1\n"
		"cheat0_memory_search_size = 5\n"
		"cheat0_cheat_type = 1\n";
	struct stat st;
	mkdir(CHEAT_DIR, 0777);
	if (stat(CHEAT_DIR "example.cht", &st) == 0)
		return;
	FILE* f = fopen(CHEAT_DIR "example.cht", "wb");
	if (!f)
		return;
	fwrite(k_text, 1, sizeof(k_text) - 1, f);
	fclose(f);
}
