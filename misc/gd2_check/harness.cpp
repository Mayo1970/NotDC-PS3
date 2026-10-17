// Bundles 2 and 19 PC check (WSL): gdromv3.cpp, fake drive, no worker. trace <seed> <ops>: with every option at 0 the
// replies hash as the frozen ref_gdromv3.inc (GD_REF). model <group>: one option ON against the rule it ports.
#include <stdarg.h>
#include "types.h"
#include "ps3_timebase.h"
#include "plugins/plugin_manager.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/asic/asic.h"
#include "dc/sh4/sh4_sched.h"
#include "gdromv3.h"
#include "ps3_gdasync.h"
#include "dc/sh4/rec_v2/jit_dma.h"

uint64_t ps3_timebase_hz = 1000000000ull;

// ---- the options ----
static int o_status, o_position, o_scd_q, o_rules, o_speed, o_dma_fix, o_miss, o_cmd_fix, o_set_mode, o_swap, o_holly,
           o_timing;
extern "C" {
int g_jit_ccalls_preset = 0;
int get_debug_loop() { return 0; }
int get_debug_gdrom() { return 0; }
int get_sched_preset() { return 0; }
int get_gd_status_preset() { return o_status; }
int get_gd_position_preset() { return o_position; }
int get_gd_scd_q_preset() { return o_scd_q; }
int get_cdda_rules_preset() { return o_rules; }
int get_gd_speed_preset() { return o_speed; }
int get_gd_timing_preset() { return o_timing; }
int get_gd_dma_fix_preset() { return o_dma_fix; }
int get_cdda_miss_preset() { return o_miss; }
int get_gd_cmd_fix_preset() { return o_cmd_fix; }
int get_gd_set_mode_preset() { return o_set_mode; }
int get_disc_swap_preset() { return o_swap; }
int get_holly_regs_preset() { return o_holly; }   // bundle 20's ATA soft reset values: model p68
}
bool g_stall_on;
void stall_push_on(int) {}
void stall_pop_on() {}

// ---- errors ----
static int g_errors;
static void fail(const char* what, u64 got, u64 want)
{
	if (g_errors++ < 30)
		printf("ERROR: %s (got %llu / 0x%llX, want %llu / 0x%llX)\n", what, (unsigned long long)got,
		       (unsigned long long)got, (unsigned long long)want, (unsigned long long)want);
}
static void eq(const char* what, u64 got, u64 want)
{
	if (got != want)
		fail(what, got, want);
}
// A verify or die of the drive: on the PS3 it is an endless loop.
void check_fail(const char* what, const char* file, int line)
{
	printf("ERROR: %s at %s:%d (a hang on the PS3)\n", what, file, line);
	exit(1);
}
int msgboxf(const char* text, unsigned int, ...)
{
	printf("ERROR: msgboxf %s\n", text);
	g_errors++;
	return 0;
}

// ---- the disc worker: off ----
static u32 g_seeks, g_seek_fad, g_loop_start = GDA_NO_LOOP, g_loop_end;
void gda_start() {}
void gda_stop() {}
void gda_lock() {}
void gda_unlock() {}
bool gda_async() { return false; }
void gda_fill(uint8_t*, uint32_t, uint32_t, uint32_t) { check_fail("gda_fill with the worker off", __FILE__, __LINE__); }
bool gda_busy() { return false; }
void gda_wait() {}
bool gda_cdda(uint8_t*, uint32_t) { return false; }
void gda_cdda_next(bool, uint32_t) {}
bool gda_cdda_on() { return false; }
void gda_cdda_seek(uint32_t fad) { g_seeks++; g_seek_fad = fad; }
void gda_cdda_loop(uint32_t start, uint32_t end) { g_loop_start = start; g_loop_end = end; }
void gda_cdda_reset() {}
void gda_count(int, uint32_t, uint64_t) {}
void gda_stats(double) {}

// ---- the drive: a GD-ROM with tracks 1 (data), 2 (audio), 3 (data), 4 and 5 (audio) ----
static u32 g_disc_type = GdRom, g_disc;
static const struct { u32 fad, ctrl; } k_trk[5] = { {150, 4}, {600, 0}, {45150, 4}, {300000, 0}, {400000, 0} };
#define SD_LEAD 1126u
#define HD_LEAD 549300u
static u32 g_lead = HD_LEAD;   // P82: where the disc ends
// Every byte depends on the whole FAD, the sector size and the disc; bytes 4-7 hold the FAD + 1 for pull().
static inline u8 pat(u32 fad, u32 secsz, u32 j)
{
	if (j >= 4 && j < 8)
		return (u8)((fad + 1) >> ((j - 4) * 8));
	const u32 x = (fad + g_disc * 7919u) * 2654435761u + j * 40503u + secsz * 97u;
	return (u8)(x ^ (x >> 13) ^ (x >> 24));
}

void FASTCALL libGDR_ReadSector(u8* buff, u32 fad, u32 n, u32 secsz)
{
	for (u32 k = 0; k < n; k++)
		for (u32 j = 0; j < secsz; j++)
			buff[k * secsz + j] = pat(fad + k, secsz, j);
}
void FASTCALL libGDR_GetToc(u32* toc, u32 area)
{
	for (u32 i = 0; i < 102; i++)
		toc[i] = 0x01020304u * (i + 1) + area * 0x55u;
}
u32 FASTCALL libGDR_GetDiscType() { return g_disc_type; }
void FASTCALL libGDR_GetSessionInfo(u8* out, u8 ses)
{
	const u32 fad = ses == 0 ? HD_LEAD : ses == 1 ? 150 : 45150;
	out[0] = 2;
	out[1] = 0;
	out[2] = ses == 0 ? 2 : ses == 1 ? 1 : 3;
	out[3] = (u8)(fad >> 16);
	out[4] = (u8)(fad >> 8);
	out[5] = (u8)fad;
}
u32 FASTCALL libGDR_GetTrackNumber(u32 fad, u32* elapsed)
{
	u32 t = 0xAA;
	*elapsed = 0;
	for (u32 i = 0; i < 5; i++)
		if (k_trk[i].fad <= fad && fad < (i < 2 ? SD_LEAD : HD_LEAD))
			t = i + 1;
	if (t != 0xAA)
		*elapsed = fad - k_trk[t - 1].fad;
	return t;
}
void FASTCALL libGDR_GetTrackAdrAndControl(u32 track, u8* adr, u8* ctrl)
{
	*adr = *ctrl = 0;
	if (track < 1 || track > 5)
		return;
	*ctrl = (u8)k_trk[track - 1].ctrl;
	*adr = *ctrl ? 0 : 1;
}
u32 FASTCALL libGDR_GetLeadOut() { return g_lead; }
bool libGDR_SwapDisc(const char*)
{
	g_disc++;
	return true;
}

// gdrom_response.cpp: the real sizes are 0x50 and 32 bytes; IDENTIFY_DEV and REQ_MODE of the old path read past them.
u8 reply_a1[1024];
u8 gd_data_0x11[1024];
u8 gd_data_0x71[16];
u32 gd_data_0x71_len = 16;
char szExDT[8][32];

