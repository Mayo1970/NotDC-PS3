// Plan section 7, bundle 16, PC check: the real dc/maple sources. With every row OFF they must act as the old
// maple_if.cpp (ref_maple_if.inc, commit 5188b36); then each row's rule. Arg: the count of random DMA tables.
#include "types.h"
#include <cstdarg>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

// -- The model console ---------------------------------------------------------
#define RAM_BYTES  (16u << 20)
#define WIN_BYTES  (1u << 20)   // the tests keep every address in the first MB of RAM
static u32 g_ram[RAM_BYTES / 4];
static u8 g_bios[2u << 20];
u32 SB_MDSTAR, SB_MDTSEL, SB_MDEN, SB_MDST, SB_MSYS, SB_MSHTCL, SB_MDAPRO, SB_MMSEL;
int g_check_clock_mhz = 200;
static std::vector<int> g_irq;
static std::string g_log;
static bool g_verbose;

#include "dc/mem/sb.h"
RegisterStruct sb_regs[4];

int check_printf(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g_log += buf;
	if (g_verbose)
		fputs(buf, stdout);
	return n;
}

// The real GetMemPtr (dc/mem/sh4_mem.cpp): RAM in area 3, the BIOS in area 0, null elsewhere.
u8* GetMemPtr(u32 addr, u32)
{
	if (((addr >> 29) & 7) == 7)
		return 0;
	switch ((addr >> 26) & 7)
	{
	case 3:
		return (u8*)g_ram + (addr & (RAM_BYTES - 1));
	case 0:
		addr &= 0x01FFFFFF;
		return addr <= 0x001FFFFF ? g_bios + addr : 0;
	default:
		return 0;
	}
}
u32 ReadMem32_nommu(u32 addr)
{
	if (((addr >> 26) & 7) == 3 && ((addr >> 29) & 7) != 7)
		return g_ram[(addr & (RAM_BYTES - 1)) >> 2];
	return 0;
}
void asic_RaiseInterrupt(HollyInterruptID inter)
{
	g_irq.push_back((int)inter);
}

// -- What ps3_presets.cpp and ps3_input.cpp give the maple code ------------------
int g_maple_delay_preset, g_maple_speed_preset, g_maple_cmds_preset, g_maple_ddt_preset, g_maple_guard_preset;
int g_maple_frame_preset, g_vmu_per_game_preset, g_rumble_pack_preset;
static int g_players = 1, g_ctrl_type;
extern "C" int get_controller_type() { return g_ctrl_type; }
extern "C" int get_player_count() { return g_players; }
u16 kcode[4];
u32 vks[4];
s8 joyx[4], joyy[4], joyrx[4], joyry[4];
u8 rt[4], lt[4];
void UpdateInputState(u32 port)
{
	kcode[port] = (u16)(0xFFFF ^ (0x0404 << port));
	joyx[port] = (s8)(17 + port);
	joyy[port] = (s8)(-33 - (int)port);
	joyrx[port] = 5;
	joyry[port] = -6;
	rt[port] = (u8)(200 + port);
	lt[port] = (u8)(9 + port);
}
void UpdateLightGunState(u32) {}
void UpdateMaracasState(u32) {}
void UpdateKeyboardState(u32) {}
void UpdateFishingRodState(u32) {}
void InitControllers() {}
void InitLightGun() {}
void InitMaracas() {}
void InitKeyboard() {}
void InitFishingRod() {}
void UpdateVibration(u32, float, float, u32) {}
extern "C" u16 GetLightGunX(int) { return 0; }
extern "C" u16 GetLightGunY(int) { return 0; }
static u32 g_lcd_pushes, g_lcd_bus, g_lcd_port;
static u8 g_lcd_img[48 * 32];
void ps3_vmulcd_push(u32 bus, u32 port, const u8* img)
{
	g_lcd_pushes++;
	g_lcd_bus = bus;
	g_lcd_port = port;
	memcpy(g_lcd_img, img, sizeof(g_lcd_img));
}
void ps3_vmulcd_reset() {}

// The JIT's DMA hook (dc/sh4/rec_v2/driver.cpp): test_dma_hook wants each reply's RAM in one of these ranges.
#include "dc/sh4/rec_v2/jit_dma.h"
static std::vector<std::pair<u32, u32>> g_dma_rep;
void rdv_DmaWrite(u32 addr, u32 size, JitDma who) { g_dma_rep.push_back({ addr, who == JDMA_MAPLE ? size : 0 }); }

// -- The code under test, and the old bus code in its own namespace -------------
#include "dc/maple/maple_helper.cpp"
#include "dc/maple/maple_devs.cpp"
// The old keyboard device has no report on the PS3 (ps3/ps3_input.cpp).
extern "C" const DC_KeyboardReport* GetKeyboardReport(int) { return 0; }
#include "dc/maple/maple_cfg.cpp"
#include "dc/maple/maple_if.cpp"
namespace ref {
#include "ref_maple_if.inc"
}
// Only its inline function is checked: the draw needs the RSX.
#include "ps3/ps3_vmulcd.h"

