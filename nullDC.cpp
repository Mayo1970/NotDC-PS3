// nullDC.cpp : Defines the entry point for the console application.
//

//initialse Emu
#include "types.h"
#include "dc/mem/_vmem.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "stdclass.h"
#include "dc/dc.h"
#include "config/config.h"
#include "plugins/plugin_manager.h"
#include "cl/cl.h"
#include "plugs/ImgReader/elf_loader.h"   // ELF boot support
#include <time.h>

// Frameskipping (preparation : working on it)
int g_current_frameskip = 0; // 0 = no skip, 1 = skip 1 frame, 2 = skip 2 frame
int g_frame_counter = 0;


__settings settings;

// Defined in ps3_presets.cpp — menu override for settings.emulator.AudioBuffers (-1 = keep the cfg value).
extern "C" int get_audio_buffers_preset();

// Defined in ps3_presets.cpp — menu preset selecting the SH4 core
// back-end (0=Interpreter, 1=Dynarec, default).
extern "C" int get_dynarec_preset();

// ---- ELF boot state ----
// Set by main___() for a .elf; consumed by RunDC() after Reset() so the entry point wins over the BIOS vector.
static bool  g_elf_pending = false;
static u32   g_elf_entry   = 0;

// ---- Helper: case-insensitive check for a file extension ----
static bool HasExtension(const char* path, const char* ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    const char* tail = path + plen - elen;
    // Compare ignoring case
    for (size_t i = 0; i < elen; i++)
    {
        char a = tail[i] >= 'A' && tail[i] <= 'Z' ? tail[i] + 32 : tail[i];
        char b = ext[i]  >= 'A' && ext[i]  <= 'Z' ? ext[i]  + 32 : ext[i];
        if (a != b) return false;
    }
    return true;
}

//mainloop
int RunDC()
{ 

	if(settings.dynarec.Enable)
	{
		Get_Sh4Recompiler(&sh4_cpu);
		printf("[nullDC.cpp] Using Recompiler (Dynarec Enable)\n");
	}
	else
	{
		Get_Sh4Interpreter(&sh4_cpu);
		printf("[nullDC.cpp] Using Interpreter\n");
	}

	// ELF boot: Start_DC() runs sh4_cpu.Reset(), which points nextpc at the BIOS vector (0xA0000000); the ELF entry must
	// override it after that Reset and before the first instruction fetch.
	if (g_elf_pending)
	{
		// Only logged here: dc.cpp calls dc_elf_pre_run_hook() (below) between Reset() and Run() to write the PC.
		printf("[nullDC.cpp] [ELF] Boot mode active – entry will be applied after CPU reset\n");
	}

	Start_DC();	//this call is blocking ...
	
	return 0;
}

// Called by dc.cpp right after sh4_cpu.Reset(), before the execution loop starts.
extern "C" void dc_elf_pre_run_hook()
{
	if (g_elf_pending)
	{
		elf_apply_entry(g_elf_entry);
		g_elf_pending = false;

		// Flush the block table AND the code cache: bm_Reset() only clears the hash table, and BIOS blocks still
		// hold direct pointers into the compiled code; sh4_cpu.ResetCache() empties the emit buffer.
		extern void bm_Reset();
		bm_Reset();
		sh4_cpu.ResetCache();
	}
}

//////////////////////////////////////

//command line parsing & init
int main___(int argc,char* argv[])
{
	if(ParseCommandLine(argc,argv))
	{
		printf("\n\n[nullDC.cpp] (Exiting due to command line, without starting nullDC)\n");
		return 69;
	}

	if(!cfgOpen())
	{
		msgboxf(_T("[nullDC.cpp] Unable to open config file"),MBX_ICONERROR);
		return -4;
	}
	printf("[nullDC] cfgOpen OK\n");
	LoadSettings();
	printf("[nullDC] LoadSettings done\n");

	int rv= 0;

	char* temp_path=GetEmuPath(_T("data/dc_boot.bin"));
	printf("[nullDC] checking BIOS: %s\n", temp_path);
	FILE* fr=fopen(temp_path,"r");
	if (!fr)
	{
		msgboxf(_T("Unable to find bios -- exiting\n%s"),MBX_ICONERROR,temp_path);
		rv=-3;
		goto cleanup;
	}
	fclose(fr);
	printf("[nullDC] BIOS found OK\n");
	free(temp_path);

	temp_path=GetEmuPath(_T("data/dc_flash.bin"));
	printf("[nullDC] checking flash: %s\n", temp_path);
	fr=fopen(temp_path,"r");
	if (!fr)
	{
		msgboxf(_T("Unable to find flash -- exiting\n%s"),MBX_ICONERROR,temp_path);
		rv=-6;
		goto cleanup;
	}
	fclose(fr);
	printf("[nullDC] flash found OK\n");
	free(temp_path);

	printf("[nullDC] Loading plugins!\n");
	while (!plugins_Load())
	{
		//if (!plugins_Select())
		{
			msgboxf("Unable to load plugins -- exiting\n",MBX_ICONERROR);
			rv = -2;
			goto cleanup;
		}
	}
	printf("[nullDC] plugins_Load OK\n");

	// ELF detection: a selected .elf gets its segments loaded into emulated RAM now (memory reserved, plugins loaded);
	// the PC override happens later, in dc_elf_pre_run_hook() after the CPU back-end reset.
	{
		// Read the selected path directly – we cannot use os_GetFile() here because
		// we already patched it to return false for .elf files (to block the disc plugin).
		extern char selectedFilePath[512];
		const char* elfPath = selectedFilePath;

		if (HasExtension(elfPath, ".elf"))
		{
			printf("[ELF] Detected ELF file: %s\n", elfPath);
			u32 entry = 0;
			if (elf_load(elfPath, &entry))
			{
				g_elf_entry   = entry;
				g_elf_pending = true;
				printf("[ELF] Segments loaded, will boot to 0x%08X\n", entry);
			}
			else
			{
				printf("[ELF] Failed to load ELF – falling back to BIOS\n");
				// g_elf_pending stays false → normal BIOS boot
			}
		}
	}

	printf("[nullDC] calling RunDC()...\n");
	rv = RunDC();
	printf("[nullDC] RunDC() returned rv=%d\n", rv);

cleanup:

	printf("__Cleanup\n");

	SaveSettings();
	return rv;
}

