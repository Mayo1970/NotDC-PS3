// drkPvr.cpp: the PVR plugin's global state, settings and entry points.

#include "drkPvr.h"
#include "ta.h"
#include "spg.h"
#include "regs.h"
#include "Renderer_if.h"
#include "config/config.h"
#include <algorithm>

// Configuration constants
#define params PVRPARAMS

// Global state
wchar emu_name[512];
pvr_init_params params;
_settings_type settings;

// Feature flags (currently unused but reserved for future use)
u32 enable_FS_mid = 0;
u32 AA_mid_menu = 0;
u32 AA_mid_0 = 0;

// Callback for a write (at addr) to a locked VRAM block. Disabled; texture-cache invalidation would hook in here.
void FASTCALL libPvr_vramLockCB(vram_block* block, u32 addr)
{
}

// One-time init when the emulator first loads the plugin.
s32 FASTCALL libPvr_Load()
{
    LoadSettings();
    printf("drkpvr: Using %s\n", REND_NAME);
    return rv_ok;
}

// Only called if libPvr_Init succeeded.
void FASTCALL libPvr_Unload()
{
    // Nothing to cleanup currently
}

// Leaves VRAM alone (the emulator resets it). Manual = user-triggered reset, false = automatic.
void FASTCALL libPvr_Reset(bool Manual)
{
    Regs_Reset(Manual);
    spg_Reset(Manual);
    rend_reset(Manual);
}

// Runs on the new SH4 thread as it starts (thread-specific init); param carries the VRAM pointer.
s32 FASTCALL libPvr_Init(pvr_init_params* param)
{
    // Store initialization parameters
    memcpy(&params, param, sizeof(params));

    // Initialize subsystems in order, with proper cleanup on failure
    if (!Regs_Init())
    {
        printf("drkpvr: Failed to initialize registers\n");
        return rv_error;
    }
    
    if (!spg_Init())
    {
        printf("drkpvr: Failed to initialize SPG\n");
        Regs_Term();  // Cleanup on failure
        return rv_error;
    }
    
    if (!rend_init())
    {
        printf("drkpvr: Failed to initialize renderer\n");
        spg_Term();   // Cleanup on failure
        Regs_Term();
        return rv_error;
    }

    // Start renderer thread (only the renderer needs thread-specific initialization)
    if (!rend_thread_start())
    {
        printf("drkpvr: Failed to start render thread\n");
        rend_term();  // Cleanup on failure
        spg_Term();
        Regs_Term();
        return rv_error;
    }

    return rv_ok;
}

// Runs on the SH4 thread as it exits (thread-specific cleanup).
void FASTCALL libPvr_Term()
{
    // Cleanup in reverse order of initialization
    rend_thread_end();
    rend_term();
    spg_Term();
    Regs_Term();
}

// Forward declaration for utility function
char* GetNullDCSoruceFileName(char* full);

int cfgGetInt(const char* key, int def)
{
    return cfgLoadInt("drkpvr", key, def);
}

void cfgSetInt(const char* key, int val)
{
    cfgSaveInt("drkpvr", key, val);
}

// Keys missing from the config take the defaults below.
void LoadSettings()
{
    // Emulation settings - affect accuracy and compatibility
    settings.Emulation.AlphaSortMode    = cfgGetInt("Emulation.AlphaSortMode", 1);
    settings.Emulation.PaletteMode      = cfgGetInt("Emulation.PaletteMode", 1);
    settings.Emulation.ModVolMode       = cfgGetInt("Emulation.ModVolMode", 1);
    settings.Emulation.ZBufferMode      = cfgGetInt("Emulation.ZBufferMode", 0);

    // OSD settings - display overlays
    settings.OSD.ShowFPS                = cfgGetInt("OSD.ShowFPS", 0);
    settings.OSD.ShowStats              = cfgGetInt("OSD.ShowStats", 0);

    // Fullscreen settings - defaults to auto-detect (-1)
    settings.Fullscreen.Enabled         = cfgGetInt("Fullscreen.Enabled", 0);
    settings.Fullscreen.Res_X           = cfgGetInt("Fullscreen.Res_X", -1);
    settings.Fullscreen.Res_Y           = cfgGetInt("Fullscreen.Res_Y", -1);
    settings.Fullscreen.Refresh_Rate    = cfgGetInt("Fullscreen.Refresh_Rate", -1);

    // Enhancement settings - visual improvements
    settings.Enhancements.MultiSampleCount   = cfgGetInt("Enhancements.MultiSampleCount", 0);
    settings.Enhancements.MultiSampleQuality = cfgGetInt("Enhancements.MultiSampleQuality", 0);
    settings.Enhancements.AspectRatioMode    = cfgGetInt("Enhancements.AspectRatioMode", 1);
}

// Called when the user changes settings or on clean shutdown.
void SaveSettings()
{
    // Emulation settings
    cfgSetInt("Emulation.AlphaSortMode", settings.Emulation.AlphaSortMode);
    cfgSetInt("Emulation.PaletteMode", settings.Emulation.PaletteMode);
    cfgSetInt("Emulation.ModVolMode", settings.Emulation.ModVolMode);
    cfgSetInt("Emulation.ZBufferMode", settings.Emulation.ZBufferMode);

    // OSD settings
    cfgSetInt("OSD.ShowFPS", settings.OSD.ShowFPS);
    cfgSetInt("OSD.ShowStats", settings.OSD.ShowStats);

    // Fullscreen settings
    cfgSetInt("Fullscreen.Enabled", settings.Fullscreen.Enabled);
    cfgSetInt("Fullscreen.Res_X", settings.Fullscreen.Res_X);
    cfgSetInt("Fullscreen.Res_Y", settings.Fullscreen.Res_Y);
    cfgSetInt("Fullscreen.Refresh_Rate", settings.Fullscreen.Refresh_Rate);

    // Enhancement settings
    cfgSetInt("Enhancements.MultiSampleCount", settings.Enhancements.MultiSampleCount);
    cfgSetInt("Enhancements.MultiSampleQuality", settings.Enhancements.MultiSampleQuality);
    cfgSetInt("Enhancements.AspectRatioMode", settings.Enhancements.AspectRatioMode);
}
