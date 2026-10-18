// Maple input from DS3 pads via ioPad.
#include "types.h"
#include "dc/maple/maple_devs.h"
#include "dc_pad_bits.h"
#include "ps3_menu.h"
#include "ps3_padmap.h"
#include "ps3_padports.h"
#include "ps3_presets.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <io/pad.h>

#define MAX_CONTROLLERS 4
#define ANALOG_DEADZONE 20

// kcode is active-low: 0xFFFF = nothing pressed.
u16 kcode[MAX_CONTROLLERS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
u32 vks[MAX_CONTROLLERS];
s8  joyx[MAX_CONTROLLERS];
s8  joyy[MAX_CONTROLLERS];
// G12: the right stick; read only for the full controller.
s8  joyrx[MAX_CONTROLLERS];
s8  joyry[MAX_CONTROLLERS];
u8  rt[MAX_CONTROLLERS];
u8  lt[MAX_CONTROLLERS];

extern "C" int get_controller_type();

static bool ps3_pad_ok;
// ioPadGetData does not refresh the buffer when there is no new input, so keep one per port.
static padData ps3_pad_data[MAX_PORT_NUM];
static bool ps3_select_down[MAX_PORT_NUM];
static int ps3_pad_of_dc[MAX_CONTROLLERS] = { -1, -1, -1, -1 };

// Last logged state per DC port, for the G4 log.
static u16 log_kcode[MAX_CONTROLLERS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
static u8  log_trig[MAX_CONTROLLERS];
static u8  log_stick[MAX_CONTROLLERS];

// P13: the Puru Puru pack's last SetCondition per DC port, played on the DS3's large motor.
struct Rumble
{
	float power, slope;  // slope: Flycast's vib_inclination, power per ms left
	double stop;         // os_GetSeconds() time the motor stops
	u8 sent;             // the motor level the pad has now
};
static Rumble s_rumble[MAX_CONTROLLERS];

static void rumble_send(u32 port, float power)
{
	Rumble& r = s_rumble[port];
	const u8 level = power <= 0.0f ? 0 : power >= 1.0f ? 255 : (u8)(power * 255.0f + 0.5f);
	if (level == r.sent)
		return;
	r.sent = level;
	if (ps3_pad_of_dc[port] < 0)
		return;
	padActParam act;
	memset(&act, 0, sizeof(act));
	act.large_motor = level;
	ioPadSetActDirect(ps3_pad_of_dc[port], &act);
}

// maple_cfg.cpp calls this at each SetCondition of the pack (Flycast SDLGamepad::rumble).
void UpdateVibration(u32 port, float power, float inclination, u32 duration_ms)
{
	if (port >= MAX_CONTROLLERS)
		return;
	Rumble& r = s_rumble[port];
	r.power = power;
	r.slope = inclination * power;
	r.stop = os_GetSeconds() + duration_ms / 1000.0;
	rumble_send(port, power);
	static u32 s_logged;
	if (s_logged++ < 16)
		printf("[pad] dc%u rumble power=%.2f slope=%g ms=%u motor=%u\n", port, power, r.slope, duration_ms, r.sent);
}

// At each controller poll: Flycast's update_rumble, and the stop at the end that the DS3 does not do alone.
static void rumble_update(u32 port)
{
	Rumble& r = s_rumble[port];
	if (!r.sent)
		return;
	const double left_ms = (r.stop - os_GetSeconds()) * 1000.0;
	if (left_ms <= 0.0)
		rumble_send(port, 0.0f);
	else if (r.slope > 0.0f)
		rumble_send(port, r.slope * (float)left_ms);
}

// The motors off: the guest does not poll while the in-game menu is open or after an exit.
void ps3_rumble_stop(void)
{
	for (u32 port = 0; port < MAX_CONTROLLERS; port++)
		rumble_send(port, 0.0f);
}

void ps3_input_init()
{
	int rc = ioPadInit(MAX_PORT_NUM);
	// PRE_L2/PRE_R2 read 0 unless pressure mode is on (xashPS3 in_ps3.c).
	for (int port = 0; port < MAX_PORT_NUM; port++)
		ioPadSetPressMode(port, PAD_PRESS_MODE_ON);
	memset(ps3_pad_data, 0, sizeof(ps3_pad_data));
	ps3_pad_ok = (rc == 0);
	printf("[pad] ioPadInit(%d) -> %d\n", MAX_PORT_NUM, rc);
}

void ps3_input_term()
{
	if (ps3_pad_ok)
	{
		ps3_rumble_stop();
		ioPadEnd();
	}
	ps3_pad_ok = false;
}

static s8 ClampAnalogValue(s32 value)
{
	if (abs(value) < ANALOG_DEADZONE)
		return 0;
	if (value > 127) value = 127;
	if (value < -128) value = -128;
	return (s8)value;
}

// G15: maple_cfg.cpp asks at each maple DMA which DC ports have a pad (bit N = port N), cap = the Players row.
// A port keeps its pad while it stays connected (ps3_padports.h); on an ioPad error the ports stay as they are.
u32 ps3_pad_ports(u32 cap)
{
	padInfo info;
	if (ps3_pad_ok && ioPadGetInfo(&info) == 0)
	{
		int old[MAX_CONTROLLERS];
		for (u32 port = 0; port < MAX_CONTROLLERS; port++)
		{
			old[port] = ps3_pad_of_dc[port];
			if (padports_lost(ps3_pad_of_dc, info.status, MAX_PORT_NUM, cap, port))
			{
				// P13: the pad that leaves this port must not keep its motor on.
				rumble_send(port, 0.0f);
				ps3_pad_of_dc[port] = -1;
			}
		}
		padports_fill(ps3_pad_of_dc, info.status, MAX_PORT_NUM, cap);
		for (u32 port = 0; port < MAX_CONTROLLERS; port++)
			if (ps3_pad_of_dc[port] != old[port])
				printf("[pad] dc%u -> ps3 pad %d\n", port, ps3_pad_of_dc[port]);
	}
	return padports_mask(ps3_pad_of_dc);
}

static void ps3_log_state(u32 port)
{
	u8 trig = (lt[port] ? 1 : 0) | (rt[port] ? 2 : 0);
	u8 stick = (joyx[port] < 0 ? 1 : 0) | (joyx[port] > 0 ? 2 : 0) |
	           (joyy[port] < 0 ? 4 : 0) | (joyy[port] > 0 ? 8 : 0) |
	           (joyrx[port] < 0 ? 16 : 0) | (joyrx[port] > 0 ? 32 : 0) |
	           (joyry[port] < 0 ? 64 : 0) | (joyry[port] > 0 ? 128 : 0);
	if (kcode[port] == log_kcode[port] && trig == log_trig[port] && stick == log_stick[port])
		return;
	log_kcode[port] = kcode[port];
	log_trig[port] = trig;
	log_stick[port] = stick;
	// The right stick is in the line only when it moved: a standard pad's lines stay as they were.
	if (stick & 0xF0)
		printf("[pad] dc%u kcode=%04X lt=%u rt=%u joy=%d,%d joy2=%d,%d\n",
		       port, kcode[port], lt[port], rt[port], joyx[port], joyy[port], joyrx[port], joyry[port]);
	else
		printf("[pad] dc%u kcode=%04X lt=%u rt=%u joy=%d,%d\n",
		       port, kcode[port], lt[port], rt[port], joyx[port], joyy[port]);
}

void InitControllers(void)
{
	for (int i = 0; i < MAX_CONTROLLERS; i++)
	{
		kcode[i] = 0xFFFF;
		vks[i] = 0;
		joyx[i] = joyy[i] = 0;
		joyrx[i] = joyry[i] = 0;
		rt[i] = lt[i] = 0;
	}
}

// One stick, raw 0..255 per axis: the Stick Dead Zone row picks the old cut per axis or P84's radial zone.
static void ps3_stick(u32 raw_h, u32 raw_v, s8* x, s8* y)
{
	const int h = (int)raw_h - 128, v = (int)raw_v - 128;
	if (!g_stick_deadzone_preset)
	{
		*x = ClampAnalogValue(h);
		*y = ClampAnalogValue(v);
		return;
	}
	int ox, oy;
	pad_stick_radial(h, v, &ox, &oy);
	*x = (s8)ox;
	*y = (s8)oy;
}

void UpdateInputState(u32 port)
{
	if (port >= MAX_CONTROLLERS)
		return;

	kcode[port] = 0xFFFF;
	joyx[port] = joyy[port] = 0;
	joyrx[port] = joyry[port] = 0;
	lt[port] = rt[port] = 0;

	// G15: ps3_pad_ports() gave the port its pad at the start of this maple DMA.
	const int pad = ps3_pad_ok ? ps3_pad_of_dc[port] : -1;
	rumble_update(port);
	if (pad < 0)
	{
		ps3_log_state(port);
		return;
	}

	// Never branch on len: the buffer keeps the last reported state (ioQuake3-PS3, xashPS3).
	padData* d = &ps3_pad_data[pad];
	ioPadGetData(pad, d);

	// SELECT is no Dreamcast button: a press opens the in-game menu at the next vblank.
	if (d->BTN_SELECT && !ps3_select_down[pad])
		ps3_menu_request();
	ps3_select_down[pad] = d->BTN_SELECT;

	// Each DS3 button's travel; the Map* keys (g_pad_map) name the DC control it drives. By default the face
	// buttons go by position (Cross=A, Circle=B, Square=X, Triangle=Y) and L1/R1 are the triggers at full travel.
	u8 ana[PADBTN_COUNT];
	ana[PADBTN_CROSS]    = d->BTN_CROSS    ? 255 : 0;
	ana[PADBTN_CIRCLE]   = d->BTN_CIRCLE   ? 255 : 0;
	ana[PADBTN_SQUARE]   = d->BTN_SQUARE   ? 255 : 0;
	ana[PADBTN_TRIANGLE] = d->BTN_TRIANGLE ? 255 : 0;
	ana[PADBTN_START]    = d->BTN_START    ? 255 : 0;
	ana[PADBTN_UP]       = d->BTN_UP       ? 255 : 0;
	ana[PADBTN_DOWN]     = d->BTN_DOWN     ? 255 : 0;
	ana[PADBTN_LEFT]     = d->BTN_LEFT     ? 255 : 0;
	ana[PADBTN_RIGHT]    = d->BTN_RIGHT    ? 255 : 0;
	ana[PADBTN_L1]       = d->BTN_L1       ? 255 : 0;
	ana[PADBTN_R1]       = d->BTN_R1       ? 255 : 0;
	ana[PADBTN_L3]       = d->BTN_L3       ? 255 : 0;
	ana[PADBTN_R3]       = d->BTN_R3       ? 255 : 0;
	// L2/R2 analog by pressure; the digital bit alone gives full travel.
	u8 l2 = d->PRE_L2, r2 = d->PRE_R2;
	if (d->BTN_L2 && l2 == 0) l2 = 255;
	if (d->BTN_R2 && r2 == 0) r2 = 255;
	ana[PADBTN_L2] = l2;
	ana[PADBTN_R2] = r2;

	u8 l = 0, r = 0;
	padmap_apply(g_pad_map, ana, &kcode[port], &l, &r);
	lt[port] = l;
	rt[port] = r;
	// maple_cfg masks C/D; set them anyway so kcode holds every pressed bit.
	if (l) kcode[port] &= ~key_CONT_D;
	if (r) kcode[port] &= ~key_CONT_C;

	// Raw stick is 0..255 with 255 = down, and DC positive Y is down too.
	ps3_stick(d->ANA_L_H, d->ANA_L_V, &joyx[port], &joyy[port]);
	// G12: only the full controller reports the right stick.
	if (get_controller_type() == 5)
		ps3_stick(d->ANA_R_H, d->ANA_R_V, &joyrx[port], &joyry[port]);

	ps3_log_state(port);
}

// Special peripherals are not supported on PS3; get_controller_type() is 0 or 5 (the full controller).
void InitLightGun(void) {}
void InitMaracas(void) {}
void InitKeyboard(void) {}
void InitFishingRod(void) {}
void UpdateLightGunState(u32 port) {}
void UpdateMaracasState(u32 port) {}
void UpdateKeyboardState(u32 port) {}
void UpdateFishingRodState(u32 port) {}

extern "C" u16 GetLightGunX(int port) { return 0; }
extern "C" u16 GetLightGunY(int port) { return 0; }
// Layout must match maple_devs.cpp; only a null pointer is ever returned here.
struct DC_KeyboardReport;
extern "C" const DC_KeyboardReport* GetKeyboardReport(int port) { return 0; }

extern "C" void ExitCombo_HostPoll() {}