static int g_fail, g_checks;
#define EXPECT(c, ...) do { g_checks++; if (!(c)) { g_fail++; fprintf(stdout, "FAIL: " __VA_ARGS__); fputc('\n', stdout); } } while (0)

// -- Helpers ---------------------------------------------------------------------
#define TABLE 0x0C010000u
#define DEST  0x0C020000u
static u32 g_seed = 1;
static u32 rnd()
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return g_seed >> 8;
}
static u32& ram(u32 addr) { return g_ram[(addr & (RAM_BYTES - 1)) >> 2]; }
static void rows_off()
{
	g_maple_delay_preset = g_maple_speed_preset = g_maple_cmds_preset = g_maple_ddt_preset = 0;
	g_maple_guard_preset = g_maple_frame_preset = 0;
}
static void vmu_files_clear()
{
	static const char* ids[] = { "", "MK-51035_", "T-8116D_05_" };
	for (const char* id : ids)
		for (int bus = 0; bus < 4; bus++)
			for (int slot = 1; slot <= 2; slot++)
			{
				char path[160];
				snprintf(path, sizeof(path), VMU_DIR "/%svmu_save_%c%d.bin", id, 'A' + bus, slot);
				unlink(path);
			}
}
// New devices with blank cards, the registers at reset, an empty first MB.
static void world_reset(bool cards = true)
{
	mcfg_DestroyDevices();
	if (cards)
	{
		s_vmu_game[0] = 0;
		vmu_files_clear();
	}
	mcfg_CreateDevices();
	memcpy(ref::MapleDevices, MapleDevices, sizeof(MapleDevices));
	memset(g_ram, 0, WIN_BYTES);
	maple_Reset(false);
	ref::maple_Reset(false);
	g_irq.clear();
	g_log.clear();
}

struct Frame { u32 op, bus, plen_extra, dest, cmd, reci, send; std::vector<u32> data; bool wild; };
// The DMA table of the frames at TABLE; swap = each frame word MSB first.
static void table_write(const std::vector<Frame>& fr, bool swap = false)
{
	u32 a = TABLE;
	for (size_t i = 0; i < fr.size(); i++)
	{
		const Frame& f = fr[i];
		const u32 last = i + 1 == fr.size() ? 0x80000000u : 0;
		if (f.op != 0)
		{
			ram(a) = last | (f.bus << 16) | (f.op << 8);
			a += 4;
			continue;
		}
		const u32 words = 1 + (u32)f.data.size() + f.plen_extra;
		ram(a) = last | (f.bus << 16) | (words - 1);
		ram(a + 4) = f.dest;
		u32 w = f.cmd | (f.reci << 8) | (f.send << 16) | ((u32)f.data.size() << 24);
		ram(a + 8) = swap ? __builtin_bswap32(w) : w;
		for (size_t k = 0; k < f.data.size(); k++)
			ram(a + 12 + 4 * (u32)k) = swap ? __builtin_bswap32(f.data[k]) : f.data[k];
		a += 8 + words * 4;
	}
}
static Frame frame(u32 bus, u32 unit, u32 cmd, std::vector<u32> data = {}, u32 dest = DEST)
{
	Frame f = { 0, bus, 0, dest, cmd, (bus << 6) | unit, bus << 6, data, false };
	return f;
}
static void dma_start(bool use_ref = false)
{
	SB_MDSTAR = TABLE;
	SB_MDEN = 1;
	if (use_ref)
		ref::maple_SB_MDST_Write(1);
	else
		maple_SB_MDST_Write(1);
}
// Runs the DMA's time out: the end interrupt.
static void dma_end(bool use_ref = false)
{
	for (int i = 0; i < 100000 && (use_ref ? ref::maple_dma_pending : maple_dma_pending) > 0; i++)
	{
		if (use_ref)
			ref::maple_Update(28672);
		else
			maple_Update(28672);
	}
}
static u32 reply(u32 word, u32 dest = DEST) { return ram(dest + word * 4); }