// ---- the bus ----
RegisterStruct sb_regs[0x800];
u32 SB_GDSTAR, SB_GDLEN, SB_GDDIR, SB_GDEN, SB_GDST, SB_GDSTARD, SB_GDLEND;
DMAC_DMAOR_type DMAC_DMAOR = { 0x8201 };
static u8 g_ram[1 << 24];
static u32 g_irq_dma, g_irq_cmd;

// The JIT's DMA hook (dc/sh4/rec_v2/driver.cpp): each report names exactly the words written since the last one.
static u32 g_dw_lo = ~0u, g_dw_hi, g_dw_n;
void WriteMem32_nommu(u32 addr, u32 data)
{
	if (addr & 3)
		check_fail("unaligned DMA address", __FILE__, __LINE__);
	memcpy(&g_ram[addr & 0xFFFFFC], &data, 4);
	g_dw_lo = addr < g_dw_lo ? addr : g_dw_lo;
	g_dw_hi = addr > g_dw_hi ? addr : g_dw_hi;
	g_dw_n++;
}
void rdv_DmaWrite(u32 addr, u32 size, JitDma who)
{
	if (g_dw_n ? g_dw_lo != addr || g_dw_hi + 4 - addr != size || g_dw_n * 4 != size : size != 0)
		fail("a DMA report is not the words written since the last one", size, g_dw_n * 4);
	if (who != JDMA_GDROM)
		fail("a GD-ROM DMA report names another DMA", who, JDMA_GDROM);
	g_dw_lo = ~0u;
	g_dw_hi = g_dw_n = 0;
}
void asic_RaiseInterrupt(HollyInterruptID id)
{
	if (id == holly_GDROM_DMA)
		g_irq_dma++;
	else if (id == holly_GDROM_CMD)
		g_irq_cmd++;
}
void asic_CancelInterrupt(HollyInterruptID) {}
int sh4_sched_register(int, sh4_sched_callback*) { return 0; }
void sh4_sched_request(int, int) { check_fail("sched used", __FILE__, __LINE__); }
void NotifyEvent_gdrom(u32 info, void* param);

// ---- the guest ----
static u64 g_seed = 1;
static u32 rnd()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 7;
	g_seed ^= g_seed << 17;
	return (u32)g_seed;
}

static u64 g_hash = 1469598103934665603ull;
static void h(u64 v)
{
	for (int i = 0; i < 8; i++)
	{
		g_hash = (g_hash ^ (v & 0xFF)) * 1099511628211ull;
		v >>= 8;
	}
}

#define SLOW 14336   // cycles per SlowUpdate at Accuracy FAST
static u64 g_cycles;
static void tick()
{
	UpdateGDRom();
	gdrom_spinup_tick(SLOW);
	g_cycles += SLOW;
}
static void tick_ms(double ms)
{
	for (s64 c = (s64)(ms * 200000.0); c > 0; c -= SLOW)
		tick();
}

static u8 g_pkt[12];
static void ata(u32 cmd) { WriteMem_gdrom(GD_COMMAND_Write, cmd, 1); }
static void spi(const u8 p[12])
{
	memcpy(g_pkt, p, 12);
	ata(ATA_SPI_PACKET);
	for (int i = 0; i < 6; i++)
		WriteMem_gdrom(GD_DATA, p[2 * i] | (p[2 * i + 1] << 8), 2);
}
static u32 alt() { return ReadMem_gdrom(GD_ALTSTAT_Read, 1); }
static u32 drive_status() { return ReadMem_gdrom(GD_SECTNUM, 1) & 0xF; }
static u32 bytecount() { return ReadMem_gdrom(GD_BYCTLLO, 1) | (ReadMem_gdrom(GD_BYCTLHI, 1) << 8); }

// The bytes a data-in command offers, read to their end; the count is the Byte Count register's.
static u32 pio_read(u8* out, u32 max)
{
	u32 n = 0;
	while (alt() & 8)
	{
		const u32 w = ReadMem_gdrom(GD_DATA, 2);
		if (n + 1 < max)
		{
			out[n] = (u8)w;
			out[n + 1] = (u8)(w >> 8);
		}
		n += 2;
		if (n > 0x200000)
			check_fail("PIO never ends", __FILE__, __LINE__);
	}
	return n;
}
static u32 spi_in(const u8 p[12], u8* out, u32 max)
{
	spi(p);
	return pio_read(out, max);
}

static void regs_hash()
{
	h(alt());
	h(ReadMem_gdrom(GD_SECTNUM, 1));
	h(ReadMem_gdrom(GD_ERROR_Read, 1));
	h(ReadMem_gdrom(GD_IREASON_Read, 1));
	h(bytecount());
	h(ReadMem_gdrom(GD_DRVSEL, 1));
	h(g_irq_cmd);
	h(g_irq_dma);
	h(SB_GDST);
	h(SB_GDLEND);
	h(SB_GDSTARD);
}

static void fad3(u8* p, u32 fad)
{
	p[0] = (u8)(fad >> 16);
	p[1] = (u8)(fad >> 8);
	p[2] = (u8)fad;
}
static void msf3(u8* p, u32 fad)
{
	p[0] = (u8)(fad / 4500);
	p[1] = (u8)(fad / 75 % 60);
	p[2] = (u8)(fad % 75);
}

static void cmd_play(u32 type, u32 start, u32 end, u32 rep)
{
	u8 p[12] = {0};
	p[0] = SPI_CD_PLAY;
	p[1] = (u8)type;
	if (type == 2)
	{
		msf3(p + 2, start);
		msf3(p + 8, end);
	}
	else if (type == 1)
	{
		fad3(p + 2, start);
		fad3(p + 8, end);
	}
	p[6] = (u8)rep;
	spi(p);
}
static void cmd_seek(u32 type, u32 fad)
{
	u8 p[12] = {0};
	p[0] = SPI_CD_SEEK;
	p[1] = (u8)type;
	if (type == 2)
		msf3(p + 2, fad);
	else if (type == 1)
		fad3(p + 2, fad);
	spi(p);
}
static void cmd_read(u32 op, u32 fad, u32 count, u32 secsz, bool dma)
{
	WriteMem_gdrom(GD_FEATURES_Write, dma ? 1 : 0, 1);
	u8 p[12] = {0};
	p[0] = (u8)op;
	p[1] = secsz == 2340 ? 0xE6 : secsz == 2352 ? 0x22 : 0x20;
	fad3(p + 2, fad);
	if (op == SPI_CD_READ2)
	{
		p[6] = (u8)(count >> 8);
		p[7] = (u8)count;
	}
	else
		fad3(p + 8, count);
	spi(p);
}
static void dma_start(u32 dst, u32 len)
{
	SB_GDSTAR = dst;
	SB_GDLEN = len;
	SB_GDDIR = 1;
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(1);
	sb_regs[(SB_GDST_addr - SB_BASE) >> 2].writeFunction(1);
}
static void dma_run(u32 dst, u32 len)
{
	dma_start(dst, len);
	for (u32 n = 0; (SB_GDST & 1); n++)
	{
		tick();
		if (n > 4000000)
			check_fail("DMA never ends", __FILE__, __LINE__);
	}
}
// 0: silence. Else the FAD + 1 of the sector, or ~0 for bytes that are no sector of this disc.
static u32 pull()
{
	s16 sec[1176];
	gdrom_get_cdda(sec);
	const u8* b = (const u8*)sec;
	bool silent = true;
	for (u32 j = 0; j < 2352 && silent; j++)
		silent = b[j] == 0;
	if (silent)
		return 0;
	const u32 f = (b[4] | (b[5] << 8) | (b[6] << 16) | ((u32)b[7] << 24)) - 1;
	for (u32 j = 0; j < 2352; j++)
		if (b[j] != pat(f, 2352, j))
			return ~0u;
	return f + 1;
}

