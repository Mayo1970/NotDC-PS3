// Plan section 7, bundle 15, PC check: the real dc/flash_fix.cpp against its text of commit 977df14 (rows OFF) and
// against Flycast's flash code, written here in Flycast's own form (rows ON); then the three cut sections.
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include "types.h"
#include "dc/mem/sh4_mem.h"

Array<u8> flash_b;
int g_broadcast_preset, g_bios_lang_preset, g_flash_defaults_preset, g_console_id_preset, g_exit_save_preset;
int g_rtc_local_preset, g_modem_preset;

static char s_log[16384];
static u32 s_log_len;
int check_printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(s_log + s_log_len, sizeof(s_log) - s_log_len, fmt, ap);
	va_end(ap);
	if (n > 0)
		s_log_len = s_log_len + n < sizeof(s_log) ? s_log_len + n : sizeof(s_log) - 1;
	return n;
}
static void log_clear() { s_log_len = 0; s_log[0] = 0; }
static bool log_has(const char* t) { return strstr(s_log, t) != 0; }

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 12) { fprintf(stdout, "FAIL: "); fprintf(stdout, __VA_ARGS__); \
	fprintf(stdout, "\n"); } } } while (0)

// ---- the code under test: the new file, then the old one in its own namespace ----
#include "dc/flash_fix.cpp"
namespace old_code {
#include "ref_flash_fix.inc"
}

#define IMG 0x20000u
#define USER 0x1c000u

// ---- Flycast core/hw/flashrom/flashrom.h and flashrom.cpp, for a little-endian host as it is ----
namespace fly {
enum { PT_FACTORY, PT_RESERVED, PT_USER, PT_GAME, PT_UNKNOWN };
enum { BLOCK = 0x40, BITMAP_BLOCKS = BLOCK * 8, BITMAP_BYTES = BITMAP_BLOCKS * 64, USER_SYSCFG = 5 };
struct syscfg_block { u16 block_id; u16 time_lo; u16 time_hi; int8_t time_zone; u8 lang; u8 mono; u8 autostart;
	u8 unknown2[4]; u8 reserved[50]; };
struct header_block { char magic[16]; u8 part_id; u8 version; u8 reserved[46]; };
struct user_block { u16 block_id; u8 data[60]; u16 crc; };
static_assert(sizeof(syscfg_block) == 64 && sizeof(header_block) == 64 && sizeof(user_block) == 64, "block size");

struct Chip
{
	u8* data;

