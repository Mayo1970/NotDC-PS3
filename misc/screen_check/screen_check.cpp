// Plan section 7, bundle 12, PC check: the real ps3/ps3_screen.h and ps3/ps3_cheats.h against Flycast's code (written
// here in Flycast's own form) and against the rules of the plan.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "ps3/ps3_screen.h"
#include "ps3/ps3_cheats.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 12) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u32 g_seed = 12345;
static u32 rnd(u32 n)
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return (u32)(((unsigned long long)(g_seed >> 4) * n) >> 28);
}

// ---- the screen format -----------------------------------------------------------

// The display column of canvas column x.
static float col(const ScreenView& v, float x)
{
	return (float)v.x + (x - v.cx0) * (float)v.w / v.cw;
}

// Flycast TransformMatrix::CalcMatrices with rend.WideScreen: the display column of canvas column x.
static float fly_wide_col(float dc_w, float dc_h, float out_w, float out_h, float x)
{
	float dcViewport_x = dc_w, dcViewport_y = dc_h;
	const float widescreenShift = 1.f - dcViewport_x / dcViewport_y * out_h / out_w;
	dcViewport_x *= 4.f / 3.f;
	const float x_coef = 2.0f / dcViewport_x;
	const float ndc = -1 + widescreenShift + x * x_coef;
	return (ndc + 1.f) * out_w / 2;
}

static void test_view()
{
	ScreenView v;
	screen_view(1280, 720, SCREEN_43, 640.0f, &v);
	CHECK(v.x == 160 && v.w == 960 && v.cx0 == 0.0f && v.cw == 640.0f, "4:3 at 720p: %d %d %g %g", v.x, v.w, v.cx0, v.cw);
	screen_view(1280, 720, SCREEN_STRETCH, 640.0f, &v);
	CHECK(v.x == 0 && v.w == 1280 && v.cx0 == 0.0f && v.cw == 640.0f, "STRETCH at 720p: %d %d %g %g", v.x, v.w, v.cx0, v.cw);
	screen_view(1280, 720, SCREEN_WIDE, 640.0f, &v);
	CHECK(v.x == 0 && v.w == 1280 && fabsf(v.cx0 + 106.6667f) < 0.001f && fabsf(v.cw - 853.3333f) < 0.001f,
	      "WIDE at 720p: %d %d %g %g", v.x, v.w, v.cx0, v.cw);
	// A value the row does not have is the old path.
	screen_view(1280, 720, 7, 640.0f, &v);
	CHECK(v.x == 160 && v.w == 960 && v.cx0 == 0.0f && v.cw == 640.0f, "format 7: %d %d %g %g", v.x, v.w, v.cx0, v.cw);
	// A 4:3 display has no side to fill.
	for (int f = SCREEN_43; f <= SCREEN_WIDE; f++)
	{
		screen_view(640, 480, f, 640.0f, &v);
		CHECK(v.x == 0 && v.w == 640 && v.cx0 == 0.0f && v.cw == 640.0f, "4:3 display, format %d: %d %d %g %g", f, v.x,
		      v.w, v.cx0, v.cw);
	}
	static const u32 disp[][2] = { { 1280, 720 }, { 1920, 1080 }, { 720, 480 }, { 720, 576 }, { 1024, 768 }, { 1366, 768 } };
	static const float canvas[] = { 640.0f, 1280.0f, 320.0f };
	for (u32 d = 0; d < sizeof(disp) / sizeof(disp[0]); d++)
		for (u32 c = 0; c < 3; c++)
		{
			const u32 sw = disp[d][0], sh = disp[d][1];
			const float cw = canvas[c];
			ScreenView a, s, w;
			screen_view(sw, sh, SCREEN_43, cw, &a);
			screen_view(sw, sh, SCREEN_STRETCH, cw, &s);
			screen_view(sw, sh, SCREEN_WIDE, cw, &w);
			// The 4:3 picture is in the middle and no wider than the display.
			CHECK(a.x >= 0 && a.w > 0 && a.x * 2 + a.w <= (int)sw && (int)sw - (a.x * 2 + a.w) <= 1,
			      "%ux%u: the 4:3 picture is not in the middle: %d %d", sw, sh, a.x, a.w);
			CHECK(a.w == (int)std::min(sh * 4 / 3, sw), "%ux%u: the 4:3 width is %d", sw, sh, a.w);
			CHECK(s.x == 0 && s.w == (int)sw && s.cx0 == 0.0f && s.cw == cw, "%ux%u: STRETCH is not the whole display", sw, sh);
			CHECK(w.x == 0 && w.w == (int)sw, "%ux%u: WIDE is not the whole display", sw, sh);
			// WIDE keeps each canvas column of the 4:3 picture at its display column, and ends at the display's edges.
			for (float x = 0.0f; x <= cw; x += cw / 8.0f)
				CHECK(fabsf(col(w, x) - col(a, x)) < 0.01f, "%ux%u canvas %g: column %g is at %g in WIDE, %g in 4:3", sw, sh, cw, x,
				      col(w, x), col(a, x));
			CHECK(fabsf(col(w, w.cx0)) < 0.01f && fabsf(col(w, w.cx0 + w.cw) - (float)sw) < 0.01f,
			      "%ux%u: the WIDE canvas does not end at the display's edges", sw, sh);
			CHECK(fabsf(w.cx0 * 2.0f + w.cw - cw) < 0.01f, "%ux%u: the WIDE canvas is not centred", sw, sh);
		}
	// Flycast's matrix on a 16:9 display, for canvases of the 4:3 shape.
	static const float dc[][2] = { { 640, 480 }, { 320, 240 }, { 1280, 960 } };
	for (u32 k = 0; k < 3; k++)
	{
		screen_view(1280, 720, SCREEN_WIDE, dc[k][0], &v);
		for (float x = -100.0f; x <= dc[k][0] + 100.0f; x += 37.0f)
			CHECK(fabsf(col(v, x) - fly_wide_col(dc[k][0], dc[k][1], 1280, 720, x)) < 0.01f,
			      "canvas %g: column %g is at %g, Flycast has %g", dc[k][0], x, col(v, x),
			      fly_wide_col(dc[k][0], dc[k][1], 1280, 720, x));
	}
}