static u32 ram_hash(u32 at, u32 len)
{
	u32 x = 2166136261u;
	for (u32 i = 0; i < len; i++)
		x = (x ^ g_ram[(at + i) & 0xFFFFFF]) * 16777619u;
	return x;
}
static bool ram_is(u32 at, u32 fad, u32 count, u32 secsz)
{
	for (u32 i = 0; i < count * secsz; i++)
		if (g_ram[(at + i) & 0xFFFFFF] != pat(fad + i / secsz, secsz, i % secsz))
			return false;
	return true;
}

// ---- trace: only work the old file takes without a verify or die ----
static void trace(u32 ops)
{
	for (u32 i = 0; i < sizeof(reply_a1); i++)
	{
		reply_a1[i] = (u8)(i * 7 + 1);
		gd_data_0x11[i] = (u8)(i * 13 + 5);
	}
	for (u32 i = 0; i < 16; i++)
		gd_data_0x71[i] = (u8)(0xF0 + i);
	gdrom_reg_Init();
	NotifyEvent_gdrom(DiskChange, 0);
	gdrom_reg_Reset(false);
	static u8 buf[150000];
	static const u32 k_types[] = { GdRom, GdRom, GdRom, CdRom_XA, CdRom, NoDisk, Open, Busy };
	for (u32 n = 0; n < ops; n++)
	{
		const u32 r = rnd() % 100;
		u8 p[12] = {0};
		u32 got = 0;
		if (r < 4)
			ata(ATA_NOP);
		else if (r < 6)
			ata(ATA_SOFT_RESET);
		else if (r < 8)
			ata(ATA_SET_FEATURES);
		else if (r < 10)
			ata(ATA_IDENTIFY);
		else if (r < 12)
		{
			// The old path takes the offset and length from the last SPI packet. A length of 1 never ends.
			p[0] = SPI_TEST_UNIT;
			p[2] = (u8)(rnd() % 64);
			p[4] = (u8)((rnd() % 60) * 2);
			spi(p);
			ata(ATA_IDENTIFY_DEV);
			got = pio_read(buf, sizeof(buf));
		}
		else if (r < 16)
		{
			p[0] = SPI_TEST_UNIT;
			spi(p);
		}
		else if (r < 20)
		{
			p[0] = SPI_REQ_MODE;
			p[2] = (u8)(rnd() % 20);
			p[4] = (u8)((rnd() % 6) * 2);
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 24)
		{
			p[0] = SPI_SET_MODE;
			p[2] = (u8)(rnd() % 10);
			p[4] = (u8)((rnd() % (11 - p[2])) & ~1u);
			spi(p);
			for (u32 k = 0; k < (u32)(p[4] >> 1); k++)
				WriteMem_gdrom(GD_DATA, rnd() & 0xFFFF, 2);
		}
		else if (r < 30)
		{
			p[0] = SPI_REQ_STAT;
			p[2] = (u8)(rnd() % 10);
			p[4] = (u8)((rnd() % (11 - p[2])) & ~1u);
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 33)
		{
			p[0] = SPI_REQ_ERROR;
			p[4] = (u8)((rnd() % 6) * 2);
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 36)
		{
			p[0] = SPI_REQ_SES;
			p[2] = (u8)(rnd() % 3);
			p[4] = 6;
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 39)
		{
			p[0] = SPI_GET_TOC;
			p[1] = (u8)(rnd() & 1);
			const u32 len = (rnd() % 205) * 2;
			p[3] = (u8)(len >> 8);
			p[4] = (u8)len;
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 47)
		{
			const u32 type = (rnd() % 4 == 0) ? 7 : 1 + (rnd() & 1);
			const u32 s = 150 + rnd() % 500000;
			cmd_play(type, s, (rnd() % 8 == 0) ? 0 : s + rnd() % 40, (rnd() % 3 == 0) ? 15 : rnd() % 3);
		}
		else if (r < 53)
			cmd_seek(1 + rnd() % 4, 150 + rnd() % 500000);
		else if (r < 55)
		{
			p[0] = (rnd() & 1) ? SPI_CD_SCAN : SPI_CD_OPEN;
			spi(p);
		}
		else if (r < 62)
		{
			p[0] = SPI_GET_SCD;
			p[1] = (u8)(rnd() % 3);
			p[4] = (u8)((rnd() % 51) * 2);
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 68)
		{
			const u32 secsz = (rnd() % 4) ? 2048 : (rnd() & 1) ? 2340 : 2352;
			cmd_read(SPI_CD_READ, 150 + rnd() % 500000, 1 + rnd() % 60, secsz, false);
			got = pio_read(buf, sizeof(buf));
		}
		else if (r < 76)
		{
			const u32 secsz = (rnd() % 4) ? 2048 : (rnd() & 1) ? 2340 : 2352;
			const u32 step = secsz == 2048 ? 1 : secsz == 2352 ? 2 : 8;
			const u32 count = (1 + rnd() % 80 + step - 1) / step * step;
			cmd_read(SPI_CD_READ, 150 + rnd() % 500000, count, secsz, true);
			const u32 dst = 0x100000 + (rnd() % 0x800) * 32, total = count * secsz;
			// The old path ends a DMA of length 0 at once, with its interrupt and no data.
			if (rnd() % 8 == 0)
			{
				dma_start(dst, 0);
				regs_hash();
			}
			for (u32 done = 0; done < total;)
			{
				u32 len = total - done;
				if (rnd() & 1)
					len = (1 + rnd() % (len / 32)) * 32;
				dma_run(dst + done, len);
				done += len;
				regs_hash();
			}
			h(ram_hash(dst, total));
		}
		else if (r < 78)
		{
			// A read cut off: DMA disabled, then a NOP or a soft reset.
			cmd_read(SPI_CD_READ, 150 + rnd() % 500000, 40 + 8 * (rnd() % 8), 2048, true);
			dma_start(0x200000, 32000);
			sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
			ata((rnd() & 1) ? ATA_NOP : ATA_SOFT_RESET);
		}
		else if (r < 80)
		{
			p[0] = (rnd() & 1) ? 0x70 : (rnd() & 1) ? 0x71 : 0x55;
			got = spi_in(p, buf, sizeof(buf));
		}
		else if (r < 88)
		{
			for (u32 k = 1 + rnd() % 6; k; k--)
				h(pull());
		}
		else if (r < 93)
		{
			for (u32 k = 1 + rnd() % 400; k; k--)
				tick();
		}
		else if (r < 95)
			WriteMem_gdrom(GD_DRVSEL, rnd() & 0xFF, 1);
		else if (r < 97)
		{
			g_disc_type = k_types[rnd() % 8];
			NotifyEvent_gdrom(DiskChange, 0);
		}
		else if (r < 98)
			gdrom_reg_Reset(false);
		else
			h(ReadMem_gdrom(GD_STATUS_Read, 1));
		h(got);
		for (u32 k = 0; k < got && k < sizeof(buf); k++)
			h(buf[k]);
		regs_hash();
	}
	printf("trace %016llX\n", (unsigned long long)g_hash);
}