	void GetPartitionInfo(int part_id, int* offset, int* size)
	{
		switch (part_id)
		{
		case PT_FACTORY:  *offset = 0x1a000; *size = 8 * 1024; break;
		case PT_RESERVED: *offset = 0x18000; *size = 8 * 1024; break;
		case PT_USER:     *offset = 0x1c000; *size = 16 * 1024; break;
		case PT_GAME:     *offset = 0x10000; *size = 32 * 1024; break;
		default:          *offset = 0x00000; *size = 64 * 1024; break;
		}
	}
	int crc_block(user_block* block)
	{
		const u8* buf = (const u8*)block;
		// Flycast has an int here; its bits above 15 never reach the result, and unsigned keeps the shifts defined.
		u32 n = 0xffff;
		for (int i = 0; i < 62; i++)
		{
			n ^= (buf[i] << 8);
			for (int c = 0; c < 8; c++)
				n = (n & 0x8000) ? (n << 1) ^ 4129 : (n << 1);
		}
		return (~n) & 0xffff;
	}
	int validate_crc(user_block* user) { return user->crc == crc_block(user); }
	int num_physical_blocks(u32 size) { return size / BLOCK; }
	int num_bitmap_blocks(u32 size) { return (int)std::ceil(size / (float)BITMAP_BYTES); }
	int num_user_blocks(u32 size) { return num_physical_blocks(size) - num_bitmap_blocks(size) - 1; }
	int is_allocated(const u8* bitmap, u32 phys_id)
	{
		int index = (phys_id - 1) % BITMAP_BLOCKS;
		return (bitmap[index / 8] & (0x80 >> (index % 8))) == 0x0;
	}
	void set_allocated(u8* bitmap, u32 phys_id)
	{
		int index = (phys_id - 1) % BITMAP_BLOCKS;
		bitmap[index / 8] &= ~(0x80 >> (index % 8));
	}
	void write_physical_block(u32 offset, u32 phys_id, const void* d) { memcpy(&data[offset + phys_id * BLOCK], d, BLOCK); }
	void read_physical_block(u32 offset, u32 phys_id, void* d) { memcpy(d, &data[offset + phys_id * BLOCK], BLOCK); }
	int validate_header(u32 offset, u32 part_id)
	{
		header_block header;
		read_physical_block(offset, 0, &header);
		if (memcmp(header.magic, "KATANA_FLASH____", sizeof(header.magic)) != 0)
			return 0;
		return header.part_id == part_id;
	}
	int validate_part(u32 part_id)
	{
		int offset, size;
		GetPartitionInfo(part_id, &offset, &size);
		return validate_header(offset, part_id);
	}
	int lookup_block(u32 offset, u32 size, u32 block_id)
	{
		u8 bitmap[BLOCK];
		int blocks = num_user_blocks(size);
		int bitmap_id = 1 + blocks;
		int phys_id = 1;
		int phys_end = bitmap_id;
		int result = 0;
		while (phys_id < phys_end)
		{
			if (phys_id % BITMAP_BLOCKS == 1)
				read_physical_block(offset, bitmap_id++, bitmap);
			if (!is_allocated(bitmap, phys_id))
				break;
			user_block user;
			read_physical_block(offset, phys_id, &user);
			if (user.block_id == block_id && validate_crc(&user))
				result = phys_id;
			phys_id++;
		}
		return result;
	}
	int alloc_block(u32 offset, u32 size)
	{
		u8 bitmap[BLOCK];
		int blocks = num_user_blocks(size);
		int bitmap_id = blocks;
		int phys_id = 1;
		int phys_end = 1 + blocks;
		while (phys_id < phys_end)
		{
			if (phys_id % BITMAP_BLOCKS == 1)
				read_physical_block(offset, ++bitmap_id, bitmap);
			if (!is_allocated(bitmap, phys_id))
				break;
			// if the current block has been rewritten, use it
			u16 id;
			memcpy(&id, &data[offset + phys_id * BLOCK], 2);
			if (lookup_block(offset, size, id) != phys_id)
				break;
			phys_id++;
		}
		if (phys_id >= phys_end)
			return 0;
		set_allocated(bitmap, phys_id);
		write_physical_block(offset, bitmap_id, bitmap);
		return phys_id;
	}
	int WriteBlock(u32 part_id, u32 block_id, const void* d)
	{
		int offset, size;
		GetPartitionInfo(part_id, &offset, &size);
		if (!validate_header(offset, part_id))
			return 0;
		int phys_id = lookup_block(offset, size, block_id);
		if (!phys_id)
		{
			phys_id = alloc_block(offset, size);
			if (!phys_id)
				return 0;
		}
		user_block user;
		memcpy(&user, d, sizeof(user));
		user.block_id = block_id;
		user.crc = crc_block(&user);
		write_physical_block(offset, phys_id, &user);
		return 1;
	}
	int ReadBlock(u32 part_id, u32 block_id, void* d)
	{
		int offset, size;
		GetPartitionInfo(part_id, &offset, &size);
		if (!validate_header(offset, part_id))
			return 0;
		int phys_id = lookup_block(offset, size, block_id);
		if (!phys_id)
			return 0;
		read_physical_block(offset, phys_id, d);
		return 1;
	}
	void erase_partition(u32 part_id)
	{
		int offset, size;
		GetPartitionInfo(part_id, &offset, &size);
		memset(data + offset, 0xFF, size);
	}
	void write_header(int part_id)
	{
		int offset, size;
		GetPartitionInfo(part_id, &offset, &size);
		header_block header;
		memset(&header, 0xff, sizeof(header));
		memcpy(header.magic, "KATANA_FLASH____", sizeof(header.magic));
		header.part_id = part_id;
		header.version = 0;
		write_physical_block(offset, 0, &header);
	}
	void Validate()
	{
		bool valid = true;
		char sysinfo[16];
		memcpy(sysinfo, data + 0x1a000, 16);
		valid = valid && memcmp(&sysinfo[5], "Dreamcast  ", 11) == 0;
		memcpy(sysinfo, data + 0x1a0a0, 16);
		valid = valid && memcmp(&sysinfo[5], "Dreamcast  ", 11) == 0;
		if (!valid)
		{
			memcpy(sysinfo, "00000Dreamcast  ", sizeof(sysinfo));
			erase_partition(PT_FACTORY);
			memcpy(data + 0x1a000, sysinfo, sizeof(sysinfo));
			memcpy(data + 0x1a0a0, sysinfo, sizeof(sysinfo));
		}
		erase_partition(PT_RESERVED);
		for (int part = PT_USER; part <= PT_UNKNOWN; part++)
			if (!validate_part(part))
			{
				erase_partition(part);
				write_header(part);
			}
	}
};

// nvmem.cpp fixUpDCFlash(). 6 and 4 are its "default" values, which write nothing; the ISP blocks are G11's.
struct Cfg
{
	bool validate;
	int region, broadcast;        // region 3 and broadcast 4: no write
	int lang_factory;             // 6: no write
	int lang_old, lang_new;       // the Language option for a syscfg block that exists / that is new; 6: no write
	bool console_id;
	const u8* blank_id;           // stands for rand()
};

static void fixUpDCFlash(u8* data, const Cfg& c, u32 now)
{
	Chip chip = { data };
	if (c.validate)
		chip.Validate();
	if (c.region <= 2)
	{
		data[0x1a002] = '0' + c.region;
		data[0x1a0a2] = '0' + c.region;
	}
	if (c.lang_factory <= 5)
	{
		data[0x1a003] = '0' + c.lang_factory;
		data[0x1a0a3] = '0' + c.lang_factory;
	}
	if (c.broadcast <= 3)
	{
		data[0x1a004] = '0' + c.broadcast;
		data[0x1a0a4] = '0' + c.broadcast;
	}
	syscfg_block syscfg;
	int res = chip.ReadBlock(PT_USER, USER_SYSCFG, &syscfg);
	if (!res)
	{
		memset(&syscfg, 0xff, sizeof(syscfg));
		syscfg.time_lo = 0;
		syscfg.time_hi = 0;
		syscfg.time_zone = 0;
		syscfg.lang = 0;
		syscfg.mono = 0;
		syscfg.autostart = 1;
	}
	syscfg.time_lo = now & 0xffff;
	syscfg.time_hi = now >> 16;
	const int language = res ? c.lang_old : c.lang_new;
	if (language <= 5)
		syscfg.lang = language;
	chip.WriteBlock(PT_USER, USER_SYSCFG, &syscfg);
	if (!c.console_id)
		return;
	u8* console_id = &data[0x1A058];
	if (!memcmp(console_id, "\377\377\377\377\377\377", 6))
	{
		u8 sum = 0;
		for (int i = 0; i < 6; i++)
		{
			console_id[i] = c.blank_id[i];
			console_id[i + 0xA0] = console_id[i];
			sum += console_id[i];
		}
		console_id[-1] = console_id[0xA0 - 1] = sum;
		console_id[-2] = console_id[0xA0 - 2] = ~sum;
	}
	else
	{
		u8 sum = 0;
		for (int i = 0; i < 6; i++)
			sum += console_id[i];
		console_id[-1] = console_id[0xA0 - 1] = sum;
		console_id[-2] = console_id[0xA0 - 2] = ~sum;
	}
	console_id[7] = console_id[0xA0 + 7] = 0xfe;
}

// G11: nvmem.cpp add_isp_to_nvmem with Flycast's default user name, less its branch that renames the user of an
// account that exists (flash_fix.cpp keeps such an account as it is).
enum { USER_INET = 0x80, USER_ISP1 = 0xC0, USER_ISP2 = 0xC6, USER_BBA = 0xE0 };
struct isp1_block { u16 block_id; u8 _unknown[4]; char sega[4]; char username[28]; char password[16]; char phone[8]; u16 crc; };
struct isp2_block { u16 block_id; char sega[4]; char username[28]; char password[16]; char phone[12]; u16 crc; };
#pragma pack(push, 1)
struct bba_block { u16 block_id; char sega[4]; u32 unk1; u32 ipAddress; u32 netmask; u32 broadcast; u32 dns1; u32 dns2;
	u32 gateway; char unk2[28]; u16 crc; };
#pragma pack(pop)
static_assert(sizeof(isp1_block) == 64 && sizeof(isp2_block) == 64 && sizeof(bba_block) == 64, "block size");

static void add_isp_to_nvmem(u8* data)
{
	Chip chip = { data };
	Chip* flash = &chip;
	const char* username = "flycast1";
	u8 block[64];
	if (!flash->ReadBlock(PT_USER, USER_INET, block))
	{
		memset(block, 0, sizeof(block));
		strcpy((char *)block + 2, "PWBrowser");
		block[12] = 0x1c;
		flash->WriteBlock(PT_USER, USER_INET, block);

		memset(block, 0, sizeof(block));
		flash->WriteBlock(PT_USER, USER_INET + 1, block);
		strcpy((char *)block + 32, "AT&F");
		flash->WriteBlock(PT_USER, USER_INET + 2, block);
		memset(block, 0, sizeof(block));
		flash->WriteBlock(PT_USER, USER_INET + 3, block);
		memset(block + 27, 0xFF, sizeof(block) - 27);
		block[10] = 1;
		block[14] = 1;
		block[16] = 1;
		block[19] = 6;
		block[26] = 5;
		flash->WriteBlock(PT_USER, USER_INET + 4, block);
		memset(block, 0xFF, sizeof(block));
		for (u32 i = USER_INET + 5; i <= 0xbf; i++)
			flash->WriteBlock(PT_USER, i, block);

		isp1_block isp1{};
		isp1._unknown[3] = 1;
		memcpy(isp1.sega, "SEGA", 4);
		strncpy(isp1.username, username, sizeof(isp1.username) - 1);
		strcpy(isp1.password, "password");
		strcpy(isp1.phone, "1234567");
		flash->WriteBlock(PT_USER, USER_ISP1, &isp1);

		memset(block, 0, sizeof(block));
		flash->WriteBlock(PT_USER, USER_ISP1 + 1, block);
		flash->WriteBlock(PT_USER, USER_ISP1 + 2, block);
		strcpy((char *)block + 2, "flycast@gmail.com");
		flash->WriteBlock(PT_USER, USER_ISP1 + 3, block);
		memset(block, 0, sizeof(block));
		flash->WriteBlock(PT_USER, USER_ISP1 + 4, block);
		block[60] = 1;
		flash->WriteBlock(PT_USER, USER_ISP1 + 5, block);

		isp2_block isp2{};
		memcpy(isp2.sega, "SEGA", 4);
		strncpy(isp2.username, username, sizeof(isp2.username) - 1);
		strcpy(isp2.password, "password");
		strcpy(isp2.phone, "1234567");
		flash->WriteBlock(PT_USER, USER_ISP2, &isp2);
		memset(block, 0, sizeof(block));
		for (u32 i = USER_ISP2 + 1; i <= 0xEA; i++)
		{
			if (i == 0xcb)
				block[56] = 1;
			else
				block[56] = 0;
			flash->WriteBlock(PT_USER, i, block);
		}
	}
	bba_block bba{};
	if (!flash->ReadBlock(PT_USER, USER_BBA, &bba) || memcmp(bba.sega, "SEGA", 4) || bba.netmask == 0)
	{
		memcpy(bba.sega, "SEGA", 4);
		bba.netmask = 0xffffff00;
		flash->WriteBlock(PT_USER, USER_BBA, &bba);
	}
}
}