// Flycast TransformMatrix::isClipped with intersectTileAndFBScissor and getScissorScaling, the x parts.
static bool fly_is_clipped(int fb_min, int fb_max, int tile_origin, int tile_size, int glob_x, bool hscale)
{
	const int fbClip_origin = fb_min, fbClip_size = fb_max - fb_min + 1;
	float xscale = 1.f;
	if (hscale)
		xscale *= 2.f;
	int botRight = fbClip_origin + fbClip_size;
	const int origin = std::max((int)std::round(fbClip_origin * xscale), tile_origin);
	botRight = std::min((int)std::round(botRight * xscale), tile_origin + tile_size);
	const int size = std::max(botRight - origin, 0);
	const int width = glob_x;
	return origin > 0 || origin + size - 1 < width - 1;
}

static void test_clipped()
{
	// A full 640 screen; a split screen's halves; a 1280 TA with the scaler.
	CHECK(!screen_x_clipped(0, 639, 0, 640, 640, 0), "a full screen is clipped");
	CHECK(screen_x_clipped(0, 319, 0, 640, 640, 0), "the left half is not clipped");
	CHECK(screen_x_clipped(320, 639, 0, 640, 640, 0), "the right half is not clipped");
	CHECK(screen_x_clipped(0, 639, 0, 320, 640, 0), "tiles of the left half are not clipped");
	CHECK(screen_x_clipped(0, 639, 32, 640, 640, 0), "tiles from 32 are not clipped");
	CHECK(!screen_x_clipped(0, 639, 0, 1280, 1280, 0x10400), "a full 1280 TA with hscale is clipped");
	CHECK(screen_x_clipped(0, 639, 0, 1280, 1280, 0x00400), "a 640 clip on a 1280 TA with no hscale is not clipped");
	for (u32 i = 0; i < 200000; i++)
	{
		const u32 glob = (1 + rnd(40)) * 32;
		const u32 t0 = rnd(4) ? 0 : rnd(glob / 32) * 32, t1 = rnd(4) ? glob : t0 + rnd((glob - t0) / 32 + 1) * 32;
		const u32 f0 = rnd(3) ? 0 : rnd(700), f1 = rnd(3) ? (rnd(2) ? 639 : glob - 1) : f0 + rnd(1300);
		const bool hs = rnd(4) == 0;
		const u32 ctl = (hs ? 0x10000u : 0u) | (rnd(2) ? 0x400 : rnd(0x10000));
		const bool got = screen_x_clipped(f0, f1, t0, t1, glob, ctl);
		const bool want = fly_is_clipped((int)f0, (int)f1, (int)t0, (int)(t1 - t0), (int)glob, hs);
		CHECK(got == want, "clip fb %u..%u tiles %u..%u glob %u hscale %d: %d, Flycast has %d", f0, f1, t0, t1, glob, (int)hs,
		      (int)got, (int)want);
	}
}

// Flycast getPvrFramebufferSize, the height of a render to the screen (height: the TA's global clip).
static int fly_fb_height(bool vclk_div, bool interlace, u32 vscalefactor, bool line_double, int height)
{
	int maxHeight = !vclk_div && !interlace ? 240 : 480;
	if (vscalefactor != 0 && (vscalefactor > 1025 || vscalefactor < 1024) && !interlace)
		maxHeight /= 1024.f / vscalefactor;
	if (line_double)
		maxHeight /= 2;
	return std::min(maxHeight, height);
}