// A random table: every device and command that the bus can meet, valid or not.
static std::vector<Frame> table_random(bool bad_dest)
{
	static const u32 cmds[] = { 1, 1, 2, 3, 4, 5, 9, 9, 9, 0x0A, 0x0B, 0x0B, 0x0C, 0x0D, 0x0E, 0x55 };
	static const u32 units[] = { 0x20, 0x20, 0x20, 0x01, 0x01, 0x02, 0x04, 0x00, 0x21 };
	static const u32 funcs[] = { 0x01000000, 0x02000000, 0x02000000, 0x04000000, 0x00010000, 0x33 };
	std::vector<Frame> fr;
	const u32 n = 1 + rnd() % 9;
	for (u32 i = 0; i < n; i++)
	{
		Frame f = frame(rnd() % 3, units[rnd() % 9], cmds[rnd() % 16], {}, DEST + i * 0x1000);
		const u32 r = rnd() % 16;
		if (r == 0)
			f.op = 2 + rnd() % 6;
		if (r == 1)
			f.plen_extra = rnd() % 4;
		if (r == 2 && bad_dest)
			f.dest = 0x00030000 + i * 0x1000;
		if (r == 3 && bad_dest)
			f.dest = 0x14040000 + i * 0x1000;
		u32 func = funcs[rnd() % 6];
		switch (f.cmd)
		{
		case 9:
			f.data = { func };
			break;
		case 0x0A:
			f.data = { func, rnd() % 4 == 0 ? 1u : 0u };
			break;
		case 0x0B:
			// No clock read: its reply is the host's time.
			f.data = { func, (rnd() % 256) << 24 };
			break;
		case 0x0C:
			f.data = { func, ((rnd() % 256) << 24) | ((rnd() % 4) << 8) };
			for (u32 k = 0, len = rnd() % 3 ? 32 : rnd() % 48; k < len; k++)
				f.data.push_back(rnd() * 2654435761u);
			break;
		case 0x0E:
			f.data = { func == 0x02000000 ? 0x08000000u : func, rnd() & 0xFF };
			break;
		default:
			for (u32 k = 0, len = rnd() % 3; k < len; k++)
				f.data.push_back(rnd());
		}
		fr.push_back(f);
	}
	return fr;
}

struct Snap { std::vector<u8> ram; s32 pending; u32 mdst; std::vector<int> irq; bool hang; };
static Snap snap(bool use_ref, bool hang)
{
	Snap s;
	s.ram.assign((u8*)g_ram, (u8*)g_ram + WIN_BYTES);
	s.pending = use_ref ? ref::maple_dma_pending : maple_dma_pending;
	s.mdst = SB_MDST;
	s.irq = g_irq;
	s.hang = hang;
	return s;
}
static bool same(const Snap& a, const Snap& b)
{
	return a.ram == b.ram && a.pending == b.pending && a.mdst == b.mdst && a.irq == b.irq && a.hang == b.hang;
}

// -- 1. Every row OFF: the old bus code ------------------------------------------
static void test_old_path(int cases)
{
	int diff = 0;
	for (int c = 0; c < cases && diff < 3; c++)
	{
		const u32 seed = 1000 + c;
		Snap s[2][2];
		for (int use_ref = 0; use_ref < 2; use_ref++)
		{
			g_seed = seed;
			rows_off();
			world_reset();
			table_write(table_random(true));
			const bool vblank = c % 5 == 4;
			bool hang = false;
			try
			{
				if (vblank)
				{
					// The vblank trigger, sometimes with a DMA still in flight (then SB_MDST is set and it runs).
					SB_MDSTAR = TABLE;
					SB_MDEN = 1;
					SB_MDTSEL = 1;
					SB_MDST = c % 10 == 9;
					if (use_ref)
						ref::maple_vblank();
					else
						maple_vblank();
				}
				else
					dma_start(use_ref);
			}
			catch (const VerifyFail&)
			{
				hang = true;
			}
			s[use_ref][0] = snap(use_ref, hang);
			if (c % 7 == 6)
				SB_MDEN = 0;
			dma_end(use_ref);
			s[use_ref][1] = snap(use_ref, hang);
		}
		if (!same(s[0][0], s[1][0]) || !same(s[0][1], s[1][1]))
		{
			diff++;
			fprintf(stdout, "FAIL: rows OFF, table %u: not the old code (ram %d/%d pending %d/%d, %d/%d mdst %u/%u irq %zu/%zu)\n",
			        seed, s[0][0].ram == s[1][0].ram, s[0][1].ram == s[1][1].ram, s[0][0].pending, s[1][0].pending,
			        s[0][1].pending, s[1][1].pending, s[0][1].mdst, s[1][1].mdst, s[0][1].irq.size(), s[1][1].irq.size());
		}
	}
	EXPECT(diff == 0, "rows OFF: %d of the first tables differ from the old code", diff);
}

