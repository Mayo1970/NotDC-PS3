// dc.h - Dreamcast emulator core interface: init, resets, start/stop and BIOS loading.

#ifndef DC_H
#define DC_H

#ifdef __cplusplus
extern "C" {
#endif

// Call before any other emulation function. False on failure.
bool Init_DC();

// Safe while the CPU runs: it is stopped, reset and restarted. False if the CPU was not running.
bool SoftReset_DC();

// Holly reset (SB_SFRES = 0x7611) that the BIOS/IP.BIN issues mid-boot to set the video timing. Safe from inside
// SH4 execution: it resets only Holly peripherals (PVR/SPG, renderer, AICA, ARM7), never the SH4 or main RAM.
void HollySoftReset();

// Not while the CPU runs; Manual keeps some state. False if the DC is not initialized or the CPU runs.
bool Reset_DC(bool Manual);

// Stops emulation and frees everything; Init_DC() is needed again afterwards.
void Term_DC();

// Initializes and resets the emulator if needed, then starts the CPU.
void Start_DC();

// Halts the CPU; the emulator state is kept.
void Stop_DC();

// Loads dc_boot.bin, dc_flash.bin (or dc_flash_wb.bin), syscalls.bin and IP.bin from the data directory.
void LoadBiosFiles();

bool IsDCInited();

bool IsDCRunning();

#ifdef __cplusplus
}
#endif

#endif // DC_H