static void test_scaler()
{
	static const u32 vs_list[] = { 0, 1, 256, 512, 1023, 1024, 1025, 1026, 1536, 2048, 4096, 0xFFFF };
	for (u32 k = 0; k < sizeof(vs_list) / sizeof(vs_list[0]); k++)
		for (u32 m = 0; m < 16; m++)
		{
			const u32 vs = vs_list[k];
			const bool interlace = m & 1, vga = m & 2, pd = m & 4, hs = m & 8;
			// SPG.cpp's scales: a 15 kHz mode with no interlace has 240 lines.
			float sx = hs ? 2.0f : 1.0f, sy = (!interlace && !vga) ? 0.5f : 1.0f;
			const float sx0 = sx, sy0 = sy;
			screen_scaler(vs | (hs ? 0x10000u : 0u) | 0x20000u, pd ? 0x100u : 0x8u, interlace, &sx, &sy);
			const int want = fly_fb_height(vga, interlace, vs, false, 100000);
			CHECK(fabsf(480.0f * sy - (float)want) < 1.0f, "vscale %u interlace %d vga %d: %g lines, Flycast has %d", vs,
			      (int)interlace, (int)vga, (double)(480.0f * sy), want);
			if (interlace || vs == 0 || vs == 1024 || vs == 1025)
				CHECK(sy == sy0, "vscale %u interlace %d: the height factor moved from %g to %g", vs, (int)interlace,
				      (double)sy0, (double)sy);
			CHECK(sx == (pd ? sx0 * 0.5f : sx0), "pixel double %d: the width factor went from %g to %g", (int)pd, (double)sx0,
			      (double)sx);
		}
	// The reset value of VO_CONTROL (0x108) has the pixel-double bit; a game's 640 mode (0) does not.
	float sx = 1.0f, sy = 1.0f;
	screen_scaler(0x400, 0x108, true, &sx, &sy);
	CHECK(sx == 0.5f && sy == 1.0f, "VO_CONTROL 0x108: %g %g", (double)sx, (double)sy);
	sx = sy = 1.0f;
	screen_scaler(0x400, 0, false, &sx, &sy);
	CHECK(sx == 1.0f && sy == 1.0f, "no scaling: %g %g", (double)sx, (double)sy);
	// Flycast's own case: a 480-line render into a 240-line screen (factor 2).
	sx = 1.0f; sy = 0.5f;
	screen_scaler(0x800, 0, false, &sx, &sy);
	CHECK(sx == 1.0f && sy == 1.0f, "factor 2 at 240 lines: %g %g", (double)sx, (double)sy);
}

// ---- the FPS text and the screenshot rows ------------------------------------------

static void test_osd()
{
	char l[OSD_LINES][OSD_LINE_LEN];
	u32 n = osd_split("FPS: 59.94\nSPEED: 100.00%", l);
	CHECK(n == 4 && !strcmp(l[0], "FPS") && !strcmp(l[1], "59.94") && !strcmp(l[2], "SPEED") && !strcmp(l[3], "100.00%"),
	      "SPG's text: %u lines: %s / %s / %s / %s", n, l[0], l[1], l[2], l[3]);
	CHECK(osd_split("", l) == 0, "an empty text has lines");
	n = osd_split("no label", l);
	CHECK(n == 1 && !strcmp(l[0], "no label"), "a line with no label: %u %s", n, l[0]);
	n = osd_split("A: \nB:C\n\nD: E: F", l);
	CHECK(n == 4 && !strcmp(l[0], "A") && !strcmp(l[1], "B:C") && !strcmp(l[2], "D") && !strcmp(l[3], "E: F"),
	      "empty parts: %u lines: %s / %s / %s / %s", n, l[0], l[1], l[2], l[3]);
	n = osd_split("1: 2\n3: 4\n5: 6", l);
	CHECK(n == 4 && !strcmp(l[3], "4"), "more than %d lines: %u, last %s", OSD_LINES, n, l[3]);
	n = osd_split("0123456789012345678901234567890123456789: x", l);
	CHECK(n == 2 && strlen(l[0]) == OSD_LINE_LEN - 1 && !strncmp(l[0], "01234567890123456789012", OSD_LINE_LEN - 1) &&
	      !strcmp(l[1], "x"), "a long label: %u %s / %s", n, l[0], l[1]);
	// Never a write past a line, whatever the text.
	for (u32 i = 0; i < 20000; i++)
	{
		char t[80];
		const u32 len = rnd(79);
		for (u32 k = 0; k < len; k++)
			t[k] = "ab:  \n%9"[rnd(9)];
		t[len] = 0;
		struct { char guard0[8]; char l[OSD_LINES][OSD_LINE_LEN]; char guard1[8]; } b;
		memset(&b, 0x55, sizeof(b));
		const u32 c = osd_split(t, b.l);
		bool ok = c <= OSD_LINES;
		for (u32 k = 0; k < 8; k++)
			ok = ok && b.guard0[k] == 0x55 && b.guard1[k] == 0x55;
		for (u32 k = 0; k < c && k < OSD_LINES; k++)
			ok = ok && memchr(b.l[k], 0, OSD_LINE_LEN) != NULL && b.l[k][0] != 0;
		CHECK(ok, "a random text broke the lines");
	}
}

