// Maple input from DS3 pads via ioPad.
#include "types.h"
#include "dc/maple/maple_devs.h"
#include "dc_pad_bits.h"
#include "ps3_menu.h"

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
u8  rt[MAX_CONTROLLERS];
u8  lt[MAX_CONTROLLERS];

static bool ps3_pad_ok;
// ioPadGetData does not refresh the buffer when there is no new input, so keep one per port.
static padData ps3_pad_data[MAX_PORT_NUM];
static bool ps3_select_down[MAX_PORT_NUM];
static int ps3_pad_of_dc[MAX_CONTROLLERS] = { -1, -1, -1, -1 };

// Last logged state per DC port, for the G4 log.
static u16 log_kcode[MAX_CONTROLLERS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
static u8  log_trig[MAX_CONTROLLERS];
static u8  log_stick[MAX_CONTROLLERS];

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
		ioPadEnd();
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

// DC port N takes the Nth connected pad in PS3 port order; pads can move after a BT reconnect.
static int ps3_find_pad(const padInfo* info, u32 dc_port)
{
	u32 n = 0;
	for (int i = 0; i < MAX_PORT_NUM; i++)
	{
		if (!info->status[i])
			continue;
		if (n++ == dc_port)
			return i;
	}
	return -1;
}

static void ps3_log_state(u32 port)
{
	u8 trig = (lt[port] ? 1 : 0) | (rt[port] ? 2 : 0);
	u8 stick = (joyx[port] < 0 ? 1 : 0) | (joyx[port] > 0 ? 2 : 0) |
	           (joyy[port] < 0 ? 4 : 0) | (joyy[port] > 0 ? 8 : 0);
	if (kcode[port] == log_kcode[port] && trig == log_trig[port] && stick == log_stick[port])
		return;
	log_kcode[port] = kcode[port];
	log_trig[port] = trig;
	log_stick[port] = stick;
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
		rt[i] = lt[i] = 0;
	}
}

void UpdateInputState(u32 port)
{
	if (port >= MAX_CONTROLLERS)
		return;

	kcode[port] = 0xFFFF;
	joyx[port] = joyy[port] = 0;
	lt[port] = rt[port] = 0;

	padInfo info;
	int pad = -1;
	if (ps3_pad_ok && ioPadGetInfo(&info) == 0)
		pad = ps3_find_pad(&info, port);

	if (pad != ps3_pad_of_dc[port])
	{
		printf("[pad] dc%u -> ps3 pad %d\n", port, pad);
		ps3_pad_of_dc[port] = pad;
	}
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

	// Face buttons by position: Cross=A, Circle=B, Square=X, Triangle=Y.
	if (d->BTN_CROSS)    kcode[port] &= ~key_CONT_A;
	if (d->BTN_CIRCLE)   kcode[port] &= ~key_CONT_B;
	if (d->BTN_SQUARE)   kcode[port] &= ~key_CONT_X;
	if (d->BTN_TRIANGLE) kcode[port] &= ~key_CONT_Y;
	if (d->BTN_START)    kcode[port] &= ~key_CONT_START;
	if (d->BTN_UP)       kcode[port] &= ~key_CONT_DPAD_UP;
	if (d->BTN_DOWN)     kcode[port] &= ~key_CONT_DPAD_DOWN;
	if (d->BTN_LEFT)     kcode[port] &= ~key_CONT_DPAD_LEFT;
	if (d->BTN_RIGHT)    kcode[port] &= ~key_CONT_DPAD_RIGHT;

	// L2/R2 analog by pressure; digital bit or L1/R1 give full travel.
	u8 l = d->PRE_L2, r = d->PRE_R2;
	if (d->BTN_L1 || (d->BTN_L2 && l == 0)) l = 255;
	if (d->BTN_R1 || (d->BTN_R2 && r == 0)) r = 255;
	lt[port] = l;
	rt[port] = r;
	// maple_cfg masks C/D; set them anyway so kcode holds every pressed bit.
	if (l) kcode[port] &= ~key_CONT_D;
	if (r) kcode[port] &= ~key_CONT_C;

	// Raw stick is 0..255 with 255 = down, and DC positive Y is down too.
	joyx[port] = ClampAnalogValue((s32)d->ANA_L_H - 128);
	joyy[port] = ClampAnalogValue((s32)d->ANA_L_V - 128);

	ps3_log_state(port);
}

// Special peripherals are not supported on PS3; get_controller_type() is always 0.
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
