// Plan section 7, bundle 10, PC check: the real ps3/ps3_padmap.h and dc/maple/maple_puru.h against the fixed map of
// before the bundle and against Flycast's code, written here in Flycast's own form.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "ps3/ps3_padmap.h"
#include "dc/maple/maple_puru.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail++ < 12) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

// ---- the DS3 state the map reads ------------------------------------------------
struct Pad
{
	bool cross, circle, square, triangle, start, up, down, left, right, l1, r1, l2, r2, l3, r3;
	u8 pre_l2, pre_r2;
};

// ps3_input.cpp before bundle 10, line for line.
static void old_map(const Pad& d, u16* kcode, u8* lt, u8* rt)
{
	*kcode = 0xFFFF;
	if (d.cross)    *kcode &= ~key_CONT_A;
	if (d.circle)   *kcode &= ~key_CONT_B;
	if (d.square)   *kcode &= ~key_CONT_X;
	if (d.triangle) *kcode &= ~key_CONT_Y;
	if (d.start)    *kcode &= ~key_CONT_START;
	if (d.up)       *kcode &= ~key_CONT_DPAD_UP;
	if (d.down)     *kcode &= ~key_CONT_DPAD_DOWN;
	if (d.left)     *kcode &= ~key_CONT_DPAD_LEFT;
	if (d.right)    *kcode &= ~key_CONT_DPAD_RIGHT;
	u8 l = d.pre_l2, r = d.pre_r2;
	if (d.l1 || (d.l2 && l == 0)) l = 255;
	if (d.r1 || (d.r2 && r == 0)) r = 255;
	*lt = l;
	*rt = r;
	if (l) *kcode &= ~key_CONT_D;
	if (r) *kcode &= ~key_CONT_C;
}

// ps3_input.cpp's UpdateInputState now: the travel of each button, then the map.
static void pad_travel(const Pad& d, u8* ana)
{
	ana[PADBTN_CROSS]    = d.cross    ? 255 : 0;
	ana[PADBTN_CIRCLE]   = d.circle   ? 255 : 0;
	ana[PADBTN_SQUARE]   = d.square   ? 255 : 0;
	ana[PADBTN_TRIANGLE] = d.triangle ? 255 : 0;
	ana[PADBTN_START]    = d.start    ? 255 : 0;
	ana[PADBTN_UP]       = d.up       ? 255 : 0;
	ana[PADBTN_DOWN]     = d.down     ? 255 : 0;
	ana[PADBTN_LEFT]     = d.left     ? 255 : 0;
	ana[PADBTN_RIGHT]    = d.right    ? 255 : 0;
	ana[PADBTN_L1]       = d.l1       ? 255 : 0;
	ana[PADBTN_R1]       = d.r1       ? 255 : 0;
	ana[PADBTN_L3]       = d.l3       ? 255 : 0;
	ana[PADBTN_R3]       = d.r3       ? 255 : 0;
	u8 l2 = d.pre_l2, r2 = d.pre_r2;
	if (d.l2 && l2 == 0) l2 = 255;
	if (d.r2 && r2 == 0) r2 = 255;
	ana[PADBTN_L2] = l2;
	ana[PADBTN_R2] = r2;
}

static void new_map(const int* map, const Pad& d, u16* kcode, u8* lt, u8* rt)
{
	u8 ana[PADBTN_COUNT];
	pad_travel(d, ana);
	*kcode = 0xFFFF;
	u8 l = 0, r = 0;
	padmap_apply(map, ana, kcode, &l, &r);
	*lt = l;
	*rt = r;
	if (l) *kcode &= ~key_CONT_D;
	if (r) *kcode &= ~key_CONT_C;
}

static Pad pad_of(u32 bits, u8 pl, u8 pr)
{
	Pad d;
	bool* b = &d.cross;
	for (int k = 0; k < 15; k++)
		b[k] = (bits >> k) & 1;
	d.pre_l2 = pl;
	d.pre_r2 = pr;
	return d;
}