static void test_shot()
{
	for (u32 i = 0; i < 2000; i++)
	{
		u32 px[5];
		u8 want[15], got[17];
		for (u32 k = 0; k < 5; k++)
		{
			px[k] = (rnd(0x10000) << 16) | rnd(0x10000);
			// On the PS3 the word is the bytes A, R, G, B: the PNG takes bytes 1 to 3.
			const u8 be[4] = { (u8)(px[k] >> 24), (u8)(px[k] >> 16), (u8)(px[k] >> 8), (u8)px[k] };
			memcpy(want + k * 3, be + 1, 3);
		}
		memset(got, 0xA5, sizeof(got));
		shot_row_rgb(px, 5, got + 1);
		CHECK(!memcmp(got + 1, want, 15) && got[0] == 0xA5 && got[16] == 0xA5, "a row of pixels: %08X is %02X %02X %02X", px[0],
		      got[1], got[2], got[3]);
	}
}

// ---- the cheats: Flycast's code -----------------------------------------------------

// Flycast IniFile (cfg/ini.cpp), the parts loadCheatFile uses.
struct FlyIni
{
	std::map<std::string, std::map<std::string, std::string>> sections;

	static std::string trim_ws(const std::string& str, const std::string& ws)
	{
		const size_t a = str.find_first_not_of(ws);
		if (a == str.npos)
			return "";
		return str.substr(a, str.find_last_not_of(ws) - a + 1);
	}

	void load(const std::string& data)
	{
		std::istringstream fss(data);
		std::string curSection;
		for (std::string line; std::getline(fss, line);)
		{
			size_t pos = 0;
			while (pos < line.length() && std::isspace((signed char)line[pos]))
				pos++;
			if (pos == line.length() || line[pos] == ';')
				continue;
			if (line[pos] == '[')
			{
				size_t end = line.find(']', ++pos);
				if (end == line.npos)
					continue;
				curSection = line.substr(pos, end - pos);
			}
			else
			{
				pos = line.find('=', pos);
				if (pos == line.npos)
					continue;
				std::string entry = trim_ws(line.substr(0, pos), " \t");
				if (entry.empty())
					continue;
				std::string value = line.substr(pos + 1);
				if (!value.empty() && value[0] == ' ')
					value = value.substr(1);
				if (!value.empty() && value[0] == '"')
				{
					size_t end = value.rfind('"');
					if (end == value.npos || end == 0)
						end = value.length();
					value = value.substr(1, end - 1);
				}
				sections[curSection][entry] = value;
			}
		}
	}

	const std::string* getRaw(const std::string& section, const std::string& entry) const
	{
		auto s = sections.find(section);
		if (s == sections.end())
			return nullptr;
		auto e = s->second.find(entry);
		return e == s->second.end() ? nullptr : &e->second;
	}

	static bool hasHexPrefix(const std::string& s)
	{
		size_t pos = 0;
		while (pos < s.length() - 1 && std::isspace((signed char)s[pos]))
			pos++;
		if (pos + 1 >= s.length())
			return false;
		return (s[pos] == '0' && (s[pos + 1] == 'x' || s[pos + 1] == 'X'));
	}

	int getInt(const std::string& section, const std::string& entry, int defaultValue) const
	{
		const std::string* pValue = getRaw(section, entry);
		if (pValue == nullptr)
			return defaultValue;
		std::istringstream ss(*pValue);
		ss.imbue(std::locale::classic());
		if (hasHexPrefix(*pValue))
			ss.setf(std::ios_base::hex, std::ios_base::basefield);
		else
			ss.setf(std::ios_base::dec, std::ios_base::basefield);
		unsigned value = defaultValue;
		ss >> value;
		return (int)value;
	}

	bool getBool(const std::string& section, const std::string& entry, bool defaultValue) const
	{
		const std::string* pValue = getRaw(section, entry);
		if (pValue == nullptr)
			return defaultValue;
		return !strcasecmp("yes", pValue->c_str()) || !strcasecmp("true", pValue->c_str()) ||
		       !strcasecmp("on", pValue->c_str()) || !strcasecmp("1", pValue->c_str());
	}
};

// Flycast cheats.h.
struct FlyCheat
{
	enum class Type { disabled, setValue, increase, decrease, runNextIfEq, runNextIfNeq, runNextIfGt, runNextIfLt, copy };
	Type type = Type::disabled;
	bool enabled = false;
	u32 size = 0;
	u32 address = 0;
	u32 value = 0;
	u8 valueMask = 0;
	u32 repeatCount = 1;
	u32 repeatValueIncrement = 0;
	u32 repeatAddressIncrement = 0;
	u32 destAddress = 0;
};

#define FLY_RAM_SIZE 0x1000000u

