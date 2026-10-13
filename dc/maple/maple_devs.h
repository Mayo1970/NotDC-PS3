#pragma once
#include "types.h"

enum MapleDeviceType
{
	MDT_SegaController,
	MDT_SegaVMU,
	MDT_LightGun,       // Dreamcast light gun / Stunner
	MDT_Maracas,        // Samba de Amigo maracas
	MDT_Keyboard,       // DC keyboard / Typing of Dead
	MDT_FishingRod,     // Sega Bass Fishing rod
	MDT_FullController, // G12: 6 axes, the "Panther DC" controller
	MDT_PuruPuru,       // P13: vibration pack

	MDT_Count
};

struct IMapleConfigMap;

struct maple_device
{
	u8 maple_port;	//raw maple port
	u8 bus_port;	//0 .. 5
	u8 bus_id;		//0 .. 3
	wchar logical_port[3];	//A0, etc
	IMapleConfigMap* config;

	//fill in the info
	void Setup(u32 prt);

	virtual void OnSetup(){};
	// P37: the boot disc's ID is known (maple_vmu_game).
	virtual void OnGame(){};
	virtual ~maple_device();
	virtual u32 Dma(u32 Command,u32* buffer_in,u32 buffer_in_len,u32* buffer_out,u32& buffer_out_len)=0;
};

maple_device* maple_Create(MapleDeviceType type);
// P37: id10 = the 10 bytes at IP.BIN 0x40 of the boot disc; the VMUs open their per-game files.
void maple_vmu_game(const char* id10);