#ifndef GD_REF
// ---- models ----
static void boot()
{
	for (u32 i = 0; i < 0x50; i++)
		reply_a1[i] = (u8)(i + 1);
	for (u32 i = 0; i < 32; i++)
		gd_data_0x11[i] = (u8)(0x40 + i);
	gdrom_reg_Init();
	NotifyEvent_gdrom(DiskChange, 0);
	gdrom_reg_Reset(false);
}
static u32 audio_status()
{
	u8 p[12] = {0}, b[100];
	p[0] = SPI_GET_SCD;
	p[1] = 1;
	p[4] = 100;
	spi_in(p, b, sizeof(b));
	return b[1];
}
static void pio_sector(u32 fad)
{
	u8 b[4096];
	cmd_read(SPI_CD_READ, fad, 1, 2048, false);
	eq("PIO read length", pio_read(b, sizeof(b)), 2048);
}

// P21 step 1: a data read stops CD-DA.
static void m_p21s1()
{
	boot();
	cmd_play(1, 1000, 2000, 0);
	eq("play gives the sector", pull(), 1001);
	pio_sector(45200);
	eq("OFF: CD-DA goes on after a data read", pull(), 1002);
	eq("OFF: status after a data read", drive_status(), GD_PLAY);
	o_status = 1;
	pio_sector(45200);
	eq("CD-DA after a data read", pull(), 0);
	eq("status after a data read", drive_status(), GD_PAUSE);
	cmd_play(1, 1000, 2000, 0);
	cmd_read(SPI_CD_READ, 45200, 2, 2048, true);
	eq("status after a DMA read command", drive_status(), GD_PAUSE);
	dma_run(0x100000, 4096);
	eq("CD-DA after a DMA read", pull(), 0);
	eq("the DMA data", ram_is(0x100000, 45200, 2, 2048), 1);
	eq("DSC still reads as set", alt() & 0x10, 0x10);
}

// P21 step 2: Flycast dev's status timing.
static void m_p21s2()
{
	o_status = 2;
	boot();
	eq("DSC is the real bit", alt() & 0x10, 0);
	cmd_seek(1, 5000);
	eq("seek: status", drive_status(), GD_SEEK);
	eq("seek: DSC clear", alt() & 0x10, 0);
	tick_ms(19.8);
	eq("seek: still SEEK before 20 ms", drive_status(), GD_SEEK);
	tick_ms(0.4);
	eq("seek: PAUSE after 20 ms", drive_status(), GD_PAUSE);
	eq("seek: DSC set", alt() & 0x10, 0x10);

	cmd_play(1, 5000, 6000, 0);
	eq("play: status unchanged at once", drive_status(), GD_PAUSE);
	eq("play: no audio before 5 ms", pull(), 0);
	tick_ms(4.8);
	eq("play: still not PLAY", drive_status(), GD_PAUSE);
	tick_ms(0.4);
	eq("play: PLAY after 5 ms", drive_status(), GD_PLAY);
	eq("play: audio", pull(), 5001);
	eq("play: subcode says playing", audio_status(), 0x11);

	cmd_seek(4, 0);
	eq("pause: status", drive_status(), GD_PAUSE);
	eq("pause: subcode says paused", audio_status(), 0x12);
	eq("pause: no audio", pull(), 0);
	cmd_play(7, 0, 0, 0);
	eq("resume: BUSY", drive_status(), GD_BUSY);
	eq("resume: no audio yet", pull(), 0);
	tick_ms(19.8);
	eq("resume: still BUSY", drive_status(), GD_BUSY);
	tick_ms(0.4);
	eq("resume: PLAY after 20 ms", drive_status(), GD_PLAY);
	eq("resume: audio goes on", pull(), 5002);

	// Flycast dev: a seek keeps the play state until its time is up.
	cmd_seek(1, 7000);
	eq("seek in a play: status", drive_status(), GD_SEEK);
	eq("seek in a play: audio from the new place", pull(), 7001);
	tick_ms(20.2);
	eq("seek in a play: PAUSE", drive_status(), GD_PAUSE);
	eq("seek in a play: paused", audio_status(), 0x12);
	eq("seek in a play: no audio", pull(), 0);

	cmd_seek(3, 0);
	eq("stop: BUSY", drive_status(), GD_BUSY);
	eq("stop: DSC clear", alt() & 0x10, 0);
	tick_ms(4.8);
	eq("stop: still BUSY", drive_status(), GD_BUSY);
	tick_ms(0.4);
	eq("stop: STANDBY after 5 ms", drive_status(), GD_STANDBY);
	eq("stop: subcode says ended", audio_status(), 0x13);
	cmd_play(7, 0, 0, 0);
	tick_ms(25);
	eq("resume of a stopped play: nothing", drive_status(), GD_STANDBY);
	eq("resume of a stopped play: no audio", pull(), 0);

	cmd_play(1, 5000, 6000, 0);
	tick_ms(1);
	cmd_read(SPI_CD_READ, 45200, 1, 2048, false);
	eq("read: PLAY while it runs", drive_status(), GD_PLAY);
	u8 b[4096];
	pio_read(b, sizeof(b));
	tick_ms(10);
	eq("read: the play it cancelled does not start", pull(), 0);
	eq("read: subcode has no audio state", audio_status(), 0x15);
	tick_ms(56.5);
	eq("read: PLAY until 67 ms", drive_status(), GD_PLAY);
	tick_ms(1);
	eq("read: PAUSE after 67 ms", drive_status(), GD_PAUSE);

	// A DMA read sets its own status change only at its end: the play it cancels must not start inside it.
	cmd_play(1, 5000, 6000, 0);
	cmd_read(SPI_CD_READ, 45200, 2, 2048, true);
	eq("DMA read: PLAY", drive_status(), GD_PLAY);
	tick_ms(10);
	eq("DMA read: the play it cancelled does not start", pull(), 0);
	dma_run(0x100000, 4096);
	tick_ms(66);
	eq("DMA read: PLAY until 67 ms after its end", drive_status(), GD_PLAY);
	tick_ms(1.5);
	eq("DMA read: PAUSE", drive_status(), GD_PAUSE);

	cmd_seek(1, 5000);
	NotifyEvent_gdrom(DiskChange, 0);
	tick_ms(25);
	eq("a disc event cancels the change", drive_status(), GD_PAUSE);
	cmd_seek(1, 5000);
	ata(ATA_SOFT_RESET);
	const u32 dsc = alt() & 0x10;
	tick_ms(25);
	eq("a soft reset cancels the change", alt() & 0x10, dsc);
}