// Flycast CheatManager::loadCheatFile.
static std::vector<FlyCheat> fly_load(const std::string& data)
{
	FlyIni cfg;
	cfg.load(data);
	std::vector<FlyCheat> cheats;
	int count = cfg.getInt("", "cheats", 0);
	for (int i = 0; count == 0 || i < count; i++)
	{
		std::string prefix = "cheat" + std::to_string(i) + "_";
		FlyCheat cheat{};
		cheat.address = cfg.getInt("", prefix + "address", -1);
		if (count == 0 && cheat.address == (u32)-1)
			break;
		if (cheat.address >= FLY_RAM_SIZE)
			continue;
		cheat.type = (FlyCheat::Type)cfg.getInt("", prefix + "cheat_type", (int)FlyCheat::Type::disabled);
		cheat.size = 1 << cfg.getInt("", prefix + "memory_search_size", 0);
		cheat.value = cfg.getInt("", prefix + "value", cheat.value);
		cheat.repeatCount = cfg.getInt("", prefix + "repeat_count", cheat.repeatCount);
		cheat.repeatValueIncrement = cfg.getInt("", prefix + "repeat_add_to_value", cheat.repeatValueIncrement);
		cheat.repeatAddressIncrement = cfg.getInt("", prefix + "repeat_add_to_address", cheat.repeatAddressIncrement);
		cheat.enabled = cfg.getBool("", prefix + "enable", false);
		cheat.destAddress = cfg.getInt("", prefix + "dest_address", 0);
		if (cheat.destAddress >= FLY_RAM_SIZE)
			continue;
		cheat.valueMask = cfg.getInt("", prefix + "address_bit_position", 0);
		if (cheat.type != FlyCheat::Type::disabled)
			cheats.push_back(cheat);
	}
	return cheats;
}

// A guest RAM of 64 KB for both sides, little-endian as the Dreamcast's; every write is noted.
#define TEST_RAM 0x10000u
static u8 g_ram[TEST_RAM];
static std::vector<u32> g_log;

static u32 readRam(u32 addr, u32 bits)
{
	const u8* p = g_ram;
	const u32 a = addr & (TEST_RAM - 1), b = (addr + 1) & (TEST_RAM - 1), c = (addr + 2) & (TEST_RAM - 1),
	          d = (addr + 3) & (TEST_RAM - 1);
	switch (bits)
	{
	case 16: return p[a] | (p[b] << 8);
	case 32: return p[a] | (p[b] << 8) | (p[c] << 16) | ((u32)p[d] << 24);
	default: return p[a];
	}
}

static void writeRam(u32 addr, u32 value, u32 bits)
{
	const u32 n = bits == 32 ? 4 : bits == 16 ? 2 : 1;
	for (u32 k = 0; k < n; k++)
		g_ram[(addr + k) & (TEST_RAM - 1)] = (u8)(value >> (8 * k));
	g_log.push_back(addr);
	g_log.push_back(bits == 32 ? value : bits == 16 ? (value & 0xFFFF) : (value & 0xFF));
	g_log.push_back(bits);
}

// Flycast CheatManager::apply, the part for the file's cheats.
static void fly_apply(const std::vector<FlyCheat>& cheats)
{
	bool skipCheat = false;
	for (const FlyCheat& cheat : cheats)
	{
		if (skipCheat)
		{
			skipCheat = false;
			continue;
		}
		if (!cheat.enabled)
			continue;

		bool setValue = false;
		u32 valueToSet = 0;
		switch (cheat.type)
		{
		case FlyCheat::Type::disabled:
		default:
			break;
		case FlyCheat::Type::setValue:
			setValue = true;
			valueToSet = cheat.value;
			break;
		case FlyCheat::Type::increase:
			setValue = true;
			valueToSet = readRam(cheat.address, cheat.size) + cheat.value;
			break;
		case FlyCheat::Type::decrease:
			setValue = true;
			valueToSet = readRam(cheat.address, cheat.size) - cheat.value;
			break;
		case FlyCheat::Type::runNextIfEq:
			skipCheat = readRam(cheat.address, cheat.size) != cheat.value;
			break;
		case FlyCheat::Type::runNextIfNeq:
			skipCheat = readRam(cheat.address, cheat.size) == cheat.value;
			break;
		case FlyCheat::Type::runNextIfGt:
			skipCheat = readRam(cheat.address, cheat.size) <= cheat.value;
			break;
		case FlyCheat::Type::runNextIfLt:
			skipCheat = readRam(cheat.address, cheat.size) >= cheat.value;
			break;
		case FlyCheat::Type::copy:
			for (u32 i = 0; i < cheat.repeatCount; i++)
				writeRam(cheat.destAddress + i, readRam(cheat.address + i, cheat.size), cheat.size);
			break;
		}
		if (setValue)
		{
			u32 address = cheat.address;
			for (u32 repeat = 0; repeat < cheat.repeatCount; repeat++)
			{
				u32 curVal = readRam(address, cheat.size);
				if (cheat.size < 8)
				{
					for (int i = 0; i < 8; i++)
					{
						int bitmask = 1 << i;
						if ((cheat.valueMask & bitmask) == 0)
							valueToSet = (valueToSet & ~bitmask) | (curVal & bitmask);
					}
				}
				if (curVal != valueToSet)
					writeRam(address, valueToSet, cheat.size);
				address += cheat.repeatAddressIncrement * cheat.size / 8;
				valueToSet += cheat.repeatValueIncrement;
			}
		}
	}
}

// ---- the cheats: the tests ----------------------------------------------------------

static Cheat g_out[CHEAT_MAX];

static bool same(const Cheat& a, const FlyCheat& b)
{
	return a.type == (u32)b.type && a.bits == b.size && a.address == b.address && a.value == b.value && a.dest == b.destAddress &&
	       a.repeat == b.repeatCount && a.add_value == b.repeatValueIncrement && a.add_address == b.repeatAddressIncrement &&
	       a.mask == b.valueMask && a.enabled == b.enabled;
}