//entry point, platform specific main() calls this when done with init/stuff
int EmuMain(int argc, char* argv[])
{
	printf("[nullDC] EmuMain: start\n");
	printf(VER_FULLNAME " starting up ..\n");

	printf("[nullDC] EmuMain: _vmem_reserve()...\n");
	if (!_vmem_reserve())
	{
		msgboxf("Unable to reserve nullDC memory ...",MBX_OK | MBX_ICONERROR);
		return -5;
	}
	printf("[nullDC] EmuMain: _vmem_reserve OK\n");

	printf("[nullDC] EmuMain: calling main___()...\n");
	int rv=main___(argc,argv);
	printf("[nullDC] EmuMain: main___() returned rv=%d\n", rv);

// The PS3 keeps DC memory: SPUs and the render thread use it until the process ends (P6 JIT EXIT returns here).
#if !defined(_ANDROID) && HOST_OS != OS_PS3
	_vmem_release();
#endif

	return rv;
}


void LoadSettings()
{
	printf("[nullDC.cpp] Loading settings\n");

	// SH4 core back-end: menu preset (ps3_presets.cpp), default Dynarec.
	// 0 = Interpreter, 1 = Dynarec.
	settings.dynarec.Enable = (get_dynarec_preset() != 0);
	settings.dynarec.CPpass=cfgLoadInt("nullDC","Dynarec.DoConstantPropagation",1)!=0;
	settings.dynarec.UnderclockFpu=cfgLoadInt("nullDC","Dynarec.UnderclockFpu",0)!=0;
	settings.dynarec.safemode=cfgLoadInt("nullDC","Dynarec.SafeMode",0)!=0;

	settings.dreamcast.cable=cfgLoadInt("nullDC","Dreamcast.Cable",3);
	// Host clock at every boot, as Flycast does; a cfg value would freeze the DC date.
	settings.dreamcast.RTC=GetRTC_now();
	printf("[nullDC] RTC=%08X (time()=%ld)\n", settings.dreamcast.RTC, (long)time(NULL));

	settings.emulator.AutoStart=cfgLoadInt("nullDC","Emulator.AutoStart",0)!=0;
	settings.emulator.NoConsole=cfgLoadInt("nullDC","Emulator.NoConsole",0)!=0;
	settings.emulator.AudioBuffers=cfgLoadInt("nullDC","Emulator.AudioBuffers",0);
	if (settings.emulator.AudioBuffers>3)
		settings.emulator.AudioBuffers=3;

	// The menu preset (ps3_presets.cpp) is applied after the cfg value, so a menu choice always wins.
	int audio_buffers_preset = get_audio_buffers_preset();
	if (audio_buffers_preset >= 0 && audio_buffers_preset <= 3)
		settings.emulator.AudioBuffers = audio_buffers_preset;

	printf("[nullDC.cpp] Loaded settings\n");
}
void SaveSettings()
{
	cfgSaveInt("nullDC","Dynarec.Enabled",settings.dynarec.Enable);
	cfgSaveInt("nullDC","Dynarec.DoConstantPropagation",settings.dynarec.CPpass);
	cfgSaveInt("nullDC","Dynarec.UnderclockFpu",settings.dynarec.UnderclockFpu);
	cfgSaveInt("nullDC","Dynarec.SafeMode",settings.dynarec.safemode);
	cfgSaveInt("nullDC","Dreamcast.Cable",settings.dreamcast.cable);
	cfgSaveInt("nullDC","Dreamcast.RTC",settings.dreamcast.RTC);
	cfgSaveInt("nullDC","Emulator.AutoStart",settings.emulator.AutoStart);
	cfgSaveInt("nullDC","Emulator.NoConsole",settings.emulator.NoConsole);
	cfgSaveInt("nullDC","Emulator.AudioBuffers",settings.emulator.AudioBuffers);
}