// -- 2. Maple Reply Delay --------------------------------------------------------
static void test_delay(int cases)
{
	int bad = 0;
	for (int c = 0; c < cases && bad < 3; c++)
	{
		const u32 seed = 5000 + c;
		g_seed = seed;
		rows_off();
		world_reset();
		const std::vector<Frame> fr = table_random(false);
		table_write(fr);
		dma_start();
		dma_end();
		const Snap off = snap(false, false);

		g_seed = seed;
		rows_off();
		g_maple_delay_preset = 1;
		world_reset();
		table_write(fr);
		const std::vector<u8> before((u8*)g_ram, (u8*)g_ram + WIN_BYTES);
		dma_start();
		const bool kept = memcmp(g_ram, before.data(), WIN_BYTES) == 0;
		const bool busy = SB_MDST == 1 && g_irq.empty();
		dma_end();
		const Snap on = snap(false, false);
		if (!kept || !busy || !same(off, on) || s_out_n || s_out_words)
		{
			bad++;
			fprintf(stdout, "FAIL: Reply Delay, table %u: kept %d busy %d same-end %d pool %u\n", seed, kept, busy,
			        same(off, on), s_out_n);
		}
	}
	EXPECT(bad == 0, "Reply Delay: %d tables wrong", bad);

	// An aborted DMA (SB_MDEN cleared before the end) drops its replies: no write, no interrupt.
	rows_off();
	g_maple_delay_preset = 1;
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	SB_MDEN = 0;
	dma_end();
	EXPECT(reply(0) == 0 && g_irq.empty() && SB_MDST == 0 && s_out_n == 0, "Reply Delay: an aborted DMA wrote its reply");
	// SB_MDEN set again before the end: the replies come.
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	SB_MDEN = 0;
	maple_Update(1);
	SB_MDEN = 1;
	dma_end();
	EXPECT(reply(0) != 0 && g_irq.size() == 1, "Reply Delay: a DMA enabled again lost its reply");

	// A reset in the middle of a DMA drops its replies: the end of the next DMA does not write them.
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	maple_Reset(false);
	table_write({ frame(0, 0x20, 1, {}, DEST + 0x1000) });
	dma_start();
	dma_end();
	EXPECT(reply(0) == 0 && reply(0, DEST + 0x1000) == 0x1C230005, "Reply Delay: a reset keeps the waiting replies");

	// More replies than the pool holds: the first ones are written early, and the end state is the same.
	std::vector<Frame> many;
	for (u32 i = 0; i < 200; i++)
		many.push_back(frame(0, 0x01, 0x0B, { 0x02000000, (i & 0xFF) << 24 }, DEST + i * 0x400));
	rows_off();
	world_reset();
	table_write(many);
	dma_start();
	dma_end();
	const Snap off = snap(false, false);
	g_maple_delay_preset = 1;
	world_reset();
	table_write(many);
	dma_start();
	EXPECT(g_log.find("reply pool full") != std::string::npos, "Reply Delay: 200 block reads did not fill the pool");
	dma_end();
	EXPECT(same(off, snap(false, false)), "Reply Delay: a full pool changes the replies");
}

// -- 3. Maple Bus Speed -----------------------------------------------------------
static void test_speed()
{
	// One pad condition: 4 + 3 bytes out, 4 + 12 + 3 bytes back.
	rows_off();
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	EXPECT(maple_dma_pending == 24 * (200000000 / 262144), "Bus Speed OLD: %d cycles", maple_dma_pending);
	g_maple_speed_preset = 1;
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	const s32 want = (s32)(200000000ull * 11 / 250000 + 200000000ull * 19 / 92500);
	EXPECT(maple_dma_pending == want, "Bus Speed REAL: %d cycles, not %d", maple_dma_pending, want);
	// The SH4 Clock row scales it.
	g_check_clock_mhz = 150;
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	EXPECT(maple_dma_pending == (s32)(150000000ull * 11 / 250000 + 150000000ull * 19 / 92500), "Bus Speed REAL at 150 MHz: %d", maple_dma_pending);
	g_check_clock_mhz = 200;
	// A VMU block read: 12 + 3 bytes out, 4 + 8 + 512 + 3 bytes back.
	world_reset();
	table_write({ frame(0, 0x01, 0x0B, { 0x02000000, 0 }) });
	dma_start();
	EXPECT(maple_dma_pending == (s32)(200000000ull * 15 / 250000 + 200000000ull * 527 / 92500), "Bus Speed REAL, block read: %d", maple_dma_pending);
	// No device: no bytes, but the end interrupt still comes. A reset pattern is one byte.
	world_reset();
	table_write({ frame(2, 0x20, 9, { 0x01000000 }) });
	dma_start();
	EXPECT(maple_dma_pending == 1, "Bus Speed REAL, no device: %d", maple_dma_pending);
	dma_end();
	EXPECT(g_irq.size() == 1 && SB_MDST == 0, "Bus Speed REAL, no device: no end interrupt");
	world_reset();
	Frame rst = frame(0, 0, 0);
	rst.op = 3;
	Frame occ = frame(0, 0, 0);
	occ.op = 2;
	Frame occ_empty = frame(3, 0, 0);
	occ_empty.op = 2;
	Frame nop = frame(0, 0, 0);
	nop.op = 7;
	table_write({ rst, occ, occ_empty, nop });
	dma_start();
	EXPECT(maple_dma_pending == 2 * 800, "Bus Speed REAL, patterns: %d", maple_dma_pending);
	// One second at most.
	std::vector<Frame> many;
	for (u32 i = 0; i < 200; i++)
		many.push_back(frame(0, 0x01, 0x0B, { 0x02000000, 0 }, DEST + i * 0x400));
	world_reset();
	table_write(many);
	dma_start();
	EXPECT(maple_dma_pending == 200000000, "Bus Speed REAL, 200 block reads: %d", maple_dma_pending);
}