static u32 compare_file(const std::string& text, const char* what)
{
	const std::vector<FlyCheat> want = fly_load(text);
	const u32 n = cheats_parse(text.c_str(), FLY_RAM_SIZE, g_out);
	CHECK(n == want.size(), "%s: %u cheats, Flycast has %u\n%s", what, n, (u32)want.size(), text.c_str());
	for (u32 i = 0; i < n && i < want.size(); i++)
		CHECK(same(g_out[i], want[i]), "%s: cheat %u: type %u bits %u address %X value %X dest %X repeat %u +%u +%u mask %X on %d,"
		      " Flycast has type %u bits %u address %X value %X dest %X repeat %u +%u +%u mask %X on %d\n%s", what, i,
		      g_out[i].type, g_out[i].bits, g_out[i].address, g_out[i].value, g_out[i].dest, g_out[i].repeat, g_out[i].add_value,
		      g_out[i].add_address, g_out[i].mask, (int)g_out[i].enabled, (u32)want[i].type, want[i].size, want[i].address,
		      want[i].value, want[i].destAddress, want[i].repeatCount, want[i].repeatValueIncrement,
		      want[i].repeatAddressIncrement, want[i].valueMask, (int)want[i].enabled, text.c_str());
	return n;
}

static std::string num(u32 v)
{
	char b[32];
	const u32 form = rnd(8);
	if (form == 0)
		snprintf(b, sizeof(b), "0x%X", v);
	else if (form == 1)
		snprintf(b, sizeof(b), "0X%x", v);
	else
		snprintf(b, sizeof(b), "%u", v);
	return b;
}

// One "key = value" line in one of the forms a file can have.
static std::string entry(const std::string& key, const std::string& value)
{
	static const char* const pre[] = { "", "", "", " ", "\t" };
	static const char* const eq[] = { " = ", " = ", "=", " =", "= ", "  =  " };
	const u32 q = rnd(6);
	std::string v = q < 4 ? "\"" + value + "\"" : value;
	// Text after the value's quotes; with a quote in it, Flycast's value runs to the last quote of the line.
	if (q == 3)
		v += rnd(2) ? " ; a note" : " ; a \"note\"";
	return std::string(pre[rnd(5)]) + key + eq[rnd(6)] + v + "\n";
}

static std::string random_file(u32* cheats)
{
	static const char* const bools[] = { "true", "false", "TRUE", "yes", "on", "1", "0", "no", "tru", "" };
	const u32 n = rnd(12);
	std::string s;
	std::vector<std::string> lines;
	// The count: none, right, too small, too large.
	const u32 cmode = rnd(6);
	if (cmode >= 2)
		lines.push_back(entry("cheats", num(cmode == 2 ? n : cmode == 3 ? rnd(n + 1) : cmode == 4 ? n + rnd(4) : 0)));
	for (u32 i = 0; i < n; i++)
	{
		const std::string p = "cheat" + std::to_string(i) + "_";
		// Some numbers have no address; some addresses are outside the RAM.
		if (rnd(10))
			lines.push_back(entry(p + "address", num(rnd(12) ? rnd(FLY_RAM_SIZE) : FLY_RAM_SIZE + rnd(3) * 0x7FFFFF)));
		if (rnd(8))
			lines.push_back(entry(p + "cheat_type", num(rnd(10))));
		if (rnd(6))
			lines.push_back(entry(p + "memory_search_size", num(rnd(6))));
		if (rnd(6))
			lines.push_back(entry(p + "value", rnd(10) ? num(rnd(2) ? rnd(256) : (rnd(0x10000) << 16) | rnd(0x10000)) : "-" + num(rnd(9))));
		if (rnd(3) == 0)
			lines.push_back(entry(p + "repeat_count", num(rnd(40))));
		if (rnd(3) == 0)
			lines.push_back(entry(p + "repeat_add_to_value", num(rnd(300))));
		if (rnd(3) == 0)
			lines.push_back(entry(p + "repeat_add_to_address", num(rnd(9))));
		if (rnd(8))
			lines.push_back(entry(p + "enable", bools[rnd(10)]));
		if (rnd(4) == 0)
			lines.push_back(entry(p + "dest_address", num(rnd(12) ? rnd(FLY_RAM_SIZE) : FLY_RAM_SIZE + rnd(100))));
		if (rnd(3) == 0)
			lines.push_back(entry(p + "address_bit_position", num(rnd(256))));
		lines.push_back(entry(p + "desc", "a name = with \"quotes\""));
		if (rnd(6) == 0)
			lines.push_back(entry(p + "big_endian", "false"));
		// A second value for a key: the last one counts.
		if (rnd(8) == 0)
			lines.push_back(entry(p + "value", num(rnd(1000))));
	}
	// Keys that are not a cheat's.
	static const char* const junk[] = { "; a comment\n", "\n", "   \n", "cheat01_address = \"16\"\n", "cheat_address = 5\n",
	                                    "cheats_address = 5\n", "cheat0_addres = 9\n", "no equal sign\n", " = 4\n",
	                                    "cheat0_address_ = 3\n", "Cheat0_address = 3\n", "cheat300_address = \"64\"\n" };
	for (u32 k = rnd(4); k > 0; k--)
		lines.push_back(junk[rnd(12)]);
	// File order does not count: the keys are looked up by name.
	for (size_t i = lines.size(); i > 1; i--)
		std::swap(lines[i - 1], lines[rnd((u32)i)]);
	for (const std::string& l : lines)
		s += l;
	// Keys under a section header are another section's.
	if (rnd(10) == 0)
		s += "[other]\ncheat0_address = \"1\"\ncheat0_value = \"2\"\ncheats = \"99\"\n";
	*cheats = n;
	return s;
}

