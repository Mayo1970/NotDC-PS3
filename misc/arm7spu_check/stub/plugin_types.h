// PC stand-in for plugins/plugin_types.h: enough for the vbaARM header chain.
#pragma once

enum PluginType { Plugin_ARM = 1, Plugin_GFX = 2, Plugin_SPU = 3, Plugin_SH4 = 4 };

struct emu_info;
struct arm_init_params { u8* aica_ram; };

#define ARAM_SIZE (2*1024*1024)
#define ARAM_MASK (ARAM_SIZE-1)
