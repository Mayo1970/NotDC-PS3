#pragma once
#include "types.h"

// ---- Main memory banks (defined in sh4_mem.cpp) ----

extern VArray2   mem_b;    // Main system RAM  (16 MB)
extern Array<u8> bios_b;  // BIOS ROM         (2 MB)
extern Array<u8> flash_b; // Flash / NVRAM    (128 KB)

// ---- Calling convention ----

#define MEMCALL FASTCALL

#include "_vmem.h"

// ---- Standard read/write: the vmem dispatch table ----
// MMU-on and MMU-off names alias the same vmem functions; _vmem handles the difference at runtime.

// ---- Reads (with MMU) ----
#define ReadMem8        _vmem_ReadMem8
#define ReadMem16       _vmem_ReadMem16
#define IReadMem16      _vmem_ReadMem16   // instruction fetch (same path)
#define ReadMem32       _vmem_ReadMem32
#define ReadMem64       _vmem_ReadMem64

// ---- Writes (with MMU) ----
#define WriteMem8       _vmem_WriteMem8
#define WriteMem16      _vmem_WriteMem16
#define WriteMem32      _vmem_WriteMem32
#define WriteMem64      _vmem_WriteMem64

// ---- Reads (no MMU / direct physical) ----
#define ReadMem8_nommu  _vmem_ReadMem8
#define ReadMem16_nommu _vmem_ReadMem16
#define IReadMem16_nommu _vmem_IReadMem16
#define ReadMem32_nommu _vmem_ReadMem32

// ---- Writes (no MMU / direct physical) ----
#define WriteMem8_nommu  _vmem_WriteMem8
#define WriteMem16_nommu _vmem_WriteMem16
#define WriteMem32_nommu _vmem_WriteMem32

// ---- Block transfer helpers ----

/** DMA copy: read from [src] and write to [dst], both in emulated address space. */
void MEMCALL WriteMemBlock_nommu_dma(u32 dst, u32 src, u32 size);

/** Copy from a host pointer into emulated address space (no MMU). */
void MEMCALL WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size);

/** Copy from a host pointer into emulated address space (with MMU). */
void MEMCALL WriteMemBlock_ptr(u32 dst, u32* src, u32 size);

// ---- Lifecycle ----

void mem_Init();
void mem_Term();
void mem_Reset(bool Manual);   // Manual=true → soft reset (RAM kept); false → hard reset
void mem_map_defualt();        // Build the full SH4 address-space map
void mem_SaveFlash();          // dc_flash_wb.bin, as mem_Term writes it (also P6 JIT EXIT)
void mem_SaveFlashForced();    // the same from the monitor thread, before a forced process exit (P85)

// ---- Debugger / generic access ----

/** Generic read for the GDB stub / debugger. Returns false on unmapped access. */
bool ReadMem_DB(u32 addr, u32& data, u32 size);

/** Generic write for the GDB stub / debugger. Returns false on unmapped access. */
bool WriteMem_DB(u32 addr, u32 data, u32 size);

// GetMemPtr: a host pointer for a mapped RAM/ROM region (debugger, dynarec fast paths), or NULL.
// Do NOT use this for MMIO: it returns NULL there.
u8* GetMemPtr(u32 Addr, u32 size);

/** MemInfo – describes how to access a region; used by the dynarec. */
struct MemInfo
{
	// MemType: 0 = direct pointer (read_ptr/write_ptr), 1 = direct call (ecx = data on write, no address),
	// 2 = generic call (ecx = addr for a read; edx = data for a write).
	u32 MemType;
	u32 Flags;       // reserved / platform-specific

	void* read_ptr;
	void* write_ptr;
};

void GetMemInfo(u32 addr, u32 size, MemInfo* meminfo);

/** Returns true if addr is inside main RAM (and not in P4). */
bool IsOnRam(u32 addr);

/** Return the RAM page index for a RAM address (asserts IsOnRam). */
u32 FASTCALL GetRamPageFromAddress(u32 RamAddress);

// ---- Inline array read/write helpers ----
// Typed value from a raw byte array with the host-to-LE swap; they return or assign by early return, so last in scope.

#ifndef ReadMemArrRet
#define ReadMemArrRet(arr, addr, sz)                                     \
	do {                                                                 \
		if      ((sz) == 1) return (arr)[addr];                          \
		else if ((sz) == 2) return HOST_TO_LE16(*(u16*)&(arr)[addr]);    \
		else if ((sz) == 4) return HOST_TO_LE32(*(u32*)&(arr)[addr]);    \
	} while (0)
#endif

#ifndef WriteMemArrRet
#define WriteMemArrRet(arr, addr, data, sz)                              \
	do {                                                                 \
		if ((sz) == 1)      { (arr)[addr] = (u8)(data);                return; } \
		else if ((sz) == 2) { *(u16*)&(arr)[addr] = HOST_TO_LE16((u16)(data)); return; } \
		else if ((sz) == 4) { *(u32*)&(arr)[addr] = HOST_TO_LE32((u32)(data)); return; } \
	} while (0)
#endif