// P47: REQ_STAT position bytes; level 2 also GET_SCD format 0.
static void m_p47()
{
	boot();
	u8 p[12] = {0}, s[10], q[100];
	p[0] = SPI_REQ_STAT;
	p[4] = 10;
	pio_sector(45200);
	spi_in(p, s, sizeof(s));
	eq("OFF: control/ADR", s[2], 4);
	eq("OFF: track", s[3], 1);
	o_position = 1;
	spi_in(p, s, sizeof(s));
	eq("status byte", s[0], drive_status());
	eq("data: control/ADR", s[2], 0x40);
	eq("data: track", s[3], 3);
	eq("data: index", s[4], 1);
	eq("data: FAD", (s[5] << 16) | (s[6] << 8) | s[7], 45200);
	cmd_play(1, 300010, 300100, 3);
	pull();
	pull();
	spi_in(p, s, sizeof(s));
	eq("CD-DA: repeat", s[1] & 0xF, 3);
	eq("CD-DA: control/ADR", s[2], 0x01);
	eq("CD-DA: track", s[3], 4);
	eq("CD-DA: FAD", (s[5] << 16) | (s[6] << 8) | s[7], 300012);
	cmd_seek(4, 0);
	spi_in(p, s, sizeof(s));
	eq("paused: FAD", (s[5] << 16) | (s[6] << 8) | s[7], 300012);
	cmd_seek(3, 0);
	spi_in(p, s, sizeof(s));
	eq("stopped: the last data sector", (s[5] << 16) | (s[6] << 8) | s[7], 45200);
	pio_sector(1200);
	spi_in(p, s, sizeof(s));
	eq("past the last track of the area: lead-out", s[3], 0xAA);
	eq("lead-out: control/ADR", s[2], 0);

	u8 g[12] = {0};
	g[0] = SPI_GET_SCD;
	g[4] = 100;
	cmd_play(1, 400000 + 75 * 61 + 7, 500000, 0);
	pull();
	spi_in(g, q, sizeof(q));
	eq("level 1: GET_SCD control/ADR as before", q[4], 0x14);
	eq("level 1: GET_SCD track as before", q[5], 1);
	o_position = 2;
	spi_in(g, q, sizeof(q));
	eq("GET_SCD: control/ADR", q[4], 0x01);
	eq("GET_SCD: track in BCD", q[5], 0x05);
	eq("GET_SCD: index", q[6], 1);
	eq("GET_SCD: minutes in the track", q[7], 0x01);
	eq("GET_SCD: seconds in the track", q[8], 0x01);
	eq("GET_SCD: frames in the track", q[9], 0x08);
	const u32 abs = 400000 + 75 * 61 + 8;
	eq("GET_SCD: disc minutes", q[11], ((abs / 4500 / 10) << 4) | (abs / 4500 % 10));
	eq("GET_SCD: disc seconds", q[12], ((abs / 75 % 60 / 10) << 4) | (abs / 75 % 60 % 10));
	eq("GET_SCD: disc frames", q[13], ((abs % 75 / 10) << 4) | (abs % 75 % 10));
}

// P62: GET_SCD format 1 and the reply lengths.
static void m_p62()
{
	boot();
	u8 g[12] = {0}, q[300];
	g[0] = SPI_GET_SCD;
	g[1] = 1;
	g[4] = 100;
	cmd_play(1, 300010, 300100, 0);
	pull();
	eq("OFF: format 1 length", spi_in(g, q, sizeof(q)), 12);
	g[1] = 0;
	g[3] = 1;
	g[4] = 0x2C;
	spi(g);
	eq("OFF: format 0 sends the asked 300 bytes", bytecount(), 300);
	pio_read(q, sizeof(q));
	o_scd_q = 1;
	spi(g);
	eq("format 0 stops at its 100 bytes", bytecount(), 100);
	pio_read(q, sizeof(q));
	g[1] = 1;
	g[3] = 0;
	g[4] = 100;
	eq("format 1 length", spi_in(g, q, sizeof(q)), 14);
	eq("format 1: data length", (q[2] << 8) | q[3], 14);
	eq("format 1: control/ADR", q[4], 0x01);
	eq("format 1: track", q[5], 4);
	eq("format 1: index", q[6], 1);
	eq("format 1: FADs in the track", (q[7] << 16) | (q[8] << 8) | q[9], 11);
	eq("format 1: zero", q[10], 0);
	eq("format 1: FAD", (q[11] << 16) | (q[12] << 8) | q[13], 300011);
	g[4] = 8;
	eq("format 1 stops at the asked length", spi_in(g, q, sizeof(q)), 8);
}

// P61: CD-DA play limits and resume rules.
static void m_p61()
{
	boot();
	// OFF first: each old fault shows, so the cases below test the rule and not the harness.
	cmd_play(1, 5000, 5000, 0);
	pull();
	eq("OFF: a start at the end plays on", pull(), 5002);
	cmd_seek(3, 0);
	cmd_play(7, 0, 0, 0);
	eq("OFF: a resume after a stop plays FAD 150", pull(), 151);
	o_rules = 1;

	cmd_play(1, 5000, 5000, 0);
	eq("a: the first sector", pull(), 5001);
	eq("a: then the play is over", pull(), 0);
	eq("e: PAUSE at the end", drive_status(), GD_PAUSE);
	eq("e: the subcode says ended", audio_status(), 0x13);

	cmd_play(1, HD_LEAD - 2, 0, 0);
	eq("b: end 0, first sector", pull(), HD_LEAD - 1);
	eq("b: end 0, second sector", pull(), HD_LEAD);
	eq("b: end 0 stops at the end of the disc", pull(), 0);

	cmd_seek(3, 0);
	cmd_play(7, 0, 0, 0);
	eq("c: no resume after a stop", pull(), 0);
	eq("c: the status stays", drive_status(), GD_STANDBY);
	cmd_play(1, 5000, 5010, 0);
	pull();
	cmd_seek(4, 0);
	eq("c: the subcode says paused", audio_status(), 0x12);
	cmd_play(7, 0, 0, 0);
	eq("c: a paused play resumes", pull(), 5002);
	cmd_seek(1, 6000);
	cmd_play(7, 0, 0, 0);
	eq("c: a resume past the end stops", pull(), 0);
	eq("c: STANDBY", drive_status(), GD_STANDBY);

	cmd_play(1, 5000, 5003, 15);
	pull();
	cmd_seek(4, 0);
	cmd_play(7, 0, 0, 0);
	pull();
	pull();
	eq("d: the loop goes on after a resume", pull(), 5001);

	NotifyEvent_gdrom(DiskChange, 0);
	eq("f: a disc event stops CD-DA", pull(), 0);
	eq("f: no audio state", audio_status(), 0x15);
}