// ---- test flashes ----
static u32 s_rng = 0x2545F491;
static u32 rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static u32 rnd(u32 n) { return rnd() % n; }

static void put16(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

// An own CRC, so a fault in the checked crc_block shows.
static u32 own_crc(const u8* b)
{
	u32 n = 0xffff;
	for (int i = 0; i < 62; i++)
		for (int c = 7; c >= 0; c--)
		{
			const u32 top = ((n >> 15) ^ (b[i] >> c)) & 1;
			n = ((n << 1) & 0xffff) ^ (top ? 0x1021 : 0);
		}
	return (~n) & 0xffff;
}

static void make_block(u8* img, int phys_id, u32 block_id, bool good_crc)
{
	u8* b = img + USER + phys_id * 64;
	for (int i = 2; i < 62; i++)
		b[i] = (u8)rnd();
	put16(b, block_id);
	put16(b + 62, own_crc(b) ^ (good_crc ? 0 : 0x5a5a));
	u8* bitmap = img + USER + 255 * 64;
	bitmap[(phys_id - 1) / 8] &= ~(0x80 >> ((phys_id - 1) % 8));
}

static void make_header(u8* img, u32 offset, u8 part)
{
	memcpy(img + offset, "KATANA_FLASH____", 16);
	img[offset + 16] = part;
	img[offset + 17] = 0;
}

enum { K_ZERO, K_FF, K_GARBAGE, K_ANY, K_FULL, K_BADMAP, K_FLY_SYS, K_FLY_NEW, K_COUNT };

// K_FLY_SYS: a well-formed flash with a syscfg block. K_FLY_NEW: one with none, every block valid and its id single.
static void gen_flash(u8* img, int kind)
{
	if (kind == K_ZERO || kind == K_FF)
	{
		memset(img, kind == K_ZERO ? 0 : 0xFF, IMG);
		return;
	}
	if (kind == K_GARBAGE)
	{
		for (u32 i = 0; i < IMG; i++)
			img[i] = (u8)rnd();
		if (rnd(2))
			make_header(img, USER, 2);
		return;
	}
	memset(img, 0xFF, IMG);
	make_header(img, USER, 2);
	make_header(img, 0x10000, 3);
	make_header(img, 0x00000, 4);
	const bool fly = kind == K_FLY_SYS || kind == K_FLY_NEW;
	for (int copy = 0; copy < 2; copy++)
	{
		u8* f = img + (copy ? 0x1a0a0 : 0x1a000);
		memcpy(f, "00000Dreamcast  ", 16);
		f[2] = (u8)('0' + rnd(3));
		f[3] = (u8)('0' + rnd(6));
		f[4] = (u8)('0' + rnd(4));
		// Most consoles hold two equal copies.
		if (copy && (fly || rnd(4)))
			memcpy(f + 2, img + 0x1a002, 3);
		u8* id = f + 0x58;
		const u32 mode = rnd(3);
		if (mode)
		{
			u8 sum = 0;
			for (int i = 0; i < 6; i++)
				sum += id[i] = (u8)rnd();
			id[-1] = (u8)(sum + (mode == 2));
			id[-2] = (u8)~sum;
			id[7] = (u8)(0xFD + rnd(3));
		}
	}
	if (!fly)
		for (u32 i = 0x10040; i < 0x18000; i++)
			img[i] = (u8)rnd();

	int used = kind == K_FULL ? 254 : (int)rnd(rnd(4) ? 30 : 254);
	static const u32 ids[6] = { 0x05, 0x80, 0xC0, 0x07, 0xE0, 0x05 };
	const bool sys = kind == K_FLY_SYS || (kind != K_FLY_NEW && kind != K_FULL && rnd(3));
	if (sys && used == 0)
		used = 1;
	if (kind == K_FLY_NEW && used > 200)
		used = 200;
	const int sys_at = sys ? 1 + (int)rnd(used) : 0;
	for (int p = 1; p <= used; p++)
	{
		if (kind == K_FLY_NEW)
			make_block(img, p, 0x100 + p, true);
		else if (p == sys_at)
			make_block(img, p, 0x05, true);
		else if (kind == K_FULL)
			make_block(img, p, 0x80 + (p & 31), rnd(8) != 0);
		else
			make_block(img, p, ids[rnd(6)], rnd(5) != 0);
	}
	if (kind == K_BADMAP)
		for (int i = 0; i < 64; i++)
			img[USER + 255 * 64 + i] = (u8)rnd();
}

static const char* const k_areas[] = { "JUE     ", "J       ", "U       ", "E       ", "JE      ", "UE      ",
                                       "JU      ", "        ", "XYZ12345", "   E    ", "       J" };
#define AREAS (sizeof(k_areas) / sizeof(k_areas[0]))

static u8 s_img0[IMG], s_a1[IMG], s_a2[IMG], s_b1[IMG], s_b2[IMG], s_m[IMG];
static char s_log_old[16384];

static void set_rows(int broadcast, int lang, int defaults, int console)
{
	g_broadcast_preset = broadcast;
	g_bios_lang_preset = lang;
	g_flash_defaults_preset = defaults;
	g_console_id_preset = console;
}

// The new code's whole load sequence, as dc/dc.cpp runs it.
static void new_load(u32 rtc, const char* area)
{
	dc_flash_set_time(rtc);
	dc_flash_fixup(rtc);
	if (area)
		dc_flash_set_region(area);
	dc_flash_report();
}

// ---- 1. every row OFF: the new code is the old code ----
static void test_old(int cases)
{
	set_rows(0, 0, 0, 0);
	for (int n = 0; n < cases; n++)
	{
		gen_flash(s_img0, n % (K_FLY_SYS + 1));
		const u32 rtc = rnd();
		const char* area = rnd(5) ? k_areas[rnd(AREAS)] : 0;

		memcpy(flash_b.data, s_img0, IMG);
		log_clear();
		old_code::dc_flash_set_time(rtc);
		if (area)
			old_code::dc_flash_set_region(area);
		memcpy(s_a1, flash_b.data, IMG);
		strcpy(s_log_old, s_log);
		old_code::dc_flash_restore_region();
		memcpy(s_a2, flash_b.data, IMG);

		memcpy(flash_b.data, s_img0, IMG);
		log_clear();
		new_load(rtc, area);
		memcpy(s_b1, flash_b.data, IMG);
		// The new code's own line comes last.
		char* own = strstr(s_log, "[flash] region=");
		CHECK(own != 0, "rows OFF, case %d: no [flash] region= line", n);
		if (own)
			*own = 0;
		CHECK(strcmp(s_log, s_log_old) == 0, "rows OFF, case %d: the log lines differ:\n%s---\n%s", n, s_log_old, s_log);
		dc_flash_restore_region();
		memcpy(s_b2, flash_b.data, IMG);

		CHECK(memcmp(s_a1, s_b1, IMG) == 0, "rows OFF, case %d (kind %d): the loaded flash is not the old code's", n,
		      n % (K_FLY_SYS + 1));
		CHECK(memcmp(s_a2, s_b2, IMG) == 0, "rows OFF, case %d: the flash to save is not the old code's", n);
	}
	// No flash array: nothing may be read.
	u8* keep = flash_b.data;
	flash_b.data = 0;
	dc_flash_build();
	new_load(1, "JUE     ");
	dc_flash_restore_region();
	flash_b.data = keep;
}

// Flycast emulator.cpp loadSpecialSettings: the region to use, or 3 when its option stays as it is.
static int fly_region(int cfg, bool j, bool u, bool e)
{
	if (!j && !u && !e)
		return 3;
	switch (cfg)
	{
	case 0: return !j ? (u ? 1 : 2) : 3;
	case 1: return !u ? (e ? 2 : 0) : 3;
	case 2: return !e ? (u ? 1 : 0) : 3;
	default: return u ? 1 : e ? 2 : 0;
	}
}

// ---- 2. the rows against Flycast, then what reaches the file ----
static void test_rows(int cases)
{
	for (int n = 0; n < cases; n++)
	{
		const int kind = (n & 1) ? K_FLY_SYS : K_FLY_NEW;
		gen_flash(s_img0, kind);
		const u32 rtc = rnd();
		const char* area = rnd(5) ? k_areas[rnd(AREAS)] : 0;
		const int broadcast = rnd(4), lang = rnd(7), defaults = rnd(2), console = rnd(2);
		const bool j = area && memchr(area, 'J', 8), u = area && memchr(area, 'U', 8), e = area && memchr(area, 'E', 8);

		set_rows(broadcast, lang, defaults, console);
		memcpy(flash_b.data, s_img0, IMG);
		new_load(rtc, area);
		memcpy(s_b1, flash_b.data, IMG);
		dc_flash_restore_region();
		memcpy(s_b2, flash_b.data, IMG);

		fly::Cfg c;
		c.validate = false;
		c.region = fly_region(s_img0[0x1a002] - '0', j, u, e);
		c.broadcast = broadcast == 1 ? 0 : broadcast == 2 ? 1 : (broadcast == 3 && (j || u || e)) ? ((j || u) ? 0 : 1) : 4;
		c.lang_factory = lang ? lang - 1 : 6;
		c.lang_old = lang ? lang - 1 : 6;
		c.lang_new = lang ? lang - 1 : defaults ? 1 : 6;
		c.console_id = console != 0;
		c.blank_id = s_b1 + 0x1A058;
		memcpy(s_m, s_img0, IMG);
		fly::fixUpDCFlash(s_m, c, rtc);
		CHECK(memcmp(s_b1, s_m, IMG) == 0, "rows, case %d: Broadcast %d, BIOS Language %d, Flash Defaults %d, Console ID %d,"
		      " area '%s': not Flycast's flash", n, broadcast, lang, defaults, console, area ? area : "(no disc)");
		if (console && !memcmp(s_img0 + 0x1A058, "\377\377\377\377\377\377", 6))
			CHECK(memcmp(s_b1 + 0x1A058, "\377\377\377\377\377\377", 6) != 0, "rows, case %d: the new console ID is blank", n);

		// The file: as if Broadcast and BIOS Language were at CONSOLE and the disc switched no region.
		set_rows(0, 0, defaults, console);
		memcpy(flash_b.data, s_img0, IMG);
		new_load(rtc, 0);
		dc_flash_restore_region();
		CHECK(memcmp(s_b2, flash_b.data, IMG) == 0, "rows, case %d: Broadcast %d, BIOS Language %d, area '%s': a row's or the"
		      " disc's value reaches the saved flash", n, broadcast, lang, area ? area : "(no disc)");
	}
}

// ---- 3. no flash file ----
static void test_build()
{
	set_rows(0, 0, 1, 0);
	memset(flash_b.data, 0, IMG);
	log_clear();
	dc_flash_build();
	CHECK(log_has("[flash] no flash file: built a new flash (USA, English, NTSC)\n"), "build: no log line");
	memcpy(s_b1, flash_b.data, IMG);
	// Flycast: Validate() on a chip of zeros, then Region 1, Language 1, Broadcast 0 of core/cfg/option.cpp.
	memset(s_m, 0, IMG);
	fly::Chip chip = { s_m };
	chip.Validate();
	s_m[0x1a002] = s_m[0x1a0a2] = '1';
	s_m[0x1a003] = s_m[0x1a0a3] = '1';
	s_m[0x1a004] = s_m[0x1a0a4] = '0';
	CHECK(memcmp(s_b1, s_m, IMG) == 0, "build: not Flycast's new flash");
	CHECK(memcmp(s_b1 + 0x1a000, "00110Dreamcast  ", 16) == 0, "build: the factory text");

	// Its first boot: an English syscfg block in physical block 1.
	new_load(0x12345678, "E       ");
	fly::Cfg c = { false, 2, 4, 6, 6, 1, false, 0 };
	fly::fixUpDCFlash(s_m, c, 0x12345678);
	CHECK(memcmp(flash_b.data, s_m, IMG) == 0, "build: the first boot is not Flycast's");
	CHECK(flash_b.data[USER + 64 + 7] == 1, "build: the new syscfg block is not English");
	// Flash Defaults OLD: Japanese, as before.
	set_rows(0, 0, 0, 0);
	memset(flash_b.data, 0, IMG);
	dc_flash_build();
	new_load(1, 0);
	CHECK(flash_b.data[USER + 64 + 7] == 0, "OLD: a new syscfg block is not Japanese");
}

// ---- 4. Broadcast: the values, by hand ----
static void test_broadcast()
{
	static const struct { const char* area; int disc; } k[] = {
		{ "JUE     ", '0' }, { "J       ", '0' }, { "U       ", '0' }, { "E       ", '1' }, { "JE      ", '0' },
		{ "UE      ", '0' }, { "   E    ", '1' }, { "        ", 0 }, { "XYZ12345", 0 }, { 0, 0 },
	};
	for (int own = '0'; own <= '3'; own++)
		for (u32 a = 0; a < sizeof(k) / sizeof(k[0]); a++)
			for (int row = 0; row < 4; row++)
			{
				gen_flash(s_img0, K_FLY_SYS);
				s_img0[0x1a004] = s_img0[0x1a0a4] = (u8)own;
				memcpy(flash_b.data, s_img0, IMG);
				set_rows(row, 0, 0, 0);
				new_load(7, k[a].area);
				int want = row == 1 ? '0' : row == 2 ? '1' : (row == 3 && k[a].disc) ? k[a].disc : own;
				CHECK(flash_b.data[0x1a004] == want && flash_b.data[0x1a0a4] == want,
				      "Broadcast row %d, flash '%c', area '%s': %c, not %c", row, own, k[a].area ? k[a].area : "(no disc)",
				      flash_b.data[0x1a004], want);
				dc_flash_restore_region();
				CHECK(flash_b.data[0x1a004] == own && flash_b.data[0x1a0a4] == own, "Broadcast row %d: the flash's own byte"
				      " is not back before the save", row);
			}
}

// ---- 4b. Broadcast DISC: Flycast's PAL list by product ID, after the area rule as dc/dc.cpp calls it ----
static void test_disc_list()
{
	static const char* const k_ids[] = { "T46605D 80", "T46605D 81", "T46605D 8 ", "T-8107N   " };
	for (int own = '0'; own <= '1'; own++)
		for (u32 i = 0; i < sizeof(k_ids) / sizeof(k_ids[0]); i++)
			for (int row = 0; row < 4; row++)
			{
				gen_flash(s_img0, K_FLY_SYS);
				s_img0[0x1a004] = s_img0[0x1a0a4] = (u8)own;
				memcpy(flash_b.data, s_img0, IMG);
				set_rows(row, 0, 0, 0);
				log_clear();
				dc_flash_set_time(7);
				dc_flash_fixup(7);
				dc_flash_set_region("JUE     ");
				dc_flash_disc_broadcast(k_ids[i]);
				const bool listed = i == 0;
				const int want = row == 1 ? '0' : row == 2 ? '1' : row == 3 ? (listed ? '1' : '0') : own;
				CHECK(flash_b.data[0x1a004] == want && flash_b.data[0x1a0a4] == want,
				      "PAL list: Broadcast row %d, flash '%c', disc '%s': %c, not %c", row, own, k_ids[i],
				      flash_b.data[0x1a004], want);
				CHECK(log_has("[flash] broadcast PAL for disc T46605D 80 (Flycast list)\n") == (listed && row == 3),
				      "PAL list: Broadcast row %d, disc '%s': the log line", row, k_ids[i]);
				dc_flash_restore_region();
				CHECK(flash_b.data[0x1a004] == own && flash_b.data[0x1a0a4] == own, "PAL list: Broadcast row %d, disc '%s':"
				      " the flash's own byte is not back before the save", row, k_ids[i]);
			}
	// No flash array: nothing may be read.
	u8* keep = flash_b.data;
	flash_b.data = 0;
	set_rows(3, 0, 0, 0);
	dc_flash_disc_broadcast("T46605D 80");
	flash_b.data = keep;
}

// ---- 5. BIOS Language: the syscfg byte, and what the save keeps ----
static void test_language()
{
	for (int row = 0; row <= 6; row++)
	{
		gen_flash(s_img0, K_FLY_SYS);
		memcpy(flash_b.data, s_img0, IMG);
		set_rows(0, 0, 0, 0);
		new_load(9, 0);
		const int at = lookup_block(FLASH_USER_SYSCFG);
		const u8 own = phys(at)[7];
		const u8 own_f = flash_b.data[0x1a003];

		memcpy(flash_b.data, s_img0, IMG);
		set_rows(0, row, 0, 0);
		log_clear();
		new_load(9, 0);
		const u8 want = row ? (u8)(row - 1) : own;
		CHECK(phys(at)[7] == want, "BIOS Language row %d: syscfg language %d, not %d", row, phys(at)[7], want);
		CHECK(rd16(phys(at) + 62) == own_crc(phys(at)), "BIOS Language row %d: the block's CRC", row);
		CHECK(flash_b.data[0x1a003] == (row ? '0' + row - 1 : own_f) && flash_b.data[0x1a0a3] == flash_b.data[0x1a003],
		      "BIOS Language row %d: the factory byte", row);
		char text[64];
		snprintf(text, sizeof(text), "syscfg language=%d (flash file: %d) ", want, own);
		CHECK(log_has(text), "BIOS Language row %d: the report has no '%s':\n%s", row, text, s_log);
		if (!row)
			continue;

		// (a) The BIOS wrote the block again, into the next physical block: the save puts the own language there.
		memcpy(s_a1, flash_b.data, IMG);
		const int next = used_blocks() + 1;
		memcpy(phys(next), phys(at), 64);
		u8* bitmap = flash_b.data + USER + 255 * 64;
		bitmap[(next - 1) / 8] &= ~(0x80 >> ((next - 1) % 8));
		dc_flash_restore_region();
		CHECK(lookup_block(FLASH_USER_SYSCFG) == next, "language: the moved block is not found");
		CHECK(phys(next)[7] == own && rd16(phys(next) + 62) == own_crc(phys(next)), "BIOS Language row %d: the moved "
		      "block keeps the row's language in the file", row);
		CHECK(flash_b.data[0x1a003] == own_f && flash_b.data[0x1a0a3] == s_img0[0x1a0a3], "BIOS Language row %d: the "
		      "factory byte is not back", row);

		// (b) The user picked another language in the BIOS in this run: it stays.
		memcpy(flash_b.data, s_img0, IMG);
		new_load(9, 0);
		const u8 picked = (u8)((want + 1) % 6);
		phys(at)[7] = picked;
		put16(phys(at) + 62, own_crc(phys(at)));
		dc_flash_restore_region();
		CHECK(phys(at)[7] == picked, "BIOS Language row %d: the language the BIOS wrote is lost", row);

		// (c) A second save changes nothing more.
		memcpy(s_a2, flash_b.data, IMG);
		dc_flash_restore_region();
		CHECK(memcmp(s_a2, flash_b.data, IMG) == 0, "BIOS Language row %d: a second save changes the flash", row);

		// (d) After a save the row's language is the BIOS's own choice like any other.
		phys(at)[7] = want;
		put16(phys(at) + 62, own_crc(phys(at)));
		dc_flash_restore_region();
		CHECK(phys(at)[7] == want, "BIOS Language row %d: a save after the first one takes the language back again", row);
	}
}

// ---- 6. the region switch of before, and the report line ----
static void test_region_report()
{
	gen_flash(s_img0, K_FLY_NEW);
	memcpy(s_img0 + 0x1a000, "00101Dreamcast  ", 16);
	memcpy(s_img0 + 0x1a0a0, "00101Dreamcast  ", 16);
	memset(s_img0 + USER + 64, 0xFF, 0x4000 - 64);
	make_block(s_img0, 1, 0x80, true);
	make_block(s_img0, 2, 0x05, true);
	s_img0[USER + 2 * 64 + 7] = 3;
	put16(s_img0 + USER + 2 * 64 + 62, own_crc(s_img0 + USER + 2 * 64));
	make_block(s_img0, 3, 0x81, true);
	static const u8 id[6] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
	for (int copy = 0; copy < 2; copy++)
	{
		u8* p = s_img0 + 0x1A058 + copy * 0xA0;
		memcpy(p, id, 6);
		p[-1] = (u8)(0x12 + 0x34 + 0x56 + 0x78 + 0x9A + 0xBC);
		p[-2] = (u8)~p[-1];
		p[7] = 0xFF;
	}
	memcpy(flash_b.data, s_img0, IMG);
	set_rows(1, 2, 0, 0);
	log_clear();
	new_load(0x5BFC8900, "E       ");
	CHECK(log_has("[flash] region 1 -> 2 for disc area 'E       '\n"), "region: no switch line:\n%s", s_log);
	CHECK(flash_b.data[0x1a002] == '2' && flash_b.data[0x1a0a2] == '2', "region: no switch to Europe");
	CHECK(log_has("[flash] region=2 language=1 broadcast=0 (flash file: 1 0 1) syscfg language=1 (flash file: 3) "
	              "console id=123456789ABC checksum=ok version=FF user blocks=3/254\n"), "the report line:\n%s", s_log);
	dc_flash_restore_region();
	CHECK(memcmp(flash_b.data + 0x1a000, "00101Dreamcast  ", 16) == 0 && memcmp(flash_b.data + 0x1a0a0,
	      "00101Dreamcast  ", 16) == 0, "region: the flash's own bytes are not back");

	// A new load forgets the last load's patches: its save keeps the new flash's own bytes.
	set_rows(1, 2, 0, 0);
	new_load(1, "E       ");
	memcpy(s_a1, s_img0, IMG);
	memcpy(s_a1 + 0x1a000, "00222Dreamcast  ", 16);
	memcpy(s_a1 + 0x1a0a0, "00222Dreamcast  ", 16);
	s_a1[USER + 2 * 64 + 7] = 1;
	put16(s_a1 + USER + 2 * 64 + 62, own_crc(s_a1 + USER + 2 * 64));
	memcpy(flash_b.data, s_a1, IMG);
	set_rows(0, 0, 0, 0);
	new_load(1, 0);
	dc_flash_restore_region();
	CHECK(memcmp(flash_b.data + 0x1a000, "00222Dreamcast  ", 16) == 0 && memcmp(flash_b.data + 0x1a0a0,
	      "00222Dreamcast  ", 16) == 0 && flash_b.data[USER + 2 * 64 + 7] == 1, "a new load keeps the last load's patches");

	// A bad checksum (each of its two bytes) and a blank flash in the report.
	memcpy(flash_b.data, s_img0, IMG);
	flash_b.data[0x1A057]++;
	log_clear();
	new_load(1, 0);
	CHECK(log_has("checksum=bad version=FF"), "report: a bad sum byte reads ok:\n%s", s_log);
	flash_b.data[0x1A057]--;
	flash_b.data[0x1A056]++;
	log_clear();
	new_load(1, 0);
	CHECK(log_has("checksum=bad version=FF"), "report: a bad inverted sum byte reads ok:\n%s", s_log);
	memset(flash_b.data, 0xFF, IMG);
	log_clear();
	dc_flash_report();
	CHECK(log_has("[flash] region=? language=? broadcast=? (flash file: ? ? ?) syscfg language=-1 (flash file: -1) "
	              "console id=FFFFFFFFFFFF checksum=bad version=FF user blocks=0/254\n"), "report of a blank flash:\n%s", s_log);
}

// ---- 7. Console ID ----
static void test_console_id()
{
	set_rows(0, 0, 0, 1);
	u8 first[6];
	for (int n = 0; n < 4; n++)
	{
		gen_flash(s_img0, K_FLY_SYS);
		memset(s_img0 + 0x1A050, 0xFF, 16);
		memset(s_img0 + 0x1A0F0, 0xFF, 16);
		memcpy(flash_b.data, s_img0, IMG);
		const u32 seed = n == 0 ? 0 : n == 3 ? 1000 : 1000 + n;
		new_load(seed, 0);
		const u8* a = flash_b.data + 0x1A058;
		const u8* b = a + 0xA0;
		u8 sum = 0;
		for (int i = 0; i < 6; i++)
			sum += a[i];
		CHECK(memcmp(a, "\377\377\377\377\377\377", 6) != 0, "console ID: still blank, seed %u", seed);
		CHECK(memcmp(a, b, 6) == 0, "console ID: the second copy differs");
		CHECK(a[-1] == sum && a[-2] == (u8)~sum && b[-1] == sum && b[-2] == (u8)~sum, "console ID: the checksum");
		CHECK(a[7] == 0xFE && b[7] == 0xFE, "console ID: machine version %02X %02X", a[7], b[7]);
		if (n == 1)
			memcpy(first, a, 6);
		if (n == 2)
			CHECK(memcmp(first, a, 6) != 0, "console ID: two seeds give one ID");
		// Seed 1000 again (n == 3) is not seed 1001: the ID comes from the seed alone.
		if (n == 3)
			CHECK(memcmp(first, a, 6) != 0, "console ID: seed 1000 gives seed 1001's ID");
		// It is in the file: a save keeps it, and the next boot does not change it.
		dc_flash_restore_region();
		memcpy(s_a1, flash_b.data, IMG);
		new_load(seed, 0);
		dc_flash_restore_region();
		CHECK(memcmp(s_a1 + 0x1A000, flash_b.data + 0x1A000, 0x2000) == 0, "console ID: the next boot changes it");
	}
	// Five 0xFF bytes are not a blank ID.
	memcpy(flash_b.data, s_img0, IMG);
	memcpy(flash_b.data + 0x1A058, "\377\377\377\377\377\001", 6);
	new_load(77, 0);
	CHECK(memcmp(flash_b.data + 0x1A058, "\377\377\377\377\377\001", 6) == 0, "console ID: a known ID is replaced");
	// OLD: nothing in the factory partition changes.
	set_rows(0, 0, 0, 0);
	memcpy(flash_b.data, s_img0, IMG);
	new_load(5, 0);
	CHECK(memcmp(s_img0 + 0x1A000, flash_b.data + 0x1A000, 0x2000) == 0, "Console ID OLD writes the factory partition");
}

// ---- 8. dc/mem/sh4_mem.cpp, the save section ----
static int s_usleeps;
static int fake_usleep(unsigned) { s_usleeps++; return 0; }
#define usleep fake_usleep
static char s_saved_path[600];
static int s_saves;
static bool s_save_result = true;
static u8 s_saved_img[IMG];
static char* GetEmuPath(const char* sub)
{
	char* p = (char*)malloc(512);
	snprintf(p, 512, "/emu/%s", sub);
	return p;
}
static bool SaveSh4FlashromToFile(char* path)
{
	s_saves++;
	snprintf(s_saved_path, sizeof(s_saved_path), "%s", path);
	memcpy(s_saved_img, flash_b.data, IMG);
	return s_save_result;
}
#include "sec_save.inc"
#undef usleep

static void test_save()
{
	gen_flash(s_img0, K_FLY_SYS);
	s_img0[0x1a004] = s_img0[0x1a0a4] = '1';
	memcpy(flash_b.data, s_img0, IMG);
	set_rows(1, 0, 0, 0);
	new_load(3, 0);
	CHECK(flash_b.data[0x1a004] == '0', "save: the row did not act");

	// The forced save refuses an array that holds no flash.
	g_flash_loaded = false;
	log_clear();
	mem_SaveFlashForced();
	CHECK(s_saves == 0 && log_has("[flash] forced exit: no flash loaded, nothing saved\n"), "save: an unloaded flash is saved");
	g_flash_loaded = true;
	u8* keep = flash_b.data;
	flash_b.data = 0;
	mem_SaveFlashForced();
	flash_b.data = keep;
	CHECK(s_saves == 0, "save: a flash with no array is saved");

	// The plain save, twice: the lock is free again after each.
	mem_SaveFlash();
	CHECK(s_saves == 1 && strcmp(s_saved_path, "/emu/data/dc_flash_wb.bin") == 0, "save: %d save(s) to '%s'", s_saves,
	      s_saved_path);
	CHECK(s_saved_img[0x1a004] == '1' && s_saved_img[0x1a0a4] == '1', "save: the row's byte is in the file");
	mem_SaveFlash();
	CHECK(s_saves == 2 && s_usleeps == 0, "save: the second plain save did not run");

	// The forced save keeps the lock: a later plain save does not open the file again.
	memcpy(flash_b.data, s_img0, IMG);
	new_load(3, 0);
	log_clear();
	mem_SaveFlashForced();
	CHECK(s_saves == 3 && log_has("[flash] forced exit: flash saved\n"), "save: the forced save did not run:\n%s", s_log);
	CHECK(s_saved_img[0x1a004] == '1', "save: the forced save writes the row's byte");
	mem_SaveFlash();
	CHECK(s_saves == 3 && s_usleeps == 0, "save: a plain save ran or waited after the forced one");
	// A second forced save waits 20 x 100 ms for the lock, then gives up.
	s_usleeps = 0;
	log_clear();
	mem_SaveFlashForced();
	CHECK(s_saves == 3 && s_usleeps == 20 && log_has("[flash] forced exit: flash NOT saved\n"), "save: %d sleeps, %d saves"
	      " with the lock taken", s_usleeps, s_saves);
	// A write that fails is named.
	s_flash_saving = 0;
	s_save_result = false;
	log_clear();
	mem_SaveFlashForced();
	CHECK(s_saves == 4 && log_has("flash NOT saved"), "save: a failed write reads as saved");
	s_flash_saving = 0;
	s_save_result = true;
}

// ---- 9. ps3/ps3_main.cpp, the zone section, then dc/aica/aica_if.cpp, the RTC section ----
#define SYSUTIL_SYSTEMPARAM_ID_TIMEZONE   0x0116
#define SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME 0x0117
static s32 s_zone, s_zone_rc, s_summer, s_summer_rc;
static int s_zone_calls;
static s32 sysUtilGetSystemParamInt(s32 id, s32* value)
{
	s_zone_calls++;
	*value = id == SYSUTIL_SYSTEMPARAM_ID_TIMEZONE ? s_zone : s_summer;
	if (id != SYSUTIL_SYSTEMPARAM_ID_TIMEZONE && id != SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME)
		g_fail++;
	return id == SYSUTIL_SYSTEMPARAM_ID_TIMEZONE ? s_zone_rc : s_summer_rc;
}
#include "sec_zone.inc"

static int get_rtc_local_preset() { return g_rtc_local_preset; }
static time_t s_now;
static time_t fake_time(time_t*) { return s_now; }
#define time fake_time
#include "sec_rtc.inc"
#undef time

static int zone_of(s32 zone, s32 zrc, s32 summer, s32 src)
{
	s_zone = zone;
	s_zone_rc = zrc;
	s_summer = summer;
	s_summer_rc = src;
	return ps3_local_offset_seconds();
}

static time_t utc(int y, int mo, int d, int h)
{
	struct tm t = {};
	t.tm_year = y - 1900;
	t.tm_mon = mo - 1;
	t.tm_mday = d;
	t.tm_hour = h;
	return timegm(&t);
}

static void test_clock()
{
	// The XMB's values: minutes east of UTC, and the summer time's minutes.
	CHECK(zone_of(60, 0, 60, 0) == 7200, "zone: Rome in summer is %d s", zone_of(60, 0, 60, 0));
	CHECK(zone_of(60, 0, 0, 0) == 3600, "zone: Rome in winter");
	CHECK(zone_of(-300, 0, 60, 0) == -14400, "zone: New York in summer");
	CHECK(zone_of(0, 0, 0, 0) == 0, "zone: UTC");
	CHECK(zone_of(840, 0, 0, 0) == 50400 && zone_of(-720, 0, 0, 0) == -43200, "zone: the two ends");
	CHECK(zone_of(841, 0, 60, 0) == 3600 && zone_of(-721, 0, 0, 0) == 0, "zone: a zone out of range is not 0");
	CHECK(zone_of(60, (s32)0x80028804, 60, 0) == 3600, "zone: a refused zone is not 0");
	CHECK(zone_of(60, 0, 60, (s32)0x80028804) == 3600, "zone: a refused summer time is not 0");
	CHECK(zone_of(60, 0, 1, 0) == 7200, "zone: summer time 1 is not one hour");
	CHECK(zone_of(60, 0, 121, 0) == 3600 && zone_of(60, 0, -60, 0) == 3600, "zone: a summer time out of range is not 0");
	CHECK(zone_of(60, 0, 120, 0) == 10800 && zone_of(60, 0, 30, 0) == 5400, "zone: summer minutes");
	log_clear();
	zone_of(60, 0, 60, 0);
	CHECK(log_has("[rtc] PS3 time zone 60 min (rc 00000000), summer time 60 (rc 00000000)\n"), "zone: the log line:\n%s", s_log);

	const u32 dc1970 = 631152000u;
	const time_t summer = utc(2026, 7, 1, 12), winter = utc(2026, 1, 15, 12);
	// The PS3 as it is: UTC, no zone in the C library. 293 old boots have RTC = time() + 631152000.
	setenv("TZ", "UTC0", 1);
	tzset();
	g_rtc_local_preset = 0;
	s_now = summer;
	s_zone_calls = 0;
	zone_of(60, 0, 60, 0);
	CHECK(GetRTC_now() == dc1970 + (u32)summer, "RTC OFF on a UTC host: not UTC");
	CHECK(s_zone_calls == 4, "RTC: the [rtc] line is not printed once per boot with the row OFF");
	// RTC Local Time: UTC plus the XMB's offset, on any host zone.
	g_rtc_local_preset = 1;
	CHECK(GetRTC_now() == dc1970 + (u32)summer + 7200, "RTC ON: Rome in summer is %u s off",
	      GetRTC_now() - dc1970 - (u32)summer);
	s_now = winter;
	zone_of(60, 0, 0, 0);
	CHECK(GetRTC_now() == dc1970 + (u32)winter + 3600, "RTC ON: Rome in winter");
	zone_of(-480, 0, 0, 0);
	CHECK(GetRTC_now() == dc1970 + (u32)winter - 28800, "RTC ON: a zone west of UTC");
	setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
	tzset();
	zone_of(60, 0, 0, 0);
	CHECK(GetRTC_now() == dc1970 + (u32)winter + 3600, "RTC ON: the host's own zone is added too");
	// P12, the old path on a host that has a zone: local time, one hour more under summer time, not one day.
	g_rtc_local_preset = 0;
	zone_of(0, 0, 0, 0);
	CHECK(GetRTC_now() == dc1970 + (u32)winter + 3600, "RTC OFF, CET host, winter: %d s off",
	      (int)(GetRTC_now() - dc1970 - (u32)winter));
	s_now = summer;
	CHECK(GetRTC_now() == dc1970 + (u32)summer + 7200, "RTC OFF, CET host, summer: %d s off, not 7200",
	      (int)(GetRTC_now() - dc1970 - (u32)summer));
}

// ---- 10. G11, row NETWORK > Modem DCNET: the dial-up account against Flycast's add_isp_to_nvmem ----
// As Matt's flash: the BIOS takes a new block per settings save, so most used blocks are old copies of block 5.
static void gen_copies(u8* img, int copies, bool account)
{
	gen_flash(img, K_FLY_SYS);
	memset(img + USER + 64, 0xFF, 0x4000 - 64);
	for (int p = 1; p <= copies; p++)
		make_block(img, p, (account && p == copies / 2) ? 0x80 : 0x05, true);
	// Half the accounts have a BBA block with "SEGA" and netmask 0, which Flycast writes again.
	if (account && rnd(2))
	{
		u8* b = img + USER + (copies / 3) * 64;
		make_block(img, copies / 3, 0xE0, true);
		memcpy(b + 2, "SEGA", 4);
		memset(b + 14, 0, 4);
		put16(b + 62, own_crc(b));
	}
}

// Full, and no block has a newer copy: nothing can be reused.
static void gen_full_single(u8* img)
{
	gen_flash(img, K_FLY_SYS);
	memset(img + USER + 64, 0xFF, 0x4000 - 64);
	make_block(img, 1, 0x05, true);
	for (int p = 2; p <= 254; p++)
		make_block(img, p, 0x100 + p, true);
}

static void test_isp(int cases)
{
	set_rows(0, 0, 0, 0);
	const fly::Cfg c = { false, 3, 4, 6, 6, 6, false, 0 };
	int written = 0, kept = 0, full = 0;
	for (int n = 0; n < cases; n++)
	{
		const int kind = n % 5;
		const bool account = kind == 0 && rnd(4) == 0;
		if (kind == 0)
			gen_copies(s_img0, 150 + (int)rnd(104), account);
		else if (kind == 1)
			gen_flash(s_img0, K_FLY_SYS);
		else if (kind == 2)
			gen_flash(s_img0, K_FLY_NEW);
		else if (kind == 3)
			gen_copies(s_img0, 254, false);
		else
			gen_full_single(s_img0);
		const u32 rtc = rnd();
		g_modem_preset = 1;
		memcpy(flash_b.data, s_img0, IMG);
		log_clear();
		new_load(rtc, 0);
		memcpy(s_m, s_img0, IMG);
		fly::fixUpDCFlash(s_m, c, rtc);
		fly::add_isp_to_nvmem(s_m);
		CHECK(memcmp(flash_b.data, s_m, IMG) == 0, "dial-up, case %d (kind %d): not Flycast's flash", n, kind);
		written += log_has("[flash] dial-up account written, BBA block written: ISP1 user 'flycast1', user blocks ");
		kept += log_has("[flash] dial-up account kept, ");
		full += log_has("[flash] dial-up account NO ROOM, BBA block NO ROOM: ISP1 user '', user blocks 254/254\n");
		if (kind == 0 || kind == 3)
			CHECK(log_has(account ? "[flash] dial-up account kept, BBA block written: ISP1 user '', user blocks " :
			              "[flash] dial-up account written, BBA block written: ISP1 user 'flycast1', user blocks "),
			      "dial-up, case %d (kind %d): the log line:\n%s", n, kind, s_log);
		if (kind == 4)
			CHECK(log_has("dial-up account NO ROOM, BBA block NO ROOM"), "dial-up, full flash: the log line:\n%s", s_log);
		// The next boot finds the account: as Flycast's second boot, and the log says so.
		log_clear();
		new_load(rtc + 1, 0);
		fly::fixUpDCFlash(s_m, c, rtc + 1);
		fly::add_isp_to_nvmem(s_m);
		CHECK(memcmp(flash_b.data, s_m, IMG) == 0, "dial-up, case %d (kind %d): the second boot is not Flycast's", n, kind);
		// A flash too full for the BBA block stays so; no block is written twice.
		if (kind != 4)
			CHECK(log_has("[flash] dial-up account kept, BBA block ") && !log_has("BBA block written"),
			      "dial-up, case %d: second boot:\n%s", n, s_log);
		// Row OFF: no account, as Flycast's flash without add_isp_to_nvmem.
		g_modem_preset = 0;
		memcpy(flash_b.data, s_img0, IMG);
		log_clear();
		new_load(rtc, 0);
		memcpy(s_m, s_img0, IMG);
		fly::fixUpDCFlash(s_m, c, rtc);
		CHECK(memcmp(flash_b.data, s_m, IMG) == 0 && !log_has("dial-up"), "dial-up, case %d: row OFF writes", n);
	}
	CHECK(written > cases / 10 && kept > cases / 20 && full >= cases / 5, "dial-up: %d written, %d kept, %d full",
	      written, kept, full);
}

int main(int argc, char** argv)
{
	const int cases = argc > 1 ? atoi(argv[1]) : 400;
	flash_b.data = (u8*)malloc(IMG);
	flash_b.Size = IMG;
	test_old(cases);
	test_rows(cases);
	test_build();
	test_broadcast();
	test_disc_list();
	test_language();
	test_region_report();
	test_console_id();
	test_isp(cases);
	test_save();
	test_clock();
	if (g_fail)
	{
		fprintf(stdout, "FAIL: %d check(s)\n", g_fail);
		return 1;
	}
	fprintf(stdout, "PASS: %d + %d + %d flashes, rows OFF = the old code, rows ON = Flycast, dial-up account = Flycast; "
	        "build, save, zone, RTC\n", cases, cases, cases);
	return 0;
}
