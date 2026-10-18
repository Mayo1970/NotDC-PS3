/*
	Copyright 2023 flyinghead (core/hw/flashrom/nvmem.cpp)
	Copyright 2021 flyinghead (core/emulator.cpp)
	Flash block handling code borrowed from redream (https://github.com/inolen/redream), via Flycast
	core/hw/flashrom (flashrom.h, flashrom.cpp)

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.

	Changed for NotDC (PS3 port) from 2026-09-19 to 2026-10-18.
 */
// Flycast fixUpDCFlash() port (block code from redream): RTC into the syscfg block so the BIOS skips
// the Date/Time screen, plus the factory bytes. Flash bytes are DC order, accessed bytewise on BE.
#include "types.h"
#include "dc/mem/sh4_mem.h"
#include "dc/flash_fix.h"
#include "ps3/ps3_presets.h"
#include <string.h>

#define FLASH_BLOCK_SIZE    0x40
#define FLASH_BITMAP_BLOCKS (FLASH_BLOCK_SIZE * 8)
#define FLASH_BITMAP_BYTES  (FLASH_BITMAP_BLOCKS * 64)
#define FLASH_PT_USER       2
#define FLASH_USER_OFFSET   0x1c000
#define FLASH_USER_SIZE     0x4000
#define FLASH_USER_SYSCFG   0x05
#define FLASH_USER_INET     0x80
#define FLASH_USER_ISP1     0xC0
#define FLASH_USER_ISP2     0xC6
#define FLASH_USER_BBA      0xE0
#define SYSCFG_LANG         7

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

// Physical blocks in use; they are allocated in order, so the first free one ends the count.
static int used_blocks()
{
	int blocks = num_user_blocks();
	int bitmap_id = 1 + blocks;
	const u8* bitmap = 0;
	int id = 1;
	for (; id < 1 + blocks; id++)
	{
		if (id % FLASH_BITMAP_BLOCKS == 1)
			bitmap = phys(bitmap_id++);
		if (!is_allocated(bitmap, id))
			break;
	}
	return id - 1;
}

// G11, Flycast's alloc_block: the first free block, or a used one whose data has a newer copy (a full flash has many).
// Only the dial-up account takes it; the syscfg block keeps the rule above.
static int alloc_block_reuse()
{
	int blocks = num_user_blocks();
	int bitmap_id = blocks;
	u8* bitmap = 0;
	int id = 1;
	for (; id < 1 + blocks; id++)
	{
		if (id % FLASH_BITMAP_BLOCKS == 1)
			bitmap = phys(++bitmap_id);
		if (!is_allocated(bitmap, id) || lookup_block(rd16(phys(id))) != id)
			break;
	}
	if (id >= 1 + blocks)
		return 0;
	u32 i = (id - 1) % FLASH_BITMAP_BLOCKS;
	bitmap[i / 8] &= ~(0x80 >> (i % 8));
	return id;
}

// Flycast DCFlashChip::WriteBlock: in place when the block exists, else a new one; its id and CRC set.
static bool write_block(u32 block_id, const u8* data)
{
	int id = lookup_block(block_id);
	if (!id)
		id = alloc_block_reuse();
	if (!id)
		return false;
	u8* b = phys(id);
	memcpy(b, data, FLASH_BLOCK_SIZE);
	wr16(b, block_id);
	wr16(b + 62, crc_block(b));
	return true;
}

// Factory partition, two copies: "00", then region, language and broadcast as digits, then "Dreamcast  ".
#define FLASH_FACTORY_A 0x1a000
#define FLASH_FACTORY_B 0x1a0a0
enum { FACT_REGION = 2, FACT_LANG = 3, FACT_BROADCAST = 4, FACT_COUNT = 5 };
// 6 ID bytes; [-2] = ~sum, [-1] = sum, [7] = machine version. The second copy is 0xA0 further.
#define FLASH_CONSOLE_ID 0x1a058
#define FLASH_COPY_STEP  0xA0

// A row's or a disc's value must not reach dc_flash_wb.bin: dc_flash_restore_region puts these back.
static u8 s_fact_orig[2][FACT_COUNT];
static u32 s_fact_patched;   // bit n: factory byte n is not the flash's own
static bool s_lang_forced;   // the syscfg language is the BIOS Language row's
static u8 s_lang_orig, s_lang_set;

static void set_factory(u32 at, u32 digit)
{
	if (!(s_fact_patched & (1u << at)))
	{
		s_fact_orig[0][at] = flash_b[FLASH_FACTORY_A + at];
		s_fact_orig[1][at] = flash_b[FLASH_FACTORY_B + at];
		s_fact_patched |= 1u << at;
	}
	flash_b[FLASH_FACTORY_A + at] = (u8)('0' + digit);
	flash_b[FLASH_FACTORY_B + at] = (u8)('0' + digit);
}

