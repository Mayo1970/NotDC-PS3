// High-level emulation interface: init, start, stop, reset.

#include "mem/sh4_mem.h"
#include "mem/memutil.h"
#include "sh4/sh4_opcode_list.h"
#include "pvr/pvr_if.h"
#include "mem/sh4_internal_reg.h"
#include "aica/aica_if.h"
#include "maple/maple_if.h"
#include "dc.h"
#include "flash_fix.h"
#include "ps3/ps3_presets.h"
#include "config/config.h"
#include "plugins/plugin_manager.h"
#include "ps3/ps3_sndthr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ARM7 (vbaARM) forward declarations only: vbaARM.h redefines ReadMemArrRet/WriteMemArrRet.
// arm_Init() runs in libARM_Init() (plugin_manager.cpp); here only Reset and Term are needed.
extern void FASTCALL armTerm ();
extern void FASTCALL armReset(bool Manual);

// ELF boot hook – defined in nullDC.cpp, called after every CPU reset
// so the ELF entry point overrides the BIOS reset vector.
extern "C" void dc_elf_pre_run_hook();
extern "C" int get_jit_exit_preset();

static inline void arm_Term()             { armTerm(); }
static inline void arm_Reset(bool manual) { armReset(manual); }

// Global state flags
static bool dc_inited = false;
static bool dc_reseted = false;
static bool dc_running = false;

// Emulator thread states
enum emu_thread_state_t
{
	EMU_IDLE,
	EMU_CPU_START,
	EMU_SOFTRESET,
	EMU_NOP,
	EMU_QUIT,
	EMU_INIT,
	EMU_TERM,
	EMU_RESET,
	EMU_RESET_MANUAL,
};

// Emulator thread return values
enum emu_thread_rv_t
{
	RV_OK = 1,
	RV_ERROR = 2,
	RV_EXCEPTION = -2,
	RV_WAIT = -1,
};

static volatile emu_thread_state_t emu_thread_state = EMU_IDLE;
static volatile emu_thread_rv_t emu_thread_rv = RV_WAIT;

// Forward declarations
static void ResetDC(bool manual);
static emu_thread_rv_t ExecuteEmulatorCommand(emu_thread_state_t cmd);

static emu_thread_rv_t ExecuteEmulatorCommand(emu_thread_state_t cmd)
{
	emu_thread_state = cmd;
	
	while (emu_thread_state != EMU_QUIT)
	{
		switch (emu_thread_state)
		{
		case EMU_IDLE:
			return emu_thread_rv;

		case EMU_NOP:
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_OK;
			break;

		case EMU_CPU_START:
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_OK;
			dc_running = true;
			sh4_cpu.Run();
			snd_thread_stop();   // 5.14: resets and term run with the sound on this thread
			// P6 JIT EXIT: Run returns only on a stop request (XMB exit), and EMU_TERM never runs on the PS3.
			if (get_jit_exit_preset())
				mem_SaveFlash();
			dc_running = false;
			break;

		case EMU_SOFTRESET:
			emu_thread_state = EMU_CPU_START;
			ResetDC(true);
			break;

		case EMU_INIT:
			emu_thread_state = EMU_IDLE;

			if (!plugins_Init())
			{
				printf("ERROR: Plugin initialization failed\n");
				plugins_Term();
				emu_thread_rv = RV_ERROR;
				break;
			}

			sh4_cpu.Init();
			mem_Init();
			pvr_Init();
			aica_Init();   // AICA RAM is ready after this call
			// The ARM7 was already initialised by libARM_Init() inside plugins_Init() above.
			mem_map_defualt();

			emu_thread_rv = RV_OK;
			break;

		case EMU_TERM:
			emu_thread_state = EMU_IDLE;

			// Reverse init order. The ARM goes before AICA: it holds a pointer into aica_ram.
			arm_Term();
			aica_Term();
			pvr_Term();
			mem_Term();
			sh4_cpu.Term();
			plugins_Term();

			emu_thread_rv = RV_OK;
			break;

		case EMU_RESET:
			emu_thread_state = EMU_IDLE;
			ResetDC(false);
			emu_thread_rv = RV_OK;
			break;

		case EMU_RESET_MANUAL:
			emu_thread_state = EMU_IDLE;
			ResetDC(true);
			emu_thread_rv = RV_OK;
			break;

		case EMU_QUIT:
			emu_thread_rv = RV_OK;
			break;

		default:
			printf("ERROR: Unknown emulator state: %d\n", emu_thread_state);
			emu_thread_state = EMU_IDLE;
			emu_thread_rv = RV_ERROR;
			break;
		}
	}
	
	return emu_thread_rv;
}