// P22: commands that hang.
static void m_cmd()
{
	o_cmd_fix = 1;
	boot();
	u32 irq = g_irq_cmd;
	ata(0x55);
	eq("unknown ATA: interrupt", g_irq_cmd, irq + 1);
	eq("unknown ATA: CHECK, no BSY", alt() & 0x81, 0x01);
	eq("unknown ATA: abort, illegal request", ReadMem_gdrom(GD_ERROR_Read, 1), 0x54);
	irq = g_irq_cmd;
	ata(ATA_EXEC_DIAG);
	eq("EXEC_DIAG: interrupt", g_irq_cmd, irq + 1);
	eq("EXEC_DIAG: no BSY", alt() & 0x80, 0);
	eq("EXEC_DIAG: error register", ReadMem_gdrom(GD_ERROR_Read, 1), 1);

	WriteMem_gdrom(GD_DRVSEL, 0x10, 1);
	eq("slave: drive select reads back", ReadMem_gdrom(GD_DRVSEL, 1), 0xB0);
	eq("slave: status is 0", ReadMem_gdrom(GD_STATUS_Read, 1), 0);
	irq = g_irq_cmd;
	ata(ATA_NOP);
	ata(0x55);
	eq("slave: commands do nothing", g_irq_cmd, irq);
	WriteMem_gdrom(GD_DRVSEL, 0xEF, 1);
	eq("master: drive select", ReadMem_gdrom(GD_DRVSEL, 1), 0xAF);
	ata(ATA_NOP);
	eq("master: commands run", g_irq_cmd, irq + 1);
	WriteMem_gdrom(GD_COMMAND_Write, ATA_NOP, 2);
	eq("a 16-bit command write does nothing", g_irq_cmd, irq + 1);

	u8 b[70000];
	cmd_read(SPI_CD_READ2, 45200, 2, 2048, false);
	eq("CD_READ2: length from bytes 6-7", pio_read(b, sizeof(b)), 4096);
	bool same = true;
	for (u32 i = 0; i < 4096; i++)
		same = same && b[i] == pat(45200 + i / 2048, 2048, i % 2048);
	eq("CD_READ2: data", same, 1);

	u8 p[12] = {0};
	p[0] = SPI_GET_TOC;
	p[3] = 0x10;
	spi(p);
	eq("GET_TOC: no more than the TOC", bytecount(), 408);
	pio_read(b, sizeof(b));

	p[0] = SPI_SET_MODE;
	p[2] = 6;
	p[3] = 0;
	p[4] = 20;
	spi(p);
	eq("SET_MODE: cut at the writable bytes", bytecount(), 4);
	WriteMem_gdrom(GD_DATA, 0x2211, 2);
	WriteMem_gdrom(GD_DATA, 0x4433, 2);
	eq("SET_MODE: done after the cut length", alt() & 0x88, 0);
	irq = g_irq_cmd;
	p[2] = 12;
	spi(p);
	eq("SET_MODE past the block: done with no data", g_irq_cmd, irq + 1);
	eq("SET_MODE past the block: idle", alt() & 0x88, 0);

	ata(ATA_IDENTIFY_DEV);
	eq("IDENTIFY: 0x50 bytes", pio_read(b, sizeof(b)), 0x50);
	same = true;
	for (u32 i = 0; i < 0x50; i++)
		same = same && b[i] == i + 1;
	eq("IDENTIFY: from the start of the block", same, 1);
}

// P48: SET_MODE writes where REQ_MODE reads.
static void m_p48()
{
	boot();
	u8 p[12] = {0}, b[32];
	p[0] = SPI_SET_MODE;
	p[2] = 2;
	p[4] = 4;
	spi(p);
	WriteMem_gdrom(GD_DATA, 0xBBAA, 2);
	WriteMem_gdrom(GD_DATA, 0xDDCC, 2);
	p[0] = SPI_REQ_MODE;
	spi_in(p, b, sizeof(b));
	eq("OFF: the write lands at twice the offset", b[0], 0x42);
	p[2] = 4;
	spi_in(p, b, sizeof(b));
	eq("OFF: at offset 4", b[0], 0xAA);
	o_set_mode = 1;
	p[0] = SPI_SET_MODE;
	p[2] = 2;
	spi(p);
	WriteMem_gdrom(GD_DATA, 0x2211, 2);
	WriteMem_gdrom(GD_DATA, 0x4433, 2);
	p[0] = SPI_REQ_MODE;
	spi_in(p, b, sizeof(b));
	eq("the write is read back at its offset", (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3], 0x11223344);
}

// P22 SB_GDLEN 0 and P9: a DMA longer than its read.
static void m_dma()
{
	o_dma_fix = 1;
	boot();
	u32 cmd = g_irq_cmd, dmai = g_irq_dma;
	cmd_read(SPI_CD_READ, 45200, 2, 2048, true);
	dma_start(0x100000, 8192);
	for (u32 i = 0; i < 8; i++)
		tick();
	eq("long DMA: the read's data", ram_is(0x100000, 45200, 2, 2048), 1);
	eq("long DMA: the read ends", g_irq_cmd, cmd + 1);
	eq("long DMA: no DMA end", g_irq_dma, dmai);
	eq("long DMA: still active", SB_GDST & 1, 1);
	eq("long DMA: counter", SB_GDLEND, 4096);
	cmd_read(SPI_CD_READ, 60000, 2, 2048, true);
	for (u32 i = 0; i < 8; i++)
		tick();
	eq("long DMA: the next read goes on in it", ram_is(0x101000, 60000, 2, 2048), 1);
	eq("long DMA: its end", g_irq_dma, dmai + 1);
	eq("long DMA: idle", SB_GDST & 1, 0);

	// 40 sectors are two buffer fills; the DMA asks for 64.
	cmd = g_irq_cmd;
	cmd_read(SPI_CD_READ, 45200, 40, 2048, true);
	dma_start(0x200000, 64 * 2048);
	for (u32 i = 0; i < 16; i++)
		tick();
	eq("long DMA over two fills: data", ram_is(0x200000, 45200, 40, 2048), 1);
	eq("long DMA over two fills: the read ends", g_irq_cmd, cmd + 1);
	eq("long DMA over two fills: counter", SB_GDLEND, 40 * 2048);
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);

	// SB_GDLEN 0 is 32 MB (Pro Pinball Trilogy): the read's data moves and the DMA stays open.
	cmd = g_irq_cmd;
	dmai = g_irq_dma;
	cmd_read(SPI_CD_READ, 45200, 4, 2048, true);
	dma_start(0x300000, 0);
	for (u32 i = 0; i < 8; i++)
		tick();
	eq("length 0: data", ram_is(0x300000, 45200, 4, 2048), 1);
	eq("length 0: the read ends", g_irq_cmd, cmd + 1);
	eq("length 0: no DMA end", g_irq_dma, dmai);
	eq("length 0: counter", SB_GDLEND, 8192);
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);

	// The whole 32 MB: the counter wraps to 0 and the DMA ends.
	dmai = g_irq_dma;
	cmd_read(SPI_CD_READ, 45200, 16384, 2048, true);
	dma_start(0, 0);
	for (u32 i = 0; i < 4000 && (SB_GDST & 1); i++)
		tick();
	eq("32 MB: the DMA ends", g_irq_dma, dmai + 1);
	eq("32 MB: idle", SB_GDST & 1, 0);
	eq("32 MB: counter", SB_GDLEND, 0);
	eq("32 MB: the last sectors", ram_is(0xFFF000, 45200 + 16382, 2, 2048), 1);
}