static bool lang_row_set() { return g_bios_lang_preset >= 1 && g_bios_lang_preset <= 6; }

// P85: with no flash file, Flycast's Validate() on an empty chip, then its region, language and broadcast options.
void dc_flash_build()
{
	if (flash_b.data == 0)
		return;
	memset(&flash_b[0], 0xFF, flash_b.Size);
	// Block partitions 2 (user), 3 (game) and 4: the magic, the partition number, version 0.
	static const u32 part[3] = { FLASH_USER_OFFSET, 0x10000, 0x00000 };
	for (u32 p = 0; p < 3; p++)
	{
		memcpy(&flash_b[part[p]], s_magic, 16);
		flash_b[part[p] + 16] = (u8)(FLASH_PT_USER + p);
		flash_b[part[p] + 17] = 0;
	}
	memcpy(&flash_b[FLASH_FACTORY_A], "00110Dreamcast  ", 16);
	memcpy(&flash_b[FLASH_FACTORY_B], "00110Dreamcast  ", 16);
	printf("[flash] no flash file: built a new flash (USA, English, NTSC)\n");
}

// The first fix-up after each flash load: it also forgets the last load's patches.
void dc_flash_set_time(u32 rtc)
{
	s_fact_patched = 0;
	s_lang_forced = false;
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
		// Flycast's defaults: tz 0, stereo, autostart on. Language 0 is its struct reset, 1 its Language option (P85).
		memset(blk, 0xFF, sizeof(blk));
		blk[6] = 0;
		blk[SYSCFG_LANG] = g_flash_defaults_preset ? 1 : 0;
		blk[8] = 0;
		blk[9] = 1;
		id = alloc_block();
		if (!id)
		{
			printf("[flash] no free block for syscfg\n");
			return;
		}
	}
	// P37: the BIOS Language row; rows 1-6 are the DC's languages 0-5.
	if (lang_row_set())
	{
		s_lang_orig = blk[SYSCFG_LANG];
		s_lang_set = (u8)(g_bios_lang_preset - 1);
		s_lang_forced = true;
		blk[SYSCFG_LANG] = s_lang_set;
	}

	wr16(blk + 0, FLASH_USER_SYSCFG);
	wr16(blk + 2, rtc & 0xFFFF);
	wr16(blk + 4, rtc >> 16);
	wr16(blk + 62, crc_block(blk));
	memcpy(phys(id), blk, FLASH_BLOCK_SIZE);
	printf("[flash] syscfg time set to %08X (block %d)\n", rtc, id);
}

// P85, Flycast fixUpDCFlash: a blank ID gets a random one in both copies; checksum and machine version always.
static void fix_console_id(u32 seed)
{
	u8* id = &flash_b[FLASH_CONSOLE_ID];
	bool blank = true;
	for (int i = 0; i < 6; i++)
		blank = blank && id[i] == 0xFF;
	if (blank)
	{
		// Own generator: rand() would move the C library's sequence.
		u32 x = seed ? seed : 1;
		for (int i = 0; i < 6; i++)
		{
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			id[i] = id[i + FLASH_COPY_STEP] = (u8)(x >> 8);
		}
		// Six 0xFF bytes mean "blank": never make them.
		if (id[0] == 0xFF)
			id[0] = id[FLASH_COPY_STEP] = 0xFE;
	}
	u8 sum = 0;
	for (int i = 0; i < 6; i++)
		sum += id[i];
	id[-1] = id[FLASH_COPY_STEP - 1] = sum;
	id[-2] = id[FLASH_COPY_STEP - 2] = (u8)~sum;
	// 0xFF = VA0, 0xFE = VA1, 0xFD = VA2; Flycast: it must not be 0xFF.
	id[7] = id[FLASH_COPY_STEP + 7] = 0xFE;
}

// Flycast's texts; the user name is its ISPUsername default. One table, so one TOC entry.
static const struct { char browser[10], atf[5], sega[5], user[9], pass[9], phone[8], mail[18], how[3][8]; } k_isp = {
	"PWBrowser", "AT&F", "SEGA", "flycast1", "password", "1234567", "flycast@gmail.com", { "kept", "written", "NO ROOM" } };

// A text of k_isp. Not inlined: GCC makes each inlined copy its own string constant, one TOC entry each.
static __attribute__((noinline, noclone)) void put_text(u8* dst, const char* src, u32 n)
{
	memcpy(dst, src, n);
}