static void test_cheat_parse()
{
	// Flycast's own test file (tests/src/CheatManagerTest.cpp).
	const char* fly = "\ncheat0_address = \"1234\"\ncheat0_address_bit_position = \"0\"\ncheat0_big_endian = \"false\"\n"
	                  "cheat0_cheat_type = \"1\"\ncheat0_code = \"\"\ncheat0_desc = \"widescreen\"\ncheat0_dest_address = \"7890\"\n"
	                  "cheat0_enable = \"false\"\ncheat0_handler = \"1\"\ncheat0_memory_search_size = \"5\"\n"
	                  "cheat0_repeat_add_to_address = \"4\"\ncheat0_repeat_add_to_value = \"16\"\ncheat0_repeat_count = \"3\"\n"
	                  "cheat0_value = \"5678\"\ncheats = \"1\"\n";
	u32 n = compare_file(fly, "Flycast's test file");
	CHECK(n == 1 && g_out[0].address == 1234 && g_out[0].value == 5678 && g_out[0].type == CHEAT_SET && g_out[0].bits == 32 &&
	      g_out[0].repeat == 3 && g_out[0].add_address == 4 && g_out[0].add_value == 16 && g_out[0].dest == 7890 &&
	      !g_out[0].enabled, "Flycast's test file: %u cheats, address %u", n, g_out[0].address);

	u32 total = 0, files = 0;
	for (u32 i = 0; i < 60000; i++)
	{
		u32 made;
		const std::string s = random_file(&made);
		total += compare_file(s, "a random file");
		files++;
	}
	CHECK(total > files, "the random files gave %u cheats in %u files", total, files);

	// A file from a Windows editor: CR before each line end, with and without quotes.
	n = cheats_parse("cheats = 2\r\ncheat0_address = 16\r\ncheat0_cheat_type = 1\r\ncheat0_enable = true\r\ncheat0_value = \"7\"\r\n"
	                 "cheat1_address = \"32\"\r\ncheat1_cheat_type = \"2\"\r\ncheat1_enable = \"on\"\r\n", FLY_RAM_SIZE, g_out);
	CHECK(n == 2 && g_out[0].address == 16 && g_out[0].enabled && g_out[0].value == 7 && g_out[1].address == 32 &&
	      g_out[1].type == CHEAT_INC && g_out[1].enabled, "a CRLF file: %u cheats, enabled %d %d", n, (int)g_out[0].enabled,
	      (int)g_out[1].enabled);
	// No line end after the last line.
	n = cheats_parse("cheat0_address = 5\ncheat0_cheat_type = 1", FLY_RAM_SIZE, g_out);
	CHECK(n == 1 && g_out[0].address == 5 && g_out[0].type == CHEAT_SET, "no last line end: %u cheats", n);
	// Our two limits (Flycast has none): the repeat count, and a size over 32 bits.
	n = cheats_parse("cheat0_address = 5\ncheat0_cheat_type = 1\ncheat0_repeat_count = 4000000000\n"
	                 "cheat0_memory_search_size = 9\n", FLY_RAM_SIZE, g_out);
	CHECK(n == 1 && g_out[0].repeat == CHEAT_REPEAT_MAX && g_out[0].bits == 32, "the limits: %u cheats, repeat %u, bits %u", n,
	      g_out[0].repeat, g_out[0].bits);
	n = cheats_parse("cheat0_address = 5\ncheat0_cheat_type = 1\ncheat0_repeat_count = 65536\n", FLY_RAM_SIZE, g_out);
	CHECK(n == 1 && g_out[0].repeat == 65536, "a repeat count at the limit: %u", g_out[0].repeat);
	// The table's last number is read; the next one is not.
	n = cheats_parse("cheats = 400\ncheat255_address = 8\ncheat255_cheat_type = 3\ncheat256_address = 9\n"
	                 "cheat256_cheat_type = 1\n", FLY_RAM_SIZE, g_out);
	CHECK(n == 1 && g_out[0].address == 8 && g_out[0].type == CHEAT_DEC, "the last number: %u cheats, address %u", n,
	      g_out[0].address);
	// The RAM size is the caller's.
	n = cheats_parse("cheats = 2\ncheat0_address = 4095\ncheat0_cheat_type = 1\ncheat1_address = 4096\ncheat1_cheat_type = 1\n",
	                 4096, g_out);
	CHECK(n == 1 && g_out[0].address == 4095, "a RAM of 4096 bytes: %u cheats", n);
	CHECK(cheats_parse("", FLY_RAM_SIZE, g_out) == 0, "an empty file has cheats");
	CHECK(cheat_int("  0x1f") == 31 && cheat_int("0X10") == 16 && cheat_int("12abc") == 12 && cheat_int("") == 0 &&
	      cheat_int("x") == 0 && cheat_int("+7") == 7 && cheat_int("-1") == 0xFFFFFFFFu && cheat_int("99999999999") == 0xFFFFFFFFu &&
	      cheat_int("0x") == 0 && cheat_int("010") == 10, "cheat_int");
	CHECK(cheat_bool("TRUE") && cheat_bool("Yes") && cheat_bool("on") && cheat_bool("1") && !cheat_bool("true ") &&
	      !cheat_bool("") && !cheat_bool("0") && !cheat_bool("off"), "cheat_bool");
}

