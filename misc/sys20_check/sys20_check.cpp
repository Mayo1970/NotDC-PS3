// Plan section 7, bundle 20, PC check (WSL): the system rows on their real sources (s20_*.cpp), each row OLD and new:
// Memory Map, Block Writes, On-chip Regs, Holly Regs (its SB part), Reg Access Size, ELF Loader. Arg: a work folder.
#include "types.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/ccn.h"
#include "dc/mem/mmu.h"
#include "dc/mem/sb.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/sh4_internal_reg.h"
#include "plugs/ImgReader/elf_loader.h"
#include "ps3/ps3_presets.h"
#include <string>
#include <vector>
#undef r   // sh4_registers.h: Sh4cntx.r below is the field

// ---- the rows and what the sources link against ----
int g_mem_map_preset, g_mmu_bits_preset, g_ocr_banks_preset, g_reg_size_preset, g_block_write_preset;
int g_holly_regs_preset, g_onchip_regs_preset, g_elf_loader_preset;
extern "C" { int g_blockcopy_preset; int get_debug_loop() { return 0; } void HollySoftReset() {} }
Sh4Context Sh4cntx;
CCN_CCR_type CCN_CCR;
CCN_PTEH_type CCN_PTEH;
TLB_Entry UTLB[64], ITLB[4];
VArray2 mem_b;
Array<u8> bios_b, flash_b;
bool g_snd_thread;
void UTLB_Sync(u32) {}
void ITLB_Sync(u32) {}
#define STUB3(m) void m##_Init() {} void m##_Term() {} void m##_Reset(bool) {}
STUB3(bsc) STUB3(ccn) STUB3(cpg) STUB3(rtc) STUB3(tmu) STUB3(ubc) STUB3(dmac) STUB3(intc) STUB3(scif)
STUB3(maple) STUB3(pvr_sb) STUB3(aica_sb) STUB3(asic_reg) STUB3(gdrom_reg)
void snd_lock(u32) {}
void snd_unlock() {}

// The device a dispatch reached, and a value each device returns.
static std::string g_dev;
static u32 g_dev_addr, g_dev_data;
static u32 dev(const char* name, u32 addr, u32 data, u32 ret)
{
	g_dev = name;
	g_dev_addr = addr;
	g_dev_data = data;
	return ret;
}
u32 ReadMem_gdrom(u32 a, u32) { return dev("gdrom", a, 0, 0x6D); }
void WriteMem_gdrom(u32 a, u32 d, u32) { dev("gdrom", a, d, 0); }
u32 pvr_readreg_TA(u32 a, u32) { return dev("ta", a, 0, 0x7A); }
void pvr_writereg_TA(u32 a, u32 d, u32) { dev("ta", a, d, 0); }
u32 ReadMem_aica_reg(u32 a, u32) { return dev("aica", a, 0, 0xA1CA); }
void WriteMem_aica_reg(u32 a, u32 d, u32) { dev("aica", a, d, 0); }
u32 ReadMem_aica_rtc(u32 a, u32) { return dev("rtc", a, 0, 0x27C); }
void WriteMem_aica_rtc(u32 a, u32 d, u32) { dev("rtc", a, d, 0); }
u32 libAICA_ReadMem_aica_ram(u32 a, u32) { return dev("aram", a, 0, 0xA7A7); }
void libAICA_WriteMem_aica_ram(u32 a, u32 d, u32) { dev("aram", a, d, 0); }
u32 libExtDevice_ReadMem_A0_006(u32 a, u32) { return dev("modem", a, 0, 0x30); }
void libExtDevice_WriteMem_A0_006(u32 a, u32 d, u32) { dev("modem", a, d, 0); }
u32 libExtDevice_ReadMem_A0_010(u32 a, u32) { return dev("ext", a, 0, 0xE7); }
void libExtDevice_WriteMem_A0_010(u32 a, u32 d, u32) { dev("ext", a, d, 0); }
struct AicaW { u32 addr, data, size; };
static std::vector<AicaW> g_aica_w;
u32 libAICA_ReadMem_aica_reg(u32 a, u32) { return dev("aica", a, 0, 0); }
void libAICA_WriteMem_aica_reg(u32 a, u32 d, u32 sz) { g_aica_w.push_back({ a, d, sz }); }