// G11, Flycast nvmem.cpp add_isp_to_nvmem: the dial-up account a modem game reads, when the flash has none. Flycast
// also puts its user name into an account that exists; here that account stays as it is. Then the BBA block.
static void flash_isp()
{
	if (!header_valid())
		return;
	u8 b[FLASH_BLOCK_SIZE];
	int acct = 0, bba = 0;   // k_isp.how: kept, written, no room
	if (!lookup_block(FLASH_USER_INET))
	{
		bool ok = true;
		memset(b, 0, sizeof(b));
		put_text(b + 2, k_isp.browser, sizeof(k_isp.browser));
		b[12] = 0x1c;
		ok &= write_block(FLASH_USER_INET, b);
		memset(b, 0, sizeof(b));
		ok &= write_block(FLASH_USER_INET + 1, b);
		put_text(b + 32, k_isp.atf, sizeof(k_isp.atf));
		ok &= write_block(FLASH_USER_INET + 2, b);
		memset(b, 0, sizeof(b));
		ok &= write_block(FLASH_USER_INET + 3, b);
		memset(b + 27, 0xFF, sizeof(b) - 27);
		b[10] = b[14] = b[16] = 1;
		b[19] = 6;
		b[26] = 5;
		ok &= write_block(FLASH_USER_INET + 4, b);
		memset(b, 0xFF, sizeof(b));
		for (u32 i = FLASH_USER_INET + 5; i <= 0xBF; i++)
			ok &= write_block(i, b);
		// flash_isp1_block: unknown[4] at 2, "SEGA" at 6, user[28] at 10, password[16] at 38, phone[8] at 54.
		memset(b, 0, sizeof(b));
		b[5] = 1;
		put_text(b + 6, k_isp.sega, 4);
		put_text(b + 10, k_isp.user, sizeof(k_isp.user));
		put_text(b + 38, k_isp.pass, sizeof(k_isp.pass));
		put_text(b + 54, k_isp.phone, sizeof(k_isp.phone));
		ok &= write_block(FLASH_USER_ISP1, b);
		memset(b, 0, sizeof(b));
		ok &= write_block(FLASH_USER_ISP1 + 1, b);
		ok &= write_block(FLASH_USER_ISP1 + 2, b);
		put_text(b + 2, k_isp.mail, sizeof(k_isp.mail));
		ok &= write_block(FLASH_USER_ISP1 + 3, b);
		memset(b, 0, sizeof(b));
		ok &= write_block(FLASH_USER_ISP1 + 4, b);
		b[60] = 1;
		ok &= write_block(FLASH_USER_ISP1 + 5, b);
		// flash_isp2_block: "SEGA" at 2, user[28] at 6, password[16] at 34, phone[12] at 50.
		memset(b, 0, sizeof(b));
		put_text(b + 2, k_isp.sega, 4);
		put_text(b + 6, k_isp.user, sizeof(k_isp.user));
		put_text(b + 34, k_isp.pass, sizeof(k_isp.pass));
		put_text(b + 50, k_isp.phone, sizeof(k_isp.phone));
		ok &= write_block(FLASH_USER_ISP2, b);
		memset(b, 0, sizeof(b));
		for (u32 i = FLASH_USER_ISP2 + 1; i <= 0xEA; i++)
		{
			b[56] = i == 0xCB;
			ok &= write_block(i, b);
		}
		acct = ok ? 1 : 2;
	}
	// flash_bba_block (packed): "SEGA" at 2, the netmask 0xffffff00 at 14 in the SH4's byte order.
	const int at = lookup_block(FLASH_USER_BBA);
	if (at)
		memcpy(b, phys(at), sizeof(b));
	else
		memset(b, 0, sizeof(b));
	if (!at || memcmp(b + 2, k_isp.sega, 4) || !(b[14] | b[15] | b[16] | b[17]))
	{
		put_text(b + 2, k_isp.sega, 4);
		b[14] = 0x00;
		b[15] = b[16] = b[17] = 0xFF;
		bba = write_block(FLASH_USER_BBA, b) ? 1 : 2;
	}
	const int isp1 = lookup_block(FLASH_USER_ISP1);
	char user[28] = { 0 };
	if (isp1)
		memcpy(user, phys(isp1) + 10, 27);
	printf("[flash] dial-up account %s, BBA block %s: ISP1 user '%s', user blocks %d/%d\n", k_isp.how[acct],
	       k_isp.how[bba], user, used_blocks(), num_user_blocks());
}