static void check_map()
{
	static const int def[PADBTN_COUNT] = PADMAP_DEFAULTS;
	static const u8 pres[] = { 0, 1, 77, 128, 254, 255 };
	// The default map is the old map, for every set of buttons and the pressures at their edges.
	for (u32 bits = 0; bits < (1u << 15); bits++)
		for (u8 pl : pres)
			for (u8 pr : pres)
			{
				const Pad d = pad_of(bits, pl, pr);
				u16 k0, k1;
				u8 l0, r0, l1, r1;
				old_map(d, &k0, &l0, &r0);
				new_map(def, d, &k1, &l1, &r1);
				CHECK(k0 == k1 && l0 == l1 && r0 == r1, "default map: buttons %04X pre %u,%u: old %04X %u %u, new %04X %u %u",
				      bits, pl, pr, k0, l0, r0, k1, l1, r1);
			}

	// One button down, mapped to each control: only that control moves, by the button's travel.
	static const u16 bit[PADMAP_COUNT] = { 0, key_CONT_A, key_CONT_B, key_CONT_X, key_CONT_Y, key_CONT_START,
		key_CONT_DPAD_UP, key_CONT_DPAD_DOWN, key_CONT_DPAD_LEFT, key_CONT_DPAD_RIGHT, 0, 0 };
	for (int b = 0; b < PADBTN_COUNT; b++)
		for (int t = -3; t < PADMAP_COUNT + 3; t++)
			for (u8 travel : pres)
			{
				int map[PADBTN_COUNT];
				for (int k = 0; k < PADBTN_COUNT; k++)
					map[k] = PADMAP_START;   // the other buttons are up, so their entry must not show
				map[b] = t;
				u8 ana[PADBTN_COUNT] = { 0 };
				ana[b] = travel;
				u16 kc = 0xFFFF;
				u8 l = 0, r = 0;
				padmap_apply(map, ana, &kc, &l, &r);
				const bool in = t >= 0 && t < PADMAP_COUNT && travel;
				const u16 want_k = (u16)(0xFFFF & ~(in ? bit[t] : 0));
				const u8 want_l = (in && t == PADMAP_LT) ? travel : 0, want_r = (in && t == PADMAP_RT) ? travel : 0;
				CHECK(kc == want_k && l == want_l && r == want_r, "map: button %d to %d travel %u: %04X %u %u, want %04X %u %u",
				      b, t, travel, kc, l, r, want_k, want_l, want_r);
			}

	// Two buttons on one trigger: the larger travel wins, in both orders.
	int map[PADBTN_COUNT] = PADMAP_DEFAULTS;
	for (int a = 0; a < 256; a += 5)
	{
		u8 ana[PADBTN_COUNT] = { 0 };
		ana[PADBTN_L2] = (u8)a;
		ana[PADBTN_L1] = 200;
		u16 kc = 0xFFFF;
		u8 l = 0, r = 0;
		padmap_apply(map, ana, &kc, &l, &r);
		CHECK(l == std::max(a, 200) && r == 0 && kc == 0xFFFF, "two buttons on LT: %d and 200 give %u", a, l);
	}
}

// ---- the stick ---------------------------------------------------------------------

// Flycast gamepad_device.cpp (the radial dead zone, 16-bit axes), maple_cfg.cpp GetBtFromSgn and maple_devs.h
// limit_joystick_magnitude<128>, as they are written there.
static void fly_stick(int x8, int y8, int* ox, int* oy)
{
	const float dead_zone = 0.10f, saturation = 1.0f;
	const int v[2] = { x8 * 256, y8 * 256 };
	int out[2];
	for (int a = 0; a < 2; a++)
	{
		const int axisDirection = v[a] < 0 ? -1 : 1;
		const float otherAxisValue = (float)v[1 - a];
		const float nv = std::abs(v[a]) / 32768.f;
		const float r2 = nv * nv + otherAxisValue * otherAxisValue / 32768.f / 32768.f;
		if (r2 < dead_zone * dead_zone || r2 == 0.f)
			out[a] = 0;
		else
		{
			float pdz = nv * dead_zone / std::sqrt(r2);
			int w = (int)std::round((nv - pdz) / (1 - pdz) * 32768.f * saturation);
			out[a] = std::max(-32768, std::min(32767, w * axisDirection));
		}
	}
	s8 joyx = (s8)(((out[0] + 32768) >> 8) - 128), joyy = (s8)(((out[1] + 32768) >> 8) - 128);
	float mag = (float)joyx * joyx + (float)joyy * joyy;
	if (mag > 128.f * 128.f)
	{
		mag = sqrtf(mag) / 128.f;
		joyx = (s8)lroundf(joyx / mag);
		joyy = (s8)lroundf(joyy / mag);
	}
	*ox = joyx;
	*oy = joyy;
}