bool Init_DC()
{
	if (dc_inited)
	{
		printf("WARNING: DC already initialized\n");
		return true;
	}

	printf("Initializing Dreamcast emulator...\n");

	if (!plugins_Load())
	{
		printf("ERROR: Failed to load plugins\n");
		return false;
	}

	if (ExecuteEmulatorCommand(EMU_INIT) != RV_OK)
	{
		printf("ERROR: Emulator initialization failed\n");
		return false;
	}

	dc_inited = true;
	printf("Dreamcast emulator initialized successfully\n");
	return true;
}

static void ResetDC(bool manual)
{
	printf("Resetting DC (%s)...\n", manual ? "manual" : "auto");
	snd_thread_stop();

	plugins_Reset(manual);
	sh4_cpu.Reset(manual);
	mem_Reset(manual);
	pvr_Reset(manual);
	aica_Reset(manual);

	// After aica_Reset(), which clears aica_ram on a hard reset, so the ARM7 never runs stale code.
	arm_Reset(manual);

	// ELF boot: the ELF entry replaces the BIOS reset vector. After sh4_cpu.Reset(), so our PC
	// wins over 0xA0000000.
	dc_elf_pre_run_hook();
}

// Soft reset; safe while the CPU runs.
bool SoftReset_DC()
{
	if (!dc_inited)
	{
		printf("ERROR: Cannot soft reset - DC not initialized\n");
		return false;
	}

	if (sh4_cpu.IsCpuRunning())
	{
		printf("Performing soft reset...\n");
		sh4_cpu.Stop();
		ExecuteEmulatorCommand(EMU_SOFTRESET);
		return true;
	}
	
	printf("WARNING: Soft reset requested but CPU not running\n");
	return false;
}

// Holly reset (SB_SFRES = 0x7611): peripherals only. The SH4 issued the write and keeps running, so no
// CPU or RAM reset; aica_Reset(true) keeps AICA RAM, so the ARM7 order of ResetDC() does not apply.
void HollySoftReset()
{
	printf("Performing Holly soft reset (peripherals only, CPU keeps running)...\n");

	snd_lock(SND_LOCK_OTHER);
	plugins_Reset(true);
	pvr_Reset(true);
	aica_Reset(true);
	arm_Reset(true);
	snd_unlock();
}

bool Reset_DC(bool manual)
{
	if (!dc_inited)
	{
		printf("ERROR: Cannot reset - DC not initialized\n");
		return false;
	}

	if (sh4_cpu.IsCpuRunning())
	{
		printf("ERROR: Cannot reset while CPU is running\n");
		return false;
	}

	if (manual)
		ExecuteEmulatorCommand(EMU_RESET_MANUAL);
	else
		ExecuteEmulatorCommand(EMU_RESET);

	dc_reseted = true;
	return true;
}

void Term_DC()
{
	if (!dc_inited)
		return;

	printf("Terminating Dreamcast emulator...\n");
	
	Stop_DC();
	ExecuteEmulatorCommand(EMU_TERM);
	ExecuteEmulatorCommand(EMU_QUIT);
	
	dc_inited = false;
	dc_reseted = false;
	dc_running = false;
	
	printf("Dreamcast emulator terminated\n");
}

// Returns base_path + filename in a malloc'd string; the caller frees it.
static char* BuildFilePath(const char* base_path, const char* filename)
{
	if (!base_path || !filename)
		return NULL;

	size_t base_len = strlen(base_path);
	size_t file_len = strlen(filename);
	
	// Allocate space for base + filename + null terminator
	char* full_path = (char*)malloc(base_len + file_len + 1);
	if (!full_path)
	{
		printf("ERROR: Failed to allocate memory for file path\n");
		return NULL;
	}

	// Copy base path and append filename
	strcpy(full_path, base_path);
	strcat(full_path, filename);

	return full_path;
}

// P85: sh4_mem.cpp's forced-exit save writes the flash only while this is set.
extern volatile bool g_flash_loaded;
void maple_vmu_game(const char* id10);   // dc/maple/maple_devs.cpp (P37)

// Cable forced for this boot only; -1 = the nullDC.cfg value. Read by bsc.cpp read_BSC_PDTRA.
int g_cable_override = -1;

