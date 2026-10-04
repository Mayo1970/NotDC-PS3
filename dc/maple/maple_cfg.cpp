#include "types.h"
#include "maple_if.h"
#include "maple_helper.h"
#include "maple_devs.h"
#include "maple_cfg.h"

// Per-device input hooks; on PS3 they are in ps3/ps3_input.cpp (plugs/drkMapleDevices is not built).
void UpdateInputState(u32 port);        // Standard controller
void UpdateLightGunState(u32 port);     // Light gun
void UpdateMaracasState(u32 port);      // Maracas
void UpdateKeyboardState(u32 port);     // Keyboard
void UpdateFishingRodState(u32 port);   // Fishing rod

// Init functions — called once before the emulator starts
void InitControllers(void);
void InitLightGun(void);
void InitMaracas(void);
void InitKeyboard(void);
void InitFishingRod(void);

// From ps3_presets.cpp; always 0 (standard pad) on PS3.
extern "C" int get_controller_type();  // 0=standard 1=lightgun 2=maracas 3=keyboard 4=fishing

extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4], joyy[4];
extern u8  rt[4], lt[4];

// Signed axis [-128..127] to the Maple byte [0..255]; centre 0 maps to 0x80.
static inline u8 SignedToByte(s8 val)
{
	return static_cast<u8>(static_cast<s32>(val) + 128);
}

// Platform input update for the active controller type. The keyboard also comes
// through here, via GetInput(nullptr), and reads its own report afterwards.
static void DispatchUpdateInputState(u32 port)
{
	switch (get_controller_type())
	{
	case 1:  UpdateLightGunState(port);   break;
	case 2:  UpdateMaracasState(port);    break;
	case 3:  UpdateKeyboardState(port);   break;
	case 4:  UpdateFishingRodState(port); break;
	default: UpdateInputState(port);      break;
	}
}

// Bridges the platform input globals to PlainJoystickState. The keyboard passes pjs=nullptr
// only to trigger the scan; maple_keyboard::dma() reads GetKeyboardReport() itself.
struct MapleConfigMap : IMapleConfigMap
{
	maple_device* dev;

	explicit MapleConfigMap(maple_device* dev) : dev(dev) {}

	void GetInput(PlainJoystickState* pjs) override
	{
		if (!dev)
			return;

		DispatchUpdateInputState(dev->bus_id);

		// Keyboard: caller passes nullptr — nothing else to fill in here.
		if (!pjs)
			return;

		// kcode is active-low; bits 0, 8 and 11-15 (0xF901) are reserved by the Maple
		// protocol, so they are forced to read as "not pressed".
		pjs->kcode = kcode[dev->bus_id] | 0xF901;

		pjs->joy[PJAI_X1] = SignedToByte(joyx[dev->bus_id]);
		pjs->joy[PJAI_Y1] = SignedToByte(joyy[dev->bus_id]);
		// X2/Y2 not driven by this source; leave them centered (set in ctor).

		pjs->trigger[PJTI_R] = rt[dev->bus_id];
		pjs->trigger[PJTI_L] = lt[dev->bus_id];
	}

	void SetImage(void* /*img*/) override
	{
		// No VMU LCD output on PS3.
	}
};

// Creates a single Maple device, wires up its config map, and registers it.
static bool mcfg_Create(MapleDeviceType type, u32 bus, u32 port)
{
	maple_device* dev = maple_Create(type);
	if (!dev)
		return false;

	dev->Setup(maple_GetAddress(bus, port));
	dev->config = new MapleConfigMap(dev);
	dev->OnSetup();
	MapleDevices[bus][port] = dev;
	return true;
}

// Maps the menu's controller type number to its MapleDeviceType.
static MapleDeviceType mcfg_GetControllerDeviceType()
{
	switch (get_controller_type())
	{
	case 1:  return MDT_LightGun;
	case 2:  return MDT_Maracas;
	case 3:  return MDT_Keyboard;
	case 4:  return MDT_FishingRod;
	default: return MDT_SegaController;
	}
}

// One-time init for the selected device; the standard pad gets InitControllers().
static void mcfg_InitSpecialDevice()
{
	switch (get_controller_type())
	{
	case 1:  InitLightGun();    break;
	case 2:  InitMaracas();     break;
	case 3:  InitKeyboard();    break;
	case 4:  InitFishingRod();  break;
	default: InitControllers(); break;
	}
}

// g_player_count comes from nullDC.cfg [ps3] Players (ps3_presets.cpp), loaded before EmuMain().
extern "C" int get_player_count();

// Per player: port 5 = controller, ports 0 and 1 = VMU slots 1 and 2 (standard pad only).
void mcfg_CreateDevices()
{
	// plugins_Load() runs twice per boot; free the first set so each VMU file has one open handle.
	mcfg_DestroyDevices();

	MapleDeviceType ctrlType = mcfg_GetControllerDeviceType();
	bool isStandard = (ctrlType == MDT_SegaController);

	// One-time hardware / data-format initialisation
	mcfg_InitSpecialDevice();

	// --- Bus 0 : Player 1 ---
	mcfg_Create(ctrlType,       0, 5);  // Main controller

	// No VMUs on special devices: Confidential Mission rejected a lightgun port with one.
	// Probably wrong, though: the official lightgun has a VMU slot.
	if (isStandard)
	{
		mcfg_Create(MDT_SegaVMU, 0, 0);  // VMU slot A1
		mcfg_Create(MDT_SegaVMU, 0, 1);  // VMU slot A2
	}

	// --- Buses 1-3 : Players 2-4 (when 2/3/4-player mode is active) ---
	int players = get_player_count();
	for (u32 bus = 1; bus < (u32)players && bus < 4; bus++)
	{
		mcfg_Create(ctrlType,     bus, 5);
		if (isStandard)
		{
			mcfg_Create(MDT_SegaVMU, bus, 0);
			mcfg_Create(MDT_SegaVMU, bus, 1);
		}
	}
}

void mcfg_DestroyDevices()
{
	for (int bus = 0; bus < 4; bus++)
	{
		for (int port = 0; port < 6; port++)
		{
			if (MapleDevices[bus][port])
			{
				delete MapleDevices[bus][port];
				MapleDevices[bus][port] = nullptr;
			}
		}
	}
}