// ---- _vmem: the handlers registered and the map calls made ----
struct Handler
{
	u8 (*r8)(u32); u16 (*r16)(u32); u32 (*r32)(u32);
	void (*w8)(u32, u8); void (*w16)(u32, u16); void (*w32)(u32, u32);
};
static std::vector<Handler> g_handlers;
struct MapCall { char kind; u32 a, b, c; };
static std::vector<MapCall> g_maps;
_vmem_handler _vmem_register_handler(_vmem_ReadMem8FP* r8, _vmem_ReadMem16FP* r16, _vmem_ReadMem32FP* r32,
                                     _vmem_WriteMem8FP* w8, _vmem_WriteMem16FP* w16, _vmem_WriteMem32FP* w32)
{
	g_handlers.push_back({ r8, r16, r32, w8, w16, w32 });
	return (u32)g_handlers.size();
}
void _vmem_map_handler(_vmem_handler h, u32 start, u32 end) { g_maps.push_back({ 'h', h, start, end }); }
void _vmem_map_block(void*, u32 start, u32 end, u32 mask) { g_maps.push_back({ 'b', start, end, mask }); }
void _vmem_mirror_mapping(u32 nr, u32 start, u32 size) { g_maps.push_back({ 'm', nr, start, size }); }

// ---- the parts the harness units export ----
void s20_regs_init();
u32 s20_regs_get(u32 i);
void s20_regs_set(u32 i, u32 v);
u32 s20_regs_read(u32 i, u32 sz);
void s20_regs_write(u32 i, u32 sz, u32 v);
u32 s20_sci_read(u32 addr);
void s20_sci_write(u32 addr, u32 v);
void s20_area7_init();
void s20_area7(u32 base);
u32 s20_area7_handler();
u32 s20_ocr_handler();
void sci_Init();
void sci_Reset(bool Manual);
void map_area0_init();
void map_area0(u32 base);
extern _vmem_handler area0_handler_0000_00FF, area0_handler_0100_01FF;
extern u8 g_blk_ram[];
extern u32 g_blk_writes[3];
void WriteMemBlock_nommu_dma(u32 dst, u32 src, u32 size);
void WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size);
void WriteMemBlock_ptr(u32 addr, u32* data, u32 size);

