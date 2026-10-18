#include "types.h"
#include "maple_if.h"
#include "maple_helper.h"
#include "maple_devs.h"
#include "maple_cfg.h"

// Per-device input hooks, in ps3/ps3_input.cpp.
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

// From ps3_presets.cpp; 0 (standard pad) or 5 (full controller) on PS3.
extern "C" int get_controller_type();  // 0=standard 1=lightgun 2=maracas 3=keyboard 4=fishing 5=full (2 sticks)
// G15: the CONTROLS slot rows, bus * 2 + slot: 0 = nothing, 1 = a VMU, 2 = the Puru Puru pack (its motor: P13).
extern "C" int get_slot_dev_preset(u32 slot);
void UpdateVibration(u32 port, float power, float inclination, u32 duration_ms);
// G15, ps3/ps3_input.cpp: the ports that have a PS3 pad, bit N = port N, below cap.
u32 ps3_pad_ports(u32 cap);
// ps3/ps3_vmulcd.cpp: row "VMU Screen" (plan section 7, bundle 16).
void ps3_vmulcd_push(u32 bus, u32 port, const u8* img);
void ps3_vmulcd_reset(void);
void ps3_vmulcd_unplug(u32 bus);

extern u16 kcode[4];
extern u32 vks[4];
extern s8  joyx[4], joyy[4];
extern s8  joyrx[4], joyry[4];
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
		// G12: the right stick; 0 (centred) unless the full controller is selected.
		pjs->joy[PJAI_X2] = SignedToByte(joyrx[dev->bus_id]);
		pjs->joy[PJAI_Y2] = SignedToByte(joyry[dev->bus_id]);

		pjs->trigger[PJTI_R] = rt[dev->bus_id];
		pjs->trigger[PJTI_L] = lt[dev->bus_id];
	}

	void SetImage(void* img) override
	{
		// P37: the picture goes to the TV when row "VMU Screen" is on.
		if (dev)
			ps3_vmulcd_push(dev->bus_id, dev->bus_port, (const u8*)img);
	}

	void SetVibration(float power, float inclination, u32 duration_ms) override
	{
		if (dev)
			UpdateVibration(dev->bus_id, power, inclination, duration_ms);
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
	case 5:  return MDT_FullController;
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

// The Players row (ps3_presets.cpp), loaded before EmuMain(): the most ports since G15.
extern "C" int get_player_count();

// One player: port 5 = controller, ports 0 and 1 = slots 1 and 2 (standard pad and full controller only).
static void mcfg_PlugBus(u32 bus)
{
	MapleDeviceType ctrlType = mcfg_GetControllerDeviceType();
	bool isStandard = (ctrlType == MDT_SegaController || ctrlType == MDT_FullController);
	mcfg_Create(ctrlType, bus, 5);

	// No VMUs on special devices: Confidential Mission rejected a lightgun port with one.
	// Probably wrong, though: the official lightgun has a VMU slot.
	if (!isStandard)
		return;
	for (u32 slot = 0; slot < 2; slot++)
	{
		const int dev = get_slot_dev_preset(bus * 2 + slot);
		if (dev == 1 || dev == 2)
			mcfg_Create(dev == 1 ? MDT_SegaVMU : MDT_PuruPuru, bus, slot);
	}
}

static void mcfg_UnplugBus(u32 bus)
{
	for (int port = 0; port < 6; port++)
	{
		delete MapleDevices[bus][port];
		MapleDevices[bus][port] = nullptr;
	}
	ps3_vmulcd_unplug(bus);
}

void mcfg_CreateDevices()
{
	// plugins_Load() runs twice per boot; free the first set so each VMU file has one open handle.
	mcfg_DestroyDevices();

	// One-time hardware / data-format initialisation
	mcfg_InitSpecialDevice();

	// G15: only the ports that have a PS3 pad, at most Players of them; mcfg_HotPlug() follows the pads after this.
	const u32 ports = ps3_pad_ports((u32)get_player_count());
	for (u32 bus = 0; bus < 4; bus++)
		if ((ports >> bus) & 1)
			mcfg_PlugBus(bus);
}

// G15: at each maple DMA a port is plugged while its PS3 pad is there; the game sees it come and go.
void mcfg_HotPlug()
{
	const u32 ports = ps3_pad_ports((u32)get_player_count());
	for (u32 bus = 0; bus < 4; bus++)
	{
		const bool want = (ports >> bus) & 1;
		if (want == (MapleDevices[bus][5] != nullptr))
			continue;
		if (want)
			mcfg_PlugBus(bus);
		else
			mcfg_UnplugBus(bus);
		printf("[maple] port %c plugged=%d\n", 'A' + bus, (int)want);
	}
}

void mcfg_DestroyDevices()
{
	ps3_vmulcd_reset();
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