// -- 4. Maple Commands -----------------------------------------------------------
static u32 one_reply(u32 unit, u32 cmd, std::vector<u32> data = {})
{
	world_reset();
	table_write({ frame(0, unit, cmd, data) });
	dma_start();
	return reply(0);
}
static void test_commands()
{
	rows_off();
	EXPECT((one_reply(0x20, 3) & 0xFF) == 0xFE && (one_reply(0x20, 4) & 0xFF) == 0xFE && (one_reply(0x20, 2) & 0xFF) == 0xFE,
	       "Commands OLD: the pad answers reset, kill or all-status");
	EXPECT((one_reply(0x01, 3) & 0xFF) == 0xFD && (one_reply(0x01, 2) & 0xFF) == 0xFD, "Commands OLD: the VMU answers reset or all-status");
	one_reply(0x01, 0x0A, { 0x04000000, 0 });
	EXPECT(reply(2) == 0x00101F2F, "Commands OLD: LCD media info %08X", reply(2));
	g_maple_cmds_preset = 1;
	EXPECT(one_reply(0x20, 3) == 0x00230007 && one_reply(0x20, 4) == 0x00230007, "Commands FLYCAST: pad reset / kill");
	EXPECT(one_reply(0x01, 3) == 0x00010007 && one_reply(0x01, 4) == 0x00010007, "Commands FLYCAST: VMU reset / kill");
	EXPECT((one_reply(0x20, 0x55) & 0xFF) == 0xFD, "Commands FLYCAST: an unknown pad command is not UnknownCmd");
	// All-status: the device status (28 words) and 80 bytes of text; the request's reply is unchanged.
	const u32 st = one_reply(0x20, 1);
	std::vector<u32> status(&ram(DEST), &ram(DEST) + 29);
	const u32 all = one_reply(0x20, 2);
	EXPECT(st == 0x1C230005 && all == 0x30230006, "Commands FLYCAST: pad status %08X, all-status %08X", st, all);
	EXPECT(memcmp(&status[1], &ram(DEST + 4), 112) == 0, "Commands FLYCAST: the pad's all-status does not start with its status");
	EXPECT(memcmp(&ram(DEST + 116), maple_pad_extra, 80) == 0 && strlen(maple_pad_extra) == 80, "Commands FLYCAST: the pad's text");
	EXPECT(one_reply(0x01, 2) == 0x30010006 && memcmp(&ram(DEST + 116), maple_vmu_extra, 80) == 0 && strlen(maple_vmu_extra) == 80,
	       "Commands FLYCAST: the VMU's all-status");
	EXPECT(one_reply(0x01, 1) == 0x1C010005, "Commands FLYCAST: the VMU's status changed");
	one_reply(0x01, 0x0A, { 0x04000000, 0 });
	EXPECT(reply(2) == 0x02101F2F, "Commands FLYCAST: LCD media info %08X", reply(2));
	// The full controller is the same device.
	g_ctrl_type = 5;
	EXPECT(one_reply(0x20, 3) == 0x00230007 && one_reply(0x20, 2) == 0x30230006, "Commands FLYCAST: the full controller");
	g_ctrl_type = 0;
}

// -- 5. Maple VBlank DMA ---------------------------------------------------------
static bool vblank_run(bool use_ref)
{
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	SB_MDSTAR = TABLE;
	SB_MDEN = 1;
	SB_MDTSEL = 1;
	try
	{
		if (use_ref)
			ref::maple_vblank();
		else
			maple_vblank();
	}
	catch (const VerifyFail&)
	{
		return false;
	}
	return true;
}
static void test_ddt()
{
	rows_off();
	EXPECT(!vblank_run(true) && !vblank_run(false), "VBlank DMA OLD: the vblank DMA does not stop in the verify");
	g_maple_ddt_preset = 1;
	EXPECT(vblank_run(false), "VBlank DMA FIXED: the vblank DMA stops in the verify");
	EXPECT(SB_MDST == 1 && reply(0) == 0x03230008 && maple_dma_pending > 0 && g_irq.empty(), "VBlank DMA FIXED: after the start");
	dma_end();
	EXPECT(SB_MDST == 0 && g_irq.size() == 1 && g_irq[0] == holly_MAPLE_DMA, "VBlank DMA FIXED: the end");
	// The manual trigger reset still waits for SB_MSHTCL.
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	SB_MDSTAR = TABLE;
	SB_MDEN = SB_MDTSEL = 1;
	SB_MSYS |= 1 << 12;
	maple_vblank();
	dma_end();
	ram(DEST) = 0;
	maple_vblank();
	EXPECT(reply(0) == 0 && SB_MDST == 0, "VBlank DMA FIXED: a second DMA before the trigger reset");
	maple_SB_MSHTCL_Write(1);
	maple_vblank();
	EXPECT(reply(0) == 0x03230008, "VBlank DMA FIXED: no DMA after the trigger reset");
	// The vblank does nothing without the trigger select or the enable.
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	SB_MDSTAR = TABLE;
	SB_MDEN = 1;
	maple_vblank();
	SB_MDEN = 0;
	SB_MDTSEL = 1;
	maple_vblank();
	EXPECT(reply(0) == 0 && SB_MDST == 0, "VBlank DMA FIXED: a DMA without its trigger");
}