// Flycast loadSpecialSettings: region and cable from the disc's IP.BIN (area symbols at 0x30,
// VGA flag at 0x3D). Byte reads only, so host endianness does not matter.
static void apply_disc_meta()
{
	g_cable_override = -1;
	u32 type = libGDR_GetDiscType();
	if (type == NoDisk || type == Open || type == Busy || type == CdDA)
		return;

	u32 fad = 45150;   // GD-ROM high-density area
	if (type != GdRom)
	{
		// CD: IP.BIN opens the last session (Flycast Disc::GetBaseFAD).
		u8 ses[6];
		libGDR_GetSessionInfo(ses, 0);
		if (ses[2] == 0)
			return;
		libGDR_GetSessionInfo(ses, ses[2]);
		fad = (ses[3] << 16) | (ses[4] << 8) | ses[5];
	}

	static u8 ip[2048];
	libGDR_ReadSector(ip, fad, 1, sizeof(ip));
	if (memcmp(ip, "SEGA SEGAKATANA ", 16) != 0)
	{
		printf("[disc] no IP.BIN at FAD %u, region and cable kept\n", fad);
		return;
	}
	dc_flash_set_region((const char*)&ip[0x30]);
	// P37: the product number, for row "VMU Per Game".
	maple_vmu_game((const char*)&ip[0x40]);
	if (settings.dreamcast.cable <= 1 && ip[0x3D] != '1')
	{
		g_cable_override = 3;
		printf("[disc] no VGA support, TV composite for this boot\n");
	}
}

void LoadBiosFiles()
{
	printf("Loading BIOS files...\n");

	char* base_path = GetEmuPath("data/");
	if (!base_path)
	{
		printf("ERROR: Failed to get emulator data path\n");
		return;
	}

	bool any_loaded = false;

	// Load boot ROM
	char* boot_path = BuildFilePath(base_path, "dc_boot.bin");
	if (boot_path)
	{
		if (LoadFileToSh4Bootrom(boot_path))
		{
			printf("Loaded dc_boot.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load dc_boot.bin\n");
		}
		free(boot_path);
	}

	// Try to load saved flash first, fallback to default
	bool flash_ok = false;
	g_flash_loaded = false;
	char* flash_path = BuildFilePath(base_path, "dc_flash_wb.bin");
	if (flash_path)
	{
		if (LoadFileToSh4Flashrom(flash_path))
		{
			printf("Loaded dc_flash_wb.bin (writeback)\n");
			any_loaded = true;
			flash_ok = true;
		}
		else
		{
			printf("No writeback flash found, trying default...\n");
			free(flash_path);
			
			flash_path = BuildFilePath(base_path, "dc_flash.bin");
			if (flash_path)
			{
				if (LoadFileToSh4Flashrom(flash_path))
				{
					printf("Loaded dc_flash.bin\n");
					any_loaded = true;
					flash_ok = true;
				}
				else
				{
					printf("WARNING: Failed to load dc_flash.bin\n");
				}
			}
		}
		if (flash_path)
			free(flash_path);
		// P85: no flash file at all.
		if (!flash_ok && g_flash_defaults_preset)
		{
			dc_flash_build();
			flash_ok = true;
		}
		if (any_loaded)
		{
			dc_flash_set_time(settings.dreamcast.RTC);
			dc_flash_fixup(settings.dreamcast.RTC);
			apply_disc_meta();
			dc_flash_report();
		}
		g_flash_loaded = flash_ok;
	}

	// Load syscalls
	char* syscalls_path = BuildFilePath(base_path, "syscalls.bin");
	if (syscalls_path)
	{
		if (LoadFileToSh4Mem(0x00000, syscalls_path))
		{
			printf("Loaded syscalls.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load syscalls.bin\n");
		}
		free(syscalls_path);
	}

	// Load IP.BIN
	char* ip_path = BuildFilePath(base_path, "IP.bin");
	if (ip_path)
	{
		if (LoadFileToSh4Mem(0x08000, ip_path))
		{
			printf("Loaded IP.bin\n");
			any_loaded = true;
		}
		else
		{
			printf("WARNING: Failed to load IP.bin\n");
		}
		free(ip_path);
	}

	free(base_path);

	if (any_loaded)
		printf("BIOS files loaded\n");
	else
		printf("WARNING: No BIOS files were loaded successfully\n");
}

void Start_DC()
{
	printf("Starting Dreamcast emulator...\n");

	if (sh4_cpu.IsCpuRunning())
	{
		printf("WARNING: CPU already running\n");
		return;
	}

	if (!dc_inited)
	{
		if (!Init_DC())
		{
			printf("ERROR: Failed to initialize DC\n");
			return;
		}
	}

	if (!dc_reseted)
	{
		printf("Performing initial reset...\n");
		if (!Reset_DC(false))
		{
			printf("ERROR: Failed to reset DC\n");
			return;
		}
	}

	if (ExecuteEmulatorCommand(EMU_CPU_START) != RV_OK)
	{
		printf("ERROR: Failed to start CPU\n");
		return;
	}

	printf("Dreamcast emulator started\n");
}

void Stop_DC()
{
	if (!dc_inited)
		return;

	if (sh4_cpu.IsCpuRunning())
	{
		printf("Stopping Dreamcast emulator...\n");
		sh4_cpu.Stop();
		ExecuteEmulatorCommand(EMU_NOP);
		printf("Dreamcast emulator stopped\n");
	}
}

bool IsDCInited()
{
	return dc_inited;
}

bool IsDCRunning()
{
	return dc_running;
}