// After dc_flash_set_time: the rows that write the factory partition (P37, P59, P85).
void dc_flash_fixup(u32 seed)
{
	if (flash_b.data == 0)
		return;
	if (lang_row_set())
		set_factory(FACT_LANG, g_bios_lang_preset - 1);
	// Broadcast row: 1 = NTSC ('0'), 2 = PAL ('1'); 3 = DISC waits for dc_flash_set_region.
	if (g_broadcast_preset == 1 || g_broadcast_preset == 2)
		set_factory(FACT_BROADCAST, g_broadcast_preset - 1);
	if (g_console_id_preset)
		fix_console_id(seed);
	if (g_modem_preset)
		flash_isp();
}

// Flycast loadSpecialSettings: keep the console region if the disc lists it, else switch to one
// it lists (USA first). The flash's own bytes come back in dc_flash_restore_region before the save.
void dc_flash_set_region(const char* area)
{
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
	// P59, Broadcast DISC: NTSC when the disc lists Japan or USA, PAL when it lists only Europe.
	if (g_broadcast_preset == 3)
		set_factory(FACT_BROADCAST, (j || u) ? 0 : 1);
	int cur = (int)flash_b[FLASH_FACTORY_A + FACT_REGION] - '0';
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
	set_factory(FACT_REGION, want);
	printf("[flash] region %d -> %d for disc area '%.8s'\n", cur, want, area);
}

// Flycast loadSpecialSettings: Broadcast DISC gives PAL to the discs that break at NTSC (Evil Twin: the fans,
// Flycast issue #2048). product: the 10 bytes of IP.BIN 0x40.
void dc_flash_disc_broadcast(const char* product)
{
	static const char k_pal[][11] = { "T46605D 80" };
	if (flash_b.data == 0 || g_broadcast_preset != 3)
		return;
	for (u32 i = 0; i < sizeof(k_pal) / sizeof(k_pal[0]); i++)
		if (!memcmp(product, k_pal[i], 10))
		{
			set_factory(FACT_BROADCAST, 1);
			printf("[flash] broadcast PAL for disc %.10s (Flycast list)\n", product);
		}
}

// Keeps the flash's own factory bytes and language in dc_flash_wb.bin; a row or a per-disc switch must not persist.
void dc_flash_restore_region()
{
	if (flash_b.data == 0)
		return;
	for (u32 at = FACT_REGION; at <= FACT_BROADCAST; at++)
	{
		if (!(s_fact_patched & (1u << at)))
			continue;
		flash_b[FLASH_FACTORY_A + at] = s_fact_orig[0][at];
		flash_b[FLASH_FACTORY_B + at] = s_fact_orig[1][at];
	}
	s_fact_patched = 0;
	if (!s_lang_forced)
		return;
	s_lang_forced = false;
	// The BIOS may have moved the block; a language it wrote itself in this run stays.
	int id = header_valid() ? lookup_block(FLASH_USER_SYSCFG) : 0;
	if (id && phys(id)[SYSCFG_LANG] == s_lang_set)
	{
		phys(id)[SYSCFG_LANG] = s_lang_orig;
		wr16(phys(id) + 62, crc_block(phys(id)));
	}
}

static char digit(u8 b) { return (b >= '0' && b <= '9') ? (char)b : '?'; }

// After every fix-up: the bytes the BIOS and the game read, and the flash file's own in brackets.
void dc_flash_report()
{
	if (flash_b.data == 0)
		return;
	char now[3], own[3];
	for (u32 at = FACT_REGION; at <= FACT_BROADCAST; at++)
	{
		const u8 b = flash_b[FLASH_FACTORY_A + at];
		now[at - FACT_REGION] = digit(b);
		own[at - FACT_REGION] = digit((s_fact_patched & (1u << at)) ? s_fact_orig[0][at] : b);
	}
	int id = header_valid() ? lookup_block(FLASH_USER_SYSCFG) : 0;
	int lang = id ? phys(id)[SYSCFG_LANG] : -1;
	const u8* cid = &flash_b[FLASH_CONSOLE_ID];
	u8 sum = 0;
	for (int i = 0; i < 6; i++)
		sum += cid[i];
	bool sum_ok = cid[-1] == sum && cid[-2] == (u8)~sum;
	printf("[flash] region=%c language=%c broadcast=%c (flash file: %c %c %c) syscfg language=%d (flash file: %d) "
	       "console id=%02X%02X%02X%02X%02X%02X checksum=%s version=%02X user blocks=%d/%d\n",
	       now[0], now[1], now[2], own[0], own[1], own[2], lang, s_lang_forced ? (int)s_lang_orig : lang,
	       cid[0], cid[1], cid[2], cid[3], cid[4], cid[5], sum_ok ? "ok" : "bad", cid[7],
	       header_valid() ? used_blocks() : 0, num_user_blocks());
}