// -- 6. Maple Guards -------------------------------------------------------------
static void test_guards()
{
	const u32 outside = 0x14020000;   // area 5; the old code writes at 0x0C020000
	for (int delay = 0; delay < 2; delay++)
	{
		rows_off();
		g_maple_delay_preset = delay;
		world_reset();
		table_write({ frame(0, 0x20, 9, { 0x01000000 }, outside), frame(0, 0x20, 1, {}, DEST + 0x1000) });
		dma_start();
		dma_end();
		EXPECT(reply(0) == 0x03230008 && g_irq.size() == 1, "Guards OFF (delay %d): the masked address is not written", delay);
		g_maple_guard_preset = 1;
		world_reset();
		table_write({ frame(0, 0x20, 9, { 0x01000000 }, outside), frame(0, 0x20, 1, {}, DEST + 0x1000) });
		dma_start();
		EXPECT(g_irq.empty(), "Guards ON (delay %d): an interrupt before the end", delay);
		dma_end();
		EXPECT(reply(0) == 0 && reply(0, DEST + 0x1000) == 0x1C230005, "Guards ON (delay %d): the replies", delay);
		EXPECT(g_irq.size() == 2 && g_irq[0] == holly_MAPLE_OVERRUN && g_irq[1] == holly_MAPLE_DMA && SB_MDST == 0,
		       "Guards ON (delay %d): the overrun interrupt, then the end", delay);
		// The next DMA has no overrun left.
		g_irq.clear();
		table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
		dma_start();
		dma_end();
		EXPECT(g_irq.size() == 1 && g_irq[0] == holly_MAPLE_DMA, "Guards ON (delay %d): the overrun stays for the next DMA", delay);
		// A reply to a device that is not there has the same rule.
		world_reset();
		table_write({ frame(3, 0x20, 1, {}, outside) });
		dma_start();
		dma_end();
		EXPECT(reply(0) == 0 && g_irq.size() == 2 && g_irq[0] == holly_MAPLE_OVERRUN, "Guards ON (delay %d): no device", delay);
	}
	// A command table outside RAM: the DMA stops (the old code reads a null pointer there).
	rows_off();
	g_maple_guard_preset = 1;
	g_maple_delay_preset = 1;
	world_reset();
	table_write({ frame(0, 0x20, 9, { 0x01000000 }) });
	dma_start();
	EXPECT(s_out_n == 1, "Guards: no reply waits");
	SB_MDSTAR = 0x10000000;
	maple_SB_MDST_Write(1);
	EXPECT(SB_MDST == 0 && s_out_n == 0 && g_log.find("COMMAND TABLE NOT ON SH4 RAM") != std::string::npos,
	       "Guards ON: a table outside RAM does not stop the DMA");
}

// -- 7. Maple Frame Rules --------------------------------------------------------
static void test_frames()
{
	const std::vector<Frame> fr = { frame(0, 0x20, 9, { 0x01000000 }), frame(0, 0x01, 0x0B, { 0x02000000, 0xFF000000 }, DEST + 0x1000),
	                                frame(2, 0x20, 1, {}, DEST + 0x2000) };
	for (int delay = 0; delay < 2; delay++)
	{
		rows_off();
		g_maple_delay_preset = delay;
		world_reset();
		table_write(fr);
		dma_start();
		dma_end();
		const std::vector<u32> normal(&ram(DEST), &ram(DEST) + 0x3000 / 4);
		// SB_MMSEL 1 (the reset value): the row changes nothing.
		g_maple_frame_preset = 1;
		world_reset();
		table_write(fr);
		dma_start();
		dma_end();
		EXPECT(memcmp(normal.data(), &ram(DEST), 0x3000) == 0, "Frame Rules (delay %d): SB_MMSEL 1 changes the replies", delay);
		// SB_MMSEL 0: the table's frames and the replies have each word swapped.
		world_reset();
		SB_MMSEL = 0;
		table_write(fr, true);
		dma_start();
		dma_end();
		bool ok = true;
		for (u32 i = 0; i < 0x3000 / 4; i++)
			ok = ok && ram(DEST + i * 4) == __builtin_bswap32(normal[i]);
		EXPECT(ok && normal[0] == 0x03230008 && normal[0x400] == 0x82010008 && normal[0x800] == 0xFFFFFFFF,
		       "Frame Rules (delay %d): SB_MMSEL 0 does not swap the replies", delay);
		// With the row OFF, SB_MMSEL 0 is ignored: a normal table gives the normal replies.
		g_maple_frame_preset = 0;
		world_reset();
		SB_MMSEL = 0;
		table_write(fr);
		dma_start();
		dma_end();
		EXPECT(memcmp(normal.data(), &ram(DEST), 0x3000) == 0, "Frame Rules OFF (delay %d): SB_MMSEL 0 acts", delay);
	}
	// An address with no unit bit: sub-unit 1 (the VMU) before, the main unit with the row.
	rows_off();
	EXPECT(one_reply(0x00, 1) == 0x1C000005 && reply(1) == 0x0E000000, "Frame Rules OFF: no unit bit is not the VMU");
	g_maple_frame_preset = 1;
	EXPECT(one_reply(0x00, 1) == 0x1C000005 && reply(1) == 0x01000000, "Frame Rules FLYCAST: no unit bit is not the pad");
	EXPECT(one_reply(0x01, 1) == 0x1C010005 && reply(1) == 0x0E000000 && one_reply(0x20, 1) == 0x1C230005,
	       "Frame Rules FLYCAST: a unit bit changed its device");
}

