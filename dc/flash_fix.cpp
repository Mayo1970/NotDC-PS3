// Writes the RTC time into the flash user syscfg block, so the BIOS sees a set clock and plays
// the boot animation instead of the Date/Time screen. Port of Flycast fixUpDCFlash() (block code
// from redream). Flash bytes are DC little-endian; fields are accessed bytewise on any host.
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