// GD-ROM Speed REAL: Flycast's pacing.
static void m_speed()
{
	boot();
	cmd_read(SPI_CD_READ, 45200, 15, 2048, true);
	dma_start(0x100000, 15 * 2048);
	eq("FAST: the transfer is inside the start write", SB_GDST & 1, 0);
	o_speed = 1;
	cmd_read(SPI_CD_READ, 45200, 32, 2048, true);
	dma_start(0x100000, 65536);
	eq("REAL: nothing moves in the start write", SB_GDLEND, 0);
	u64 t0 = g_cycles;
	for (u32 i = 0; i < 82; i++)
		tick();
	eq("REAL: one 10 KB chunk after 5.7 ms", SB_GDLEND, 10240);
	while (SB_GDST & 1)
		tick();
	// 6 chunks of 10240 at 1.8 MB/s, then 4096 bytes at 25 MB/s; each wait ends on a SlowUpdate.
	const u64 big = (u64)200000000 * 10240 / 1800000, want = 6 * big + 4096 * 8;
	if (g_cycles - t0 < want || g_cycles - t0 > want + 9 * SLOW)
		fail("REAL: cycles for 64 KB", g_cycles - t0, want);
	eq("REAL: data", ram_is(0x100000, 45200, 32, 2048), 1);

	cmd_read(SPI_CD_READ, 45200, 1, 2048, true);
	dma_start(0x100000, 2048);
	eq("REAL: one sector is not inside the write", SB_GDST & 1, 1);
	t0 = g_cycles;
	while (SB_GDST & 1)
		tick();
	if (g_cycles - t0 < 2048 * 8 || g_cycles - t0 > 2048 * 8 + 2 * SLOW)
		fail("REAL: cycles for one sector", g_cycles - t0, 2048 * 8);

	// 10240 bytes are one small chunk: 25 MB/s.
	cmd_read(SPI_CD_READ, 45200, 5, 2048, true);
	dma_start(0x100000, 10240);
	t0 = g_cycles;
	while (SB_GDST & 1)
		tick();
	if (g_cycles - t0 < 10240 * 8 || g_cycles - t0 > 10240 * 8 + 2 * SLOW)
		fail("REAL: cycles for 10 KB", g_cycles - t0, 10240 * 8);

	// A start write does not keep the wait of the transfer it replaces: 32 bytes move inside the write.
	cmd_read(SPI_CD_READ, 45200, 32, 2048, true);
	dma_start(0x100000, 65536);
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
	ata(ATA_SOFT_RESET);
	cmd_read(SPI_CD_READ, 45200, 1, 2048, true);
	dma_start(0x100000, 32);
	eq("REAL: 32 bytes are inside the write", SB_GDLEND, 32);
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
	ata(ATA_SOFT_RESET);
	o_speed = 0;
	cmd_read(SPI_CD_READ, 45200, 2, 2048, true);
	dma_start(0x100000, 4096);
	eq("FAST again: inside the start write", SB_GDST & 1, 0);
}

// GD-ROM Timing: SEEK holds a DMA read that does not go on from the last one for 100 or 330 ms; HOST and FIXED do
// not (no worker here: misc/gdasync_check tests FIXED's wait). A PIO read moves the head with no wait; a reset forgets it.
static u64 ticks_to_end()
{
	const u64 t0 = g_cycles;
	for (u32 i = 0; (SB_GDST & 1) && i < 30000; i++)
		tick();
	return g_cycles - t0;
}

static void m_timing()
{
	boot();
	for (int t = 0; t <= 1; t++)
	{
		o_timing = t;
		cmd_read(SPI_CD_READ, 50000 + t * 100, 2, 2048, true);
		dma_start(0x100000, 4096);
		eq("HOST, FIXED: the data is inside the start write", SB_GDST & 1, 0);
	}
	const u32 ms[2] = { 100, 330 };
	for (int t = 2; t <= 3; t++)
	{
		o_timing = t;
		const u32 fad = 60000 + t * 1000;
		cmd_read(SPI_CD_READ, fad, 2, 2048, true);
		dma_start(0x100000, 4096);
		eq("SEEK: nothing moves in the start write", SB_GDLEND, 0);
		const u64 took = ticks_to_end(), want = (u64)200000 * ms[t - 2];
		if (took < want || took > want + 2 * SLOW)
			fail("SEEK: cycles until the data moves", took, want);
		eq("SEEK: data", ram_is(0x100000, fad, 2, 2048), 1);
		cmd_read(SPI_CD_READ, fad + 2, 2, 2048, true);
		dma_start(0x100000, 4096);
		eq("SEEK: a read that goes on from the last one is inside the write", SB_GDST & 1, 0);
	}
	pio_sector(80000);
	cmd_read(SPI_CD_READ, 80001, 2, 2048, true);
	dma_start(0x100000, 4096);
	eq("SEEK: a DMA read that goes on from a PIO read", SB_GDST & 1, 0);
	ata(ATA_SOFT_RESET);
	cmd_read(SPI_CD_READ, 80003, 2, 2048, true);
	dma_start(0x100000, 4096);
	eq("SEEK: after a reset the head is not known", SB_GDST & 1, 1);
	const u64 took = ticks_to_end();
	if (took < (u64)200000 * 330)
		fail("SEEK: cycles after a reset", took, (u64)200000 * 330);
	o_timing = 0;
}

// CD-DA Miss PLAY AHEAD: the worker is told at the command.
static void m_miss()
{
	boot();
	cmd_play(1, 5000, 6000, 0);
	eq("OFF: no read-ahead call at CD_PLAY", g_seeks, 0);
	o_miss = 1;
	cmd_play(1, 5000, 6000, 0);
	eq("the read-ahead call", g_seeks, 1);
	eq("its FAD", g_seek_fad, 5000);
	pull();
	cmd_seek(4, 0);
	cmd_play(7, 0, 0, 0);
	eq("a resume calls it too", g_seeks, 2);
	eq("with the place the play is at", g_seek_fad, 5001);

	// + LOOP AHEAD: only a play that turns back names its loop, and only at level 3.
	cmd_play(1, 5000, 6000, 3);
	pull();
	eq("PLAY AHEAD: no loop named", g_loop_start, GDA_NO_LOOP);
	o_miss = 2;
	pull();
	eq("+ SILENCE: no loop named", g_loop_start, GDA_NO_LOOP);
	o_miss = 3;
	pull();
	eq("the loop start", g_loop_start, 5000);
	eq("the loop end", g_loop_end, 6000);
	cmd_play(1, 7000, 7100, 0);
	pull();
	eq("no repeat: no loop", g_loop_start, GDA_NO_LOOP);
	cmd_play(1, 7000, 7002, 1);
	pull();
	eq("one repeat: the loop", g_loop_start, 7000);
	pull();
	eq("the last pass: no loop", g_loop_start, GDA_NO_LOOP);
	cmd_play(1, 7000, 7002, 15);
	for (u32 i = 0; i < 7; i++)
		pull();
	eq("repeat 15 turns for ever", g_loop_start, 7000);
	o_miss = 2;
	pull();
	eq("back to + SILENCE: the loop is dropped", g_loop_start, GDA_NO_LOOP);
}

