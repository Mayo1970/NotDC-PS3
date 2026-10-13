// Flycast fixUpDCFlash() port (block code from redream): RTC into the syscfg block so the BIOS skips
// the Date/Time screen, plus the region bytes. Flash bytes are DC order, accessed bytewise on BE.
#include "types.h"
#include "dc/mem/sh4_mem.h"
#include <string.h>

#define FLASH_BLOCK_SIZE    0x40
#define FLASH_BITMAP_BLOCKS (FLASH_BLOCK_SIZE * 8)
#define FLASH_BITMAP_BYTES  (FLASH_BITMAP_BLOCKS * 64)
#define FLASH_PT_USER       2
#define FLASH_USER_OFFSET   0x1c000
#define FLASH_USER_SIZE     0x4000
#define FLASH_USER_SYSCFG   0x05

static const char s_magic[16] = { 'K','A','T','A','N','A','_','F','L','A','S','H','_','_','_','_' };

static u8* phys(u32 id) { return &flash_b[FLASH_USER_OFFSET + id * FLASH_BLOCK_SIZE]; }
static u32 rd16(const u8* p) { return p[0] | (p[1] << 8); }
static void wr16(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static u32 crc_block(const u8* b)
{
	u32 n = 0xffff;
	for (int i = 0; i < 62; i++)
	{
		n ^= (u32)b[i] << 8;
		for (int c = 0; c < 8; c++)
			n = (n & 0x8000) ? ((n << 1) ^ 4129) : (n << 1);
	}
	return (~n) & 0xffff;
}

static int num_user_blocks()
{
	int physical = FLASH_USER_SIZE / FLASH_BLOCK_SIZE;
	int bitmaps = (FLASH_USER_SIZE + FLASH_BITMAP_BYTES - 1) / FLASH_BITMAP_BYTES;
	return physical - bitmaps - 1;
}

// A bitmap bit is 1 while the block is free (flash erases to 0xFF).
static bool is_allocated(const u8* bitmap, u32 id)
{
	u32 i = (id - 1) % FLASH_BITMAP_BLOCKS;
	return (bitmap[i / 8] & (0x80 >> (i % 8))) == 0;
}

static bool header_valid()
{
	const u8* h = phys(0);
	return memcmp(h, s_magic, 16) == 0 && h[16] == FLASH_PT_USER;
}

// Last valid physical block holding block_id; the highest one wins, as on the DC.
static int lookup_block(u32 block_id)
{
	int blocks = num_user_blocks();
	int bitmap_id = 1 + blocks;
	const u8* bitmap = 0;
	int result = 0;
	for (int id = 1; id < 1 + blocks; id++)
	{
		if (id % FLASH_BITMAP_BLOCKS == 1)
			bitmap = phys(bitmap_id++);
		if (!is_allocated(bitmap, id))
			break;
		const u8* b = phys(id);
		if (rd16(b) == block_id && rd16(b + 62) == crc_block(b))
			result = id;
	}
	return result;
}

static int alloc_block()
{
	int blocks = num_user_blocks();
	int bitmap_id = blocks;
	u8* bitmap = 0;
	int id = 1;
	for (; id < 1 + blocks; id++)
	{
		if (id % FLASH_BITMAP_BLOCKS == 1)
			bitmap = phys(++bitmap_id);
		if (!is_allocated(bitmap, id))
			break;
	}
	if (id >= 1 + blocks)
		return 0;
	u32 i = (id - 1) % FLASH_BITMAP_BLOCKS;
	bitmap[i / 8] &= ~(0x80 >> (i % 8));
	return id;
}

void dc_flash_set_time(u32 rtc)
{
	if (flash_b.data == 0)
		return;

	if (!header_valid())
	{
		memset(phys(0), 0xFF, FLASH_USER_SIZE);
		memcpy(phys(0), s_magic, 16);
		phys(0)[16] = FLASH_PT_USER;
		phys(0)[17] = 0;
		printf("[flash] user partition reset\n");
	}

	u8 blk[FLASH_BLOCK_SIZE];
	int id = lookup_block(FLASH_USER_SYSCFG);
	if (id)
		memcpy(blk, phys(id), FLASH_BLOCK_SIZE);
	else
	{
		// Defaults as in Flycast: tz 0, lang 0, stereo, autostart on.
		memset(blk, 0xFF, sizeof(blk));
		blk[6] = 0;
		blk[7] = 0;
		blk[8] = 0;
		blk[9] = 1;
		id = alloc_block();
		if (!id)
		{
			printf("[flash] no free block for syscfg\n");
			return;
		}
	}

	wr16(blk + 0, FLASH_USER_SYSCFG);
	wr16(blk + 2, rtc & 0xFFFF);
	wr16(blk + 4, rtc >> 16);
	wr16(blk + 62, crc_block(blk));
	memcpy(phys(id), blk, FLASH_BLOCK_SIZE);
	printf("[flash] syscfg time set to %08X (block %d)\n", rtc, id);
}

// Factory partition region bytes ('0' JP, '1' USA, '2' EU), two copies, as Flycast fixUpDCFlash.
#define FLASH_REGION_A 0x1a002
#define FLASH_REGION_B 0x1a0a2

static bool s_region_patched;
static u8 s_region_orig[2];

// Flycast loadSpecialSettings: keep the console region if the disc lists it, else switch to one
// it lists (USA first). The original bytes come back in dc_flash_restore_region before the save.
void dc_flash_set_region(const char* area)
{
	s_region_patched = false;
	if (flash_b.data == 0)
		return;
	bool j = memchr(area, 'J', 8) != 0;
	bool u = memchr(area, 'U', 8) != 0;
	bool e = memchr(area, 'E', 8) != 0;
	if (!j && !u && !e)
	{
		printf("[flash] disc lists no region (area '%.8s'), region kept\n", area);
		return;
	}
	int cur = (int)flash_b[FLASH_REGION_A] - '0';
	int want = cur;
	switch (cur)
	{
	case 0: if (!j) want = u ? 1 : 2; break;
	case 1: if (!u) want = e ? 2 : 0; break;
	case 2: if (!e) want = u ? 1 : 0; break;
	default: want = u ? 1 : (e ? 2 : 0); break;
	}
	if (want == cur)
	{
		printf("[flash] region %d fits disc area '%.8s'\n", cur, area);
		return;
	}
	s_region_orig[0] = flash_b[FLASH_REGION_A];
	s_region_orig[1] = flash_b[FLASH_REGION_B];
	flash_b[FLASH_REGION_A] = (u8)('0' + want);
	flash_b[FLASH_REGION_B] = (u8)('0' + want);
	s_region_patched = true;
	printf("[flash] region %d -> %d for disc area '%.8s'\n", cur, want, area);
}

// Keeps the console's own region in dc_flash_wb.bin; a per-disc switch must not persist.
void dc_flash_restore_region()
{
	if (!s_region_patched || flash_b.data == 0)
		return;
	flash_b[FLASH_REGION_A] = s_region_orig[0];
	flash_b[FLASH_REGION_B] = s_region_orig[1];
	s_region_patched = false;
}