static void check_stick()
{
	int worst = 0;
	for (int x = -128; x <= 127; x++)
		for (int y = -128; y <= 127; y++)
		{
			int ox, oy, fx, fy;
			pad_stick_radial(x, y, &ox, &oy);
			fly_stick(x, y, &fx, &fy);
			// Flycast cuts the 16-bit value down to 8 bits; ours rounds to nearest: 1 step apart at most, and 2 at
			// the rim, where the magnitude limit scales both axes after that.
			const int d = std::max(abs(ox - fx), abs(oy - fy));
			worst = std::max(worst, d);
			CHECK(d <= (x * x + y * y >= 120 * 120 ? 2 : 1), "stick %d,%d: %d,%d, Flycast %d,%d", x, y, ox, oy, fx, fy);
			CHECK(ox >= -128 && ox <= 127 && oy >= -128 && oy <= 127, "stick %d,%d: %d,%d does not fit an s8", x, y, ox, oy);
			CHECK(ox * ox + oy * oy <= 129 * 129, "stick %d,%d: %d,%d is past the magnitude limit", x, y, ox, oy);
			// Inside the dead radius nothing; outside it the sign of each axis stays.
			const bool dead = x * x + y * y < 12.8 * 12.8;
			CHECK(!dead || (ox == 0 && oy == 0), "stick %d,%d: %d,%d inside the dead zone", x, y, ox, oy);
			CHECK(ox * x >= 0 && oy * y >= 0, "stick %d,%d: %d,%d changes a sign", x, y, ox, oy);
			// The four mirror images agree (the travel is -128..127, so only where all exist).
			if (x > -128 && y > -128)
			{
				int mx, my;
				pad_stick_radial(-x, y, &mx, &my);
				CHECK(mx == -ox && my == oy, "stick %d,%d: no mirror in x", x, y);
				pad_stick_radial(x, -y, &mx, &my);
				CHECK(mx == ox && my == -oy, "stick %d,%d: no mirror in y", x, y);
			}
		}
	// Along a ray from the centre to the edge of the square travel, the output starts near 0 at the dead radius,
	// never goes back and ends at full travel.
	for (int deg = 0; deg < 360; deg += 3)
	{
		int last = 0;
		bool first = true;
		for (int r = 0; r <= 180; r++)
		{
			const int x = std::max(-127, std::min(127, (int)lround(r * cos(deg * M_PI / 180))));
			const int y = std::max(-127, std::min(127, (int)lround(r * sin(deg * M_PI / 180))));
			int ox, oy;
			pad_stick_radial(x, y, &ox, &oy);
			const int m = (int)lround(sqrt((double)ox * ox + oy * oy));
			if (m && first)
			{
				first = false;
				CHECK(m <= 4, "ray %d: the first output is %d, not near 0", deg, m);
			}
			CHECK(m + 2 >= last, "ray %d: the output falls from %d to %d at radius %d", deg, last, m, r);
			last = std::max(last, m);
		}
		CHECK(last >= 124, "ray %d: full travel gives only %d", deg, last);
	}
	int ox, oy;
	pad_stick_radial(127, 0, &ox, &oy);
	CHECK(ox == 127 && oy == 0, "full right is %d,%d", ox, oy);
	pad_stick_radial(-128, 0, &ox, &oy);
	CHECK(ox == -128 && oy == 0, "full left is %d,%d", ox, oy);
	pad_stick_radial(127, 127, &ox, &oy);
	CHECK(ox == oy && ox >= 89 && ox <= 91, "the corner is %d,%d, not on the circle", ox, oy);
	printf("stick: 65536 positions, largest step from Flycast %d\n", worst);
}

// ---- the Puru Puru pack ----------------------------------------------------------

// Flycast maple_sega_purupuru::dma, MDCF_SetCondition, with its types.
static void fly_puru(u32 VIBSET, u16 AST_ms, float* o_power, float* o_inclination, u32* o_duration)
{
	u8 POW_POS = (VIBSET >> 8) & 0x7;
	u8 POW_NEG = (VIBSET >> 12) & 0x7;
	u8 FREQ = (VIBSET >> 16) & 0xFF;
	s16 INC = (VIBSET >> 24) & 0xFF;
	if (VIBSET & 0x8000)			// INH
		INC = -INC;
	else if (!(VIBSET & 0x0800))	// EXH
		INC = 0;
	bool CNT = VIBSET & 1;

	float power = std::min((POW_POS + POW_NEG) / 7.0, 1.0);

	u32 duration_ms;
	if (FREQ > 0 && (!CNT || INC))
		duration_ms = std::min((int)(1000 * (INC ? abs(INC) * std::max(POW_POS, POW_NEG) : 1) / FREQ), (int)AST_ms);
	else
		duration_ms = AST_ms;
	float inclination;
	if (INC == 0 || power == 0)
		inclination = 0.0;
	else
		inclination = FREQ / (1000.0 * INC * std::max(POW_POS, POW_NEG));
	*o_power = power;
	*o_inclination = inclination;
	*o_duration = duration_ms;
}

static void check_puru()
{
	static const u16 asts[] = { 250, 5000, 64000 };   // AST 0, 19 (the reset value) and 255
	u32 n = 0;
	for (u16 ast : asts)
		for (u32 lo = 0; lo < 0x10000; lo++)   // CNT, the two powers, EXH, INH; the bits no field uses too
		{
			// Every FREQ and INC with the low word's edge cases, a sample of them with the rest.
			const bool all = (lo & 0x00F6) == 0;
			for (u32 hi = 0; hi < 0x10000; hi += all ? 1 : 257 + (lo & 7))
			{
				const u32 v = (hi << 16) | lo;
				float p, i;
				u32 d;
				fly_puru(v, ast, &p, &i, &d);
				const PuruVib m = puru_decode(v, ast);
				n++;
				CHECK(m.power == p && m.inclination == i && m.duration_ms == d,
				      "puru %08X ast %u: %g %g %u, Flycast %g %g %u", v, ast, m.power, m.inclination, m.duration_ms, p, i, d);
			}
		}
	printf("puru: %u words\n", n);
}

int main()
{
	check_map();
	check_stick();
	check_puru();
	if (g_fail)
	{
		printf("FAIL: %d checks\n", g_fail);
		return 1;
	}
	printf("PASS: map, stick, puru\n");
	return 0;
}