static void test_cheat_apply()
{
	u32 writes_seen = 0, skips = 0;
	for (u32 round = 0; round < 60000; round++)
	{
		const u32 n = 1 + rnd(8);
		std::vector<FlyCheat> fly(n);
		Cheat mine[8];
		for (u32 i = 0; i < n; i++)
		{
			FlyCheat& c = fly[i];
			c.type = (FlyCheat::Type)rnd(10);
			c.enabled = rnd(5) != 0;
			c.size = 1u << rnd(6);
			// Near each other, so tests, copies and sets meet; some at the RAM's end.
			c.address = rnd(8) ? rnd(64) : TEST_RAM - 1 - rnd(6);
			c.value = rnd(3) ? rnd(4) : (rnd(0x10000) << 16) | rnd(0x10000);
			c.valueMask = (u8)(rnd(4) ? rnd(256) : 0);
			c.repeatCount = rnd(4) ? 1 : rnd(6);
			c.repeatValueIncrement = rnd(3) ? 0 : rnd(5);
			c.repeatAddressIncrement = rnd(3) ? 0 : rnd(5);
			c.destAddress = rnd(64);
			Cheat& m = mine[i];
			memset(&m, 0, sizeof(m));
			m.type = (u32)c.type;
			m.enabled = c.enabled;
			m.bits = c.size;
			m.address = c.address;
			m.value = c.value;
			m.mask = c.valueMask;
			m.repeat = c.repeatCount;
			m.add_value = c.repeatValueIncrement;
			m.add_address = c.repeatAddressIncrement;
			m.dest = c.destAddress;
			m.has_address = true;
		}
		u8 start[TEST_RAM > 128 ? 128 : TEST_RAM];
		for (u32 k = 0; k < sizeof(start); k++)
			start[k] = (u8)rnd(rnd(2) ? 4 : 256);
		// Two vblanks: the second one sees the first one's writes.
		std::vector<u32> log_fly, log_mine;
		u8 ram_fly[sizeof(start)], end_fly[8], ram_mine[sizeof(start)], end_mine[8];
		u32 ret = 0;
		for (int side = 0; side < 2; side++)
		{
			memset(g_ram, 0, sizeof(g_ram));
			memcpy(g_ram, start, sizeof(start));
			memcpy(g_ram + TEST_RAM - 8, start + 64, 8);
			g_log.clear();
			for (int vbl = 0; vbl < 2; vbl++)
			{
				if (side == 0)
					fly_apply(fly);
				else
					ret += cheats_apply(mine, n, readRam, writeRam);
			}
			memcpy(side ? ram_mine : ram_fly, g_ram, sizeof(start));
			memcpy(side ? end_mine : end_fly, g_ram + TEST_RAM - 8, 8);
			(side ? log_mine : log_fly) = g_log;
		}
		CHECK(log_fly == log_mine, "round %u: %u writes, Flycast has %u", round, (u32)log_mine.size() / 3, (u32)log_fly.size() / 3);
		CHECK(!memcmp(ram_fly, ram_mine, sizeof(start)) && !memcmp(end_fly, end_mine, 8), "round %u: the RAM differs", round);
		CHECK(ret == log_mine.size() / 3, "round %u: %u writes counted, %u made", round, ret, (u32)log_mine.size() / 3);
		writes_seen += (u32)log_fly.size() / 3;
		for (u32 i = 0; i + 1 < n; i++)
			skips += fly[i].enabled && (u32)fly[i].type >= 4 && (u32)fly[i].type <= 7;
	}
	CHECK(writes_seen > 100000 && skips > 10000, "the random cheats made %u writes and %u tests", writes_seen, skips);
}

int main()
{
	test_view();
	test_clipped();
	test_scaler();
	test_osd();
	test_shot();
	test_cheat_parse();
	test_cheat_apply();
	if (g_fail)
	{
		printf("FAIL: %d check(s)\n", g_fail);
		return 1;
	}
	printf("PASS: screen_check (the view, the clip rule, the scaler, the FPS lines, the PNG rows, the cheat file, the cheats)\n");
	return 0;
}