// -- 8. VMU Per Game, VMU Screen, the devices ----------------------------------
static std::string vmu_name(const char* port, const char* id, int level)
{
	char out[64];
	vmu_file_name(out, sizeof(out), port, id, level);
	return out;
}
static bool file_there(const char* name)
{
	char path[160];
	snprintf(path, sizeof(path), VMU_DIR "/%s", name);
	return access(path, F_OK) == 0;
}
static void test_vmu()
{
	EXPECT(vmu_name("A1", "MK-51035  ", 0) == "vmu_save_A1.bin" && vmu_name("B2", "MK-51035  ", 0) == "vmu_save_B2.bin", "VMU names: OFF");
	EXPECT(vmu_name("A1", "MK-51035  ", 1) == "MK-51035_vmu_save_A1.bin", "VMU names: A1 %s", vmu_name("A1", "MK-51035  ", 1).c_str());
	EXPECT(vmu_name("A2", "MK-51035  ", 1) == "vmu_save_A2.bin" && vmu_name("B1", "MK-51035  ", 1) == "vmu_save_B1.bin", "VMU names: SLOT A1 changes another slot");
	EXPECT(vmu_name("A2", "MK-51035  ", 2) == "MK-51035_vmu_save_A2.bin" && vmu_name("D1", "MK-51035  ", 2) == "MK-51035_vmu_save_D1.bin", "VMU names: ALL SLOTS");
	EXPECT(vmu_name("A1", "T-8116D 05", 1) == "T-8116D_05_vmu_save_A1.bin", "VMU names: a blank inside");
	EXPECT(vmu_name("A1", "A/B\\C:*?|<", 1) == "A_B_C______vmu_save_A1.bin", "VMU names: %s", vmu_name("A1", "A/B\\C:*?|<", 1).c_str());
	EXPECT(vmu_name("A1", "", 1) == "vmu_save_A1.bin" && vmu_name("A1", "          ", 2) == "vmu_save_A1.bin" && vmu_name("A1", 0, 2) == "vmu_save_A1.bin", "VMU names: no disc ID");
	EXPECT(vmu_name("A1", "T1234\x01\xFFNMORETEXT", 1) == "T1234__NMO_vmu_save_A1.bin", "VMU names: 10 bytes, no control bytes: %s", vmu_name("A1", "T1234\x01\xFFNMORETEXT", 1).c_str());

	// The devices. OFF: the old files, and a disc ID changes nothing.
	rows_off();
	g_vmu_per_game_preset = 0;
	world_reset();
	EXPECT(file_there("vmu_save_A1.bin") && file_there("vmu_save_A2.bin"), "VMU Per Game OFF: the old files are not there");
	maple_vmu_game("MK-51035  ");
	EXPECT(!file_there("MK-51035_vmu_save_A1.bin"), "VMU Per Game OFF: a file per game");
	// SLOT A1: a block written after the disc is known goes to the game's file; the shared file keeps its bytes.
	g_vmu_per_game_preset = 1;
	world_reset();
	std::vector<u32> wr = { 0x02000000, 0x05000000 };
	for (u32 k = 0; k < 32; k++)
		wr.push_back(0xA5A50000 + k);
	one_reply(0x01, 0x0C, wr);
	maple_vmu_game("MK-51035  ");
	EXPECT(file_there("MK-51035_vmu_save_A1.bin") && !file_there("MK-51035_vmu_save_A2.bin"), "VMU Per Game SLOT A1: the files");
	table_write({ frame(0, 0x01, 0x0B, { 0x02000000, 0x05000000 }) });
	dma_start();
	EXPECT(reply(0) == 0x82010008 && reply(3) == 0, "VMU Per Game SLOT A1: the game's card is not blank");
	wr[2] = 0x12345678;
	table_write({ frame(0, 0x01, 0x0C, wr) });
	dma_start();
	// The same disc again (a second device set, as at each boot): the card is the game's, with its block.
	mcfg_DestroyDevices();
	mcfg_CreateDevices();
	table_write({ frame(0, 0x01, 0x0B, { 0x02000000, 0x05000000 }) });
	dma_start();
	EXPECT(reply(3) == 0x12345678, "VMU Per Game SLOT A1: the game's block is %08X", reply(3));
	// No disc: the shared card, with the first block.
	maple_vmu_game("");
	table_write({ frame(0, 0x01, 0x0B, { 0x02000000, 0x05000000 }) });
	dma_start();
	EXPECT(reply(3) == 0xA5A50000, "VMU Per Game SLOT A1: the shared block is %08X", reply(3));
	// ALL SLOTS.
	g_vmu_per_game_preset = 2;
	maple_vmu_game("T-8116D 05");
	EXPECT(file_there("T-8116D_05_vmu_save_A1.bin") && file_there("T-8116D_05_vmu_save_A2.bin"), "VMU Per Game ALL SLOTS: the files");
	g_vmu_per_game_preset = 0;

	// VMU Screen: an LCD write gives the decoded picture with its slot; a lit dot is 0.
	world_reset();
	std::vector<u32> lcd = { 0x04000000, 0 };
	for (u32 k = 0; k < 48; k++)
		lcd.push_back(0);
	lcd[2] = 0x00000080;   // row 0, byte 0, bit 7: the first dot of the raw picture
	g_lcd_pushes = 0;
	EXPECT(one_reply(0x02, 0x0C, lcd) == 0x00020007 && g_lcd_pushes == 1 && g_lcd_bus == 0 && g_lcd_port == 1, "VMU Screen: no picture from slot 2");
	u32 lit = 0, at = 0;
	for (u32 i = 0; i < 48 * 32; i++)
		if (!g_lcd_img[i])
		{
			lit++;
			at = i;
		}
	// The raw picture's first dot is the last dot of the TV's last row (the 180 degree turn).
	EXPECT(lit == 1 && at == 47 && vmulcd_src(47, 31) == 47 && vmulcd_src(0, 0) == 31 * 48, "VMU Screen: dot %u of %u", at, lit);
}