// P82: a play that reaches the lead-out stops there, in PAUSE, its position on the last sector played.
static void m_p82()
{
	boot();
	g_lead = 5010;
	o_rules = 1;
	cmd_play(1, 5008, 5020, 0);
	pull();
	pull();
	eq("ON: the play reads past the lead-out", pull(), 5011);
	o_rules = 2;
	cmd_play(1, 5008, 5020, 0);
	eq("the sector before the last", pull(), 5009);
	eq("the last sector", pull(), 5010);
	eq("the lead-out stops the play", pull(), 0);
	eq("then silence", pull(), 0);
	eq("PAUSE", drive_status(), GD_PAUSE);
	eq("the subcode says ended", audio_status(), 0x13);
	// The old GET_SCD format 1 sends the FADs since the play's start: one, so the position is on the last sector.
	u8 p[12] = {0}, b[100];
	p[0] = SPI_GET_SCD;
	p[1] = 1;
	p[4] = 100;
	spi_in(p, b, sizeof(b));
	eq("the position stays on the last sector", (b[8] << 16) | (b[9] << 8) | b[10], 1);
	cmd_play(1, 5004, 5007, 0);
	pull();
	pull();
	pull();
	eq("an end before the lead-out ends the play as before", pull(), 0);
	cmd_play(1, 5006, 5030, 15);
	for (u32 i = 0; i < 4; i++)
		pull();
	eq("a loop over the lead-out stops there", pull(), 0);
	eq("PAUSE after the loop", drive_status(), GD_PAUSE);
	cmd_play(1, 6000, 6100, 0);
	eq("a start past the lead-out", pull(), 0);
	g_lead = 0;
	cmd_play(1, 5000, 5100, 0);
	eq("no disc: lead-out 0", pull(), 0);
	g_lead = HD_LEAD;
}

// Bundle 20 (P68, Flycast 959b6344b): Holly Regs FLYCAST leaves DC Checker's register values after an ATA soft reset.
// The status read ORs in DSC unless GD Status is 2, so its other bits are tested.
static void m_p68()
{
	boot();
	ata(ATA_SOFT_RESET);
	const u32 old_alt = alt() & ~0x10u, old_err = ReadMem_gdrom(GD_ERROR_Read, 1);
	const u32 old_ir = ReadMem_gdrom(GD_IREASON_Read, 1), old_bc = bytecount();
	o_holly = 1;
	ata(ATA_SOFT_RESET);
	eq("ON: status 0", alt() & ~0x10u, 0);
	eq("ON: error 1", ReadMem_gdrom(GD_ERROR_Read, 1), 1);
	eq("ON: PAUSE", drive_status(), GD_PAUSE);
	eq("ON: interrupt reason 1", ReadMem_gdrom(GD_IREASON_Read, 1), 1);
	eq("ON: byte count 0xEB14", bytecount(), 0xEB14);
	eq("OFF leaves other values", old_alt != 0 || old_err != 1 || old_ir != 1 || old_bc != 0xEB14, 1);
	o_holly = 0;
}

// P34: the sense a swap leaves, and a DMA the swap emptied.
static void m_swap()
{
	boot();
	u8 p[12] = {0}, r[10];
	p[0] = SPI_REQ_ERROR;
	p[4] = 10;
	spi_in(p, r, sizeof(r));
	eq("no swap: no sense", r[2], 0);
	cmd_play(1, 5000, 6000, 3);
	cmd_read(SPI_CD_READ, 45200, 40, 2048, true);
	dma_start(0x100000, 40 * 2048);
	gdrom_disc_swap("next");
	eq("BUSY", drive_status(), GD_BUSY);
	eq("CD-DA is off", pull(), 0);
	for (u32 i = 0; i < 8; i++)
		tick();
	ata(ATA_NOP);
	eq("NOP: the sense key is in the error register", ReadMem_gdrom(GD_ERROR_Read, 1) >> 4, 2);
	p[0] = SPI_TEST_UNIT;
	spi(p);
	eq("TEST_UNIT: CHECK while BUSY", alt() & 1, 1);
	p[0] = SPI_REQ_ERROR;
	spi_in(p, r, sizeof(r));
	eq("not ready: key", r[2], 2);
	eq("not ready: ASC", r[8], 4);
	eq("not ready: ASCQ", r[9], 1);
	tick_ms(999);
	eq("still BUSY before 1 s", drive_status(), GD_BUSY);
	tick_ms(1);
	eq("PAUSE after 1 s", drive_status(), GD_PAUSE);
	p[0] = 0x70;
	spi(p);
	eq("CHECK with unit attention", alt() & 1, 1);
	p[0] = SPI_REQ_ERROR;
	spi_in(p, r, sizeof(r));
	eq("unit attention: key", r[2], 6);
	eq("unit attention: ASC", r[8], 0x28);
	spi_in(p, r, sizeof(r));
	eq("read once", r[2], 0);
	sb_regs[(SB_GDEN_addr - SB_BASE) >> 2].writeFunction(0);
	pio_sector(45200);

	// No reader took the image: no disc, ASC 0x29. The 3 s of the boot spin-up are over first.
	tick_ms(1100);
	g_disc_type = NoDisk;
	gdrom_disc_swap("bad");
	tick_ms(1001);
	eq("no disc: status", drive_status(), GD_NODISC);
	spi_in(p, r, sizeof(r));
	eq("no disc: ASC", r[8], 0x29);

	// OFF: a NOP leaves DRQ; ON: it ends the data phase.
	g_disc_type = GdRom;
	NotifyEvent_gdrom(DiskChange, 0);
	p[0] = SPI_REQ_STAT;
	spi(p);
	ata(ATA_NOP);
	eq("OFF: DRQ stays after a NOP", alt() & 8, 8);
	ata(ATA_SOFT_RESET);
	o_swap = 1;
	spi(p);
	ata(ATA_NOP);
	eq("ON: a NOP clears DRQ", alt() & 8, 0);
}
#endif

int main(int argc, char** argv)
{
	const char* mode = argc > 1 ? argv[1] : "";
	if (!strcmp(mode, "trace"))
	{
		g_seed = (argc > 2 ? (u64)atoll(argv[2]) : 1) * 2654435761ull + 1;
		trace(argc > 3 ? (u32)atoi(argv[3]) : 20000);
		return g_errors ? 1 : 0;
	}
#ifndef GD_REF
	static const struct { const char* name; void (*fn)(); } k_models[] = {
		{ "p21s1", m_p21s1 }, { "p21s2", m_p21s2 }, { "p47", m_p47 }, { "p62", m_p62 }, { "p61", m_p61 },
		{ "cmd", m_cmd }, { "p48", m_p48 }, { "dma", m_dma }, { "speed", m_speed }, { "miss", m_miss },
		{ "swap", m_swap }, { "p82", m_p82 }, { "p68", m_p68 }, { "timing", m_timing },
	};
	if (!strcmp(mode, "model") && argc > 2)
		for (u32 i = 0; i < sizeof(k_models) / sizeof(k_models[0]); i++)
			if (!strcmp(argv[2], k_models[i].name))
			{
				k_models[i].fn();
				eq("DMA words not reported to the JIT", g_dw_n, 0);
				printf("%s: model %s, %d errors\n", g_errors ? "FAIL" : "PASS", argv[2], g_errors);
				return g_errors ? 1 : 0;
			}
#endif
	printf("usage: trace <seed> <ops> | model <group>\n");
	return 2;
}