static int g_fails;
static u32 g_checks;
static const char* g_part = "";
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { if (g_fails++ < 30) { printf("FAIL %s: ", g_part); \
	printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u32 s_seed = 0x9E3779B9u;
static u32 rnd()
{
	s_seed ^= s_seed << 13;
	s_seed ^= s_seed >> 17;
	s_seed ^= s_seed << 5;
	return s_seed;
}

// ---- Reg Access Size: the SH4 on-chip register helpers ----
static void t_regs()
{
	g_part = "Reg Access Size FIXED, on-chip registers";
	g_reg_size_preset = 1;
	s20_regs_init();
	static const u32 k_width[3] = { 4, 2, 1 };
	for (int n = 0; n < 3000; n++)
	{
		const u32 i = rnd() % 3, sz = 1u << (rnd() % 3), w = k_width[i];
		const u32 wmask = w == 4 ? 0xFFFFFFFFu : w == 2 ? 0xFFFFu : 0xFFu;
		const u32 smask = sz == 4 ? 0xFFFFFFFFu : sz == 2 ? 0xFFFFu : 0xFFu;
		const u32 v = rnd() & wmask, d = rnd();
		// The SH4 is little-endian: a narrow access takes the low bytes; a wide one sees the register's own bits.
		s20_regs_set(i, v);
		CHECK(s20_regs_read(i, sz) == (v & smask), "read%u of a %u-bit register %08X: %08X", sz * 8, w * 8, v,
		      s20_regs_read(i, sz));
		s20_regs_write(i, sz, d);
		const u32 want = ((v & ~smask) | (d & smask)) & wmask;
		CHECK(s20_regs_get(i) == want, "write%u %08X to a %u-bit register %08X: %08X, want %08X", sz * 8, d, w * 8, v,
		      s20_regs_get(i), want);
	}
	g_reg_size_preset = 0;
}

// ---- Memory Map: area 7 in P0-P3, the 0x02000000 mirror of area 0 ----
static void t_area7()
{
	g_part = "Memory Map, area 7";
	s20_area7_init();
	for (int row = 0; row < 2; row++)
	{
		g_mem_map_preset = row;
		g_maps.clear();
		for (u32 base = 0; base < 0xE0; base += 0x20)
			s20_area7(base);
		u32 regs = 0, ocr = 0;
		for (const MapCall& m : g_maps)
		{
			if (m.kind == 'h' && m.a == s20_area7_handler() && m.b == m.c && (m.b & 0x1F) == 0x1F)
				regs++;
			else if (m.kind == 'h' && m.a == s20_ocr_handler() && m.b == 0x7C && m.c == 0x7F)
				ocr++;
			else
				CHECK(false, "row %d: a map call %c %X %X %X", row, m.kind, m.a, m.b, m.c);
		}
		// OLD: the registers at 1F, 3F, 5F, 9F, BF, DF; FLYCAST: none (P4 has its own handler). OCR at 7C-7F both.
		CHECK(regs == (row ? 0u : 6u) && ocr == 1, "row %d: %u register maps, %u OCR maps", row, regs, ocr);
	}
	g_mem_map_preset = 0;
}

static void t_area0()
{
	g_part = "Memory Map, area 0";
	g_handlers.clear();
	map_area0_init();
	CHECK(g_handlers.size() == 2, "area 0 registers %u handlers, want 2 (_vmem ids 0-7 only)", (u32)g_handlers.size());
	const u32 h0 = area0_handler_0000_00FF, h1 = area0_handler_0100_01FF;
	for (int row = 0; row < 2; row++)
	{
		// Both rows map the same: the handlers tell the 0x02 mirror by address bit 25.
		g_mem_map_preset = row;
		g_maps.clear();
		map_area0(0x80);
		const bool ok = g_maps.size() == 3 && g_maps[0].kind == 'h' && g_maps[0].a == h0 && g_maps[0].b == 0x80 &&
		                g_maps[1].kind == 'h' && g_maps[1].a == h1 && g_maps[1].b == 0x81 && g_maps[2].kind == 'm' &&
		                g_maps[2].a == 0x82 && g_maps[2].b == 0x80 && g_maps[2].c == 0x02;
		CHECK(ok, "row %d: %u map calls", row, (u32)g_maps.size());
	}
	const Handler& lo = g_handlers[h0 - 1];
	const Handler& ex = g_handlers[h1 - 1];
	for (u32 i = 0; i < bios_b.Size; i++)
		bios_b.data[i] = (u8)(i * 7 + 1);
	const u32 bios_word = bios_b.data[0x10] | bios_b.data[0x11] << 8 | bios_b.data[0x12] << 16 | (u32)bios_b.data[0x13] << 24;
	sb_Init();
	SB_G2ID = 0x12;
	for (int row = 0; row < 2; row++)
	{
		g_mem_map_preset = row;
		memset(flash_b.data, 0x5A, flash_b.Size);
		// OLD: the mirror shows the BIOS and the flash; FLYCAST: it reads 0 there and drops writes.
		CHECK(lo.r32(0x80000010) == bios_word && lo.r32(0x82000010) == (row ? 0 : bios_word), "row %d: BIOS %08X, mirror %08X",
		      row, lo.r32(0x80000010), lo.r32(0x82000010));
		CHECK((lo.r16(0x821FFFFE) == 0) == (row != 0) && (lo.r8(0x82200003) == 0) == (row != 0),
		      "row %d: the mirror's BIOS end or flash", row);
		lo.w32(0x82200000, 0x11223344);
		CHECK(flash_b.data[0] == (row ? 0x5A : 0x44), "row %d: a mirror write to the flash gives %02X", row, flash_b.data[0]);
		lo.w32(0x80200000, 0x55667788);
		CHECK(flash_b.data[0] == 0x88, "row %d: a flash write in 0x00 got lost", row);
		g_dev.clear();
		CHECK(lo.r32(0x82700000) == 0xA1CA && g_dev == "aica" && g_dev_addr == 0x00700000, "row %d: AICA through the mirror: %s",
		      row, g_dev.c_str());
		CHECK(lo.r32(0x825F7000) == 0x6D && g_dev == "gdrom", "row %d: GD-ROM through the mirror: %s", row, g_dev.c_str());
		CHECK(lo.r32(0x82800000) == 0xA7A7 && g_dev == "aram", "row %d: wave RAM through the mirror: %s", row, g_dev.c_str());
		CHECK(lo.r32(0x825F7880) == 0x12, "row %d: SB_G2ID through the mirror", row);
		CHECK(ex.r32(0x83000000) == 0xE7 && g_dev == "ext", "row %d: the external device at 0x03: %s", row, g_dev.c_str());
	}
	g_mem_map_preset = 0;
}

// ---- Holly Regs (SB part), Block Writes (SB_PDLEN), Reg Access Size (SB) ----
static void t_sb()
{
	g_part = "SB registers";
	static const u32 k_g2[8] = { SB_ADSTAG_addr, SB_ADSTAR_addr, SB_E1STAG_addr, SB_E1STAR_addr, SB_E2STAG_addr,
	                             SB_E2STAR_addr, SB_DDSTAG_addr, SB_DDSTAR_addr };
	for (int holly = 0; holly < 2; holly++)
		for (int blockw = 0; blockw < 2; blockw++)
		{
			g_holly_regs_preset = holly;
			g_block_write_preset = blockw;
			sb_Init();
			for (u32 a : k_g2)
			{
				sb_WriteMem(a, 0xEC123457, 4);
				const u32 want = holly ? 0x0C123440u : 0xEC123457u;
				CHECK(sb_ReadMem(a, 4) == want, "holly %d: G2 address %08X reads %08X, want %08X", holly, a,
				      sb_ReadMem(a, 4), want);
			}
			sb_WriteMem(SB_PDLEN_addr, 0x12345677, 4);
			CHECK(sb_ReadMem(SB_PDLEN_addr, 4) == (blockw ? 0x00345660u : 0x12345677u), "block writes %d: SB_PDLEN %08X",
			      blockw, sb_ReadMem(SB_PDLEN_addr, 4));
			CHECK(SB_TFREM == (holly ? 8u : 0u) && sb_ReadMem(SB_TFREM_addr, 4) == SB_TFREM, "holly %d: SB_TFREM %u",
			      holly, SB_TFREM);
		}
	g_holly_regs_preset = g_block_write_preset = 0;
	g_reg_size_preset = 1;
	sb_Init();
	sb_WriteMem(SB_ADSTAR_addr, 0x11223344, 4);
	sb_WriteMem(SB_ADSTAR_addr, 0xAB, 1);
	CHECK(SB_ADSTAR == 0x112233AB, "byte write: %08X", SB_ADSTAR);
	sb_WriteMem(SB_ADSTAR_addr, 0xCDEF, 2);
	CHECK(SB_ADSTAR == 0x1122CDEF, "halfword write: %08X", SB_ADSTAR);
	CHECK(sb_ReadMem(SB_ADSTAR_addr, 1) == 0xEF && sb_ReadMem(SB_ADSTAR_addr, 2) == 0xCDEF, "narrow reads %X %X",
	      sb_ReadMem(SB_ADSTAR_addr, 1), sb_ReadMem(SB_ADSTAR_addr, 2));
	g_reg_size_preset = 0;
}

// ---- On-chip Regs: SCI ----
static void t_sci()
{
	g_part = "On-chip Regs, SCI";
	g_onchip_regs_preset = 0;
	sci_Init();
	sci_Reset(false);
	CHECK(s20_sci_read(SCI_SCBRR1_addr) == 0, "OLD: SCBRR1 reads %02X", s20_sci_read(SCI_SCBRR1_addr));
	g_onchip_regs_preset = 1;
	sci_Init();
	sci_Reset(false);
	static const u32 k_addr[7] = { SCI_SCSMR1_addr, SCI_SCBRR1_addr, SCI_SCSCR1_addr, SCI_SCTDR1_addr, SCI_SCSSR1_addr,
	                               SCI_SCRDR1_addr, SCI_SCSPTR1_addr };
	static const u32 k_reset[7] = { 0, 0xFF, 0, 0xFF, 0x84, 0, 0 };
	for (int i = 0; i < 7; i++)
		CHECK(s20_sci_read(k_addr[i]) == k_reset[i], "reset value of %08X: %02X, want %02X", k_addr[i],
		      s20_sci_read(k_addr[i]), k_reset[i]);
	// The write masks: SCSSR1 0xF9, SCSPTR1 0x8F, SCRDR1 read-only, the rest whole.
	static const u32 k_after[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xF9, 0, 0x8F };
	for (int i = 0; i < 7; i++)
	{
		s20_sci_write(k_addr[i], 0xFF);
		CHECK(s20_sci_read(k_addr[i]) == k_after[i], "write 0xFF to %08X: %02X, want %02X", k_addr[i],
		      s20_sci_read(k_addr[i]), k_after[i]);
	}
	g_onchip_regs_preset = 0;
}

// ---- Block Writes: WriteMemBlock_* with a size that is not a multiple of 4 ----
static void t_block()
{
	g_part = "Block Writes";
	u32 src[4];
	for (int copy = 0; copy < 2; copy++)
		for (u32 size = 1; size <= 15; size++)
			for (int fn = 0; fn < 3; fn++)
			{
				g_blockcopy_preset = copy;
				g_block_write_preset = 1;
				memset(g_blk_ram, 0xEE, RAM_SIZE);
				for (u32 i = 0; i < 4; i++)
					src[i] = rnd();
				memcpy(&g_blk_ram[0x200000], src, sizeof(src));
				if (fn == 0)
					WriteMemBlock_nommu_ptr(0x0C100000, src, size);
				else if (fn == 1)
					WriteMemBlock_ptr(0x8C100000, src, size);
				else
					WriteMemBlock_nommu_dma(0x0C100000, 0x0C200000, size);
				// The guest's bytes, little-endian: the source's first size bytes, and the next byte as it was.
				const bool ok = !memcmp(&g_blk_ram[0x100000], src, size) && g_blk_ram[0x100000 + size] == 0xEE;
				CHECK(ok, "copy %d fn %d size %u", copy, fn, size);
			}
	g_block_write_preset = 0;
	g_blockcopy_preset = 0;
	u32 verifies = 0;
	for (u32 size = 1; size <= 7; size++)
	{
		try { WriteMemBlock_nommu_ptr(0x0C100000, src, size); }
		catch (const VerifyFail&) { verifies++; }
	}
	CHECK(verifies == 6, "OLD: %u of 6 odd sizes hit the verify (the PS3 hangs there)", verifies);
}

// ---- ELF Loader: an ELF with an odd segment start, a zero tail, a section outside the segments ----
static void put16(std::vector<u8>& f, u32 o, u32 v) { f[o] = (u8)(v >> 8); f[o + 1] = (u8)v; }
static void put32(std::vector<u8>& f, u32 o, u32 v) { for (int i = 0; i < 4; i++) f[o + i] = (u8)(v >> (24 - 8 * i)); }
static u8 sh4_byte(u32 a) { return mem_b.data[(a & RAM_MASK) ^ 3]; }

static void t_elf(const char* dir)
{
	g_part = "ELF Loader";
	// The loader swaps every header field (it was written for the big-endian PPU), so on this PC they are stored
	// big-endian: after the swap they read as on the PS3.
	std::vector<u8> f(0x400, 0);
	f[0] = 0x7F; f[1] = 'E'; f[2] = 'L'; f[3] = 'F'; f[4] = 1; f[5] = 1; f[6] = 1;
	put16(f, 16, 2); put16(f, 18, 42); put32(f, 20, 1); put32(f, 24, 0x8C010000); put32(f, 28, 52);
	const u32 sh = 0x310;
	put32(f, 32, sh); put16(f, 40, 52); put16(f, 42, 32); put16(f, 44, 2); put16(f, 46, 40);
	put16(f, 48, 4); put16(f, 50, 0);
	// Segment 0: file 0x23 bytes at 0x8C010001, memory 0x40; segment 1: 0x10 bytes at 0x8C020000.
	const u32 ph = 52;
	put32(f, ph + 0, 1); put32(f, ph + 4, 0x100); put32(f, ph + 8, 0x8C010001); put32(f, ph + 16, 0x23);
	put32(f, ph + 20, 0x40);
	put32(f, ph + 32, 1); put32(f, ph + 36, 0x200); put32(f, ph + 40, 0x8C020000); put32(f, ph + 48, 0x10);
	put32(f, ph + 52, 0x10);
	// Sections: null, .text in segment 0, .init_array outside every segment, .bss (NOBITS).
	put32(f, sh + 40 + 4, 1); put32(f, sh + 40 + 8, 6); put32(f, sh + 40 + 12, 0x8C010001); put32(f, sh + 40 + 16, 0x100);
	put32(f, sh + 40 + 20, 0x23);
	put32(f, sh + 80 + 4, 14); put32(f, sh + 80 + 8, 3); put32(f, sh + 80 + 12, 0x8C030000); put32(f, sh + 80 + 16, 0x300);
	put32(f, sh + 80 + 20, 8);
	put32(f, sh + 120 + 4, 8); put32(f, sh + 120 + 8, 3); put32(f, sh + 120 + 12, 0x8C010024); put32(f, sh + 120 + 20, 0x1C);
	for (u32 i = 0; i < 0x23; i++)
		f[0x100 + i] = (u8)(i * 7 + 3);
	for (u32 i = 0; i < 0x10; i++)
		f[0x200 + i] = (u8)(0x80 + i);
	for (u32 i = 0; i < 8; i++)
		f[0x300 + i] = (u8)(0xC0 + i);
	const std::string path = std::string(dir) + "/test.elf";
	FILE* fp = fopen(path.c_str(), "wb");
	if (!fp || fwrite(f.data(), 1, f.size(), fp) != f.size())
	{
		CHECK(false, "cannot write %s", path.c_str());
		return;
	}
	fclose(fp);

	g_elf_loader_preset = 1;
	memset(mem_b.data, 0xEE, mem_b.size);
	u32 entry = 0;
	const bool loaded = elf_load(path.c_str(), &entry);
	CHECK(loaded && entry == 0x8C010000, "load %d entry %08X", loaded, entry);
	u32 bad = 0;
	for (u32 i = 0; i < 0x23; i++)
		bad += sh4_byte(0x8C010001 + i) != (u8)(i * 7 + 3);
	for (u32 i = 0x23; i < 0x40; i++)
		bad += sh4_byte(0x8C010001 + i) != 0;
	for (u32 i = 0; i < 0x10; i++)
		bad += sh4_byte(0x8C020000 + i) != (u8)(0x80 + i);
	for (u32 i = 0; i < 8; i++)
		bad += sh4_byte(0x8C030000 + i) != (u8)(0xC0 + i);
	CHECK(bad == 0, "%u bytes differ in the segments, the zero tail and the section", bad);
	CHECK(sh4_byte(0x8C010000) == 0xEE && sh4_byte(0x8C010041) == 0xEE && sh4_byte(0x8C020010) == 0xEE &&
	      sh4_byte(0x8C030008) == 0xEE, "a byte next to a load changed");

	g_aica_w.clear();
	elf_apply_entry(entry);
	const AicaW want[4] = { { 0x0070289C, 0x48, 2 }, { 0x007028A8, 0x18, 1 }, { 0x007028AC, 0x50, 1 },
	                        { 0x007028B0, 0x08, 1 } };
	bool aica_ok = g_aica_w.size() == 4;
	for (u32 i = 0; aica_ok && i < 4; i++)
		aica_ok = g_aica_w[i].addr == want[i].addr && g_aica_w[i].data == want[i].data && g_aica_w[i].size == want[i].size;
	CHECK(aica_ok, "%u AICA writes, not SCIEB 0x48, SCILV0-2 0x18 0x50 0x08", (u32)g_aica_w.size());
	CHECK(Sh4cntx.pc == entry && Sh4cntx.r[15] == 0x8CFFF000, "pc %08X r15 %08X", Sh4cntx.pc, Sh4cntx.r[15]);
	g_elf_loader_preset = 0;
	g_aica_w.clear();
	elf_apply_entry(entry);
	CHECK(g_aica_w.empty(), "OLD wrote the AICA");
}

int main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : "/tmp";
	mem_b.size = RAM_SIZE;
	mem_b.data = (u8*)calloc(RAM_SIZE + 8, 1);
	bios_b.Size = 2 << 20;
	bios_b.data = (u8*)calloc(bios_b.Size, 1);
	flash_b.Size = 128 << 10;
	flash_b.data = (u8*)calloc(flash_b.Size, 1);
	t_regs();
	t_area7();
	t_area0();
	t_sb();
	t_sci();
	t_block();
	t_elf(dir);
	printf("%s: %u checks of Memory Map, Block Writes, On-chip Regs, Holly Regs, Reg Access Size, ELF Loader, %d failed\n",
	       g_fails ? "FAIL" : "PASS", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