// The ports are the old ones: a pad and two VMUs for each player, nothing on the other ports.
static void test_devices()
{
	rows_off();
	for (g_players = 1; g_players <= 4; g_players++)
	{
		world_reset();
		bool ok = true;
		for (int bus = 0; bus < 4; bus++)
			for (int port : { 5, 0, 1 })
				ok = ok && (MapleDevices[bus][port] != 0) == (bus < g_players);
		EXPECT(ok, "devices: Players %d", g_players);
	}
	g_players = 1;
}

// -- 9. The JIT's DMA hook -------------------------------------------------------
// The words of the first MB that changed since before and that no report covers.
static u32 dma_unreported(const std::vector<u8>& before)
{
	u32 n = 0;
	for (u32 a = 0; a < WIN_BYTES; a += 4)
	{
		if (!memcmp(&before[a], (const u8*)g_ram + a, 4))
			continue;
		bool in = false;
		for (const auto& r : g_dma_rep)
			in = in || (((r.first >> 26) & 7) == 3 && a - (r.first & (RAM_BYTES - 1)) < r.second);
		n += !in;
	}
	return n;
}
// Random tables with the rows that change where a reply goes: each RAM word a DMA writes must reach the JIT.
static void test_dma_hook(int cases)
{
	int bad = 0;
	for (int c = 0; c < cases && bad < 3; c++)
	{
		g_seed = 9000 + c;
		rows_off();
		g_maple_delay_preset = c & 1;
		g_maple_guard_preset = (c >> 1) & 1;
		g_maple_frame_preset = (c >> 2) & 1;
		world_reset();
		SB_MMSEL = (c >> 3) & 1;
		table_write(table_random(true), g_maple_frame_preset && !SB_MMSEL);
		u32 miss = 0;
		for (int end = 0; end < 2; end++)
		{
			const std::vector<u8> before((u8*)g_ram, (u8*)g_ram + WIN_BYTES);
			g_dma_rep.clear();
			try
			{
				if (end)
					dma_end();
				else
					dma_start();
			}
			catch (const VerifyFail&)
			{
			}
			miss += dma_unreported(before);
		}
		if (miss)
		{
			bad++;
			fprintf(stdout, "FAIL: DMA hook, table %d: %u RAM words with no report\n", 9000 + c, miss);
		}
	}
	EXPECT(bad == 0, "DMA hook: %d tables wrote RAM the JIT was not told of", bad);
	rows_off();
}

int main(int argc, char** argv)
{
	const int cases = argc > 1 ? atoi(argv[1]) : 150;
	g_verbose = argc > 2;
	mkdir(VMU_DIR, 0777);
	maple_Init();
	EXPECT(sb_regs[SB_MDST_addr >> 2].writeFunction == maple_SB_MDST_Write, "maple_Init: no SB_MDST handler");
	test_old_path(cases);
	test_delay(cases);
	test_speed();
	test_commands();
	test_ddt();
	test_guards();
	test_frames();
	test_vmu();
	test_devices();
	test_dma_hook(cases);
	mcfg_DestroyDevices();
	fprintf(stdout, "%s: maple_check, %d checks, %d random tables\n", g_fail ? "FAIL" : "PASS", g_checks, cases * 2);
	return g_fail ? 1 : 0;
}
